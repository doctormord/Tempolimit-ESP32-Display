#!/usr/bin/env python3
"""
osm_to_grid.py - Converts a Geofabrik OSM extract into a compact per-region
data file for the ESP32.

Usage:
    pip install osmium
    wget https://download.geofabrik.de/europe/germany/berlin-latest.osm.pbf
    python3 osm_to_grid.py berlin-latest.osm.pbf ../data/maps/ --name berlin

Result: a single <name>.msg file. Several regions simply sit side by side in
the same folder; the reader picks the right one based on position. Regions
are allowed to overlap.

--------------------------------------------------------------------------
WHY THE FORMAT LOOKS THE WAY IT DOES
--------------------------------------------------------------------------
Measured against the Berlin data of the predecessor format (MSG1, 4.20 MiB):

  Index dense over all of Germany     2.24 MiB -> region gets its own bbox
  Ways cut apart at every cell edge   4.4 pts/segment -> chain them together
  Coordinates at 0.11 m resolution    with a 30 m search radius -> 0.5 m grid
  Absolute uint16 per point           -> zigzag varint on deltas

Together: 4.20 MiB -> 0.66 MiB. The single biggest item is giving each
region its own bounding box: the old index always described the whole of
Germany, even when it only contained Berlin.

--------------------------------------------------------------------------
FILE FORMAT (little endian)  -  mirrored in speedlimit_grid.h
--------------------------------------------------------------------------
Header, 64 bytes:
    char[4]  magic = "MSG2"
    int32    lat_min_e6, lon_min_e6   bottom-left corner, snapped to the grid
    uint32   cell_lat_e6, cell_lon_e6 cell size in microdegrees
    uint16   n_rows, n_cols
    uint16   quant_lat_e6, quant_lon_e6   microdegrees per quantization step
    uint32   n_cells                  number of occupied cells
    uint32   index_off, data_off      byte offsets within this file
    char[24] region name, NUL-terminated

Index, (n_cells + 1) * 8 bytes, ascending by cell_id:
    uint32 cell_id = row * n_cols + col
    uint32 offset  relative to data_off
    The last entry is a sentinel with cell_id = 0xFFFFFFFF; its offset field
    holds the total payload length. A block's length therefore falls out as
    the difference to the next entry - storing a length per cell would be
    wasted space.

Payload, one block per cell:
    varint  n_chains
    per chain:
        uint8   speed       0=unknown, 255=unrestricted, else km/h
        uint8   flags       bit7   = a time condition follows
                            bit0-2 = reason (REASON_*, see below)
                            bit3-6 = unused
                            The reason costs no extra byte, since the flags
                            byte was already there.
        [4-byte condition: cond_speed, hour_from, hour_to, weekdays]
        varint  n_points
        varint  zigzag(q_lat)   first point, grid steps from the cell corner
        varint  zigzag(q_lon)   may be negative (point from a neighboring cell)
        (n_points-1) x varint zigzag(dq_lat), varint zigzag(dq_lon)

Absolute position of a point:
    lat_e6 = lat_min_e6 + row * cell_lat_e6 + q_lat * quant_lat_e6
--------------------------------------------------------------------------
"""

import argparse
import math
import os
import re
import struct
import sys
from collections import defaultdict

import osmium

# --- Global cell grid --------------------------------------------------
# Fixed in place so that cell boundaries of different regions line up with
# each other. Only the bounding box differs per region.
GRID_ORIGIN_LAT_E6 = 47_000_000
GRID_ORIGIN_LON_E6 = 5_700_000
CELL_LAT_E6 = 10_000   # 0.010 degrees, ~1113 m
CELL_LON_E6 = 20_000   # 0.020 degrees, ~1354 m at 52 degrees north

M_PER_ULAT = 0.1113    # meters per microdegree of latitude

# --- Simplification ------------------------------------------------------
# The simplification error is subtracted directly from the matcher's search
# radius (MATCH_MAX_DIST_M, 30 m). Deliberately fine-grained in town, since
# parallel streets there sit only 20 m apart. On a motorway the nearest
# competing road is hundreds of meters away, so it can be coarse.
TOL_M = {"autobahn": 8.0, "land": 3.0, "stadt": 0.5}
QUANT_M = 0.5          # coordinate grid, in meters

HIGHWAY_TYPES = {
    "motorway", "motorway_link", "trunk", "trunk_link",
    "primary", "primary_link", "secondary", "secondary_link",
    "tertiary", "tertiary_link", "unclassified", "residential",
    "living_street", "service",
}

