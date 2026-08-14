/*
 * webupdate.cpp - see webupdate.h and the "MAP UPDATES OVER WI-FI" section
 * in config.h for the rationale.
 */

#include "webupdate.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>

#include "config.h"

static WebServer server(80);
static bool ap_running = false;
static uint32_t ap_deadline_ms = 0;
static bool hold_active = false;
static uint32_t hold_start_ms = 0;

// State of the upload currently in progress - the WebServer calls the
// chunk handler multiple times before the actual response handler runs.
static File upload_file;
static String upload_pending_path;
static String upload_error;
static bool upload_ok = false;

struct RegionEntry {
  String name;   // base name without extension, e.g. "berlin"
  size_t bytes;
};

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

/*
 * basenameOf(path) - strip any directory prefix and return just the file
 * name.
 *
 * Parameters:
 *   path - a path that may use '/' or '\' as separator
 *
 * Handles both separators because the upload's filename comes from the
 * browser/OS the user is on, which may be Windows.
 */
static String basenameOf(const String &path) {
  int cut = path.lastIndexOf('/');
  int cut2 = path.lastIndexOf('\\');
  if (cut2 > cut) cut = cut2;
  return cut >= 0 ? path.substring(cut + 1) : path;
}

/*
 * htmlEscape(s) - escape &, < and > for safe inclusion in HTML output.
 *
 * Parameters:
 *   s - the raw string to escape
 *
 * Region names are constrained by validRegionName() and shouldn't contain
 * these characters anyway, but escaping here is cheap insurance against
 * anything unexpected ending up in region-derived filenames.
 */
static String htmlEscape(const String &s) {
  String o;
  o.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else o += c;
  }
  return o;
}

/*
 * validRegionName(n) - check whether n is a safe region name.
 *
 * Parameters:
 *   n - candidate region name (without extension)
 *
 * Only letters, digits, '_' and '-' are allowed: this rules out path
 * tricks and stays uncontroversial on any filesystem (LittleFS today, see
 * backlog.md item 5 for SD as a possible successor).
 */
