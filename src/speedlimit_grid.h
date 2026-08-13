/*
 * speedlimit_grid.h - Kartenlookup für die Tempolimit-Anzeige
 *
 * Liest Regionsdateien im Format MSG2 (siehe tools/osm_to_grid.py, dort steht
 * das Format vollständig - Änderungen immer in beiden Dateien).
 *
 * Mehrere Regionen liegen als einzelne .msg-Dateien nebeneinander, etwa
 * /maps/berlin.msg und /maps/brandenburg.msg. `begin()` liest beim Start alle
 * Köpfe und Indizes ins PSRAM; ein Lookup fragt nur die Regionen, deren
 * Bounding-Box die Position enthält. Überlappungen sind erlaubt, dann gewinnt
 * der nächstgelegene Treffer.
 *
 * Die Karte kommt aus einem beliebigen Arduino-Dateisystem: `begin()` bekommt
 * ein bereits gemountetes `fs::FS`. Damit läuft derselbe Lookup über LittleFS
 * (Flash) und später über SD, ohne dass hier etwas geändert wird.
 */

#pragma once

#include <Arduino.h>
#include <FS.h>
#include <math.h>

#include "config.h"   // MATCH_*, CELL_CACHE_SLOTS, MAX_REGIONS, GRID_DIR

class SpeedLimitGrid {
 public:
  // fs muss gemountet sein. dir enthält die .msg-Dateien.
  bool begin(fs::FS &fs, const char *dir = GRID_DIR) {
    fs_ = &fs;
    File d = fs_->open(dir);
    if (!d || !d.isDirectory()) {
      Serial.printf("[Grid] Ordner %s fehlt\n", dir);
      return false;
    }
    for (File e = d.openNextFile(); e; e = d.openNextFile()) {
      if (e.isDirectory()) continue;
      const char *n = e.name();
      size_t l = strlen(n);
      if (l < 5 || strcmp(n + l - 4, ".msg") != 0) continue;
      if (n_regions_ >= MAX_REGIONS) {
        Serial.println("[Grid] mehr Regionen als MAX_REGIONS, Rest ignoriert");
        break;
      }
      char path[96];
      snprintf(path, sizeof(path), "%s/%s", dir, n);
      if (loadRegion(path)) n_regions_++;
    }
    ready_ = (n_regions_ > 0);
    if (!ready_) Serial.println("[Grid] keine gueltige Region gefunden");
    return ready_;
  }

  bool isReady() const { return ready_; }
  uint8_t regionCount() const { return n_regions_; }
  const char *regionName(uint8_t i) const {
    return i < n_regions_ ? regions_[i].name : "";
  }
  // Begruendung des zuletzt gelieferten Limits (REASON_* aus osm_to_grid.py,
  // gespiegelt als UI_REASON_* in ui.h). 0 = keine Angabe.
  uint8_t reason() const { return last_reason_; }

  uint32_t cacheHits() const { return cache_hits_; }
  uint32_t cacheReads() const { return cache_reads_; }

