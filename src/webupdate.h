/*
 * webupdate.h - update map data without a toolchain (backlog item 1a)
 *
 * Access point + web UI through which finished .msg region files can be
 * uploaded and installed regions deleted, without PlatformIO or osmium on
 * the user's computer. Details and rationale live with the constants in
 * config.h.
 *
 * Arduino/ESP32-specific (WiFi, LittleFS) - unlike ui.c/ui.h this is not
 * platform-neutral and is not compiled by the simulator (see
 * sim/CMakeLists.txt, which lists its source files individually).
 */

#pragma once

#include <FS.h>

/*
 * applyPendingMapChanges(fs) - apply changes staged in PENDING_DIR (new/
 * replaced regions, deletion markers) to GRID_DIR.
 *
 * Parameters:
 *   fs - the already-mounted filesystem to operate on
 *
 * Must be called before SpeedLimitGrid::begin(), and specifically before
 * any file handle on a region file is open - that's exactly why changes
 * made through the web UI only take effect after a restart.
 */
void applyPendingMapChanges(fs::FS &fs);

/*
 * webupdateBegin() - start the access point and the web UI.
 *
 * Call after LittleFS.begin() and applyPendingMapChanges().
 */
void webupdateBegin();

/*
 * webupdateLoop() - service the access point and web server.
 *
 * Belongs in every loop() iteration. Outside of an active maintenance
 * session this only costs a pin read and a millis() comparison.
 */
void webupdateLoop();
