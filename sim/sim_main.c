/*
 * sim_main.c - PC simulator for the LVGL UI
 *
 * Shows exactly the same interface as the device, because ui.c is compiled
 * in unchanged. Instead of GPS, a simulated drive runs.
 *
 * --------------------------------------------------------------------------
 * BUILDING (Linux/macOS, SDL2 required)
 * --------------------------------------------------------------------------
 *   git clone https://github.com/lvgl/lvgl.git --branch release/v9.2
 *   cp lvgl/lv_conf_template.h lv_conf.h
 *
 *   Set in lv_conf.h:
 *      #define LV_CONF_SKIP 0        (top line: #if 0 -> #if 1)
 *      #define LV_COLOR_DEPTH 16
 *      #define LV_USE_SDL 1
 *      #define LV_FONT_MONTSERRAT_48 1
 *      #define LV_MEM_SIZE (64 * 1024)
 *
 *   gcc -o sim sim_main.c ui.c $(find lvgl/src -name '*.c') \
 *       -I. -Ilvgl -lSDL2 -lm -O2
 *   ./sim
 *
 * On Windows, easiest via the official lv_port_pc_vscode project: copy
 * ui.c/ui.h and this main into it.
 * --------------------------------------------------------------------------
 */

#include <stdio.h>
#include <unistd.h>

#include "lvgl.h"
#include "config.h"
#include "ui.h"

// One driving leg: the limit changes, speed ramps toward it.
typedef struct {
  int limit;
  float target_speed;
  int hold_ms;
  uint8_t reason;
  const char *note;
} leg_t;

static const leg_t ROUTE[] = {
    {50, 30, 4000, UI_REASON_NONE, "urban road, relaxed"},
    {50, 58, 4000, UI_REASON_NONE, "too fast -> area turns red"},
    {30, 30, 3000, UI_REASON_ZONE, "30 km/h zone"},
    {30, 28, 3000, UI_REASON_CHILDREN, "children / school"},
    {7, 6, 3000, UI_REASON_PLAY_STREET, "play street"},
    {30, 20, 3000, UI_REASON_BICYCLE_STREET, "bicycle street"},
    {100, 85, 4000, UI_REASON_NONE, "rural road"},
    {255, 160, 4000, UI_REASON_NONE, "unrestricted -> bar empty"},
    {-1, 90, 3000, UI_REASON_NONE, "no map data -> question mark"},
    {120, 60, 4000, UI_REASON_NONE, "motorway, half fill"},
};
#define N_LEGS (sizeof(ROUTE) / sizeof(ROUTE[0]))

/*
 * main() - simulator entry point: sets up the LVGL/SDL window and the UI,
 * then drives ROUTE[] in an endless loop.
 *
 * Each iteration ticks the fill-bar animation, lets LVGL redraw, and
 * advances a simple speed ramp toward the current leg's target_speed.
 * Every simulated second the local-time fields are advanced and
 * ui_update() is called, mirroring the device's 5Hz GPS data rate; the
 * 5ms sleep/tick cadence in between mirrors UI_UPDATE_MS on the device, so
 * the animation looks the same in both places.
 */
int main(void) {
  lv_init();

  // LVGL 9 ships its own SDL driver - no separate lv_drivers needed.
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
      .demo = false,   // the simulator displays as if it had a real fix
      .reason = UI_REASON_ZONE,
      .course = 137.0f,
      .speedo = false,
      .over = false,
  };

  size_t leg = 0;
  uint32_t leg_ms = 0;
  uint32_t tick_ms = 0;

  for (;;) {
    ui_tick(5);            // advance the fill bar on every iteration
    lv_timer_handler();
    usleep(5000);
    tick_ms += 5;
    leg_ms += 5;

    // Leg transition
    if (leg_ms >= (uint32_t)ROUTE[leg].hold_ms) {
      leg_ms = 0;
      leg = (leg + 1) % N_LEGS;
      st.limit = ROUTE[leg].limit;
      st.reason = ROUTE[leg].reason;
      printf("-> %s (limit %d)\n", ROUTE[leg].note, ROUTE[leg].limit);
      fflush(stdout);
    }

    // Ease speed toward the target value
    float target = ROUTE[leg].target_speed;
    st.speed_kmh += (target - st.speed_kmh) * 0.01f;
    // The caller decides "too fast" - see the comment in ui.c
    st.over = (st.limit > 0 && st.limit != 255) &&
              (st.speed_kmh > st.limit * (1.0f + OVER_TOLERANCE_PCT / 100.0f));

    // Keep the clock running so the status line looks alive
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
