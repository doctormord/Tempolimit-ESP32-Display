/*
 * speedlimit_grid.h - map lookup for the speed-limit display
 *
 * Reads region files in the MSG2 format (see tools/osm_to_grid.py, which
 * documents the format in full - changes must always be made in both
 * files).
 *
 * Multiple regions sit side by side as individual .msg files, e.g.
 * /maps/berlin.msg and /maps/brandenburg.msg. `begin()` reads all headers
 * and indexes into PSRAM at startup; a lookup only queries the regions
 * whose bounding box contains the position. Overlaps are allowed, and the
 * closest match wins.
 *
 * The map comes from any Arduino filesystem: `begin()` receives an
 * already-mounted `fs::FS`. That way the same lookup code runs over
 * LittleFS (flash) today and over SD later, without any change here.
 */

#pragma once

#include <Arduino.h>
#include <FS.h>
#include <math.h>

#include "config.h"   // MATCH_*, CELL_CACHE_SLOTS, MAX_REGIONS, GRID_DIR

class SpeedLimitGrid {
 public:
  /*
   * begin(fs, dir) - scan a directory for region files and load them all.
   *
   * Parameters:
   *   fs  - already-mounted filesystem (LittleFS today, SD later)
   *   dir - directory containing the .msg region files
   *
   * Opens every entry ending in ".msg", hands each to loadRegion(), and
   * stops once MAX_REGIONS is reached (extra region files are silently
   * skipped, logged once). Ready state is true only if at least one region
   * loaded successfully.
   */
  bool begin(fs::FS &fs, const char *dir = GRID_DIR) {
    fs_ = &fs;
    File d = fs_->open(dir);
    if (!d || !d.isDirectory()) {
      Serial.printf("[Grid] directory %s missing\n", dir);
      return false;
    }
    for (File e = d.openNextFile(); e; e = d.openNextFile()) {
      if (e.isDirectory()) continue;
      const char *n = e.name();
      size_t l = strlen(n);
      if (l < 5 || strcmp(n + l - 4, ".msg") != 0) continue;
      if (n_regions_ >= MAX_REGIONS) {
        Serial.println("[Grid] more regions than MAX_REGIONS, rest ignored");
        break;
      }
      char path[96];
      snprintf(path, sizeof(path), "%s/%s", dir, n);
      if (loadRegion(path)) n_regions_++;
    }
    ready_ = (n_regions_ > 0);
    if (!ready_) Serial.println("[Grid] no valid region found");
    return ready_;
  }

  bool isReady() const { return ready_; }
  uint8_t regionCount() const { return n_regions_; }
  const char *regionName(uint8_t i) const {
    return i < n_regions_ ? regions_[i].name : "";
  }
  // Reason code for the most recently delivered limit (REASON_* from
  // osm_to_grid.py, mirrored as UI_REASON_* in ui.h). 0 = none given.
  uint8_t reason() const { return last_reason_; }

  uint32_t cacheHits() const { return cache_hits_; }
  uint32_t cacheReads() const { return cache_reads_; }