static bool validRegionName(const String &n) {
  if (n.isEmpty() || n.length() > 40) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

/*
 * listDir(fs, dir, suffix, out, maxOut) - list files in a directory whose
 * name ends in 'suffix', returning the name with that suffix stripped.
 *
 * Parameters:
 *   fs     - filesystem to read from
 *   dir    - directory path to scan (not recursive)
 *   suffix - required filename suffix, stripped from the returned name
 *   out    - destination array of RegionEntry
 *   maxOut - capacity of out
 *
 * Serves both GRID_DIR (suffix ".msg") and PENDING_DIR (also used with the
 * ".msg.del" marker suffix). Returns the number of entries written.
 */
static int listDir(fs::FS &fs, const char *dir, const char *suffix,
                   RegionEntry *out, int maxOut) {
  int n = 0;
  if (!fs.exists(dir)) return 0;
  File d = fs.open(dir);
  if (!d || !d.isDirectory()) return 0;
  size_t suflen = strlen(suffix);
  for (File e = d.openNextFile(); e && n < maxOut; e = d.openNextFile()) {
    if (!e.isDirectory()) {
      String nm = e.name();
      if (nm.length() > suflen && nm.endsWith(suffix)) {
        out[n].name = nm.substring(0, nm.length() - suflen);
        out[n].bytes = e.size();
        n++;
      }
    }
    e.close();
  }
  d.close();
  return n;
}

// Region names left over after applying all staged changes - the same
// logic as applyPendingMapChanges(), just without touching anything.
// Used for the status display and the MAX_REGIONS check on upload.
#define PENDING_ARR_MAX 20

/*
 * effectiveNames(fs, out, maxOut) - compute the region names that would be
 * installed if all currently staged changes were applied right now.
 *
 * Parameters:
 *   fs     - filesystem to read from
 *   out    - destination array of region name strings
 *   maxOut - capacity of out
 *
 * Starts from the installed regions in GRID_DIR, drops any with a pending
 * ".msg.del" marker, then adds any pending ".msg" replacements/new regions
 * not already counted. Returns the resulting count.
 */
static int effectiveNames(fs::FS &fs, String out[], int maxOut) {
  RegionEntry installed[PENDING_ARR_MAX], added[PENDING_ARR_MAX],
      removed[PENDING_ARR_MAX];
  int ni = listDir(fs, GRID_DIR, ".msg", installed, PENDING_ARR_MAX);
  int na = listDir(fs, PENDING_DIR, ".msg", added, PENDING_ARR_MAX);
  int nr = listDir(fs, PENDING_DIR, ".msg.del", removed, PENDING_ARR_MAX);

  int cnt = 0;
  for (int i = 0; i < ni && cnt < maxOut; i++) {
    bool deleted = false;
    for (int j = 0; j < nr; j++)
      if (installed[i].name == removed[j].name) { deleted = true; break; }
    if (!deleted) out[cnt++] = installed[i].name;
  }
  for (int i = 0; i < na && cnt < maxOut; i++) {
    bool have = false;
    for (int j = 0; j < cnt; j++)
      if (out[j] == added[i].name) { have = true; break; }
    if (!have) out[cnt++] = added[i].name;
  }
  return cnt;
}

/*
 * applyPendingMapChanges(fs) - apply changes staged in PENDING_DIR (new/
 * replaced regions, deletion markers) to GRID_DIR.
 *
 * Parameters:
 *   fs - the already-mounted filesystem to operate on
 *
 * Must run before SpeedLimitGrid::begin(), and specifically before any
 * file handle on a region file is open - that's exactly why changes made
 * through the web UI only take effect after a restart.
 */
void applyPendingMapChanges(fs::FS &fs) {
  if (!fs.exists(PENDING_DIR)) return;
  // On a freshly flashed device that has never seen "uploadfs", GRID_DIR
  // may not exist yet - without this, the apply step below would fail
  // silently.
  if (!fs.exists(GRID_DIR)) fs.mkdir(GRID_DIR);

  // Collect all names first, then apply changes - renaming or deleting
  // files in a directory while iterating it at the same time is not
  // guaranteed to be safe.
  String names[PENDING_ARR_MAX];
  int n = 0;
  {
    File d = fs.open(PENDING_DIR);
    if (!d || !d.isDirectory()) return;
    for (File e = d.openNextFile(); e && n < PENDING_ARR_MAX;
        e = d.openNextFile()) {
      if (!e.isDirectory()) names[n++] = String(e.name());
      e.close();
    }
    d.close();
  }

  for (int i = 0; i < n; i++) {
    String full = String(PENDING_DIR) + "/" + names[i];
    if (names[i].endsWith(".msg.del")) {
      String base = names[i].substring(0, names[i].length() - 8);
      String target = String(GRID_DIR) + "/" + base + ".msg";
      if (fs.exists(target)) {
        fs.remove(target);
        Serial.printf("[Update] %s deleted\n", target.c_str());
      }
      fs.remove(full);
    } else if (names[i].endsWith(".msg")) {
      String target = String(GRID_DIR) + "/" + names[i];
      if (fs.exists(target)) fs.remove(target);
      if (fs.rename(full, target)) {
        Serial.printf("[Update] %s applied\n", target.c_str());
      } else {
        Serial.printf("[Update] %s could not be applied\n",
                      target.c_str());
      }
    } else {
      Serial.printf("[Update] unexpected file %s in %s, deleted\n",
                    names[i].c_str(), PENDING_DIR);
      fs.remove(full);
    }
  }
}

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------

/*
 * handleUploadChunk() - WebServer upload callback, invoked repeatedly as
 * multipart chunks of an incoming .msg file arrive.
 *
 * Runs through UPLOAD_FILE_START / _WRITE / _END / _ABORTED. On START it
 * validates the filename and available space and opens a file in
 * PENDING_DIR; on WRITE it streams bytes to that file; on END it checks
 * the "MSG2" magic and marks the upload as ready for the next restart; on
 * ABORTED it cleans up the partial file. All user-visible outcomes are
 * left in upload_error/upload_ok for handleUploadDone() to report once the
 * response handler runs.
 */
static void handleUploadChunk() {
  HTTPUpload &up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    upload_ok = false;
    upload_error = "";
    upload_pending_path = "";

    String base = basenameOf(up.filename);
    String region = base.endsWith(".msg") ? base.substring(0, base.length() - 4)
                                          : base;

    if (!base.endsWith(".msg") || !validRegionName(region)) {
      upload_error = "Dateiname: nur .msg, Buchstaben/Ziffern/_/-";
    }

    if (upload_error.isEmpty()) {
      String names[PENDING_ARR_MAX];
      int cnt = effectiveNames(LittleFS, names, PENDING_ARR_MAX);
      bool have = false;
      for (int i = 0; i < cnt; i++)
        if (names[i] == region) { have = true; break; }
      if (!have && cnt >= MAX_REGIONS) {
        upload_error = "schon " + String(MAX_REGIONS) +
                       " Regionen (MAX_REGIONS) - erst eine loeschen";
      }
    }

    if (upload_error.isEmpty()) {
      // clientContentLength() is the size of the whole request including
      // the multipart framing, so it's an upper bound, not the exact file
      // size - which is exactly why AP_FREE_MARGIN_BYTES exists as a
      // safety margin.
      int need = server.clientContentLength();
      size_t free_bytes = LittleFS.totalBytes() - LittleFS.usedBytes();
      if (need > 0 && (size_t)need + AP_FREE_MARGIN_BYTES > free_bytes) {
        upload_error = "zu wenig Platz (" + String(need / 1024) +
                       " KiB noetig, " + String(free_bytes / 1024) +
                       " KiB frei)";
      }
    }

    if (upload_error.isEmpty()) {
      if (!LittleFS.exists(PENDING_DIR)) LittleFS.mkdir(PENDING_DIR);
      upload_pending_path = String(PENDING_DIR) + "/" + base;
      upload_file = LittleFS.open(upload_pending_path, FILE_WRITE);
      if (!upload_file) upload_error = "Datei konnte nicht angelegt werden";
    }

    if (!upload_error.isEmpty()) {
      Serial.printf("[Update] upload rejected: %s\n", upload_error.c_str());
    } else {
      Serial.printf("[Update] upload starting: %s\n", base.c_str());
    }

  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (upload_error.isEmpty() && upload_file) {
      if (upload_file.write(up.buf, up.currentSize) != up.currentSize) {
        upload_error = "Schreibfehler - Karte voll?";
        upload_file.close();
        LittleFS.remove(upload_pending_path);
      }
    }

  } else if (up.status == UPLOAD_FILE_END) {
    if (upload_file) upload_file.close();
    if (upload_error.isEmpty()) {
      // Same magic-number check as SpeedLimitGrid::loadRegion() performs
      // on the real load - just up front here instead of afterwards.
      File f = LittleFS.open(upload_pending_path, FILE_READ);
      uint8_t magic[4] = {0};
      bool okmagic = f && f.read(magic, 4) == 4 && memcmp(magic, "MSG2", 4) == 0;
      if (f) f.close();
      if (!okmagic) {
        upload_error = "keine gueltige .msg-Datei (Format MSG2 erwartet)";
        LittleFS.remove(upload_pending_path);
      } else {
        upload_ok = true;
        Serial.printf("[Update] %s waiting for restart (%u bytes)\n",
                      upload_pending_path.c_str(), (unsigned)up.totalSize);
      }
    }

  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (upload_file) upload_file.close();
    if (upload_pending_path.length()) LittleFS.remove(upload_pending_path);
    upload_error = "Uebertragung abgebrochen";
  }
}

