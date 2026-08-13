/*
 * sim_main.c - LVGL-Simulator fuer den PC
 *
 * Zeigt exakt dieselbe Oberflaeche wie das Geraet, weil ui.c unveraendert
 * eingebunden wird. Statt GPS laeuft eine simulierte Fahrt.
 *
 * --------------------------------------------------------------------------
 * BAUEN (Linux/macOS, SDL2 vorausgesetzt)
 * --------------------------------------------------------------------------
 *   git clone https://github.com/lvgl/lvgl.git --branch release/v9.2
 *   cp lvgl/lv_conf_template.h lv_conf.h
 *
 *   In lv_conf.h setzen:
 *      #define LV_CONF_SKIP 0        (Zeile ganz oben: #if 0 -> #if 1)
 *      #define LV_COLOR_DEPTH 16
 *      #define LV_USE_SDL 1
 *      #define LV_FONT_MONTSERRAT_48 1
 *      #define LV_MEM_SIZE (64 * 1024)
 *
 *   gcc -o sim sim_main.c ui.c $(find lvgl/src -name '*.c') \
 *       -I. -Ilvgl -lSDL2 -lm -O2
 *   ./sim
 *
 * Unter Windows am einfachsten mit dem offiziellen Projekt
 * lv_port_pc_vscode: dort ui.c/ui.h und dieses main hineinkopieren.
 * --------------------------------------------------------------------------
 */

#include <stdio.h>
#include <unistd.h>

#include "lvgl.h"
#include "config.h"
#include "ui.h"

// Ein Fahrtprofil: Limit wechselt, Tempo laeuft darauf zu.
typedef struct {
  int limit;
  float target_speed;
  int hold_ms;
  uint8_t reason;
  const char *note;
} leg_t;

static const leg_t ROUTE[] = {
    {50, 30, 4000, UI_REASON_NONE, "Ortsdurchfahrt, gemuetlich"},
    {50, 58, 4000, UI_REASON_NONE, "zu schnell -> Flaeche wird rot"},
    {30, 30, 3000, UI_REASON_ZONE, "Tempo-30-Zone"},
    {30, 28, 3000, UI_REASON_KINDER, "Kinder / Schule"},
    {7, 6, 3000, UI_REASON_SPIEL, "Spielstrasse"},
    {30, 20, 3000, UI_REASON_RAD, "Fahrradstrasse"},
    {100, 85, 4000, UI_REASON_NONE, "Landstrasse"},
    {255, 160, 4000, UI_REASON_NONE, "unbegrenzt -> Balken leer"},
    {-1, 90, 3000, UI_REASON_NONE, "keine Kartendaten -> Fragezeichen"},
    {120, 60, 4000, UI_REASON_NONE, "Autobahn, halbe Fuellung"},
};
#define N_LEGS (sizeof(ROUTE) / sizeof(ROUTE[0]))

int main(void) {
  lv_init();

  // LVGL 9 bringt den SDL-Treiber mit - kein lv_drivers noetig.
  lv_display_t *disp = lv_sdl_window_create(UI_SIZE, UI_SIZE);
  lv_sdl_window_set_title(disp, "Tempolimit-Anzeige (360x360)");

  ui_create();

  ui_state_t st = {
      .limit = 50,
      .speed_kmh = 0,
      .fix = true,
      .sats = 11,
      .lat = 52.5163,
      .lon = 13.3777,
      .hour = 22,
      .minute = 14,
      .weekday = 0,
      .time_valid = true,
      .demo = false,   // der Simulator zeigt die Anzeige wie mit echtem Fix
      .reason = UI_REASON_ZONE,
      .course = 137.0f,
      .speedo = false,
      .over = false,
  };

  size_t leg = 0;
  uint32_t leg_ms = 0;
  uint32_t tick_ms = 0;

  for (;;) {
    ui_tick(5);            // Balken bei jedem Durchlauf weiterbewegen
    lv_timer_handler();
    usleep(5000);
    tick_ms += 5;
    leg_ms += 5;

    // Abschnittswechsel
    if (leg_ms >= (uint32_t)ROUTE[leg].hold_ms) {
      leg_ms = 0;
      leg = (leg + 1) % N_LEGS;
      st.limit = ROUTE[leg].limit;
      st.reason = ROUTE[leg].reason;
      printf("-> %s (Limit %d)\n", ROUTE[leg].note, ROUTE[leg].limit);
      fflush(stdout);
    }

    // Tempo weich auf den Zielwert ziehen
    float target = ROUTE[leg].target_speed;
    st.speed_kmh += (target - st.speed_kmh) * 0.01f;
    // Der Aufrufer entscheidet ueber "zu schnell" - siehe Kommentar in ui.c
    st.over = (st.limit > 0 && st.limit != 255) &&
              (st.speed_kmh > st.limit * (1.0f + OVER_TOLERANCE_PCT / 100.0f));

    // Uhr laufen lassen, damit die Statuszeile lebt
    if (tick_ms % 1000 == 0) {
      if (++st.minute >= 60) {
        st.minute = 0;
        if (++st.hour >= 24) {
          st.hour = 0;
          st.weekday = (st.weekday + 1) % 7;
        }
      }
      ui_update(&st);
    }
  }
  return 0;
}
