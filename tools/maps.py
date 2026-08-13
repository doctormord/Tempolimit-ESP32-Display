#!/usr/bin/env python3
"""
maps.py - Kartenregionen auswählen, herunterladen, aufbereiten, hochladen.

    python3 tools/maps.py                  interaktive Auswahl
    python3 tools/maps.py --status         was liegt gerade in data/maps/
    python3 tools/maps.py --add brandenburg berlin
    python3 tools/maps.py --remove berlin
    python3 tools/maps.py --upload         nur ins Flash schieben

Lädt die Extrakte von Geofabrik, ruft osm_to_grid.py auf und zeigt an, ob
alles zusammen ins LittleFS passt. Die Kapazität wird nicht geraten, sondern
aus der in platformio.ini eingetragenen Partitionstabelle gelesen.

Braucht `osmium` (pip install osmium) für die Aufbereitung und `pio` für den
Upload.
"""

import argparse
import configparser
import math
import os
import re
import shutil
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PBF_DIR = os.path.join(ROOT, "tools", "pbf")
MAP_DIR = os.path.join(ROOT, "data", "maps")
BASE_URL = "https://download.geofabrik.de/europe/germany/"

# PBF-Größen laut Geofabrik (Stand August 2026, ändern sich langsam) und der
# Umrechnungsfaktor auf die fertige .msg. Kalibriert an Berlin und Deutschland:
# .dat des alten Formats war 2,2 % der PBF-Größe, MSG2 davon 28 % (Stadtstaat,
# viele Fragmente zum Verketten) bis 39 % (Flächenland).
# Die Schätzung dient nur der Planung - nach der Aufbereitung zählt die
# tatsächliche Dateigröße.
REGIONS = {
    "baden-wuerttemberg":      (614, 0.39, "Baden-Württemberg"),
    "bayern":                  (809, 0.39, "Bayern"),
    "berlin":                  (94, 0.28, "Berlin"),
    "brandenburg":             (284, 0.39, "Brandenburg (enthält Berlin)"),
    "bremen":                  (20.1, 0.28, "Bremen"),
    "hamburg":                 (51, 0.28, "Hamburg"),
    "hessen":                  (327, 0.39, "Hessen"),
    "mecklenburg-vorpommern":  (121, 0.39, "Mecklenburg-Vorpommern"),
    "niedersachsen":           (478, 0.39, "Niedersachsen (enthält Bremen)"),
    "nordrhein-westfalen":     (867, 0.39, "Nordrhein-Westfalen"),
    "rheinland-pfalz":         (254, 0.39, "Rheinland-Pfalz"),
    "saarland":                (52, 0.39, "Saarland"),
    "sachsen":                 (254, 0.39, "Sachsen"),
    "sachsen-anhalt":          (165, 0.39, "Sachsen-Anhalt"),
    "schleswig-holstein":      (150, 0.39, "Schleswig-Holstein"),
    "thueringen":              (151, 0.39, "Thüringen"),
}

MIB = 1048576.0
LFS_BLOCK = 4096          # LittleFS belegt blockweise
LFS_RESERVE = 64 * 1024   # Verwaltungsdaten, grosszuegig gerundet


def estimate_mib(slug):
    pbf_mb, factor, _ = REGIONS[slug]
    return pbf_mb * 0.022 * factor


# --------------------------------------------------------------- Kapazität
def fs_capacity():
    """Grösse der Dateisystempartition aus der Partitionstabelle lesen."""
    ini = configparser.ConfigParser()
    ini.read(os.path.join(ROOT, "platformio.ini"))
    table = None
    for sec in ini.sections():
        if ini.has_option(sec, "board_build.partitions"):
            table = ini.get(sec, "board_build.partitions").strip()
    if not table:
        return None, "keine board_build.partitions in platformio.ini"

    path = os.path.join(ROOT, table)
    if not os.path.exists(path):
        # Tabellen des Frameworks liegen im PlatformIO-Paket
        pat = os.path.expanduser(
            "~/.platformio/packages/framework-arduinoespressif32/tools/partitions")
        path = os.path.join(pat, table)
    if not os.path.exists(path):
        return None, f"Partitionstabelle {table} nicht gefunden"

    size = None
    with open(path) as f:
        for line in f:
            if line.strip().startswith("#") or not line.strip():
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 5:
                continue
            if parts[1] == "data" and parts[2] in ("spiffs", "fat", "littlefs"):
                size = int(parts[4], 0)      # letzte gewinnt, wie bei uploadfs
    if size is None:
        return None, f"keine Dateisystempartition in {table}"
    return size, table


