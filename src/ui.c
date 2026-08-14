/*
 * ui.c - builds and updates the display
 *
 * Layout (360x360, round):
 *   - lv_arc as a red ring, doubling as the fill bar (speed / limit)
 *   - white inner disc holding the digit
 *   - semi-transparent status band at the bottom, cutting into the ring
 *
 * Proportions: ring = 10% of the diameter, font size 55% (two digits)
 * resp. 48% (three digits) of the diameter.
 *
 * This deliberately exceeds the official road-sign proportions (which
 * would be 46% and 40%). On a dashboard what matters is the split-second
 * glance, not fidelity to the standard. Verified: the widest case "888"
 * takes up 191px, and at that height the circle offers 260px. Anyone
 * enlarging this further has to re-check that - the white area is round,
 * so usable width shrinks toward the top and bottom edges.
 */

#include "ui.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RING_W (UI_SIZE / 10)      // 36px red ring
#define GAUGE_GAP 60               // gap at the bottom, matches the status band
#define STATUS_H 58

// LVGL measures angles from 3 o'clock, clockwise. Bottom-center = 90 degrees.
#define ARC_START (90 + GAUGE_GAP / 2)          // 120
#define ARC_END (90 - GAUGE_GAP / 2 + 360)      // 420 -> wraps past 0

#define COL_RED 0xC1121F
#define COL_EMPTY 0x232830
#define COL_WHITE 0xF5F5F3
#define COL_INK 0x15171A
#define COL_GREY 0x5A626D

/*
 * Traffic blue per RAL 5017 - the color used on real German blue traffic
 * signs, including the bicycle-street sign (Zeichen 244.1). Deliberately
 * not pure blue: RAL 5017 is noticeably darker and less saturated, and
 * white text on it reads at roughly 7:1 contrast.
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

// Target and current value of the fill bar. ui_update() sets the target,
// ui_tick() eases toward it. arc_shown_ang tracks what's actually drawn -
// lv_arc_set_value() is only called on a real change, otherwise LVGL would
// invalidate the ring for nothing.
static float arc_target = 0.0f;
static float arc_value = 0.0f;
static float arc_shown_ang = -1.0f;

/*
 * Number crossfade. ui_update() only stashes the new text; ui_tick() fades
 * it out, swaps it at the zero crossing, and fades it back in. Without this,
 * the number would hard-cut, which looks jittery at a 17Hz update rate.
 */
/*
 * Color crossfade. Without it, the red<->white swap would hard-cut while
 * the digit crossfades smoothly - that looked inconsistent. Area, ring and
 * text now all transition over the same duration.
 */
static uint32_t col_from_bg, col_to_bg;
static uint32_t col_from_fg, col_to_fg;
static uint32_t col_from_arc, col_to_arc;
static float col_prog = 1.0f;

static void applyColors(void);

/*
 * mix(a, b, t) - linearly interpolate two 0xRRGGBB colors.
 *
 * Parameters:
 *   a - start color (t = 0)
 *   b - end color (t = 1)
 *   t - progress, 0..1 (not clamped; caller is expected to keep it in range)
 *
 * Blends each channel independently; used to compute the in-between frames
 * of a color crossfade (FADE_MODE == 1 only).
 */
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

// Large digits need a converted font (built-in LVGL fonts stop at 48px).
// Until one is available, the largest built-in font is used as a fallback.
#if defined(USE_DIN_FONT) && defined(USE_DIN_MITTEL)
extern const lv_font_t lv_font_din_m205;   // two-digit
extern const lv_font_t lv_font_din_m162;   // three-digit, also used for "frei"
extern const lv_font_t lv_font_din_m48;    // reason label below the digit
extern const lv_font_t lv_font_din_m72;    // speed shown below the pictogram
/* Tabular-figure fonts for the speedometer: there the number keeps
   changing, and with proportional digits it would jitter sideways because
   "1" is half as wide as the other digits. That's exactly what tabular
   figures are for. */
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
// Last-resort fallback: the default font is tiny, but at least it compiles.
// If you're seeing this, LV_FONT_MONTSERRAT_48 isn't enabled yet in
// lv_conf.h.
#define FONT_2DIGIT (LV_FONT_DEFAULT)
#define FONT_3DIGIT (LV_FONT_DEFAULT)
#define FONT_LABEL (LV_FONT_DEFAULT)
#define FONT_PICNUM (LV_FONT_DEFAULT)
#define FONT_2DIGIT_T (LV_FONT_DEFAULT)
#define FONT_3DIGIT_T (LV_FONT_DEFAULT)
#endif