/*
 * handleUploadDone() - WebServer response handler for POST /upload, called
 * once after handleUploadChunk() has processed all chunks.
 *
 * Reports the outcome left behind by handleUploadChunk() in upload_ok /
 * upload_error as a plain-text HTTP response, which the page's JS reads
 * from XMLHttpRequest.responseText to update the status line.
 */
static void handleUploadDone() {
  if (upload_ok) {
    server.send(200, "text/plain; charset=utf-8", "OK");
  } else {
    server.send(400, "text/plain; charset=utf-8",
               upload_error.length() ? upload_error : "Upload fehlgeschlagen");
  }
}

/*
 * handleDelete() - GET /delete?name=... handler, stages a region for
 * deletion.
 *
 * Doesn't touch GRID_DIR directly (see applyPendingMapChanges() for why);
 * it only drops an empty "<name>.msg.del" marker file in PENDING_DIR and
 * redirects back to the status page.
 */
static void handleDelete() {
  String name = server.arg("name");
  if (!validRegionName(name)) {
    server.send(400, "text/plain; charset=utf-8", "ungueltiger Name");
    return;
  }
  if (!LittleFS.exists(PENDING_DIR)) LittleFS.mkdir(PENDING_DIR);
  String marker = String(PENDING_DIR) + "/" + name + ".msg.del";
  File f = LittleFS.open(marker, FILE_WRITE);
  if (f) f.close();
  Serial.printf("[Update] deletion of %s staged\n", name.c_str());
  server.sendHeader("Location", "/");
  server.send(303);
}