IMPLICIT_DEFAULTS = {
    "motorway": 255,
    "motorway_link": 0,
    "living_street": 7,
    "residential": 50,
    "service": 0,
}

# --- Reason for the limit -------------------------------------------------
# Lives in the lower three bits of the flags byte, which previously only
# used bit 7 - so it costs not a single extra byte.
#
# Measured on the Berlin extract (58,550 ways with maxspeed=30): around 42%
# carry a zone marker, 1.3% hazard=children, 2% a single explicit sign.
# 46.7% carry nothing at all - which is why REASON_NONE is the normal case,
# and the display then stays blank instead of claiming something.
REASON_NONE = 0
REASON_ZONE = 1        # 30 km/h traffic-calmed zone
REASON_CHILDREN = 2    # hazard=children, usually a school or kindergarten
REASON_PLAY_STREET = 3     # "verkehrsberuhigter Bereich" (play street)
REASON_BICYCLE_STREET = 4  # "Fahrradstrasse" (bicycle-priority street)
REASON_SIGN = 5         # route-specific single sign
REASON_TIME_LIMITED = 6  # only in effect at certain times


def reason_code(tags, highway):
    """
    reason_code(tags, highway) - why does this speed limit apply?

    Parameters:
      tags    - the way's OSM tag dict
      highway - the way's highway= value

    The relevant OSM tags are written inconsistently (DE:zone30,
    DE:zone:30, de:zone30, DE:30) and spread across three redundant keys,
    so this normalizes them before classifying. Returns one of the
    REASON_* constants.
    """
    if tags.get("hazard") == "children":
        return REASON_CHILDREN
    if highway == "living_street":
        return REASON_PLAY_STREET

    vals = []
    for k in ("maxspeed:type", "zone:maxspeed", "source:maxspeed"):
        v = tags.get(k)
        if v:
            n = v.strip().lower()
            if n.startswith("de:"):
                n = n[3:]
            vals.append((k, n.replace(":", "")))

    if any("bicycle_road" in n for _, n in vals):
        return REASON_BICYCLE_STREET
    for k, n in vals:
        # zone:maxspeed carries the zone speed in the value itself ("DE:30"),
        # the other two as a prefix ("DE:zone30")
        if n.startswith("zone") or (k == "zone:maxspeed" and n.isdigit()):
            return REASON_ZONE
    if any(n == "sign" for _, n in vals):
        return REASON_SIGN
    return REASON_NONE


def klasse(speed):
    """
    klasse(speed) - derive a road class from the speed limit alone.

    Parameters:
      speed - speed limit in km/h (255 = unrestricted)

    This is as precise as it can get: the original highway= type is no
    longer available at this point in the pipeline. Used only to pick the
    simplification tolerance (TOL_M).
    """
    if speed >= 100 or speed == 255:
        return "autobahn"
    if speed >= 70:
        return "land"
    return "stadt"


def parse_maxspeed(value, highway):
    """
    parse_maxspeed(value, highway) - OSM maxspeed string -> uint8
    (0=unknown, 255=unrestricted).

    Parameters:
      value   - the maxspeed= tag value, or None if absent
      highway - the way's highway= value, used to fill in a sensible
                implicit default (IMPLICIT_DEFAULTS) when maxspeed is absent
    """
    if value is None:
        return IMPLICIT_DEFAULTS.get(highway, 0)
    v = value.strip().lower()
    if v in ("none", "unlimited"):
        return 255
    if v.startswith("de:"):
        return {"de:motorway": 255, "de:urban": 50,
                "de:rural": 100, "de:living_street": 7}.get(v, 0)
    if v == "walk":
        return 7
    if "mph" in v:
        try:
            return min(254, int(round(float(v.replace("mph", "").strip()) * 1.609)))
        except ValueError:
            return 0
    try:
        n = int(float(v))
        return n if 1 <= n <= 254 else 0
    except ValueError:
        return 0


_WEEKDAY_BITS = {"mo": 0, "tu": 1, "we": 2, "th": 3, "fr": 4, "sa": 5, "su": 6}
_COND_RE = re.compile(r"^\s*(?P<speed>[\w.]+)\s*@\s*\((?P<cond>[^)]*)\)\s*$")
_TIME_RE = re.compile(r"(\d{1,2}):(\d{2})\s*-\s*(\d{1,2}):(\d{2})")
_DAYS_RE = re.compile(r"\b(mo|tu|we|th|fr|sa|su)\b\s*-\s*\b(mo|tu|we|th|fr|sa|su)\b",
                      re.IGNORECASE)


