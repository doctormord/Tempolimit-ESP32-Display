/*
 * ui.c - Aufbau und Aktualisierung der Anzeige
 *
 * Layout (360x360, rund):
 *   - lv_arc als roter Rand, gleichzeitig Fuellbalken (Tempo / Limit)
 *   - weisses Innenfeld mit der Ziffer
 *   - halbtransparentes Statusband unten, schneidet den Rand an
 *
 * Proportionen: Rand = 10 % des Durchmessers, Schriftgrad 55 % (zweistellig)
 * bzw. 48 % (dreistellig) des Durchmessers.
 *
 * Das liegt bewusst ueber den amtlichen Schild-Proportionen (die waeren 46 %
 * und 40 %). Am Armaturenbrett zaehlt der kurze Blick, nicht die Normtreue.
 * Geprueft: der breiteste Fall "888" belegt 191 px, an dieser Hoehe stehen im
 * Kreis 260 px zur Verfuegung. Wer weiter vergroessert, muss das nachrechnen -
 * die weisse Flaeche ist rund, die nutzbare Breite nimmt zu den Raendern hin ab.
 */

#include "ui.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RING_W (UI_SIZE / 10)      // 36 px roter Rand
#define GAUGE_GAP 60               // Luecke unten, deckt sich mit dem Band
#define STATUS_H 58

// LVGL misst Winkel ab 3 Uhr im Uhrzeigersinn. Unten mittig = 90 Grad.
#define ARC_START (90 + GAUGE_GAP / 2)          // 120
#define ARC_END (90 - GAUGE_GAP / 2 + 360)      // 420 -> laeuft ueber 0 hinweg

#define COL_RED 0xC1121F
#define COL_EMPTY 0x232830
#define COL_WHITE 0xF5F5F3
#define COL_INK 0x15171A
#define COL_GREY 0x5A626D

/*
 * Verkehrsblau nach RAL 5017 - die Farbe deutscher blauer Verkehrszeichen,
 * also auch der Fahrradstrasse (Zeichen 244.1). Bewusst nicht reines Blau:
 * RAL 5017 ist deutlich dunkler und entsaettigter, weisse Schrift steht
 * darauf mit rund 7:1 Kontrast.
 */
#define COL_BLUE 0x005387

static lv_obj_t *arc;
static lv_obj_t *disc;
static lv_obj_t *lbl_limit;
static lv_obj_t *lbl_reason;
static lv_obj_t *img_sign;

static lv_obj_t *band;
static lv_obj_t *lbl_fix;
static lv_obj_t *lbl_pos;

// Ziel und Ist des Fuellbalkens. ui_update() setzt das Ziel, ui_tick() laeuft
// darauf zu. arc_shown haelt fest, was tatsaechlich gezeichnet ist - nur bei
// echter Aenderung wird lv_arc_set_value() gerufen, sonst invalidiert LVGL
// den Ring ohne Grund.
static float arc_target = 0.0f;
static float arc_value = 0.0f;
static float arc_shown_ang = -1.0f;

/*
 * Ueberblendung der Zahl. ui_update() legt den neuen Text nur ab; ui_tick()
 * blendet aus, tauscht am Nulldurchgang und blendet wieder ein. Ohne das
 * springt die Zahl hart um, was bei 17 Hz Aktualisierung unruhig wirkt.
 */
/*
 * Farbueberblendung. Ohne sie springt der Wechsel rot->weiss hart um, waehrend
 * die Ziffer weich ueberblendet - das sah widerspruechlich aus. Flaeche, Ring
 * und Schrift laufen jetzt mit derselben Zeit ineinander.
 */
static uint32_t col_from_bg, col_to_bg;
static uint32_t col_from_fg, col_to_fg;
static uint32_t col_from_arc, col_to_arc;
static float col_prog = 1.0f;

static void applyColors(void);