  /*
   * lookup(lat, lon, course_deg, speed_kmh, hour_local, weekday,
   *        time_valid, lat_ahead, lon_ahead, now_ms) - find the speed
   * limit at the given position.
   *
   * Parameters:
   *   lat, lon        - current position, decimal degrees
   *   course_deg      - GPS heading (0-360); <0 skips the direction filter
   *   speed_kmh       - driven speed, used to decide whether course is
   *                      trustworthy (see COURSE_MIN_KMH)
   *   hour_local      - local hour 0-23
   *   weekday         - 0=Monday ... 6=Sunday
   *   time_valid      - false disables conditional/time-limited speeds
   *                      (only the base limit applies)
   *   lat_ahead, lon_ahead - look-ahead position (0,0 disables it), used to
   *                      score candidates against where the vehicle will be
   *                      shortly, not just where it is now
   *   now_ms          - caller's millis(), needed for the time-bounded
   *                      hysteresis (see MATCH_HYSTERESIS_MAX_MS in
   *                      config.h)
   *
   * Returns >0 (km/h), 255 (unrestricted), or -1 (no match / unknown).
   *
   * Scans the 3x3 block of cells around the current cell in every region
   * whose bounding box could plausibly contain the position (row/col
   * allowed to be one cell outside the region, since a road can sit just
   * past the region's own edge), scores every chain segment found there via
   * scanCell(), and then applies time-bounded hysteresis (see the comment
   * further down) before returning.
   */
  int lookup(double lat, double lon, float course_deg, float speed_kmh,
             uint8_t hour_local, uint8_t weekday, bool time_valid,
             double lat_ahead, double lon_ahead, uint32_t now_ms) {
    if (!ready_) return -1;

    int32_t lat_e6 = (int32_t)llround(lat * 1e6);
    int32_t lon_e6 = (int32_t)llround(lon * 1e6);

    float m_per_ulat = EARTH_M_PER_DEG_LAT * 1e-6f;
    float m_per_ulon = EARTH_M_PER_DEG_LAT * 1e-6f * cosf((float)lat * (float)DEG_TO_RAD);

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
      // Outside this region? Neighbor cells can still count.
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

    // The range threshold uses the real current distance, not the
    // look-ahead-weighted score.
    if (cx.best_speed < 0 || cx.best_dist > MATCH_MAX_DIST_M) {
      last_speed_ = -1;
      last_reason_ = 0;
      return -1;
    }
    /*
     * Hysteresis: stay on the previous limit as long as it's plausible -
     * BUT only for MATCH_HYSTERESIS_MAX_MS. Without the time bound, the
     * display used to stick at 30 for over 400 m after turning out of a
     * 30-zone onto an unrelated 50 road, because a long zone chain running
     * parallel to the route stayed within the search radius the whole way
     * and scored close enough (confirmed on a real test drive, see
     * history.md). A restart fixed it instantly, because last_speed_ falls
     * back to -1 there and the first lookup afterward decides purely by
     * distance/course with no hysteresis at all - that was the clue that
     * the problem was the hold duration, not a wrong match.
     *
     * The time bound is enough for a brief ambiguity at an intersection
     * (seconds), but not for hundreds of meters on a parallel road.
     * hold_since_ms_ records since when last_speed_ has held its current
     * value - not since when hysteresis last kicked in, otherwise every
     * further update would reset the clock to zero and the bound would
     * never trigger.
     */
    bool hyst_ok = cx.prev_speed > 0 && cx.prev_dist <= MATCH_MAX_DIST_M &&
                   cx.prev_score < cx.best_score * MATCH_HYSTERESIS &&
                   (now_ms - hold_since_ms_) < MATCH_HYSTERESIS_MAX_MS;
    int new_speed = hyst_ok ? cx.prev_speed : cx.best_speed;
    uint8_t new_reason = hyst_ok ? cx.prev_reason : cx.best_reason;
    if (new_speed != last_speed_) hold_since_ms_ = now_ms;
    last_speed_ = new_speed;
    last_reason_ = new_reason;
    return new_speed;
  }

 private:
  struct Region {
    File f;
    char name[24];
    int32_t lat_min_e6, lon_min_e6;
    uint32_t cell_lat_e6, cell_lon_e6;
    uint16_t n_rows, n_cols;
    uint16_t q_lat, q_lon;      // microdegrees per grid step
    uint32_t n_cells;
    uint32_t data_off;
    uint8_t *index = nullptr;   // (n_cells+1) * 8 bytes, in PSRAM
  };