def parse_conditional(value, highway):
    """
    parse_conditional(value, highway) - parse 'maxspeed:conditional' into
    (cond_speed, hour_from, hour_to, weekdays), or None if it can't be
    parsed / doesn't contain a usable time window.

    Parameters:
      value   - the maxspeed:conditional= tag value, or None/empty
      highway - passed through to parse_maxspeed() for the conditional speed

    Only handles the "<speed> @ (<condition>)" form, optionally with
    multiple such clauses separated by ";" (only when there's more than one
    parenthesized group, to avoid splitting a single condition that itself
    contains a ";"). Only the first parseable clause with both a valid speed
    and a recognizable time range is used - a day range (e.g. mo-fr) is
    optional and defaults to every day.
    """
    if not value:
        return None
    candidates = [value]
    if ";" in value and value.count("(") > 1:
        candidates = value.split(";")
    for part in candidates:
        m = _COND_RE.match(part)
        if not m:
            continue
        cond_speed = parse_maxspeed(m.group("speed"), highway)
        if cond_speed == 0:
            continue
        cond = m.group("cond")
        tm = _TIME_RE.search(cond)
        if not tm:
            continue
        hour_from = int(tm.group(1)) % 24
        hour_to = int(tm.group(3)) % 24
        if hour_from == hour_to:
            continue
        weekdays = 0x7F
        dm = _DAYS_RE.search(cond)
        if dm:
            a = _WEEKDAY_BITS[dm.group(1).lower()]
            b = _WEEKDAY_BITS[dm.group(2).lower()]
            weekdays = 0
            i = a
            while True:
                weekdays |= (1 << i)
                if i == b:
                    break
                i = (i + 1) % 7
        return (cond_speed, hour_from, hour_to, weekdays)
    return None


# --- Encoding --------------------------------------------------------------
def varint(buf, v):
    """
    varint(buf, v) - append v to buf as an unsigned LEB128 varint.

    Parameters:
      buf - bytearray to append to
      v   - non-negative integer to encode
    """
    while v >= 0x80:
        buf.append((v & 0x7F) | 0x80)
        v >>= 7
    buf.append(v)


def zig(x):
    """
    zig(x) - zigzag-encode a signed integer into an unsigned one, so small
    negative and positive values both end up with a short varint encoding.

    Parameters:
      x - signed integer (must fit in 32 bits; encoded as (x << 1) ^ (x >> 31))
    """
    return (x << 1) ^ (x >> 31)


def simplify(pts, tol, lon_scale):
    """
    simplify(pts, tol, lon_scale) - Douglas-Peucker line simplification.

    Parameters:
      pts       - list of (lat_e6, lon_e6) points
      tol       - tolerance, in microdegrees of latitude
      lon_scale - factor that compensates for a microdegree of longitude
                  being shorter than a microdegree of latitude away from the
                  equator; without it, simplification would be too aggressive
                  in the east-west direction

    Recursive: keeps the point with the largest perpendicular distance from
    the line between the two endpoints whenever that distance exceeds tol,
    and recurses on both halves.
    """
    if len(pts) < 3:
        return pts
    a, b = pts[0], pts[-1]
    ax, ay = a[1] * lon_scale, a[0]
    dx, dy = b[1] * lon_scale - ax, b[0] - ay
    den = math.hypot(dx, dy)
    imax, dmax = 0, -1.0
    for i in range(1, len(pts) - 1):
        px, py = pts[i][1] * lon_scale, pts[i][0]
        d = (math.hypot(px - ax, py - ay) if den < 1e-9
             else abs(dx * (ay - py) - (ax - px) * dy) / den)
        if d > dmax:
            imax, dmax = i, d
    if dmax <= tol:
        return [a, b]
    return simplify(pts[:imax + 1], tol, lon_scale)[:-1] + \
        simplify(pts[imax:], tol, lon_scale)


