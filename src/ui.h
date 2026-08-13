/*
 * ui.h - Oberflaeche der Tempolimit-Anzeige (plattformunabhaengig)
 *
 * Dieselbe Datei laeuft im PC-Simulator (SDL) und auf dem ESP32.
 * Kein Arduino, kein GPS, keine SD-Karte hier drin - nur LVGL.
 */

#ifndef UI_H
#define UI_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_SIZE 360   // rundes Display 360x360

// Alles, was die Anzeige braucht - in einer Struktur, damit Simulator und
// Firmware denselben Aufruf benutzen.
typedef struct {
  int limit;         // km/h, 255 = unbegrenzt, <=0 = unbekannt
  float speed_kmh;   // gefahrenes Tempo
  bool fix;          // GPS-Fix vorhanden
  uint8_t sats;      // Satelliten
  double lat, lon;
  float course;      // Fahrtrichtung in Grad, <0 = unbekannt
  uint8_t hour, minute, weekday;  // Ortszeit, weekday 0 = Montag
  bool time_valid;
  bool demo;         // simulierte Fahrt (kein GPS) - Statuszeile zeigt DEMO
  uint8_t reason;    // warum gilt das Limit, UI_REASON_*
  bool speedo;       // Tachomodus: Mitte zeigt Tempo statt Limit
  bool over;         // zu schnell - vom Aufrufer entschieden, siehe ui.c
} ui_state_t;

/*
 * Begruendung des Limits. Die Werte spiegeln REASON_* aus
 * tools/osm_to_grid.py und stecken dort in den unteren drei Bit des
 * flags-Byte jeder Kette - Reihenfolge also nicht aendern, ohne die
 * Kartendaten neu zu erzeugen.
 *
 * Angezeigt wird nur, was dem Fahrer etwas sagt: SCHILD ist der Normalfall
 * und bleibt stumm, ebenso NONE.
 */
#define UI_REASON_NONE 0
#define UI_REASON_ZONE 1     // Tempo-30-Zone
#define UI_REASON_KINDER 2   // hazard=children, meist Schule/Kindergarten
#define UI_REASON_SPIEL 3    // verkehrsberuhigter Bereich
#define UI_REASON_RAD 4      // Fahrradstrasse
#define UI_REASON_SCHILD 5   // Einzelschild
#define UI_REASON_ZEIT 6     // nur zeitweise gueltig

void ui_create(void);

/*
 * ui_update() uebernimmt neue Messwerte - also im Datentakt, bei uns 5 Hz.
 * ui_tick() bewegt die Anzeige darauf zu und gehoert in jeden Schleifen-
 * durchlauf, direkt neben lv_timer_handler(). Ohne diese Trennung springt
 * der Fuellbalken im Datentakt, also fuenfmal pro Sekunde sichtbar.
 *
 * dt_ms ist die seit dem letzten ui_tick() vergangene Zeit. Die Glaettung
 * rechnet damit zeitbasiert und ist dadurch unabhaengig von der Aufrufrate -
 * Simulator und Geraet sehen gleich aus.
 */
void ui_update(const ui_state_t *s);
void ui_tick(uint32_t dt_ms);

#ifdef __cplusplus
}
#endif

#endif  // UI_H
