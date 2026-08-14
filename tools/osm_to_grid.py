#!/usr/bin/env python3
"""
osm_to_grid.py - Wandelt einen Geofabrik-OSM-Extrakt in eine kompakte
Regionsdatei für den ESP32 um.

Nutzung:
    pip install osmium
    wget https://download.geofabrik.de/europe/germany/berlin-latest.osm.pbf
    python3 osm_to_grid.py berlin-latest.osm.pbf ../data/maps/ --name berlin

Ergebnis: eine einzige Datei <name>.msg. Mehrere Regionen liegen einfach
nebeneinander im selben Ordner; der Reader sucht sich anhand der Position die
passende heraus. Regionen dürfen sich überlappen.

--------------------------------------------------------------------------
WARUM DAS FORMAT SO AUSSIEHT
--------------------------------------------------------------------------
Gemessen an den Berlin-Daten des Vorgängerformats (MSG1, 4,20 MiB):

  Index dicht über ganz Deutschland  2,24 MiB -> Region hat eigene Bounding-Box
  Wege an jeder Zellgrenze zerhackt  4,4 Punkte je Segment -> verketten
  Koordinaten mit 0,11 m Auflösung   bei 30 m Suchradius -> 0,5 m Raster
  Absolute uint16 je Punkt           -> Zickzack-Varint auf Differenzen

Zusammen 4,20 MiB -> 0,66 MiB. Der grösste Einzelposten ist die eigene
Bounding-Box je Region: der alte Index beschrieb immer ganz Deutschland,
auch wenn nur Berlin drin war.

--------------------------------------------------------------------------
DATEIFORMAT (little endian)  -  gespiegelt in speedlimit_grid.h
--------------------------------------------------------------------------
Kopf, 64 Byte:
    char[4]  magic = "MSG2"
    int32    lat_min_e6, lon_min_e6   Ecke unten links, auf Zellraster
    uint32   cell_lat_e6, cell_lon_e6 Zellgrösse in Mikrograd
    uint16   n_rows, n_cols
    uint16   quant_lat_e6, quant_lon_e6   Mikrograd je Rasterschritt
    uint32   n_cells                  Anzahl belegter Zellen
    uint32   index_off, data_off      Byteoffsets in dieser Datei
    char[24] Regionsname, 0-terminiert

Index, (n_cells + 1) * 8 Byte, aufsteigend nach cell_id:
    uint32 cell_id = row * n_cols + col
    uint32 offset  relativ zu data_off
    Der letzte Eintrag ist ein Wächter mit cell_id = 0xFFFFFFFF; seine
    offset-Angabe ist die Gesamtlänge der Nutzdaten. Die Länge eines Blocks
    ergibt sich damit als Differenz zum nächsten Eintrag - eine eigene
    Längenangabe je Zelle wäre verschenkt.

Nutzdaten, je Zelle ein Block:
    varint  n_chains
    je Kette:
        uint8   speed       0=unbekannt, 255=unbegrenzt, sonst km/h
        uint8   flags       Bit7   = zeitliche Bedingung folgt
                            Bit0-2 = Begruendung (REASON_*, siehe unten)
                            Bit3-6 = frei
                            Die Begruendung kostet kein zusaetzliches Byte,
                            weil das flags-Byte ohnehin da war.
        [4 Byte Bedingung: cond_speed, hour_from, hour_to, weekdays]
        varint  n_points
        varint  zickzack(q_lat)   erster Punkt, Rasterschritte ab Zellecke
        varint  zickzack(q_lon)   darf negativ sein (Punkt aus Nachbarzelle)
        (n_points-1) x varint zickzack(dq_lat), varint zickzack(dq_lon)

Absolute Position eines Punktes:
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

# --- Globales Zellraster ---------------------------------------------------
# Fest verankert, damit die Zellgrenzen verschiedener Regionen aufeinander
# passen. Nur die Bounding-Box unterscheidet sich je Region.
GRID_ORIGIN_LAT_E6 = 47_000_000
GRID_ORIGIN_LON_E6 = 5_700_000
CELL_LAT_E6 = 10_000   # 0.010 Grad, ~1113 m
CELL_LON_E6 = 20_000   # 0.020 Grad, ~1354 m bei 52 Grad Nord

M_PER_ULAT = 0.1113    # Meter je Mikrograd Breite

# --- Vereinfachung ---------------------------------------------------------
# Der Vereinfachungsfehler geht direkt vom Suchradius des Matchings ab
# (MATCH_MAX_DIST_M, 30 m). Innerorts bleibt es deshalb bewusst fein: dort
# liegen Parallelstrassen 20 m auseinander. Auf der Autobahn ist die nächste
# konkurrierende Strasse hunderte Meter weg, da darf es grob sein.
TOL_M = {"autobahn": 8.0, "land": 3.0, "stadt": 0.5}
QUANT_M = 0.5          # Koordinatenraster in Metern

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

# --- Begruendung des Limits ------------------------------------------------
# Steckt in den unteren drei Bit des flags-Byte, das bisher nur Bit 7 nutzt -
# kostet also kein einziges zusaetzliches Byte.
#
# Gemessen am Berlin-Extrakt (58.550 Wege mit maxspeed=30): rund 42 % tragen
# eine Zonen-Kennzeichnung, 1,3 % hazard=children, 2 % ein Einzelschild.
# 46,7 % tragen gar nichts - deshalb ist REASON_NONE der Normalfall und die
# Anzeige bleibt dann leer, statt etwas zu behaupten.
REASON_NONE = 0
REASON_ZONE = 1        # Tempo-30-Zone
REASON_KINDER = 2      # hazard=children, meist Schule oder Kindergarten
REASON_SPIEL = 3       # verkehrsberuhigter Bereich (Spielstrasse)
REASON_RAD = 4         # Fahrradstrasse
REASON_SCHILD = 5      # streckenbezogenes Einzelschild
REASON_ZEIT = 6        # nur zeitweise gueltig


def reason_code(tags, highway):
    """Warum gilt dieses Limit? Die Tags sind uneinheitlich geschrieben
    (DE:zone30, DE:zone:30, de:zone30, DE:30) und auf drei redundante
    Schluessel verteilt, deshalb wird normalisiert und zusammengefasst."""
    if tags.get("hazard") == "children":
        return REASON_KINDER
    if highway == "living_street":
        return REASON_SPIEL

    vals = []
    for k in ("maxspeed:type", "zone:maxspeed", "source:maxspeed"):
        v = tags.get(k)
        if v:
            n = v.strip().lower()
            if n.startswith("de:"):
                n = n[3:]
            vals.append((k, n.replace(":", "")))

    if any("bicycle_road" in n for _, n in vals):
        return REASON_RAD
    for k, n in vals:
        # zone:maxspeed traegt die Zone im Wert selbst ("DE:30"), die beiden
        # anderen als Praefix ("DE:zone30")
        if n.startswith("zone") or (k == "zone:maxspeed" and n.isdigit()):
            return REASON_ZONE
    if any(n == "sign" for _, n in vals):
        return REASON_SCHILD
    return REASON_NONE


def klasse(speed):
    """Strassenklasse aus dem Tempo ableiten - genauer geht es nicht mehr,
    der Highway-Typ steht in den Daten nicht mehr zur Verfügung."""
    if speed >= 100 or speed == 255:
        return "autobahn"
    if speed >= 70:
        return "land"
    return "stadt"


def parse_maxspeed(value, highway):
    """OSM-maxspeed-String -> uint8 (0=unbekannt, 255=unbegrenzt)."""
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
    """'maxspeed:conditional' -> (cond_speed, hour_from, hour_to, weekdays)."""
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


# --- Kodierung -------------------------------------------------------------
def varint(buf, v):
    while v >= 0x80:
        buf.append((v & 0x7F) | 0x80)
        v >>= 7
    buf.append(v)


def zig(x):
    return (x << 1) ^ (x >> 31)


def simplify(pts, tol, lon_scale):
    """Douglas-Peucker. pts in Mikrograd, tol in Mikrograd Breite.
    lon_scale gleicht aus, dass ein Mikrograd Länge kürzer ist als ein
    Mikrograd Breite - sonst würde in Ost-West-Richtung zu stark vereinfacht."""
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
    """Sammelt Segmente und ordnet sie den Grid-Zellen zu."""

    def __init__(self):
        super().__init__()
        self.cells = defaultdict(list)
        self.n_ways = 0
        self.n_segments = 0
        self.n_conditional = 0
        self.lat_lo = self.lon_lo = 10 ** 9
        self.lat_hi = self.lon_hi = -10 ** 9

    def way(self, w):
        highway = w.tags.get("highway")
        if highway not in HIGHWAY_TYPES:
            return
        speed = parse_maxspeed(w.tags.get("maxspeed"), highway)
        cond = parse_conditional(w.tags.get("maxspeed:conditional"), highway)
        if speed == 0 and cond is None:
            return
        why = reason_code(w.tags, highway)
        if cond is not None and why == REASON_NONE:
            why = REASON_ZEIT
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

        # Way in Teilstücke pro Zelle zerlegen, mit Überlappung an der Grenze
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
        self.cells[cell].append((speed, cond, why, list(points)))
        self.n_segments += 1
        if cond is not None:
            self.n_conditional += 1


# Ab welcher Richtungsaenderung zwei Teilstuecke NICHT mehr verkettet werden,
# obwohl Ende und Anfang sowie Tempo/Bedingung/Begruendung uebereinstimmen.
# Ohne dieses Limit verkettete chain() an jeder Kreuzung innerhalb einer
# Tempo-30-Zone zwei voellig unabhaengige Strassen zu einer einzigen Kette,
# nur weil sie sich einen Knotenpunkt teilen und beide "30, Zone" tragen -
# in einer Zone haben praktisch ALLE Strassen dieselbe Kennzeichnung. Der
# Kurs-Filter im Lookup rettet das nicht: er prueft nur, ob IRGENDEIN
# Segment der (dann viel zu langen) Kette zur Fahrtrichtung passt, und bei
# einer durch mehrere echte Strassen gezickzackten Kette trifft das eher
# zufaellig auf ein Segment zu, das mit der tatsaechlich befahrenen Strasse
# nichts zu tun hat. Gefunden bei einer echten Testfahrt (siehe history.md):
# das Limit blieb nach dem Abbiegen aus einer Zone auf eine unbeteiligte
# 50er-Strasse laenger auf 30 haengen, weil eine so zusammengeschweisste
# Zonen-Kette zufaellig ein paralleles Teilstueck enthielt.
#
# 60 Grad laesst echte, sanft geschwungene Strassenverlaeufe zusammen, hebt
# aber echte Abbiegungen/Kreuzungen zuverlaessig ab - die im echten Fall
# gefundenen Fehlverkettungen lagen bei 87-150 Grad.
CHAIN_MAX_TURN_DEG = 60.0


def _bearing(a, b, lon_scale):
    """Peilung in Grad (0 = Nord), a/b als (lat_e6, lon_e6)."""
    dlat = b[0] - a[0]
    dlon = (b[1] - a[1]) * lon_scale
    if dlat == 0 and dlon == 0:
        return None
    return math.degrees(math.atan2(dlon, dlat)) % 360.0


def _bearing_diff(a, b):
    d = abs(a - b) % 360.0
    return min(d, 360.0 - d)


def chain(segments, lon_scale):
    """Teilstücke mit gleichem Tempo aneinanderhängen, wo Ende auf Anfang
    trifft UND die Richtung an der Naht nicht zu stark springt (siehe
    CHAIN_MAX_TURN_DEG). Das Aneinanderhängen selbst ist der eigentliche
    Hebel: erst dadurch entstehen lange Linienzüge, an denen die
    Vereinfachung überhaupt etwas findet."""
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
                # Ein nulllanges Segment (identische Punkte) hat keine
                # eigene Richtung und blockiert das Verketten nicht.
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
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, name + ".msg")

    # Bounding-Box auf Zellraster ausdehnen
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
            # Bit 7 = Bedingung folgt, Bit 0-2 = Begruendung
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
    index += struct.pack("<II", 0xFFFFFFFF, len(data))   # Wächter

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
    print(f"Bounding-Box     : {lat_min/1e6:.3f}..{(lat_min+n_rows*CELL_LAT_E6)/1e6:.3f} N, "
          f"{lon_min/1e6:.3f}..{(lon_min+n_cols*CELL_LON_E6)/1e6:.3f} E")
    print(f"Raster           : {n_rows} x {n_cols} Zellen, davon {len(ids)} belegt")
    print(f"Koordinatenraster: {q_lat} x {q_lon} Mikrograd (~{QUANT_M} m)")
    print(f"Ways             : {handler.n_ways:,}")
    print(f"Teilstuecke      : {handler.n_segments:,} -> {n_chains:,} Ketten")
    print(f"davon zeitlich   : {handler.n_conditional:,}")
    print(f"Stuetzpunkte     : {n_points:,}")
    names = {REASON_NONE: "ohne Angabe", REASON_ZONE: "Zone",
             REASON_KINDER: "Kinder", REASON_SPIEL: "Spielstrasse",
             REASON_RAD: "Fahrradstrasse", REASON_SCHILD: "Einzelschild",
             REASON_ZEIT: "zeitlich"}
    print("Begruendungen    :")
    for k in sorted(why_count, key=lambda x: -why_count[x]):
        print(f"   {names.get(k, k):<16} {why_count[k]:>8,}  "
              f"({100.0*why_count[k]/max(n_chains,1):4.1f} %)")
    print(f"Kopf + Index     : {(64 + len(index))/1024:.1f} KiB")
    print(f"Nutzdaten        : {len(data)/1048576:.2f} MiB")
    print(f"Datei            : {path}  {size/1048576:.2f} MiB")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pbf")
    ap.add_argument("outdir")
    ap.add_argument("--name", help="Regionsname (Standard: aus dem Dateinamen)")
    args = ap.parse_args()

    name = args.name or os.path.basename(args.pbf).split("-")[0].split(".")[0]
    if len(name) > 23:
        sys.exit("Regionsname darf höchstens 23 Zeichen haben")

    h = WayHandler()
    h.apply_file(args.pbf, locations=True, idx="flex_mem")
    if not h.cells:
        sys.exit("Keine verwertbaren Strassen gefunden")
    write_region(h, args.outdir, name)


if __name__ == "__main__":
    main()