/*
 * handleCancel() - GET /cancel?name=...&kind=... handler, discards a
 * staged change before it's applied.
 *
 * Parameters (via query string):
 *   name - region name
 *   kind - "del" to discard a pending deletion marker, anything else to
 *          discard a pending upload/replacement
 *
 * Just removes the corresponding file from PENDING_DIR; nothing in
 * GRID_DIR is ever touched by this path.
 */
static void handleCancel() {
  String name = server.arg("name");
  String kind = server.arg("kind");
  if (!validRegionName(name)) {
    server.send(400, "text/plain; charset=utf-8", "ungueltiger Name");
    return;
  }
  String path =
      String(PENDING_DIR) + "/" + name + (kind == "del" ? ".msg.del" : ".msg");
  if (LittleFS.exists(path)) LittleFS.remove(path);
  Serial.printf("[Update] staged change to %s discarded\n",
               name.c_str());
  server.sendHeader("Location", "/");
  server.send(303);
}

/*
 * handleRestart() - POST /restart handler, reboots the device so staged
 * map changes take effect.
 *
 * The short delay() lets the HTTP response actually reach the client
 * before the connection is torn down by ESP.restart().
 */
static void handleRestart() {
  server.send(200, "text/plain; charset=utf-8", "Starte neu ...");
  delay(300);   // let the response finish writing out before the connection drops
  ESP.restart();
}

/*
 * handleRoot() - GET / handler, renders the map-management status page.
 *
 * Builds one self-contained HTML document (inline CSS/JS, no external
 * requests) showing storage usage, installed/staged regions with
 * install/replace/delete actions, an upload form, and a restart button.
 * The page text itself is intentionally German - this is user-facing UI
 * on the setup web page, not a developer log, so it follows the same rule
 * as the on-device display strings.
 */