// Status lines use a fixed-width font: with a proportional font the digits
// would shift sideways on every update, which looks jittery at 5Hz.
extern const lv_font_t lv_font_mono16;
#define FONT_STATUS (&lv_font_mono16)

/*
 * Pictograms for the bicycle-street and play-street signs, cut out from the
 * official Zeichen 244.1 and 325.1 (tools/png_to_lvgl.py). Format A8: only
 * opacity is stored, the color is applied at draw time via image_recolor -
 * so the same bitmap renders white-on-blue, or any other color, without
 * re-generating it.
 */
extern const lv_image_dsc_t img_fahrrad;
extern const lv_image_dsc_t img_spielstrasse;

/*
 * applyColors() - apply the pending background/text/arc colors in one shot.
 *
 * No parameters (reads the file-scope pend_* / pend_colors state set by
 * ui_update()).
 *
 * Batches all four style writes into a single LVGL invalidate/redraw
 * instead of one per property, and does nothing if no color change is
 * pending. Called either immediately (no number change in flight) or at the
 * dark point of a backlight fade (FADE_MODE == 2), so the color swap is
 * hidden together with the number swap.
 */
static void applyColors(void) {
  if (!pend_colors) return;
  pend_colors = false;
  lv_obj_set_style_bg_color(disc, lv_color_hex(pend_bg), 0);
  lv_obj_set_style_text_color(lbl_limit, lv_color_hex(pend_fg), 0);
  lv_obj_set_style_text_color(lbl_reason, lv_color_hex(pend_fg), 0);
  lv_obj_set_style_image_recolor(img_sign, lv_color_hex(pend_fg), 0);
  lv_obj_set_style_arc_color(arc, lv_color_hex(pend_arc), LV_PART_INDICATOR);
}

/*
 * ui_create() - build the static widget tree once at startup.
 *
 * No parameters. Must be called exactly once, before the first ui_update()/
 * ui_tick(). Creates the ring, the white disc, the digit/pictogram labels,
 * and the status band, and leaves everything in its default ("no data yet")
 * state - ui_update() fills in real values afterward.
 */