def on_flash_size(paths):
    """Wieviel die Dateien im LittleFS tatsächlich belegen (blockweise)."""
    total = LFS_RESERVE
    for p in paths:
        total += int(math.ceil(os.path.getsize(p) / LFS_BLOCK)) * LFS_BLOCK
    return total


def installed():
    if not os.path.isdir(MAP_DIR):
        return []
    return sorted(f for f in os.listdir(MAP_DIR) if f.endswith(".msg"))


def status(verbose=True):
    cap, table = fs_capacity()
    files = [os.path.join(MAP_DIR, f) for f in installed()]
    used = on_flash_size(files) if files else LFS_RESERVE
    if verbose:
        print()
        print("Karten in data/maps/")
        print("-" * 58)
        if not files:
            print("  (keine)")
        for p in files:
            print("  %-28s %8.2f MiB" % (os.path.basename(p),
                                         os.path.getsize(p) / MIB))
        print("-" * 58)
        if cap:
            pct = 100.0 * used / cap
            bar = "#" * int(pct / 4) + "." * (25 - int(pct / 4))
            print("  belegt %6.2f von %6.2f MiB  [%s] %.0f %%"
                  % (used / MIB, cap / MIB, bar, pct))
            print("  Partitionstabelle: %s" % table)
            if used > cap:
                print("  ZU GROSS - passt nicht ins LittleFS")
        else:
            print("  Kapazität unbekannt: %s" % table)
    return cap, used


# ------------------------------------------------------------------ Schritte
def download(slug):
    os.makedirs(PBF_DIR, exist_ok=True)
    dst = os.path.join(PBF_DIR, f"{slug}-latest.osm.pbf")
    if os.path.exists(dst):
        print(f"  PBF vorhanden ({os.path.getsize(dst)/MIB:.0f} MiB), "
              f"kein erneuter Download")
        return dst
    url = f"{BASE_URL}{slug}-latest.osm.pbf"
    tmp = dst + ".part"
    print(f"  lade {url}")

    def hook(blocks, bs, total):
        if total <= 0:
            return
        done = blocks * bs
        pct = min(100.0, 100.0 * done / total)
        sys.stdout.write("\r  %5.1f %%  %.0f / %.0f MiB"
                         % (pct, done / MIB, total / MIB))
        sys.stdout.flush()

    try:
        urllib.request.urlretrieve(url, tmp, hook)
    except Exception as e:
        if os.path.exists(tmp):
            os.remove(tmp)
        print(f"\n  Download fehlgeschlagen: {e}")
        return None
    print()
    os.rename(tmp, dst)
    return dst


def convert(slug, pbf):
    os.makedirs(MAP_DIR, exist_ok=True)
    script = os.path.join(ROOT, "tools", "osm_to_grid.py")
    print(f"  bereite auf (kann ein paar Minuten dauern) ...")
    r = subprocess.run([sys.executable, script, pbf, MAP_DIR, "--name", slug],
                       cwd=ROOT)
    if r.returncode != 0:
        print("  Aufbereitung fehlgeschlagen")
        return False
    return True


# Regionen, die andere komplett enthalten. Beide gleichzeitig zu laden kostet
# Platz und halbiert die Cache-Trefferquote, weil jeder Lookup beide durchsucht.
CONTAINS = {
    "brandenburg": ["berlin"],
    "niedersachsen": ["bremen"],
}