static void handleRoot() {
  RegionEntry installed[PENDING_ARR_MAX], added[PENDING_ARR_MAX],
      removed[PENDING_ARR_MAX];
  int ni = listDir(LittleFS, GRID_DIR, ".msg", installed, PENDING_ARR_MAX);
  int na = listDir(LittleFS, PENDING_DIR, ".msg", added, PENDING_ARR_MAX);
  int nr = listDir(LittleFS, PENDING_DIR, ".msg.del", removed, PENDING_ARR_MAX);

  size_t used = LittleFS.usedBytes(), total = LittleFS.totalBytes();
  int used_pct = total > 0 ? (int)((used * 100) / total) : 0;
  String effNames[PENDING_ARR_MAX];
  int effCnt = effectiveNames(LittleFS, effNames, PENDING_ARR_MAX);

  String html;
  html.reserve(5120);
  html +=
      "<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Tempolimit &ndash; Kartenverwaltung</title><style>"
      "body{font-family:sans-serif;max-width:640px;margin:1em auto;"
      "padding:0 1em;background:#111;color:#eee}"
      "table{width:100%;border-collapse:collapse;margin:1em 0}"
      "td,th{padding:.4em;border-bottom:1px solid #333;text-align:left}"
      "button{padding:.5em 1em;border:0;border-radius:4px;background:#2a6;"
      "color:#fff;font-size:1em}"
      ".bar{background:#333;border-radius:4px;overflow:hidden;height:1.1em;"
      "margin:.5em 0}"
      ".bar>div{background:#2a6;height:100%;width:0}"
      "a.del{color:#e88}"
      "small{color:#999}"
      "#prog{display:none}"
      "</style></head><body>";

  html += "<h1>Kartenverwaltung</h1>";
  html += "<p>Speicher: " + String(used / 1024) + " / " + String(total / 1024) +
          " KiB belegt</p><div class=\"bar\"><div style=\"width:" +
          String(used_pct) + "%\"></div></div>";

  html += "<table><tr><th>Region</th><th>Groesse</th><th>Status</th><th></th></tr>";
  if (ni == 0 && na == 0) {
    html += "<tr><td colspan=\"4\"><small>keine Regionen</small></td></tr>";
  }
  for (int i = 0; i < ni; i++) {
    bool del = false;
    for (int j = 0; j < nr; j++)
      if (removed[j].name == installed[i].name) { del = true; break; }
    bool rep = false;
    size_t repSize = 0;
    for (int j = 0; j < na; j++)
      if (added[j].name == installed[i].name) { rep = true; repSize = added[j].bytes; break; }

    html += "<tr><td>" + htmlEscape(installed[i].name) + "</td><td>" +
            String(installed[i].bytes / 1024) + " KiB</td><td>";
    if (del) {
      html += "wird geloescht (nach Neustart)</td><td>"
              "<a class=\"del\" href=\"/cancel?kind=del&amp;name=" +
              htmlEscape(installed[i].name) + "\">verwerfen</a>";
    } else if (rep) {
      html += "wird ersetzt, " + String(repSize / 1024) +
              " KiB (nach Neustart)</td><td>"
              "<a class=\"del\" href=\"/cancel?kind=new&amp;name=" +
              htmlEscape(installed[i].name) + "\">verwerfen</a>";
    } else {
      html += "installiert</td><td>"
              "<a class=\"del\" href=\"/delete?name=" +
              htmlEscape(installed[i].name) + "\">loeschen</a>";
    }
    html += "</td></tr>";
  }
  for (int i = 0; i < na; i++) {
    bool isNew = true;
    for (int j = 0; j < ni; j++)
      if (installed[j].name == added[i].name) { isNew = false; break; }
    if (!isNew) continue;   // already listed above as "wird ersetzt"
    html += "<tr><td>" + htmlEscape(added[i].name) + "</td><td>" +
            String(added[i].bytes / 1024) + " KiB</td><td>neu (nach Neustart)"
            "</td><td><a class=\"del\" href=\"/cancel?kind=new&amp;name=" +
            htmlEscape(added[i].name) + "\">verwerfen</a></td></tr>";
  }
  html += "</table>";

  html +=
      "<h2>Region hochladen</h2>"
      "<p><small>.msg-Datei aus <code>tools/maps.py</code> bzw. "
      "<code>tools/osm_to_grid.py</code> - die Aufbereitung aus OSM-Rohdaten "
      "passiert weiterhin auf dem Rechner, hier wird nur die fertige Datei "
      "entgegengenommen.</small></p>"
      "<input type=\"file\" id=\"f\" accept=\".msg\">"
      "<button onclick=\"upl()\">Hochladen</button>"
      "<div class=\"bar\" id=\"prog\"><div id=\"progbar\"></div></div>"
      "<p id=\"status\"></p>";

  html += "<h2>Aenderungen uebernehmen</h2>"
          "<p>Neue, ersetzte und geloeschte Regionen wirken erst nach einem "
          "Neustart - bis dahin laesst sich noch etwas verwerfen. "
          "Regionen nach Neustart: " + String(effCnt) + " von " +
          String(MAX_REGIONS) + ".</p>"
          "<form method=\"POST\" action=\"/restart\" "
          "onsubmit=\"return confirm('Jetzt neu starten?')\">"
          "<button type=\"submit\">Jetzt neu starten</button></form>";

  html +=
      "<script>"
      "function upl(){"
      "var i=document.getElementById('f');"
      "if(!i.files.length){document.getElementById('status').textContent="
      "'erst eine Datei waehlen';return;}"
      "var fd=new FormData();fd.append('f',i.files[0],i.files[0].name);"
      "var x=new XMLHttpRequest();x.open('POST','/upload');"
      "document.getElementById('prog').style.display='block';"
      "x.upload.onprogress=function(e){if(e.lengthComputable){"
      "document.getElementById('progbar').style.width="
      "(e.loaded*100/e.total)+'%';}};"
      "x.onload=function(){document.getElementById('status').textContent="
      "x.status==200?'fertig - Region wartet auf Neustart':('Fehler: '+x.responseText);"
      "if(x.status==200)setTimeout(function(){location.reload();},700);};"
      "x.onerror=function(){document.getElementById('status').textContent="
      "'Verbindung verloren';};"
      "x.send(fd);}"
      "</script></body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}

// ---------------------------------------------------------------------------
// Access point
// ---------------------------------------------------------------------------

/*
 * startAP() - bring up the Wi-Fi access point and register HTTP routes.
 *
 * Opens an unsecured AP (see config.h for why a passwordless AP is a
 * deliberate choice) under AP_SSID, wires up all route handlers, and
 * starts the idle-timeout countdown so the AP shuts itself down again if
 * nobody connects.
 */
static void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);   // NULL passphrase = open, see config.h
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[Update] AP \"%s\" started, http://%s/\n", AP_SSID,
               ip.toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadChunk);
  server.on("/delete", HTTP_GET, handleDelete);
  server.on("/cancel", HTTP_GET, handleCancel);
  server.on("/restart", HTTP_POST, handleRestart);
  server.onNotFound([]() {
    server.sendHeader("Location", "/");
    server.send(303);
  });
  server.begin();

  ap_running = true;
  ap_deadline_ms = millis() + AP_IDLE_TIMEOUT_MS;
}

