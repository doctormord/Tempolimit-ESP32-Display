/*
 * webupdate.h - Kartendaten ohne Toolchain aktualisieren (Backlog Punkt 1a)
 *
 * Access Point + Weboberflaeche, ueber die fertige .msg-Regionsdateien
 * hochgeladen und installierte Regionen geloescht werden koennen, ohne
 * PlatformIO oder osmium auf dem Rechner. Details und Begruendung stehen bei
 * den Konstanten in config.h.
 *
 * Arduino/ESP32-spezifisch (WiFi, LittleFS) - anders als ui.c/ui.h nicht
 * plattformneutral und wird vom Simulator nicht mit uebersetzt (siehe
 * sim/CMakeLists.txt, das seine Quelldateien einzeln auflistet).
 */

#pragma once

#include <FS.h>

/*
 * Uebernimmt in PENDING_DIR gesammelte Aenderungen (neue/ersetzte Regionen,
 * Loeschmarkierungen) nach GRID_DIR.
 *
 * Muss vor SpeedLimitGrid::begin() aufgerufen werden, und zwar bevor
 * irgendein File-Handle auf eine Regionsdatei offen ist - genau deshalb
 * wirken Aenderungen ueber die Weboberflaeche erst nach einem Neustart.
 */
void applyPendingMapChanges(fs::FS &fs);

/* Startet den Access Point und die Weboberflaeche. Nach LittleFS.begin()
   und applyPendingMapChanges() aufrufen. */
void webupdateBegin();

/* Gehoert in jeden loop()-Durchlauf. Ausserhalb einer Wartungssitzung kostet
   das nur einen Pin-Read und einen millis()-Vergleich. */
void webupdateLoop();