  /*
   * Liefert das Tempolimit an der Position:
   *   >0   km/h
   *   255  unbegrenzt
   *   -1   kein Treffer / unbekannt
   *
   * course_deg: GPS-Kurs (0-360), bei <0 wird der Richtungsfilter übersprungen.
   * hour_local: Ortszeit-Stunde 0-23, weekday: 0=Mo ... 6=So.
   * time_valid=false -> zeitliche Limits werden ignoriert (Grundlimit gilt).
   */
  int lookup(double lat, double lon, float course_deg, float speed_kmh,
             uint8_t hour_local = 0, uint8_t weekday = 0,
             bool time_valid = false, double lat_ahead = 0.0,
             double lon_ahead = 0.0) {
    if (!ready_) return -1;

    int32_t lat_e6 = (int32_t)llround(lat * 1e6);
    int32_t lon_e6 = (int32_t)llround(lon * 1e6);

    float m_per_ulat = 0.111320f;
    float m_per_ulon = 0.111320f * cosf((float)lat * (float)DEG_TO_RAD);

    use_course_ = (course_deg >= 0.0f) && (speed_kmh >= COURSE_MIN_KMH);
    course_ = course_deg;
    hour_ = hour_local;
    weekday_ = weekday;
    time_valid_ = time_valid;

    ScanCtx cx{};
    cx.use_ahead = (lat_ahead != 0.0 || lon_ahead != 0.0);
    int32_t lat2_e6 = cx.use_ahead ? (int32_t)llround(lat_ahead * 1e6) : 0;
    int32_t lon2_e6 = cx.use_ahead ? (int32_t)llround(lon_ahead * 1e6) : 0;
    cx.m_per_ulat = m_per_ulat;
    cx.m_per_ulon = m_per_ulon;
    cx.best_score = cx.best_dist = cx.prev_score = cx.prev_dist = 1e9f;
    cx.best_speed = cx.prev_speed = -1;

    for (uint8_t ri = 0; ri < n_regions_; ri++) {
      Region &R = regions_[ri];
      int32_t row = (lat_e6 - R.lat_min_e6) / (int32_t)R.cell_lat_e6;
      int32_t col = (lon_e6 - R.lon_min_e6) / (int32_t)R.cell_lon_e6;
      // Position ausserhalb dieser Region? Nachbarzellen zaehlen noch mit.
      if (row < -1 || col < -1 || row > (int32_t)R.n_rows ||
          col > (int32_t)R.n_cols)
        continue;

      for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
          int32_t r = row + dr, c = col + dc;
          if (r < 0 || c < 0 || r >= (int32_t)R.n_rows ||
              c >= (int32_t)R.n_cols)
            continue;
          const CellSlot *cell = loadCell(ri, r * R.n_cols + c);
          if (!cell) continue;

          int32_t base_lat = R.lat_min_e6 + r * (int32_t)R.cell_lat_e6;
          int32_t base_lon = R.lon_min_e6 + c * (int32_t)R.cell_lon_e6;
          cx.plat = lat_e6 - base_lat;
          cx.plon = lon_e6 - base_lon;
          cx.alat = lat2_e6 - base_lat;
          cx.alon = lon2_e6 - base_lon;
          scanCell(R, cell, cx);
        }
      }
    }

    // Fuer die Reichweitenschwelle zaehlt der echte Abstand jetzt, nicht die
    // mit der Vorausschau gewichtete Bewertung.
    if (cx.best_speed < 0 || cx.best_dist > MATCH_MAX_DIST_M) {
      last_speed_ = -1;
      last_reason_ = 0;
      return -1;
    }
    // Hysterese: beim vorherigen Limit bleiben, solange es plausibel ist
    if (cx.prev_speed > 0 && cx.prev_dist <= MATCH_MAX_DIST_M &&
        cx.prev_score < cx.best_score * MATCH_HYSTERESIS) {
      last_speed_ = cx.prev_speed;
      last_reason_ = cx.prev_reason;
      return cx.prev_speed;
    }
    last_speed_ = cx.best_speed;
    last_reason_ = cx.best_reason;
    return cx.best_speed;
  }

 private:
  struct Region {
    File f;
    char name[24];
    int32_t lat_min_e6, lon_min_e6;
    uint32_t cell_lat_e6, cell_lon_e6;
    uint16_t n_rows, n_cols;
    uint16_t q_lat, q_lon;      // Mikrograd je Rasterschritt
    uint32_t n_cells;
    uint32_t data_off;
    uint8_t *index = nullptr;   // (n_cells+1) * 8 Byte, im PSRAM
  };

  /*
   * Suchzustand eines Lookups. Als Struktur statt zehn Referenzparametern -
   * mit der Vorausschau kamen zwei Positionen und getrennte Bewertung dazu.
   *
   * score = d_jetzt + PREDICT_WEIGHT * d_voraus  entscheidet die Auswahl,
   * dist  = d_jetzt                              entscheidet die Reichweite.
   */
  struct ScanCtx {
    int32_t plat, plon;     // Position, relativ zur Zellecke
    int32_t alat, alon;     // vorausgeschaute Position, dito
    bool use_ahead;
    float m_per_ulat, m_per_ulon;
    float best_score, best_dist;
    int best_speed;
    uint8_t best_reason;
    float prev_score, prev_dist;
    int prev_speed;
    uint8_t prev_reason;
  };

  struct CellSlot {
    int8_t region = -1;
    uint32_t cell = 0;
    uint8_t *buf = nullptr;
    uint32_t cap = 0;
    uint32_t len = 0;
    uint32_t used = 0;
  };

  fs::FS *fs_ = nullptr;
  Region regions_[MAX_REGIONS];
  uint8_t n_regions_ = 0;
  CellSlot slots_[CELL_CACHE_SLOTS];
  uint32_t tick_ = 0;
  uint32_t cache_hits_ = 0, cache_reads_ = 0;
  bool ready_ = false;
  int last_speed_ = -1;
  uint8_t last_reason_ = 0;
  bool use_course_ = false;
  float course_ = -1.0f;
  uint8_t hour_ = 0, weekday_ = 0;
  bool time_valid_ = false;

  // ---------- Laden ----------
  bool loadRegion(const char *path) {
    Region &R = regions_[n_regions_];
    R.f = fs_->open(path, FILE_READ);
    if (!R.f) return false;

    uint8_t h[64];
    if (R.f.read(h, 64) != 64 || memcmp(h, "MSG2", 4) != 0) {
      Serial.printf("[Grid] %s: kein MSG2-Format\n", path);
      R.f.close();
      return false;
    }
    memcpy(&R.lat_min_e6, h + 4, 4);
    memcpy(&R.lon_min_e6, h + 8, 4);
    memcpy(&R.cell_lat_e6, h + 12, 4);
    memcpy(&R.cell_lon_e6, h + 16, 4);
    memcpy(&R.n_rows, h + 20, 2);
    memcpy(&R.n_cols, h + 22, 2);
    memcpy(&R.q_lat, h + 24, 2);
    memcpy(&R.q_lon, h + 26, 2);
    memcpy(&R.n_cells, h + 28, 4);
    uint32_t index_off;
    memcpy(&index_off, h + 32, 4);
    memcpy(&R.data_off, h + 36, 4);
    memcpy(R.name, h + 40, 23);
    R.name[23] = 0;

    size_t need = ((size_t)R.n_cells + 1) * 8;
    R.index = (uint8_t *)ps_malloc(need);
    if (!R.index) {
      Serial.printf("[Grid] PSRAM reicht nicht fuer %u Byte Index\n",
                    (unsigned)need);
      R.f.close();
      return false;
    }
    R.f.seek(index_off);
    uint32_t t0 = millis();
    size_t done = 0;
    const size_t CHUNK = 32768;
    while (done < need) {
      size_t n = R.f.read(R.index + done, min(CHUNK, need - done));
      if (n == 0) break;
      done += n;
    }
    if (done != need) {
      Serial.printf("[Grid] %s: Index unvollstaendig\n", path);
      free(R.index);
      R.index = nullptr;
      R.f.close();
      return false;
    }
    Serial.printf("[Grid] %-12s %ux%u Zellen, %u belegt, Index %.0f KiB in %lu ms\n",
                  R.name, R.n_rows, R.n_cols, (unsigned)R.n_cells, need / 1024.0,
                  (unsigned long)(millis() - t0));
    return true;
  }

  // ---------- Zellblöcke ----------
  /* Binärsuche im Index. Der Index ist nach cell_id sortiert und die Offsets
     wachsen monoton, deshalb ergibt sich die Blocklänge aus dem Folgeeintrag -
     eine eigene Längenangabe je Zelle wäre verschenkter Platz. */
  bool findCell(const Region &R, uint32_t cell, uint32_t *off, uint32_t *len) const {
    int32_t lo = 0, hi = (int32_t)R.n_cells - 1;
    while (lo <= hi) {
      int32_t mid = (lo + hi) / 2;
      uint32_t id;
      memcpy(&id, R.index + (size_t)mid * 8, 4);
      if (id == cell) {
        uint32_t a, b;
        memcpy(&a, R.index + (size_t)mid * 8 + 4, 4);
        memcpy(&b, R.index + (size_t)(mid + 1) * 8 + 4, 4);
        *off = a;
        *len = b - a;
        return *len > 0;
      }
      if (id < cell) lo = mid + 1;
      else hi = mid - 1;
    }
    return false;
  }

  const CellSlot *loadCell(uint8_t ri, uint32_t cell) {
    for (CellSlot &s : slots_) {
      if (s.region == (int8_t)ri && s.cell == cell && s.len) {
        s.used = ++tick_;
        cache_hits_++;
        return &s;
      }
    }
    Region &R = regions_[ri];
    uint32_t off, len;
    if (!findCell(R, cell, &off, &len)) return nullptr;

    CellSlot *v = &slots_[0];
    for (CellSlot &s : slots_) {
      if (s.region < 0) { v = &s; break; }
      if (s.used < v->used) v = &s;
    }
    if (v->cap < len) {
      uint32_t want = (len + CELL_ALLOC_GRAN - 1) / CELL_ALLOC_GRAN * CELL_ALLOC_GRAN;
      uint8_t *p = (uint8_t *)ps_realloc(v->buf, want);
      if (!p) {
        Serial.println("[Grid] Zellpuffer konnte nicht belegt werden");
        v->region = -1;
        v->len = 0;
        return nullptr;
      }
      v->buf = p;
      v->cap = want;
    }
    v->region = -1;   // ungültig, bis der Block vollständig drin ist
    v->len = 0;
    if (!R.f.seek(R.data_off + off)) return nullptr;
    if (R.f.read(v->buf, len) != len) return nullptr;
    v->region = (int8_t)ri;
    v->cell = cell;
    v->len = len;
    v->used = ++tick_;
    cache_reads_++;
    return v;
  }

  // ---------- Dekodierung ----------
  static uint32_t rdVar(const uint8_t *&p, const uint8_t *end) {
    uint32_t v = 0;
    int sh = 0;
    while (p < end) {
      uint8_t b = *p++;
      v |= (uint32_t)(b & 0x7F) << sh;
      if (!(b & 0x80)) break;
      sh += 7;
    }
    return v;
  }
  static inline int32_t unzig(uint32_t v) {
    return (int32_t)(v >> 1) ^ -(int32_t)(v & 1);
  }

  bool condActive(uint8_t hour_from, uint8_t hour_to, uint8_t weekdays) const {
    if (!time_valid_) return false;
    if (!((weekdays >> weekday_) & 0x01)) return false;
    if (hour_from < hour_to) return hour_ >= hour_from && hour_ < hour_to;
    return hour_ >= hour_from || hour_ < hour_to;   // über Mitternacht
  }

  void scanCell(const Region &R, const CellSlot *cell, ScanCtx &cx) {
    const uint8_t *p = cell->buf;
    const uint8_t *end = cell->buf + cell->len;
    uint32_t n_chains = rdVar(p, end);

    for (uint32_t ch = 0; ch < n_chains && p < end; ch++) {
      if (p + 2 > end) return;
      int speed = *p++;
      uint8_t flags = *p++;
      uint8_t why = flags & 0x07;   // Bit0-2: Begruendung
      if (flags & 0x80) {
        if (p + 4 > end) return;
        uint8_t cs = p[0], hf = p[1], ht = p[2], wd = p[3];
        p += 4;
        if (condActive(hf, ht, wd)) {
          speed = cs;
        } else {
          /*
           * Die Begruendung gehoert zur Bedingung, nicht zum Grundlimit.
           * Eine Strasse mit "50, aber 30 von 6 bis 17 Uhr wegen Kindern"
           * zeigte um 18 Uhr sonst "50 KINDER" - was Unsinn ist, weil die
           * Kinderbeschraenkung gerade nicht gilt. Ausserhalb der Zeit steht
           * deshalb nur die Zahl.
           */
          why = 0;
        }
      }
      uint32_t n_pts = rdVar(p, end);
      if (n_pts < 2) return;

      int32_t qa = unzig(rdVar(p, end));
      int32_t qb = unzig(rdVar(p, end));
      int32_t alat = qa * (int32_t)R.q_lat;
      int32_t alon = qb * (int32_t)R.q_lon;

      for (uint32_t k = 1; k < n_pts; k++) {
        if (p >= end) return;
        qa += unzig(rdVar(p, end));
        qb += unzig(rdVar(p, end));
        int32_t blat = qa * (int32_t)R.q_lat;
        int32_t blon = qb * (int32_t)R.q_lon;

        // speed==0 heisst: nur eine Bedingung, die gerade nicht greift
        if (speed > 0) {
          float d = pointSegDist(cx.plat, cx.plon, alat, alon, blat, blon,
                                 cx.m_per_ulat, cx.m_per_ulon);
          float score = d;
          if (cx.use_ahead) {
            // Wie nah liegt das Segment noch, wenn man kurz weitergefahren
            // ist? Eine Querstrasse, die man nur ueberquert, faellt hier zurueck.
            float da = pointSegDist(cx.alat, cx.alon, alat, alon, blat, blon,
                                    cx.m_per_ulat, cx.m_per_ulon);
            score = d + PREDICT_WEIGHT * da;
          }
          bool ok = true;
          if (use_course_) {
            float bear = bearingDeg(alat, alon, blat, blon, cx.m_per_ulat,
                                    cx.m_per_ulon);
            float diff = fabsf(angleDiff(course_, bear));
            if (diff > 90.0f) diff = 180.0f - diff;   // Gegenrichtung zulassen
            ok = (diff <= COURSE_TOLERANCE_DEG);
          }
          if (ok) {
            if (score < cx.best_score) {
              cx.best_score = score;
              cx.best_dist = d;
              cx.best_speed = speed;
              cx.best_reason = why;
            }
            if (last_speed_ > 0 && speed == last_speed_ && score < cx.prev_score) {
              cx.prev_score = score;
              cx.prev_dist = d;
              cx.prev_speed = speed;
              cx.prev_reason = why;
            }
          }
        }
        alat = blat;
        alon = blon;
      }
    }
  }

  // Abstand Punkt <-> Strecke in Metern (lokal-eben genähert)
  static float pointSegDist(int32_t plat, int32_t plon, int32_t alat, int32_t alon,
                            int32_t blat, int32_t blon, float m_per_ulat,
                            float m_per_ulon) {
    float px = (plon - alon) * m_per_ulon;
    float py = (plat - alat) * m_per_ulat;
    float bx = (blon - alon) * m_per_ulon;
    float by = (blat - alat) * m_per_ulat;
    float len2 = bx * bx + by * by;
    if (len2 < 0.01f) return sqrtf(px * px + py * py);
    float t = (px * bx + py * by) / len2;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float dx = px - t * bx, dy = py - t * by;
    return sqrtf(dx * dx + dy * dy);
  }

  static float bearingDeg(int32_t alat, int32_t alon, int32_t blat, int32_t blon,
                          float m_per_ulat, float m_per_ulon) {
    float dx = (blon - alon) * m_per_ulon;
    float dy = (blat - alat) * m_per_ulat;
    float deg = atan2f(dx, dy) * 180.0f / (float)PI;
    return deg < 0.0f ? deg + 360.0f : deg;
  }

  static float angleDiff(float a, float b) {
    return fmodf(a - b + 540.0f, 360.0f) - 180.0f;
  }
};