  /*
   * Search state for a single lookup. A struct instead of ten reference
   * parameters - the look-ahead feature added a second position and
   * separate scoring, which would have made the parameter list unwieldy.
   *
   * score = d_now + PREDICT_WEIGHT * d_ahead   decides which candidate wins
   * dist  = d_now                              decides the range cutoff
   */
  struct ScanCtx {
    int32_t plat, plon;     // current position, relative to the cell corner
    int32_t alat, alon;     // look-ahead position, same convention
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
  // Since when last_speed_ has held its current value - the basis of the
  // time-bounded hysteresis, see lookup().
  uint32_t hold_since_ms_ = 0;
  bool use_course_ = false;
  float course_ = -1.0f;
  uint8_t hour_ = 0, weekday_ = 0;
  bool time_valid_ = false;

  // ---------- Loading ----------
  /*
   * loadRegion(path) - open one .msg file, verify the header, and load its
   * cell index into PSRAM.
   *
   * Parameters:
   *   path - full path to the region file
   *
   * Reads the fixed 64-byte MSG2 header (magic, bounding box, cell size,
   * grid dimensions, quantization step, cell count, index/data offsets,
   * region name - see tools/osm_to_grid.py for the authoritative layout),
   * then streams the (n_cells+1)*8-byte offset index into a ps_malloc()
   * buffer in 32 KiB chunks. Only the index is kept resident; individual
   * cell payloads are loaded on demand by loadCell(). Returns false (and
   * closes the file) on any format or allocation failure.
   */
  bool loadRegion(const char *path) {
    Region &R = regions_[n_regions_];
    R.f = fs_->open(path, FILE_READ);
    if (!R.f) return false;

    uint8_t h[64];
    if (R.f.read(h, 64) != 64 || memcmp(h, "MSG2", 4) != 0) {
      Serial.printf("[Grid] %s: not MSG2 format\n", path);
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
      Serial.printf("[Grid] PSRAM insufficient for %u bytes of index\n",
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
      Serial.printf("[Grid] %s: index incomplete\n", path);
      free(R.index);
      R.index = nullptr;
      R.f.close();
      return false;
    }
    Serial.printf("[Grid] %-12s %ux%u cells, %u occupied, index %.0f KiB in %lu ms\n",
                  R.name, R.n_rows, R.n_cols, (unsigned)R.n_cells, need / 1024.0,
                  (unsigned long)(millis() - t0));
    return true;
  }

  // ---------- Cell blocks ----------
  /*
   * findCell(R, cell, off, len) - binary-search the region's index for one
   * cell's payload location.
   *
   * Parameters:
   *   R    - region to search
   *   cell - cell id (row * n_cols + col)
   *   off  - out: byte offset of the cell's payload, relative to data_off
   *   len  - out: byte length of the cell's payload
   *
   * The index is sorted by cell_id and offsets grow monotonically, so a
   * cell's block length falls out of the following entry's offset - a
   * separate length field per cell would have been wasted space. Returns
   * false if the cell id isn't present, or is present but empty (len == 0,
   * meaning no chains touch that cell).
   */
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

  /*
   * loadCell(ri, cell) - return a cached (or freshly loaded) cell payload.
   *
   * Parameters:
   *   ri   - region index
   *   cell - cell id (row * n_cols + col)
   *
   * Checks slots_ (a small LRU cache shared across all regions, sized by
   * CELL_CACHE_SLOTS) for a hit first. On a miss, looks the cell up via
   * findCell(), evicts the least-recently-used slot (grown via ps_realloc()
   * in CELL_ALLOC_GRAN steps if the incoming block is bigger than the
   * slot's current capacity), and reads the block from flash. The slot is
   * marked invalid (region = -1) while the read is in flight so a failed
   * read can't leave a stale hit behind. Returns nullptr if the cell has no
   * data or the read/allocation fails.
   */
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
        Serial.println("[Grid] could not allocate cell buffer");
        v->region = -1;
        v->len = 0;
        return nullptr;
      }
      v->buf = p;
      v->cap = want;
    }
    v->region = -1;   // invalid until the block is fully in place
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