static uint32_t mix(uint32_t a, uint32_t b, float t) {
  int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  int r = ar + (int)((br - ar) * t);
  int g = ag + (int)((bg - ag) * t);
  int bl = ab + (int)((bb - ab) * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static uint32_t pend_bg, pend_fg, pend_arc;
static bool pend_colors = false;

static char fade_pending[8] = "";
static const lv_font_t *fade_font = NULL;
static int fade_shift = 0;
static bool fade_active = false;
static float fade_opa = 255.0f;
static bool fade_out = true;

// Grosse Ziffern brauchen einen konvertierten Font (LVGL-Fonts enden bei
// 48 px). Bis der vorliegt, wird der groesste eingebaute benutzt.
#if defined(USE_DIN_FONT) && defined(USE_DIN_MITTEL)
extern const lv_font_t lv_font_din_m205;   // zweistellig
extern const lv_font_t lv_font_din_m162;   // dreistellig, auch fuer "frei"
extern const lv_font_t lv_font_din_m48;    // Beschriftung unter der Ziffer
extern const lv_font_t lv_font_din_m72;    // Tempo unter dem Piktogramm
/* Tabellenziffern fuer den Tacho: dort wechselt die Zahl staendig, und mit
   proportionalen Ziffern springt sie seitlich, weil die 1 halb so breit ist
   wie die uebrigen. Genau dafuer gibt es Tabellenziffern. */
extern const lv_font_t lv_font_din_m205t;
extern const lv_font_t lv_font_din_m162t;
#define FONT_2DIGIT (&lv_font_din_m205)
#define FONT_3DIGIT (&lv_font_din_m162)
#define FONT_LABEL (&lv_font_din_m48)
#define FONT_PICNUM (&lv_font_din_m72)
#define FONT_2DIGIT_T (&lv_font_din_m205t)
#define FONT_3DIGIT_T (&lv_font_din_m162t)
#elif defined(USE_DIN_FONT)
extern const lv_font_t lv_font_din_197;
extern const lv_font_t lv_font_din_171;
extern const lv_font_t lv_font_din_56;
extern const lv_font_t lv_font_din_88;
extern const lv_font_t lv_font_din_197t;
extern const lv_font_t lv_font_din_171t;
#define FONT_2DIGIT (&lv_font_din_197)
#define FONT_3DIGIT (&lv_font_din_171)
#define FONT_LABEL (&lv_font_din_56)
#define FONT_PICNUM (&lv_font_din_88)
#define FONT_2DIGIT_T (&lv_font_din_197t)
#define FONT_3DIGIT_T (&lv_font_din_171t)
#elif LV_FONT_MONTSERRAT_48
#define FONT_2DIGIT (&lv_font_montserrat_48)
#define FONT_3DIGIT (&lv_font_montserrat_48)
#define FONT_LABEL (&lv_font_montserrat_48)
#define FONT_PICNUM (&lv_font_montserrat_48)
#define FONT_2DIGIT_T (&lv_font_montserrat_48)
#define FONT_3DIGIT_T (&lv_font_montserrat_48)
#else
// Notnagel: der Standardfont ist winzig, aber es compiliert. Wenn du das
// siehst, ist LV_FONT_MONTSERRAT_48 in lv_conf.h noch nicht auf 1.
#define FONT_2DIGIT (LV_FONT_DEFAULT)
#define FONT_3DIGIT (LV_FONT_DEFAULT)
#define FONT_LABEL (LV_FONT_DEFAULT)
#define FONT_PICNUM (LV_FONT_DEFAULT)
#define FONT_2DIGIT_T (LV_FONT_DEFAULT)
#define FONT_3DIGIT_T (LV_FONT_DEFAULT)
#endif

// Statuszeilen in Festbreite: bei Proportionalschrift wandern die Ziffern
// bei jedem Wechsel seitlich, was bei 5 Hz unruhig aussieht.
extern const lv_font_t lv_font_mono16;
#define FONT_STATUS (&lv_font_mono16)

/*
 * Piktogramme fuer Fahrrad- und Spielstrasse, freigestellt aus den amtlichen
 * Zeichen 244.1 und 325.1 (tools/png_to_lvgl.py). Format A8: nur Deckkraft,
 * die Farbe kommt beim Zeichnen aus image_recolor - deshalb dasselbe Bild
 * weiss auf blau und bei Bedarf in jeder anderen Farbe.
 */
extern const lv_image_dsc_t img_fahrrad;
extern const lv_image_dsc_t img_spielstrasse;

/* Farben in einem Rutsch setzen - ein Neuaufbau statt mehrerer. */
static void applyColors(void) {
  if (!pend_colors) return;
  pend_colors = false;
  lv_obj_set_style_bg_color(disc, lv_color_hex(pend_bg), 0);
  lv_obj_set_style_text_color(lbl_limit, lv_color_hex(pend_fg), 0);
  lv_obj_set_style_text_color(lbl_reason, lv_color_hex(pend_fg), 0);
  lv_obj_set_style_image_recolor(img_sign, lv_color_hex(pend_fg), 0);
  lv_obj_set_style_arc_color(arc, lv_color_hex(pend_arc), LV_PART_INDICATOR);
}

void ui_create(void) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // --- Rand als Fuellbalken ---
  arc = lv_arc_create(scr);
  lv_obj_set_size(arc, UI_SIZE, UI_SIZE);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 0);
  lv_arc_set_bg_angles(arc, ARC_START, ARC_END);
  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_set_style_arc_width(arc, RING_W, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(COL_EMPTY), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, RING_W, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(COL_RED), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

  // --- weisses Schildfeld ---
  disc = lv_obj_create(scr);
  // DISC_OVERLAP groesser als die Ringinnenkante, sonst bleibt dort eine
  // dunkle Linie aus der Kantenglaettung beider Kreise stehen.
  lv_obj_set_size(disc, UI_SIZE - 2 * RING_W + 2 * DISC_OVERLAP,
                  UI_SIZE - 2 * RING_W + 2 * DISC_OVERLAP);
  lv_obj_center(disc);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(disc, lv_color_hex(COL_WHITE), 0);
  lv_obj_set_style_border_width(disc, 0, 0);
  lv_obj_set_style_pad_all(disc, 0, 0);
  lv_obj_remove_flag(disc, LV_OBJ_FLAG_SCROLLABLE);

  lbl_limit = lv_label_create(disc);
  lv_obj_set_style_text_color(lbl_limit, lv_color_hex(COL_INK), 0);
  lv_obj_set_style_text_font(lbl_limit, FONT_2DIGIT, 0);
  lv_label_set_text(lbl_limit, "?");
  lv_obj_center(lbl_limit);

  // Beschriftung unter der Ziffer. Bei y=+88 vom Mittelpunkt ist die
  // Kreisflaeche noch rund 200 px breit - der laengste Text ("KINDER", 110 px)
  // passt mit Abstand, und das Statusband beginnt erst 14 px darunter.
  lbl_reason = lv_label_create(disc);
  lv_obj_set_style_text_font(lbl_reason, FONT_LABEL, 0);
  lv_obj_set_style_text_letter_space(lbl_reason, LABEL_LETTER_SPACE, 0);
  lv_obj_set_style_text_color(lbl_reason, lv_color_hex(COL_INK), 0);
  lv_label_set_text(lbl_reason, "");
  lv_obj_align(lbl_reason, LV_ALIGN_CENTER, 0, LABEL_Y);

  // Piktogramm sitzt an der Stelle der Ziffer und bleibt versteckt, solange
  // keins gebraucht wird.
  img_sign = lv_image_create(disc);
  lv_obj_set_style_image_recolor_opa(img_sign, LV_OPA_COVER, 0);
  lv_obj_set_style_image_recolor(img_sign, lv_color_hex(COL_WHITE), 0);
  lv_obj_add_flag(img_sign, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(img_sign, LV_ALIGN_CENTER, 0, PICTO_Y);

  // --- Statusband, halbtransparent ueber dem Rand ---
  band = lv_obj_create(scr);
  lv_obj_set_size(band, UI_SIZE, STATUS_H);
  lv_obj_align(band, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(band, 0, 0);
  lv_obj_set_style_bg_color(band, lv_color_hex(BAND_COLOR), 0);
  lv_obj_set_style_bg_opa(band, BAND_OPA, 0);
  lv_obj_set_style_border_width(band, 0, 0);
  lv_obj_set_style_pad_all(band, 0, 0);
  lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);

  // Textmitte liegt damit bei y=311 und y=327. Dort ist die Kreisflaeche
  // noch 245 bzw. 207 px breit - beide Zeilen bleiben mit Abstand drin.
  lbl_fix = lv_label_create(band);
  lv_obj_set_style_text_font(lbl_fix, FONT_STATUS, 0);
  lv_obj_set_style_text_color(lbl_fix, lv_color_hex(BAND_TEXT), 0);
  lv_obj_align(lbl_fix, LV_ALIGN_TOP_MID, 0, 2);
  lv_label_set_text(lbl_fix, "  0 km/h  ---  --:--");

  lbl_pos = lv_label_create(band);
  lv_obj_set_style_text_font(lbl_pos, FONT_STATUS, 0);
  lv_obj_set_style_text_color(lbl_pos, lv_color_hex(BAND_TEXT), 0);
  lv_obj_align(lbl_pos, LV_ALIGN_TOP_MID, 0, 21);
  lv_label_set_text(lbl_pos, "kein Fix");
}

void ui_update(const ui_state_t *s) {
  char buf[48];

  bool has_ref = (s->limit > 0 && s->limit != 255);

  // Fuellstand: gefahrenes Tempo im Verhaeltnis zum Limit. Hier nur das Ziel
  // setzen - gezeichnet wird in ui_tick(), sonst ruckelt es im Datentakt.
  arc_target = 0.0f;
  if (has_ref && s->speed_kmh > 0) {
    arc_target = 1000.0f * s->speed_kmh / (float)s->limit;
    if (arc_target > 1000.0f) arc_target = 1000.0f;
  }

  /*
   * "Zu schnell" entscheidet der Aufrufer, nicht diese Datei.
   *
   * Grund ist die Blende: sie haelt Limit und Begruendung zurueck, bis das
   * Hintergrundlicht unten ist. Wuerde die Farbe hier aus dem Tempo berechnet,
   * kippte sie schon waehrend der Abwaertsrampe auf Rot - sichtbar, weil das
   * Panel da noch leuchtet. Der Aufrufer haelt deshalb alle drei Werte
   * gemeinsam zurueck.
   */
  bool over = has_ref && s->over && !s->speedo;

  /*
   * Faerbt die Begruendung die Flaeche? Orientiert an der Farbe des echten
   * Verkehrszeichens. Wo ein farbiges Schild gilt, ist die Darstellung immer
   * "Flaeche farbig, Schrift weiss" - OVER_STYLE_INVERT betrifft nur den
   * normalen weissen Fall.
   */
  uint32_t sign = 0;
  if (has_ref && !s->speedo) {
    switch (s->reason) {
      // Beide Schilder sind in echt blau: Zeichen 244.1 (Fahrradstrasse)
      // und Zeichen 325.1 (verkehrsberuhigter Bereich).
      case UI_REASON_RAD:
      case UI_REASON_SPIEL: sign = COL_BLUE; break;
      default: break;
    }
  }

  uint32_t bg = sign ? sign : COL_WHITE;
  uint32_t fg = sign ? COL_WHITE : COL_INK;
  uint32_t arc_col = sign ? sign : COL_RED;

  // Zu schnell schlaegt die Schildfarbe: eine Warnung darf nicht mehrdeutig
  // sein. Deshalb wird die Flaeche rot, egal welches Schild sonst gilt.
  if (over) {
#if OVER_STYLE_INVERT
    bg = COL_RED;
    fg = COL_WHITE;
    arc_col = COL_RED;
#else
    fg = sign ? COL_WHITE : COL_RED;
    if (sign) bg = COL_RED;
    arc_col = COL_RED;
#endif
  }
  /*
   * Fahrrad- und Spielstrasse bekommen das Piktogramm des echten Schildes
   * anstelle der Ziffer. Das Tempo rutscht dann nach unten an die Stelle der
   * Beschriftung - bei 7 km/h als Wort, weil "Schrittgeschwindigkeit" die
   * gemeinte Aussage ist und nicht die Zahl.
   */
  uint8_t picto = 0;
  if (has_ref && !s->speedo) {
    if (s->reason == UI_REASON_RAD) picto = 1;
    else if (s->reason == UI_REASON_SPIEL) picto = 2;
  }

  const lv_font_t *num_font;
  char num_txt[8];
  if (picto) {
    num_font = FONT_2DIGIT;
    num_txt[0] = '\0';          // die Ziffer weicht dem Piktogramm
  } else if (s->speedo) {
    /*
     * Tachomodus: nur das gefahrene Tempo, sonst nichts. Keine Beschriftung,
     * kein Piktogramm, keine Faerbung - das gehoert zum Schild, nicht zum
     * Tacho. Einzig der Balken bleibt auf das Limit skaliert, damit sichtbar
     * ist, wieviel der erlaubten Geschwindigkeit ausgeschoepft wird.
     *
     * Tabellenziffern: die Zahl wechselt hier staendig, und mit den sonst
     * verwendeten proportionalen Ziffern springt sie seitlich, weil die 1
     * halb so breit ist wie die uebrigen.
     */
    int v = (int)(s->speed_kmh + 0.5f);
    if (v > 999) v = 999;
    num_font = v >= 100 ? FONT_3DIGIT_T : FONT_2DIGIT_T;
    snprintf(num_txt, sizeof(num_txt), "%d", v);
  } else if (s->limit == 255) {
    num_font = FONT_3DIGIT;
    snprintf(num_txt, sizeof(num_txt), "frei");
    fg = COL_GREY;   // "unbegrenzt" kennt kein zu schnell
  } else if (s->limit > 0) {
    num_font = s->limit >= 100 ? FONT_3DIGIT : FONT_2DIGIT;
    snprintf(num_txt, sizeof(num_txt), "%d", s->limit);
  } else {
    num_font = FONT_2DIGIT;
    snprintf(num_txt, sizeof(num_txt), "?");
    fg = COL_GREY;
  }
  /*
   * Beschriftung darunter: nur was dem Fahrer etwas sagt. Das Einzelschild
   * ist der Normalfall und bleibt stumm, ebenso "ohne Angabe" - das waeren
   * zusammen ueber drei Viertel aller Strassen. Bei "frei" und "?" gibt es
   * nichts zu begruenden, deshalb haengt es an has_ref.
   */
  char why_buf[12];
  const char *why = "";
  if (s->speedo) {
    why = "";                 // im Tacho steht unter der Zahl nichts
  } else if (picto) {
    if (s->limit == 7) {
      snprintf(why_buf, sizeof(why_buf), "SCHRITT");
    } else {
      snprintf(why_buf, sizeof(why_buf), "%d", s->limit);
    }
    why = why_buf;
  } else if (has_ref) {
    switch (s->reason) {
      case UI_REASON_ZONE:   why = "ZONE"; break;
      case UI_REASON_KINDER: why = "KINDER"; break;
      case UI_REASON_SPIEL:  why = "SPIEL"; break;
      // "FAHRRAD" waere 185 px breit, an dieser Stelle sind 171 px frei.
      // Bleibt kurz, bis das Piktogramm es ersetzt.
      case UI_REASON_RAD:    why = "RAD"; break;
      case UI_REASON_ZEIT:   why = "ZEIT"; break;
      default: break;
    }
  }
  /*
   * Ab hier nur zeichnen, was sich wirklich geaendert hat.
   *
   * Der Grund ist die Groesse: eine 197-px-Ziffer ist rund 147x142 px, die
   * jedes Mal neu gerastert und uebertragen wuerde. Bei 5 Hz Datentakt waere
   * das fuenfmal pro Sekunde derselbe Inhalt - und genau diese unnoetige
   * Vollflaeche laesst den Fuellbalken stocken, weil sie sich die
   * Uebertragungszeit mit ihm teilt. Limit und Farbe wechseln in der Praxis
   * alle paar Sekunden, nicht fuenfmal pro Sekunde.
   */
  static char shown_num[8] = "";
  static char shown_why[12] = "";
  static const lv_font_t *shown_font = NULL;
  static uint32_t shown_fg = 0xFFFFFFFFu, shown_bg = 0xFFFFFFFFu;
  static uint32_t shown_arc = 0xFFFFFFFFu;
  static int shown_shift = 9999;

  static uint8_t shown_picto = 255;
  if (picto != shown_picto) {
    shown_picto = picto;
    if (picto) {
      lv_image_set_src(img_sign,
                       picto == 1 ? &img_fahrrad : &img_spielstrasse);
      lv_obj_align(img_sign, LV_ALIGN_CENTER, 0, PICTO_Y);
      lv_obj_remove_flag(img_sign, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(img_sign, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (bg != shown_bg || fg != shown_fg || arc_col != shown_arc) {
#if FADE_MODE == 1
    // Vom aktuell gezeigten Stand aus starten, nicht vom letzten Ziel -
    // sonst springt es, wenn mitten in der Ueberblendung neu gewechselt wird.
    col_from_bg = (col_prog >= 1.0f) ? shown_bg : mix(col_from_bg, col_to_bg, col_prog);
    col_from_fg = (col_prog >= 1.0f) ? shown_fg : mix(col_from_fg, col_to_fg, col_prog);
    col_from_arc = (col_prog >= 1.0f) ? shown_arc : mix(col_from_arc, col_to_arc, col_prog);
    if (shown_bg == 0xFFFFFFFFu) { col_from_bg = bg; col_from_fg = fg; col_from_arc = arc_col; }
    col_to_bg = bg;
    col_to_fg = fg;
    col_to_arc = arc_col;
    col_prog = 0.0f;
#else
    pend_bg = bg; pend_fg = fg; pend_arc = arc_col; pend_colors = true;
    if (!fade_active) applyColors();   // ohne Ziffernwechsel sofort
#endif
    shown_bg = bg;
    shown_fg = fg;
    shown_arc = arc_col;
  }
  // Ziffer nach oben ruecken, wenn darunter etwas steht, plus die optische
  // Korrektur des jeweiligen Fonts (LVGL zentriert den Kasten, nicht die Ziffer)
  int opt = (num_font == FONT_3DIGIT || num_font == FONT_3DIGIT_T)
                ? NUM_OPT_3DIGIT
                : NUM_OPT_2DIGIT;
  int shift = opt + (why[0] ? NUM_Y_SHIFT : 0);

  bool num_changed = (strcmp(num_txt, shown_num) != 0) ||
                     (num_font != shown_font) || (shift != shown_shift);
  if (num_changed) {
#if FADE_MODE == 1
    snprintf(fade_pending, sizeof(fade_pending), "%s", num_txt);
    fade_font = num_font;
    fade_shift = shift;
    if (!fade_active) {
      fade_active = true;
      fade_out = true;
    }
#else
    lv_obj_set_style_text_font(lbl_limit, num_font, 0);
    lv_label_set_text(lbl_limit, num_txt);
    lv_obj_align(lbl_limit, LV_ALIGN_CENTER, 0, shift);
#endif
    snprintf(shown_num, sizeof(shown_num), "%s", num_txt);
    shown_font = num_font;
    shown_shift = shift;
  }
  /*
   * Unter dem Piktogramm steht das Tempo - das soll gross sein, nicht so
   * klein wie ein Wort. Deshalb eigener Schriftgrad, und die Beschriftung
   * sitzt dort hoeher, weil das Piktogramm flacher ist als die Ziffer.
   */
  bool picnum = picto && why[0] >= '0' && why[0] <= '9';
  const lv_font_t *lab_font = picnum ? FONT_PICNUM : FONT_LABEL;
  int lab_y = (picto ? LABEL_Y_PICTO : LABEL_Y) +
              (picnum ? PICNUM_OPT : LABEL_OPT);
  static const lv_font_t *shown_lab_font = NULL;
  static int shown_lab_y = 9999;
  if (strcmp(why, shown_why) != 0 || lab_font != shown_lab_font ||
      lab_y != shown_lab_y) {
    snprintf(shown_why, sizeof(shown_why), "%s", why);
    if (lab_font != shown_lab_font) {
      shown_lab_font = lab_font;
      lv_obj_set_style_text_font(lbl_reason, lab_font, 0);
    }
    lv_label_set_text(lbl_reason, why);
    shown_lab_y = lab_y;
    lv_obj_align(lbl_reason, LV_ALIGN_CENTER, 0, lab_y);
  }


  /*
   * Statuszeile 1: Tempo, Quelle mit Satellitenzahl, Ortszeit.
   * Feste Feldbreiten, damit in Festbreitenschrift nichts wandert.
   */
  const char *tag = s->demo ? "DEM" : (s->fix ? "FIX" : "...");
  if (s->time_valid) {
    snprintf(buf, sizeof(buf), "%3.0f km/h %s%-2u %02u:%02u", s->speed_kmh,
             tag, s->sats, s->hour, s->minute);
  } else {
    snprintf(buf, sizeof(buf), "%3.0f km/h %s%-2u --:--", s->speed_kmh, tag,
             s->sats);
  }
  static char shown_fix[48] = "";
  if (strcmp(buf, shown_fix) != 0) {
    snprintf(shown_fix, sizeof(shown_fix), "%s", buf);
    lv_label_set_text(lbl_fix, buf);
  }

  // Statuszeile 2: Position und Fahrtrichtung
  if (s->fix || s->demo) {
    if (s->course >= 0.0f) {
      snprintf(buf, sizeof(buf), "%.3f %.3f %3.0f", s->lat, s->lon, s->course);
    } else {
      snprintf(buf, sizeof(buf), "%.3f %.3f  --", s->lat, s->lon);
    }
  } else {
    snprintf(buf, sizeof(buf), "kein Fix");
  }
  static char shown_pos[48] = "";
  if (strcmp(buf, shown_pos) != 0) {
    snprintf(shown_pos, sizeof(shown_pos), "%s", buf);
    lv_label_set_text(lbl_pos, buf);
  }
}

/*
 * Bewegt die Anzeige auf die zuletzt gesetzten Werte zu. Gehoert in jeden
 * Schleifendurchlauf, direkt neben lv_timer_handler().
 *
 * lv_arc_set_value() invalidiert nur den geaenderten Kreisausschnitt, nicht
 * den ganzen Ring - haeufige kleine Schritte sind deshalb billiger als
 * seltene grosse. Der Vergleich mit arc_shown verhindert, dass bei
 * unveraendertem Wert ueberhaupt etwas angestossen wird.
 */
void ui_tick(uint32_t dt_ms) {
  if (dt_ms == 0) return;


#if FADE_MODE == 1
  if (fade_active) {
    // Halbe Zeit raus, halbe rein
    float step = 255.0f * (float)dt_ms / (FADE_MS / 2.0f);
    if (fade_out) {
      fade_opa -= step;
      if (fade_opa <= 0.0f) {
        fade_opa = 0.0f;
        lv_obj_set_style_text_font(lbl_limit, fade_font, 0);
        lv_label_set_text(lbl_limit, fade_pending);
        lv_obj_align(lbl_limit, LV_ALIGN_CENTER, 0, fade_shift);
#if FADE_MODE != 1
        // Farbe genau hier umschalten, wo die Ziffer unsichtbar ist: beides
        // faellt in denselben Neuaufbau statt in zwei aufeinanderfolgende.
        applyColors();
#endif
        fade_out = false;
      }
    } else {
          fade_opa += step;
      if (fade_opa >= 255.0f) {
        fade_opa = 255.0f;
        fade_active = false;
      }
    }
    lv_obj_set_style_text_opa(lbl_limit, (lv_opa_t)fade_opa, 0);
  }
#endif
#if FADE_MODE == 1
  if (col_prog < 1.0f) {
    col_prog += (float)dt_ms / FADE_COLOR_MS;
    if (col_prog > 1.0f) col_prog = 1.0f;
    uint32_t b = mix(col_from_bg, col_to_bg, col_prog);
    uint32_t f = mix(col_from_fg, col_to_fg, col_prog);
    uint32_t a2 = mix(col_from_arc, col_to_arc, col_prog);
    lv_obj_set_style_bg_color(disc, lv_color_hex(b), 0);
    lv_obj_set_style_text_color(lbl_limit, lv_color_hex(f), 0);
    lv_obj_set_style_text_color(lbl_reason, lv_color_hex(f), 0);
    lv_obj_set_style_image_recolor(img_sign, lv_color_hex(f), 0);
    lv_obj_set_style_arc_color(arc, lv_color_hex(a2), LV_PART_INDICATOR);
  }
#endif

  float a = 1.0f - expf(-(float)dt_ms / ARC_TAU_MS);
  arc_value += (arc_target - arc_value) * a;
  if (fabsf(arc_target - arc_value) < 0.5f) arc_value = arc_target;

  /*
   * Winkel direkt setzen statt ueber lv_arc_set_value(): dessen Wertebereich
   * 0..1000 wird intern auf ganze Grad abgebildet, und 1 Grad sind hier
   * 2,8 px Bogenlaenge - der Balken kann damit gar nicht fluessig laufen.
   * Mit LV_USE_FLOAT = 1 nimmt lv_arc_set_angles() Bruchteile von Grad an.
   */
  float ang = ARC_START + (arc_value / 1000.0f) * (float)(ARC_END - ARC_START);
  if (fabsf(ang - arc_shown_ang) >= 0.15f) {
    arc_shown_ang = ang;
    lv_arc_set_angles(arc, ARC_START, (ang > ARC_START + 0.2f) ? ang
                                                              : ARC_START + 0.2f);
  }
}
