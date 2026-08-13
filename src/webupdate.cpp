/*
 * webupdate.cpp - siehe webupdate.h und den Abschnitt "KARTEN-UPDATE UEBER
 * WLAN" in config.h fuer die Begruendung.
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

// Zustand des gerade laufenden Uploads - der WebServer ruft den
// Chunk-Handler mehrfach auf, bevor der eigentliche Antwort-Handler drankommt.
static File upload_file;
static String upload_pending_path;
static String upload_error;
static bool upload_ok = false;

struct RegionEntry {
  String name;   // Basisname ohne Endung, z.B. "berlin"
  size_t bytes;
};

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

static String basenameOf(const String &path) {
  int cut = path.lastIndexOf('/');
  int cut2 = path.lastIndexOf('\\');
  if (cut2 > cut) cut = cut2;
  return cut >= 0 ? path.substring(cut + 1) : path;
}

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

// Nur Buchstaben, Ziffern, _ und - erlaubt: schliesst Pfadtricks aus und
// bleibt auf jedem Dateisystem unproblematisch (heute LittleFS, siehe
// backlog.md Punkt 5 fuer SD als moeglichen Nachfolger).
static bool validRegionName(const String &n) {
  if (n.isEmpty() || n.length() > 40) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

// Listet Dateien eines Ordners, deren Name auf 'suffix' endet, und liefert
// den Namen ohne dieses Suffix zurueck. Dient sowohl fuer GRID_DIR (Suffix
// ".msg") als auch fuer PENDING_DIR (zusaetzlich ".msg.del"-Marker).
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

// Regionsnamen, die nach Anwenden aller vorgemerkten Aenderungen uebrig
// blieben - dieselbe Logik wie applyPendingMapChanges(), nur ohne etwas
// anzufassen. Dient der Anzeige und der MAX_REGIONS-Pruefung beim Upload.
#define PENDING_ARR_MAX 20

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

void applyPendingMapChanges(fs::FS &fs) {
  if (!fs.exists(PENDING_DIR)) return;
  // Auf einem frisch geflashten Geraet, das noch nie "uploadfs" gesehen hat,
  // gibt es GRID_DIR unter Umstaenden noch gar nicht - ohne das wuerde das
  // Uebernehmen unten stillschweigend fehlschlagen.
  if (!fs.exists(GRID_DIR)) fs.mkdir(GRID_DIR);

  // Erst alle Namen einsammeln, dann erst aendern - waehrend der Iteration
  // eines Verzeichnisses gleichzeitig Dateien darin umzubenennen oder zu
  // loeschen ist nicht garantiert sauber.
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
        Serial.printf("[Update] %s geloescht\n", target.c_str());
      }
      fs.remove(full);
    } else if (names[i].endsWith(".msg")) {
      String target = String(GRID_DIR) + "/" + names[i];
      if (fs.exists(target)) fs.remove(target);
      if (fs.rename(full, target)) {
        Serial.printf("[Update] %s uebernommen\n", target.c_str());
      } else {
        Serial.printf("[Update] %s konnte nicht uebernommen werden\n",
                      target.c_str());
      }
    } else {
      Serial.printf("[Update] unerwartete Datei %s in %s, geloescht\n",
                    names[i].c_str(), PENDING_DIR);
      fs.remove(full);
    }
  }
}

// ---------------------------------------------------------------------------
// HTTP-Handler
// ---------------------------------------------------------------------------

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
      // clientContentLength() ist die Groesse der ganzen Anfrage inklusive
      // multipart-Rahmen, also eine Obergrenze, keine exakte Dateigroesse -
      // genau deshalb der Sicherheitsabstand AP_FREE_MARGIN_BYTES.
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
      Serial.printf("[Update] Upload abgelehnt: %s\n", upload_error.c_str());
    } else {
      Serial.printf("[Update] Upload beginnt: %s\n", base.c_str());
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
      // Dieselbe Kennungspruefung wie SpeedLimitGrid::loadRegion() beim
      // echten Laden - nur eben vorher statt hinterher.
      File f = LittleFS.open(upload_pending_path, FILE_READ);
      uint8_t magic[4] = {0};
      bool okmagic = f && f.read(magic, 4) == 4 && memcmp(magic, "MSG2", 4) == 0;
      if (f) f.close();
      if (!okmagic) {
        upload_error = "keine gueltige .msg-Datei (Format MSG2 erwartet)";
        LittleFS.remove(upload_pending_path);
      } else {
        upload_ok = true;
        Serial.printf("[Update] %s wartet auf Neustart (%u Byte)\n",
                      upload_pending_path.c_str(), (unsigned)up.totalSize);
      }
    }

  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (upload_file) upload_file.close();
    if (upload_pending_path.length()) LittleFS.remove(upload_pending_path);
    upload_error = "Uebertragung abgebrochen";
  }
}

static void handleUploadDone() {
  if (upload_ok) {
    server.send(200, "text/plain; charset=utf-8", "OK");
  } else {
    server.send(400, "text/plain; charset=utf-8",
               upload_error.length() ? upload_error : "Upload fehlgeschlagen");
  }
}

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
  Serial.printf("[Update] Loeschung von %s vorgemerkt\n", name.c_str());
  server.sendHeader("Location", "/");
  server.send(303);
}

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
  Serial.printf("[Update] vorgemerkte Aenderung an %s verworfen\n",
               name.c_str());
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleRestart() {
  server.send(200, "text/plain; charset=utf-8", "Starte neu ...");
  delay(300);   // Antwort noch rausschreiben, bevor die Verbindung abreisst
  ESP.restart();
}

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
    if (!isNew) continue;   // steht oben schon als "wird ersetzt"
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
// Access Point
// ---------------------------------------------------------------------------

static void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);   // Passphrase NULL = offen, siehe config.h
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[Update] AP \"%s\" gestartet, http://%s/\n", AP_SSID,
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

static void stopAP() {
  Serial.println("[Update] AP ohne Verbindung - abgeschaltet");
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  ap_running = false;
}

void webupdateBegin() { startAP(); }

void webupdateLoop() {
  // DEMO_PIN 10 s gehalten startet den AP erneut, falls er schon aus ist -
  // siehe backlog.md Punkt 1 und die Begruendung bei AP_HOLD_TRIGGER_MS.
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
    // Verbindung haelt den AP am Leben und startet die Abschaltfrist bei
    // jeder Pruefung neu - siehe AP_IDLE_TIMEOUT_MS in config.h.
    ap_deadline_ms = millis() + AP_IDLE_TIMEOUT_MS;
  } else if (millis() > ap_deadline_ms) {
    stopAP();
  }
}