/*
 * stopAP() - tear down the access point and web server.
 *
 * Called once the idle timeout elapses with nobody connected, so the
 * device doesn't keep burning power on Wi-Fi while parked.
 */
static void stopAP() {
  Serial.println("[Update] AP idle with no connection - shutting down");
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  ap_running = false;
}

/*
 * webupdateBegin() - start the access point and web UI.
 *
 * Thin wrapper kept separate from startAP() so the public entry point
 * stays stable even if the internal bring-up logic grows.
 */
void webupdateBegin() { startAP(); }

/*
 * webupdateLoop() - service the access point and web server; call from
 * every loop() iteration.
 *
 * Watches DEMO_PIN for a long hold to restart the AP on demand (see
 * backlog.md item 1 and the rationale at AP_HOLD_TRIGGER_MS), services
 * pending HTTP requests while the AP is up, and restarts the idle-timeout
 * countdown for as long as at least one station stays connected before
 * shutting the AP down via stopAP().
 */
void webupdateLoop() {
  // Holding DEMO_PIN for 10s restarts the AP if it's currently off - see
  // backlog.md item 1 and the rationale at AP_HOLD_TRIGGER_MS.
  bool held = (digitalRead(DEMO_PIN) == LOW);
  if (held) {
    if (!hold_active) {
      hold_active = true;
      hold_start_ms = millis();
    } else if (!ap_running && millis() - hold_start_ms >= AP_HOLD_TRIGGER_MS) {
      startAP();
    }
  } else {
    hold_active = false;
  }

  if (!ap_running) return;

  server.handleClient();

  if (WiFi.softAPgetStationNum() > 0) {
    // A connection keeps the AP alive and restarts the shutdown countdown
    // on every check - see AP_IDLE_TIMEOUT_MS in config.h.
    ap_deadline_ms = millis() + AP_IDLE_TIMEOUT_MS;
  } else if (millis() > ap_deadline_ms) {
    stopAP();
  }
}