class WayHandler(osmium.SimpleHandler):
    """Collects segments and assigns them to grid cells."""

    def __init__(self):
        """
        WayHandler() - set up empty collection state.

        No parameters. self.cells maps a (row, col) cell key to a list of
        (speed, cond, why, points) segments; the bounding box accumulators
        start at sentinel extremes and get tightened as ways are seen.
        """
        super().__init__()
        self.cells = defaultdict(list)
        self.n_ways = 0
        self.n_segments = 0
        self.n_conditional = 0
        self.lat_lo = self.lon_lo = 10 ** 9
        self.lat_hi = self.lon_hi = -10 ** 9

    def way(self, w):
        """
        way(w) - osmium callback invoked once per OSM way in the extract.

        Parameters:
          w - the osmium way object (tags, nodes with resolved locations)

        Filters out anything that isn't a road type we care about or that
        carries no usable speed information, classifies the reason, then
        cuts the way into per-cell sub-pieces (with a shared point at each
        cell boundary, so adjacent pieces still connect) and hands each
        piece to _store().
        """
        highway = w.tags.get("highway")
        if highway not in HIGHWAY_TYPES:
            return
        speed = parse_maxspeed(w.tags.get("maxspeed"), highway)
        cond = parse_conditional(w.tags.get("maxspeed:conditional"), highway)
        if speed == 0 and cond is None:
            return
        why = reason_code(w.tags, highway)
        if cond is not None and why == REASON_NONE:
            why = REASON_TIME_LIMITED
        try:
            coords = [(int(round(n.lat * 1e6)), int(round(n.lon * 1e6)))
                      for n in w.nodes if n.location.valid()]
        except osmium.InvalidLocationError:
            return
        if len(coords) < 2:
            return
        self.n_ways += 1
        for la, lo in coords:
            self.lat_lo = min(self.lat_lo, la)
            self.lat_hi = max(self.lat_hi, la)
            self.lon_lo = min(self.lon_lo, lo)
            self.lon_hi = max(self.lon_hi, lo)

        # Cut the way into per-cell pieces, overlapping at the boundary
        current, buf = None, []
        for pt in coords:
            c = ((pt[0] - GRID_ORIGIN_LAT_E6) // CELL_LAT_E6,
                 (pt[1] - GRID_ORIGIN_LON_E6) // CELL_LON_E6)
            if c != current:
                if current is not None and len(buf) >= 2:
                    self._store(current, speed, buf, cond, why)
                buf = [buf[-1], pt] if buf else [pt]
                current = c
            else:
                buf.append(pt)
        if current is not None and len(buf) >= 2:
            self._store(current, speed, buf, cond, why)

    def _store(self, cell, speed, points, cond, why):
        """
        _store(cell, speed, points, cond, why) - append one segment to its
        cell's bucket and update the running counters.

        Parameters:
          cell   - (row, col) grid cell key
          speed  - speed limit, uint8 (see parse_maxspeed)
          points - list of (lat_e6, lon_e6) points making up this segment
          cond   - conditional-speed tuple or None (see parse_conditional)
          why    - REASON_* code
        """
        self.cells[cell].append((speed, cond, why, list(points)))
        self.n_segments += 1
        if cond is not None:
            self.n_conditional += 1


# Turn angle beyond which two sub-pieces are NOT chained together anymore,
# even though their end/start points, speed, condition and reason all match.
# Without this limit, chain() would merge two completely unrelated roads
# into one chain at every intersection inside a 30 km/h zone, just because
# they share a node and both carry "30, zone" - inside a zone, practically
# ALL roads carry the same marking. The course filter in the lookup doesn't
# save this: it only checks whether ANY segment of the (then far too long)
# chain matches the direction of travel, and on a chain that zigzags through
# several unrelated real streets, that match is essentially down to chance
# and may land on a segment that has nothing to do with the street actually
# being driven. Found on a real test drive (see history.md): after turning
# out of a zone onto an unrelated 50 km/h street, the limit kept showing 30
# for too long, because a zone chain welded together like this happened to
# include a parallel sub-piece.
#
# 60 degrees keeps real, gently curving road runs together while reliably
# picking out real turns/intersections - the mis-chaining cases found in
# the real-world incident were between 87 and 150 degrees.
CHAIN_MAX_TURN_DEG = 60.0


def _bearing(a, b, lon_scale):
    """
    _bearing(a, b, lon_scale) - bearing in degrees (0 = north) from point a
    to point b.

    Parameters:
      a, b      - (lat_e6, lon_e6) points
      lon_scale - longitude scale factor, see simplify()

    Returns None if a and b are identical (undefined bearing) - callers
    treat that as "no direction to compare".
    """
    dlat = b[0] - a[0]
    dlon = (b[1] - a[1]) * lon_scale
    if dlat == 0 and dlon == 0:
        return None
    return math.degrees(math.atan2(dlon, dlat)) % 360.0


def _bearing_diff(a, b):
    """
    _bearing_diff(a, b) - smallest angular difference between two bearings,
    in degrees, always in [0, 180].

    Parameters:
      a, b - bearings in degrees
    """
    d = abs(a - b) % 360.0
    return min(d, 360.0 - d)


def chain(segments, lon_scale):
    """
    chain(segments, lon_scale) - join sub-pieces with matching speed where
    one's end meets another's start AND the direction doesn't jump too much
    at the seam (see CHAIN_MAX_TURN_DEG).

    Parameters:
      segments  - list of (speed, cond, why, points) segments, typically all
                  belonging to one grid cell
      lon_scale - longitude scale factor, see simplify()

    The chaining itself is the real lever here: only once pieces are joined
    into long polylines does simplify() have anything to work with - a
    single unchained segment averages just 4.4 points, far too short for
    Douglas-Peucker to remove anything useful. When several candidates could
    extend a chain, the one whose starting bearing is closest to the current
    end bearing wins.
    """
    starts = defaultdict(list)
    for i, (sp, cd, wy, p) in enumerate(segments):
        starts[(sp, cd, wy, p[0])].append(i)
    used, out = set(), []
    for i, (sp, cd, wy, p) in enumerate(segments):
        if i in used:
            continue
        used.add(i)
        pts = list(p)
        while True:
            candidates = [j for j in starts.get((sp, cd, wy, pts[-1]), []) if j not in used]
            if not candidates:
                break
            cur_bear = _bearing(pts[-2], pts[-1], lon_scale)
            best, best_diff = None, None
            for j in candidates:
                cand_pts = segments[j][3]
                cand_bear = _bearing(cand_pts[0], cand_pts[1], lon_scale)
                # A zero-length segment (identical points) has no direction
                # of its own and doesn't block chaining.
                diff = (0.0 if cur_bear is None or cand_bear is None
                        else _bearing_diff(cur_bear, cand_bear))
                if best_diff is None or diff < best_diff:
                    best, best_diff = j, diff
            if best is None or best_diff > CHAIN_MAX_TURN_DEG:
                break
            used.add(best)
            pts.extend(segments[best][3][1:])
        out.append((sp, cd, wy, pts))
    return out


def write_region(handler, outdir, name):
    """
    write_region(handler, outdir, name) - simplify, chain, encode and write
    the collected data as <outdir>/<name>.msg, then print a statistics
    summary to the terminal.

    Parameters:
      handler - a WayHandler that has already processed the whole .pbf file
      outdir  - output directory (created if missing)
      name    - region name, used as the output filename and stored in the
                64-byte header (truncated to 23 bytes + NUL)

    Snaps the observed bounding box out to whole grid cells, picks a
    per-region longitude quantization step (compensating for latitude, since
    a meter of longitude shrinks toward the poles), then for every occupied
    cell: chains segments, Douglas-Peucker-simplifies each chain by road
    class, quantizes points onto the coordinate grid, deduplicates
    consecutive identical points, and appends the encoded chain to that
    cell's data block. Cells whose block ends up empty are dropped from the
    index entirely.
    """
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, name + ".msg")

    # Expand the bounding box out to the cell grid
    row0 = (handler.lat_lo - GRID_ORIGIN_LAT_E6) // CELL_LAT_E6
    col0 = (handler.lon_lo - GRID_ORIGIN_LON_E6) // CELL_LON_E6
    row1 = (handler.lat_hi - GRID_ORIGIN_LAT_E6) // CELL_LAT_E6
    col1 = (handler.lon_hi - GRID_ORIGIN_LON_E6) // CELL_LON_E6
    n_rows, n_cols = int(row1 - row0 + 1), int(col1 - col0 + 1)
    lat_min = GRID_ORIGIN_LAT_E6 + row0 * CELL_LAT_E6
    lon_min = GRID_ORIGIN_LON_E6 + col0 * CELL_LON_E6

    lat_mid = (handler.lat_lo + handler.lat_hi) / 2e6
    cos_lat = math.cos(math.radians(lat_mid))
    q_lat = max(1, int(round(QUANT_M / M_PER_ULAT)))
    q_lon = max(1, int(round(QUANT_M / (M_PER_ULAT * cos_lat))))

    blocks, n_chains, n_points = {}, 0, 0
    why_count = defaultdict(int)
    for (r, c), segs in handler.cells.items():
        rr, cc = int(r - row0), int(c - col0)
        if not (0 <= rr < n_rows and 0 <= cc < n_cols):
            continue
        base_lat = lat_min + rr * CELL_LAT_E6
        base_lon = lon_min + cc * CELL_LON_E6
        buf = bytearray()
        chains = chain(segs, cos_lat)
        varint(buf, len(chains))
        for speed, cond, why, pts in chains:
            tol = TOL_M[klasse(speed)] / M_PER_ULAT
            pts = simplify(pts, tol, cos_lat)
            q = [((la - base_lat) // q_lat, (lo - base_lon) // q_lon)
                 for la, lo in pts]
            ded = [q[0]]
            for p in q[1:]:
                if p != ded[-1]:
                    ded.append(p)
            if len(ded) < 2:
                continue
            n_chains += 1
            n_points += len(ded)
            buf.append(speed)
            # bit 7 = condition follows, bit 0-2 = reason
            buf.append((0x80 if cond else 0x00) | (why & 0x07))
            why_count[why] += 1
            if cond:
                buf.extend(struct.pack("<BBBB", *cond))
            varint(buf, len(ded))
            varint(buf, zig(int(ded[0][0])))
            varint(buf, zig(int(ded[0][1])))
            for a, b in zip(ded, ded[1:]):
                varint(buf, zig(int(b[0] - a[0])))
                varint(buf, zig(int(b[1] - a[1])))
        if len(buf) > 1:
            blocks[rr * n_cols + cc] = bytes(buf)

    ids = sorted(blocks)
    index = bytearray()
    data = bytearray()
    for cid in ids:
        index += struct.pack("<II", cid, len(data))
        data += blocks[cid]
    index += struct.pack("<II", 0xFFFFFFFF, len(data))   # sentinel

    head = bytearray(64)
    struct.pack_into("<4siiIIHHHHIII", head, 0, b"MSG2", lat_min, lon_min,
                     CELL_LAT_E6, CELL_LON_E6, n_rows, n_cols, q_lat, q_lon,
                     len(ids), 64, 64 + len(index))
    head[40:40 + min(23, len(name))] = name.encode()[:23]

    with open(path, "wb") as f:
        f.write(head)
        f.write(index)
        f.write(data)

    size = os.path.getsize(path)
    print(f"Region           : {name}")
    print(f"Bounding box     : {lat_min/1e6:.3f}..{(lat_min+n_rows*CELL_LAT_E6)/1e6:.3f} N, "
          f"{lon_min/1e6:.3f}..{(lon_min+n_cols*CELL_LON_E6)/1e6:.3f} E")
    print(f"Grid             : {n_rows} x {n_cols} cells, {len(ids)} occupied")
    print(f"Coordinate grid  : {q_lat} x {q_lon} microdegrees (~{QUANT_M} m)")
    print(f"Ways             : {handler.n_ways:,}")
    print(f"Sub-pieces       : {handler.n_segments:,} -> {n_chains:,} chains")
    print(f"of which timed   : {handler.n_conditional:,}")
    print(f"Points           : {n_points:,}")
    names = {REASON_NONE: "no reason given", REASON_ZONE: "zone",
             REASON_CHILDREN: "children", REASON_PLAY_STREET: "play street",
             REASON_BICYCLE_STREET: "bicycle street", REASON_SIGN: "single sign",
             REASON_TIME_LIMITED: "time-limited"}
    print("Reasons          :")
    for k in sorted(why_count, key=lambda x: -why_count[x]):
        print(f"   {names.get(k, k):<16} {why_count[k]:>8,}  "
              f"({100.0*why_count[k]/max(n_chains,1):4.1f} %)")
    print(f"Header + index   : {(64 + len(index))/1024:.1f} KiB")
    print(f"Payload          : {len(data)/1048576:.2f} MiB")
    print(f"File             : {path}  {size/1048576:.2f} MiB")


def main():
    """
    main() - CLI entry point: parse arguments, run the osmium handler over
    the .pbf file, and write the region.

    No parameters (reads sys.argv via argparse). Derives a region name from
    the input filename's first "-"/"." separated token when --name isn't
    given (e.g. "berlin-latest.osm.pbf" -> "berlin"), since Geofabrik
    filenames follow that convention.
    """
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pbf")
    ap.add_argument("outdir")
    ap.add_argument("--name", help="region name (default: derived from the filename)")
    args = ap.parse_args()

    name = args.name or os.path.basename(args.pbf).split("-")[0].split(".")[0]
    if len(name) > 23:
        sys.exit("Region name must be at most 23 characters")

    h = WayHandler()
    h.apply_file(args.pbf, locations=True, idx="flex_mem")
    if not h.cells:
        sys.exit("No usable roads found")
    write_region(h, args.outdir, name)


if __name__ == "__main__":
    main()