  // ---------- Decoding ----------
  /*
   * rdVar(p, end) - read one LEB128 varint and advance the cursor.
   *
   * Parameters:
   *   p   - in/out: current read position, advanced past the varint
   *   end - end of the buffer, guards against reading past it
   *
   * Standard 7-bits-per-byte, continuation bit in bit 7. Matches the varint
   * encoding written by tools/osm_to_grid.py for chain point deltas and
   * counts.
   */
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
  /*
   * unzig(v) - decode a zigzag-encoded varint back to a signed value.
   *
   * Parameters:
   *   v - zigzag-encoded unsigned value from rdVar()
   *
   * Coordinate deltas between consecutive chain points are small and can be
   * negative, so the format zigzag-encodes them before varint packing
   * (0,-1,1,-2,2,... -> 0,1,2,3,4,...) to keep the varint short in both
   * directions.
   */
  static inline int32_t unzig(uint32_t v) {
    return (int32_t)(v >> 1) ^ -(int32_t)(v & 1);
  }

  /*
   * condActive(hour_from, hour_to, weekdays) - is a maxspeed:conditional
   * time window currently in effect?
   *
   * Parameters:
   *   hour_from - window start hour, inclusive
   *   hour_to   - window end hour, exclusive
   *   weekdays  - bitmask, bit n set means weekday n (0=Monday) is covered
   *
   * Returns false outright if the caller has no valid local time
   * (time_valid_). Handles windows that cross midnight (hour_from >
   * hour_to) by treating them as "at or after hour_from OR before
   * hour_to" instead of a plain range check.
   */
  bool condActive(uint8_t hour_from, uint8_t hour_to, uint8_t weekdays) const {
    if (!time_valid_) return false;
    if (!((weekdays >> weekday_) & 0x01)) return false;
    if (hour_from < hour_to) return hour_ >= hour_from && hour_ < hour_to;
    return hour_ >= hour_from || hour_ < hour_to;   // wraps past midnight
  }