def warn_overlap(slugs):
    have = {f[:-4] for f in installed()} | set(slugs)
    for big, smalls in CONTAINS.items():
        if big not in have:
            continue
        for small in smalls:
            if small in have:
                print(f"\n  Hinweis: {big} enthält {small} bereits (so liefert "
                      f"Geofabrik es aus).")
                print(f"  Beide zu behalten verdoppelt die Sucharbeit im "
                      f"Überlappungsgebiet.")
                print(f"  Empfehlung:  python3 tools/maps.py --remove {small}")


def add(slugs):
    for slug in slugs:
        if slug not in REGIONS:
            print(f"unbekannte Region: {slug}")
            continue
        print(f"\n=== {REGIONS[slug][2]} ===")
        pbf = download(slug)
        if pbf:
            convert(slug, pbf)
    warn_overlap(slugs)


def remove(slugs):
    for slug in slugs:
        p = os.path.join(MAP_DIR, slug + ".msg")
        if os.path.exists(p):
            os.remove(p)
            print(f"  {slug}.msg entfernt")
        else:
            print(f"  {slug}.msg war nicht vorhanden")


def upload():
    cap, used = status(verbose=False)
    if cap and used > cap:
        print("Passt nicht ins LittleFS - erst Regionen entfernen.")
        return False
    print("\nschiebe data/ ins Flash ...")
    r = subprocess.run(["pio", "run", "-e", "esp32s3", "-t", "uploadfs"],
                       cwd=ROOT)
    return r.returncode == 0


# ---------------------------------------------------------------- Interaktiv
def menu():
    while True:
        have = {f[:-4] for f in installed()}
        cap, used = status()
        print()
        print("Verfügbare Regionen  (* = bereits aufbereitet)")
        print("-" * 58)
        keys = list(REGIONS)
        for i, slug in enumerate(keys, 1):
            mark = "*" if slug in have else " "
            est = estimate_mib(slug)
            pbf_mb, _, label = REGIONS[slug]
            print("%2d %s %-32s ~%5.2f MiB   PBF %4.0f MB"
                  % (i, mark, label, est, pbf_mb))
        print("-" * 58)
        print("Nummern hinzufügen (z.B. '4 13'), '-4' entfernen,")
        print("'u' hochladen, 'q' beenden")
        try:
            sel = input("> ").strip()
        except EOFError:
            return
        if not sel:
            continue
        if sel in ("q", "quit", "exit"):
            return
        if sel in ("u", "upload"):
            upload()
            continue
        addl, deln = [], []
        for tok in sel.split():
            neg = tok.startswith("-")
            tok = tok.lstrip("-")
            if not tok.isdigit() or not (1 <= int(tok) <= len(keys)):
                print(f"  '{tok}' ist keine gültige Nummer")
                continue
            (deln if neg else addl).append(keys[int(tok) - 1])
        if deln:
            remove(deln)
        if addl:
            # vorher zeigen, was das für den Platz bedeutet
            extra = sum(estimate_mib(s) for s in addl if s not in have)
            if cap:
                print("\n  geschätzt danach: %.2f von %.2f MiB"
                      % (used / MIB + extra, cap / MIB))
                if used / MIB + extra > cap / MIB:
                    print("  Warnung: das wird vermutlich zu gross.")
                    if input("  trotzdem? [j/N] ").strip().lower() != "j":
                        continue
            add(addl)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="Regionen auflisten")
    ap.add_argument("--status", action="store_true", help="Belegung anzeigen")
    ap.add_argument("--add", nargs="+", metavar="REGION")
    ap.add_argument("--remove", nargs="+", metavar="REGION")
    ap.add_argument("--upload", action="store_true")
    a = ap.parse_args()

    if a.list:
        for slug, (mb, f, label) in REGIONS.items():
            print("%-24s ~%5.2f MiB   PBF %4.0f MB   %s"
                  % (slug, estimate_mib(slug), mb, label))
        return
    if a.remove:
        remove(a.remove)
    if a.add:
        add(a.add)
    if a.upload:
        upload()
        return
    if a.status or a.remove or a.add:
        status()
        return
    menu()


if __name__ == "__main__":
    main()
