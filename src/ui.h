/*
 * ui.h - the speed-limit display's UI surface (platform-independent)
 *
 * The same file is compiled for both the PC simulator (SDL) and the ESP32.
 * No Arduino, no GPS, no SD card in here - LVGL only.
 */

#ifndef UI_H
#define UI_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_SIZE 360   // round display, 360x360

// Everything the display needs, bundled into one struct so the simulator
// and the firmware go through the same call.
typedef struct {
  int limit;         // km/h, 255 = unrestricted, <=0 = unknown
  float speed_kmh;   // driven speed
  bool fix;          // GPS fix available
  uint8_t sats;      // satellite count
  double lat, lon;
  float course;      // heading in degrees, <0 = unknown
  uint8_t hour, minute, weekday;  // local time, weekday 0 = Monday
  bool time_valid;
  bool demo;         // simulated drive (no GPS) - status line shows DEMO
  uint8_t reason;    // why the limit applies, UI_REASON_*
  bool speedo;       // speedometer mode: center shows driven speed instead of limit
  bool over;         // too fast - decided by the caller, see ui.c
} ui_state_t;

/*
 * Reason the limit applies. These values mirror REASON_* in
 * tools/osm_to_grid.py, where they're packed into the low three bits of
 * each chain's flags byte - so do not change the order/values without
 * regenerating the map data.
 *
 * Only reasons that actually tell the driver something get displayed:
 * SIGN is the ordinary case and stays silent, as does NONE.
 *
 * The identifiers below are English; the text actually shown on screen for
 * each reason (see ui.c) stays German on purpose, since the device targets
 * German roads and German road signs (ZONE, KINDER, SPIEL, RAD, ZEIT).
 */
#define UI_REASON_NONE 0
#define UI_REASON_ZONE 1        // 30 km/h traffic-calmed zone
#define UI_REASON_CHILDREN 2    // hazard=children, usually a school/kindergarten
#define UI_REASON_PLAY_STREET 3 // "verkehrsberuhigter Bereich" (walking-pace zone)
#define UI_REASON_BICYCLE_STREET 4  // "Fahrradstrasse" (bicycle-priority street)
#define UI_REASON_SIGN 5         // single explicit sign, no special category
#define UI_REASON_TIME_LIMITED 6 // only in effect during certain hours

void ui_create(void);

/*
 * ui_update() vs. ui_tick() - why there are two entry points:
 *
 * ui_update() ingests new measurements, i.e. runs at the data rate (5Hz for
 * us). ui_tick() animates the display toward those values and belongs in
 * every loop iteration, right next to lv_timer_handler(). Without this
 * split, the fill bar used to jump at the data rate - visibly five times a
 * second.
 *
 * dt_ms is the time elapsed since the previous ui_tick() call. The
 * smoothing is computed from that elapsed time, so it stays independent of
 * how often this is called - the simulator and the real device end up
 * looking the same.
 */
void ui_update(const ui_state_t *s);
void ui_tick(uint32_t dt_ms);

#ifdef __cplusplus
}
#endif

#endif  // UI_H