  /*
   * scanCell(R, cell, cx) - decode every chain in a cell payload and score
   * each segment against the current (and look-ahead) position.
   *
   * Parameters:
   *   R    - owning region (needed for the quantization step q_lat/q_lon)
   *   cell - loaded cell payload (chain count, then per-chain: speed,
   *          flags, optional conditional-speed block, point count, and
   *          zigzag-varint-delta-encoded coordinates)
   *   cx   - scan context accumulating the best and previous-limit matches
   *          across every region/cell visited by this lookup
   *
   * Per chain: reads speed and flags; bit 7 of flags marks a conditional
   * time-limited speed block (4 bytes: conditional speed, hour_from,
   * hour_to, weekday mask), evaluated via condActive(). If the condition
   * exists but doesn't currently apply, the reason code is cleared to 0 -
   * the reason belongs to the condition, not the base limit, so outside the
   * time window only the plain number is shown (see the config.h/README
   * writeup on why "50 KINDER" outside school hours would be misleading).
   *
   * Chain points are stored as a running zigzag-varint-delta sum in grid
   * quantization units, decoded and rescaled to the same relative
   * micro-degree coordinate system as cx.plat/plon here. Each resulting
   * segment is scored by pointSegDist(), optionally blended with the
   * look-ahead distance (score = d_now + PREDICT_WEIGHT * d_ahead) and
   * filtered by course (bearingDeg()/angleDiff(), opposite-direction travel
   * is still accepted). speed == 0 for a chain means it's a
   * conditional-only entry whose condition isn't active right now, so it's
   * skipped entirely rather than compared.
   */
  void scanCell(const Region &R, const CellSlot *cell, ScanCtx &cx) {
    const uint8_t *p = cell->buf;
    const uint8_t *end = cell->buf + cell->len;
    uint32_t n_chains = rdVar(p, end);

    for (uint32_t ch = 0; ch < n_chains && p < end; ch++) {
      if (p + 2 > end) return;
      int speed = *p++;
      uint8_t flags = *p++;
      uint8_t why = flags & 0x07;   // bit 0-2: reason code
      if (flags & 0x80) {
        if (p + 4 > end) return;
        uint8_t cs = p[0], hf = p[1], ht = p[2], wd = p[3];
        p += 4;
        if (condActive(hf, ht, wd)) {
          speed = cs;
        } else {
          /*
           * The reason belongs to the condition, not the base limit. A
           * road with "50, but 30 from 6am-5pm for children" would
           * otherwise show "50 KINDER" at 6pm - which is nonsense, since
           * the children-related restriction doesn't apply right now.
           * Outside the time window, only the plain number is shown.
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

        // speed==0 means: only a condition that isn't currently active
        if (speed > 0) {
          float d = pointSegDist(cx.plat, cx.plon, alat, alon, blat, blon,
                                 cx.m_per_ulat, cx.m_per_ulon);
          float score = d;
          if (cx.use_ahead) {
            // How close is the segment once the vehicle has moved on a
            // little? A cross street that's merely being crossed falls
            // behind here.
            float da = pointSegDist(cx.alat, cx.alon, alat, alon, blat, blon,
                                    cx.m_per_ulat, cx.m_per_ulon);
            score = d + PREDICT_WEIGHT * da;
          }
          bool ok = true;
          if (use_course_) {
            float bear = bearingDeg(alat, alon, blat, blon, cx.m_per_ulat,
                                    cx.m_per_ulon);
            float diff = fabsf(angleDiff(course_, bear));
            if (diff > 90.0f) diff = 180.0f - diff;   // allow opposite direction
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

  /*
   * pointSegDist(plat, plon, alat, alon, blat, blon, m_per_ulat, m_per_ulon)
   * - distance in meters from a point to a line segment.
   *
   * Parameters:
   *   plat, plon           - query point, relative micro-degree units
   *   alat, alon           - segment start, same units
   *   blat, blon           - segment end, same units
   *   m_per_ulat, m_per_ulon - meters per micro-degree of latitude/
   *                            longitude at the current position, used to
   *                            convert into a locally-flat meter plane
   *                            before doing the geometry
   *
   * Standard point-to-segment projection: clamps the projection parameter
   * t to [0,1] so the closest point stays on the segment (not its
   * infinite extension), and falls back to plain point distance when the
   * segment is degenerate (len2 < 0.01, i.e. under about 10 cm).
   */
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

  /*
   * bearingDeg(alat, alon, blat, blon, m_per_ulat, m_per_ulon) - compass
   * bearing (0-360, 0 = north) from point A to point B.
   *
   * Parameters:
   *   alat, alon - segment start, relative micro-degree units
   *   blat, blon - segment end, same units
   *   m_per_ulat, m_per_ulon - meters per micro-degree, see pointSegDist()
   *
   * Used to compare a chain segment's direction against the GPS course for
   * the direction filter in scanCell().
   */
  static float bearingDeg(int32_t alat, int32_t alon, int32_t blat, int32_t blon,
                          float m_per_ulat, float m_per_ulon) {
    float dx = (blon - alon) * m_per_ulon;
    float dy = (blat - alat) * m_per_ulat;
    float deg = atan2f(dx, dy) * 180.0f / (float)PI;
    return deg < 0.0f ? deg + 360.0f : deg;
  }

  /*
   * angleDiff(a, b) - signed shortest angular difference a - b, in
   * degrees, wrapped to (-180, 180].
   *
   * Parameters:
   *   a - first angle, degrees
   *   b - second angle, degrees
   *
   * The +540/-180 trick avoids a branch for the wrap-around case (e.g.
   * comparing 5 degrees to 355 degrees should yield 10, not -350).
   */
  static float angleDiff(float a, float b) {
    return fmodf(a - b + 540.0f, 360.0f) - 180.0f;
  }
};