void ui_create(void) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // --- ring, doubling as the fill bar ---
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

  // --- white sign area ---
  disc = lv_obj_create(scr);
  // DISC_OVERLAP larger than the ring's inner edge, otherwise a dark line
  // remains there from both circles' antialiasing.
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

  // Reason label below the digit. At y=+88 from center, the circle is still
  // about 200px wide - the longest text ("KINDER", 110px) fits with margin,
  // and the status band only starts 14px further down.
  lbl_reason = lv_label_create(disc);
  lv_obj_set_style_text_font(lbl_reason, FONT_LABEL, 0);
  lv_obj_set_style_text_letter_space(lbl_reason, LABEL_LETTER_SPACE, 0);
  lv_obj_set_style_text_color(lbl_reason, lv_color_hex(COL_INK), 0);
  lv_label_set_text(lbl_reason, "");
  lv_obj_align(lbl_reason, LV_ALIGN_CENTER, 0, LABEL_Y);

  // The pictogram sits where the digit normally goes, and stays hidden
  // until one is actually needed.
  img_sign = lv_image_create(disc);
  lv_obj_set_style_image_recolor_opa(img_sign, LV_OPA_COVER, 0);
  lv_obj_set_style_image_recolor(img_sign, lv_color_hex(COL_WHITE), 0);
  lv_obj_add_flag(img_sign, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(img_sign, LV_ALIGN_CENTER, 0, PICTO_Y);

  // --- status band, semi-transparent, overlapping the ring ---
  band = lv_obj_create(scr);
  lv_obj_set_size(band, UI_SIZE, STATUS_H);
  lv_obj_align(band, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(band, 0, 0);
  lv_obj_set_style_bg_color(band, lv_color_hex(BAND_COLOR), 0);
  lv_obj_set_style_bg_opa(band, BAND_OPA, 0);
  lv_obj_set_style_border_width(band, 0, 0);
  lv_obj_set_style_pad_all(band, 0, 0);
  lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);

  // Text centers land at y=311 and y=327. There the circle is still 245
  // resp. 207px wide - both lines fit with margin.
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

/*
 * ui_update(s) - ingest a new measurement snapshot and update the display.
 *
 * Parameters:
 *   s - pointer to the current ui_state_t (limit, speed, fix, position,
 *       reason, mode flags). Not retained beyond this call.
 *
 * Runs at the data rate (5Hz on the device). Computes what SHOULD be shown
 * (fill-bar target, colors, digit/pictogram, reason text, status lines) and
 * only actually touches an LVGL widget when the new value differs from what
 * was last drawn (the various shown_* statics) - see the "only redraw on
 * real change" comment further down for why that matters. The fill bar
 * itself is not animated here; it only gets a new target. The actual easing
 * toward that target happens in ui_tick(), which must be called on every
 * loop iteration regardless of the data rate.
 */
void ui_update(const ui_state_t *s) {
  char buf[48];

  bool has_ref = (s->limit > 0 && s->limit != 255);

  // Fill level: driven speed relative to the limit. Only sets the target
  // here - actual drawing happens in ui_tick(), otherwise it would step at
  // the data rate.
  arc_target = 0.0f;
  if (has_ref && s->speed_kmh > 0) {
    arc_target = 1000.0f * s->speed_kmh / (float)s->limit;
    if (arc_target > 1000.0f) arc_target = 1000.0f;
  }

  /*
   * "Too fast" is decided by the caller, not by this file.
   *
   * Reason: the fade. It holds back the limit and reason text until the
   * backlight is fully down. If the color were computed from speed here
   * instead, it would flip to red already during the dim-down ramp -
   * visible, because the panel is still lit at that point. So the caller
   * holds back all three values together.
   */
  bool over = has_ref && s->over && !s->speedo;

  /*
   * Does the reason color the whole area? Modeled on the color of the real
   * traffic sign. Wherever a colored sign applies, the rendering is always
   * "area colored, text white" - OVER_STYLE_INVERT only affects the normal
   * white case.
   */
  uint32_t sign = 0;
  if (has_ref && !s->speedo) {
    switch (s->reason) {
      // Both signs are genuinely blue in real life: Zeichen 244.1
      // (bicycle street) and Zeichen 325.1 (play street / traffic-calmed
      // area).
      case UI_REASON_BICYCLE_STREET:
      case UI_REASON_PLAY_STREET: sign = COL_BLUE; break;
      default: break;
    }
  }

  uint32_t bg = sign ? sign : COL_WHITE;
  uint32_t fg = sign ? COL_WHITE : COL_INK;
  uint32_t arc_col = sign ? sign : COL_RED;

  // "Too fast" overrides the sign color: a warning must never be ambiguous.
  // So the area turns red regardless of which sign would otherwise apply.
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
   * Bicycle-street and play-street get the real sign's pictogram instead of
   * the digit. The speed then moves down into the reason-label slot - shown
   * as the word "SCHRITT" (walking pace) at 7km/h, because "walking pace"
   * is the actual meaning intended, not the number.
   */
  uint8_t picto = 0;
  if (has_ref && !s->speedo) {
    if (s->reason == UI_REASON_BICYCLE_STREET) picto = 1;
    else if (s->reason == UI_REASON_PLAY_STREET) picto = 2;
  }

  const lv_font_t *num_font;
  char num_txt[8];
  if (picto) {
    num_font = FONT_2DIGIT;
    num_txt[0] = '\0';          // the digit yields to the pictogram
  } else if (s->speedo) {
    /*
     * Speedometer mode: only the driven speed, nothing else. No reason
     * label, no pictogram, no coloring - those belong to the sign, not the
     * speedometer. Only the fill bar stays scaled to the limit, so it's
     * still visible how much of the allowed speed is being used.
     *
     * Tabular figures: the number keeps changing here, and with the
     * proportional digits used elsewhere it would jitter sideways, since
     * "1" is half as wide as the other digits.
     */
    int v = (int)(s->speed_kmh + 0.5f);
    if (v > 999) v = 999;
    num_font = v >= 100 ? FONT_3DIGIT_T : FONT_2DIGIT_T;
    snprintf(num_txt, sizeof(num_txt), "%d", v);
  } else if (s->limit == 255) {
    num_font = FONT_3DIGIT;
    snprintf(num_txt, sizeof(num_txt), "frei");
    fg = COL_GREY;   // "unrestricted" has no concept of "too fast"
  } else if (s->limit > 0) {
    num_font = s->limit >= 100 ? FONT_3DIGIT : FONT_2DIGIT;
    snprintf(num_txt, sizeof(num_txt), "%d", s->limit);
  } else {
    num_font = FONT_2DIGIT;
    snprintf(num_txt, sizeof(num_txt), "?");
    fg = COL_GREY;
  }
  /*
   * Reason label below the digit: only shown when it actually tells the
   * driver something. A plain sign is the ordinary case and stays silent,
   * as does "no data" - together those are over three quarters of all
   * roads. For "frei" (unrestricted) and "?" (unknown) there is nothing to
   * give a reason for, hence this is gated on has_ref.
   */
  char why_buf[12];
  const char *why = "";
  if (s->speedo) {
    why = "";                 // speedometer mode shows nothing below the number
  } else if (picto) {
    if (s->limit == 7) {
      snprintf(why_buf, sizeof(why_buf), "SCHRITT");
    } else {
      snprintf(why_buf, sizeof(why_buf), "%d", s->limit);
    }
    why = why_buf;
  } else if (has_ref) {
    switch (s->reason) {
      case UI_REASON_ZONE:            why = "ZONE"; break;
      case UI_REASON_CHILDREN:        why = "KINDER"; break;
      case UI_REASON_PLAY_STREET:     why = "SPIEL"; break;
      // "FAHRRAD" would be 185px wide, but only 171px are free at this
      // spot. Kept short until the pictogram takes over.
      case UI_REASON_BICYCLE_STREET:  why = "RAD"; break;
      case UI_REASON_TIME_LIMITED:    why = "ZEIT"; break;
      default: break;
    }
  }
  /*
   * From here on, only draw what actually changed.
   *
   * The reason is size: a 197px digit is roughly 147x142px, and would
   * otherwise be re-rasterized and re-transferred every single call. At a
   * 5Hz data rate that would mean the same content five times a second -
   * and that unnecessary full-area transfer is exactly what stalls the
   * fill bar, since they share the same transfer bandwidth. In practice,
   * limit and color only change every few seconds, not five times a
   * second.
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
    // Start from what's currently shown, not from the last target -
    // otherwise switching again mid-crossfade would jump.
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
    if (!fade_active) applyColors();   // no number change in flight: apply immediately
#endif
    shown_bg = bg;
    shown_fg = fg;
    shown_arc = arc_col;
  }
  // Nudge the digit upward when there's a reason label underneath it, plus
  // the optical correction for whichever font is active (LVGL centers the
  // text box, not the glyph).
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
   * The speed is shown below the pictogram - it should read large, not as
   * small as a word. Hence its own font size, and it sits higher than the
   * text label would because the pictogram is shorter than the digit.
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
   * Status line 1: speed, source with satellite count, local time.
   * Fixed field widths so nothing shifts around in the monospace font.
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

  // Status line 2: position and heading
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
 * ui_tick(dt_ms) - animate the display toward the values set by the last
 * ui_update() call.
 *
 * Parameters:
 *   dt_ms - milliseconds elapsed since the previous ui_tick() call. A value
 *           of 0 is a no-op (guards against a stray double call).
 *
 * Belongs in every loop iteration, right next to lv_timer_handler(). Eases
 * the fill bar toward arc_target with a time-based exponential filter (so
 * the animation looks the same regardless of call rate), and drives the
 * pixel/color crossfade state machine when FADE_MODE == 1.
 *
 * lv_arc_set_value() only invalidates the changed arc segment, not the
 * whole ring - frequent small steps are therefore cheaper than rare large
 * ones. The comparison against arc_shown_ang additionally guarantees that
 * an unchanged value never triggers a redraw at all.
 */
void ui_tick(uint32_t dt_ms) {
  if (dt_ms == 0) return;


#if FADE_MODE == 1
  if (fade_active) {
    // Half the time fading out, half fading in
    float step = 255.0f * (float)dt_ms / (FADE_MS / 2.0f);
    if (fade_out) {
      fade_opa -= step;
      if (fade_opa <= 0.0f) {
        fade_opa = 0.0f;
        lv_obj_set_style_text_font(lbl_limit, fade_font, 0);
        lv_label_set_text(lbl_limit, fade_pending);
        lv_obj_align(lbl_limit, LV_ALIGN_CENTER, 0, fade_shift);
#if FADE_MODE != 1
        // Switch the color at exactly this point, where the digit is
        // invisible: both changes land in the same redraw instead of two
        // consecutive ones.
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
   * Set the angle directly instead of via lv_arc_set_value(): that
   * function's 0..1000 range gets mapped internally to whole degrees, and
   * one degree here is 2.8px of arc length - the bar could never move
   * smoothly through that. With LV_USE_FLOAT = 1, lv_arc_set_angles()
   * accepts fractional degrees instead.
   */
  float ang = ARC_START + (arc_value / 1000.0f) * (float)(ARC_END - ARC_START);
  if (fabsf(ang - arc_shown_ang) >= 0.15f) {
    arc_shown_ang = ang;
    lv_arc_set_angles(arc, ARC_START, (ang > ARC_START + 0.2f) ? ang
                                                              : ARC_START + 0.2f);
  }
}
