#!/usr/bin/env python3
"""
maps.py - select, download, prepare, and upload map regions.

    python3 tools/maps.py                  interactive selection
    python3 tools/maps.py --status         show what's currently in data/maps/
    python3 tools/maps.py --add brandenburg berlin
    python3 tools/maps.py --remove berlin
    python3 tools/maps.py --upload         just push to flash

Downloads the extracts from Geofabrik, calls osm_to_grid.py, and reports
whether everything together fits into LittleFS. The capacity is not
guessed - it's read from the partition table referenced in platformio.ini.

Needs `osmium` (pip install osmium) for preparation and `pio` for the
upload.
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

# PBF sizes per Geofabrik (as of August 2026, change slowly) and the
# conversion factor to the finished .msg. Calibrated against Berlin and
# Germany: the old format's .dat was 2.2% of the PBF size, MSG2 of that is
# 28% (city-state, lots of fragments to chain) to 39% (flat/rural state).
# The estimate is only for planning - after conversion the actual file size
# is what counts.
REGIONS = {
    "baden-wuerttemberg":      (614, 0.39, "Baden-Württemberg"),
    "bayern":                  (809, 0.39, "Bayern"),
    "berlin":                  (94, 0.28, "Berlin"),
    "brandenburg":             (284, 0.39, "Brandenburg (contains Berlin)"),
    "bremen":                  (20.1, 0.28, "Bremen"),
    "hamburg":                 (51, 0.28, "Hamburg"),
    "hessen":                  (327, 0.39, "Hessen"),
    "mecklenburg-vorpommern":  (121, 0.39, "Mecklenburg-Vorpommern"),
    "niedersachsen":           (478, 0.39, "Niedersachsen (contains Bremen)"),
    "nordrhein-westfalen":     (867, 0.39, "Nordrhein-Westfalen"),
    "rheinland-pfalz":         (254, 0.39, "Rheinland-Pfalz"),
    "saarland":                (52, 0.39, "Saarland"),
    "sachsen":                 (254, 0.39, "Sachsen"),
    "sachsen-anhalt":          (165, 0.39, "Sachsen-Anhalt"),
    "schleswig-holstein":      (150, 0.39, "Schleswig-Holstein"),
    "thueringen":              (151, 0.39, "Thüringen"),
}

MIB = 1048576.0
LFS_BLOCK = 4096          # LittleFS allocates in blocks
LFS_RESERVE = 64 * 1024   # metadata overhead, rounded up generously


def estimate_mib(slug):
    """
    estimate_mib(slug) - rough size prediction (MiB) for a region's finished
    .msg file, before it has actually been converted.

    Parameters:
      slug - region key into REGIONS

    Multiplies the known PBF size by the empirical 2.2% base ratio and the
    region's city/rural factor (see REGIONS comment above). Planning only.
    """
    pbf_mb, factor, _ = REGIONS[slug]
    return pbf_mb * 0.022 * factor


# --------------------------------------------------------------- Capacity
def fs_capacity():
    """
    fs_capacity() - read the filesystem partition's size from the project's
    partition table.

    Returns (size_in_bytes, table_path) on success, or (None, reason) if the
    table or the data partition couldn't be found. Mirrors the same lookup
    rule `uploadfs` uses, so the reported capacity always matches what will
    actually be flashed: parse platformio.ini for board_build.partitions,
    then in that CSV take the LAST row with type "data" and subtype
    spiffs/fat/littlefs (last one wins, in case of duplicates).
    """
    ini = configparser.ConfigParser()
    ini.read(os.path.join(ROOT, "platformio.ini"))
    table = None
    for sec in ini.sections():
        if ini.has_option(sec, "board_build.partitions"):
            table = ini.get(sec, "board_build.partitions").strip()
    if not table:
        return None, "no board_build.partitions in platformio.ini"

    path = os.path.join(ROOT, table)
    if not os.path.exists(path):
        # Framework-provided tables live inside the PlatformIO package
        pat = os.path.expanduser(
            "~/.platformio/packages/framework-arduinoespressif32/tools/partitions")
        path = os.path.join(pat, table)
    if not os.path.exists(path):
        return None, f"partition table {table} not found"

    size = None
    with open(path) as f:
        for line in f:
            if line.strip().startswith("#") or not line.strip():
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 5:
                continue
            if parts[1] == "data" and parts[2] in ("spiffs", "fat", "littlefs"):
                size = int(parts[4], 0)      # last one wins, same as uploadfs
    if size is None:
        return None, f"no filesystem partition in {table}"
    return size, table


def on_flash_size(paths):
    """
    on_flash_size(paths) - actual space these files would occupy in
    LittleFS, in bytes.

    Parameters:
      paths - list of file paths to sum up

    LittleFS allocates whole 4 KiB blocks, so a file's on-flash size is
    rounded up per file, not just summed by byte count; LFS_RESERVE adds a
    generous cushion for filesystem metadata.
    """
    total = LFS_RESERVE
    for p in paths:
        total += int(math.ceil(os.path.getsize(p) / LFS_BLOCK)) * LFS_BLOCK
    return total


def installed():
    """installed() - sorted list of .msg filenames currently in data/maps/."""
    if not os.path.isdir(MAP_DIR):
        return []
    return sorted(f for f in os.listdir(MAP_DIR) if f.endswith(".msg"))


def status(verbose=True):
    """
    status(verbose=True) - print (unless verbose=False) the installed regions
    and current LittleFS usage, and return (capacity, used) in bytes.

    Parameters:
      verbose - if False, just compute and return the numbers without
                printing (used internally by upload() and the menu() loop's
                preview calculation, which don't want the full listing)
    """
    cap, table = fs_capacity()
    files = [os.path.join(MAP_DIR, f) for f in installed()]
    used = on_flash_size(files) if files else LFS_RESERVE
    if verbose:
        print()
        print("Maps in data/maps/")
        print("-" * 58)
        if not files:
            print("  (none)")
        for p in files:
            print("  %-28s %8.2f MiB" % (os.path.basename(p),
                                         os.path.getsize(p) / MIB))
        print("-" * 58)
        if cap:
            pct = 100.0 * used / cap
            bar = "#" * int(pct / 4) + "." * (25 - int(pct / 4))
            print("  used %6.2f of %6.2f MiB  [%s] %.0f %%"
                  % (used / MIB, cap / MIB, bar, pct))
            print("  Partition table: %s" % table)
            if used > cap:
                print("  TOO LARGE - does not fit in LittleFS")
        else:
            print("  Capacity unknown: %s" % table)
    return cap, used


# ------------------------------------------------------------------ Steps
def download(slug):
    """
    download(slug) - fetch the region's PBF extract from Geofabrik into
    tools/pbf/, unless it's already there.

    Parameters:
      slug - region key into REGIONS

    Returns the destination path, or None on failure. Downloads to a
    ".part" temp file first and renames on success, so an interrupted
    download can never be mistaken for a complete one.
    """
    os.makedirs(PBF_DIR, exist_ok=True)
    dst = os.path.join(PBF_DIR, f"{slug}-latest.osm.pbf")
    if os.path.exists(dst):
        print(f"  PBF already present ({os.path.getsize(dst)/MIB:.0f} MiB), "
              f"skipping download")
        return dst
    url = f"{BASE_URL}{slug}-latest.osm.pbf"
    tmp = dst + ".part"
    print(f"  downloading {url}")

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
        print(f"\n  Download failed: {e}")
        return None
    print()
    os.rename(tmp, dst)
    return dst


def convert(slug, pbf):
    """
    convert(slug, pbf) - run osm_to_grid.py on the downloaded PBF to produce
    data/maps/<slug>.msg.

    Parameters:
      slug - region key, becomes the output file's base name
      pbf  - path to the source .osm.pbf file

    Returns True on success. Just shells out to the converter script and
    checks its exit code; all the actual conversion logic lives there.
    """
    os.makedirs(MAP_DIR, exist_ok=True)
    script = os.path.join(ROOT, "tools", "osm_to_grid.py")
    print(f"  converting (can take a few minutes) ...")
    r = subprocess.run([sys.executable, script, pbf, MAP_DIR, "--name", slug],
                       cwd=ROOT)
    if r.returncode != 0:
        print("  conversion failed")
        return False
    return True


# Regions that fully contain another region. Loading both wastes space and
# halves the cache hit rate, because every lookup in the overlap area has to
# search both.
CONTAINS = {
    "brandenburg": ["berlin"],
    "niedersachsen": ["bremen"],
}


def warn_overlap(slugs):
    """
    warn_overlap(slugs) - after adding regions, warn if any newly-added or
    already-installed region is fully contained in another installed one.

    Parameters:
      slugs - region keys just added (checked together with what's already
              on disk)

    Geofabrik ships some regions as supersets of others (Brandenburg
    contains Berlin, Niedersachsen contains Bremen); keeping both loaded is
    pure waste, see CONTAINS above.
    """
    have = {f[:-4] for f in installed()} | set(slugs)
    for big, smalls in CONTAINS.items():
        if big not in have:
            continue
        for small in smalls:
            if small in have:
                print(f"\n  Note: {big} already contains {small} (that's how "
                      f"Geofabrik ships it).")
                print(f"  Keeping both doubles the search work in the "
                      f"overlap area.")
                print(f"  Recommendation:  python3 tools/maps.py --remove {small}")


def add(slugs):
    """
    add(slugs) - download and convert each region, then warn about overlaps.

    Parameters:
      slugs - list of region keys to add
    """
    for slug in slugs:
        if slug not in REGIONS:
            print(f"unknown region: {slug}")
            continue
        print(f"\n=== {REGIONS[slug][2]} ===")
        pbf = download(slug)
        if pbf:
            convert(slug, pbf)
    warn_overlap(slugs)


def remove(slugs):
    """
    remove(slugs) - delete each region's .msg file from data/maps/, if present.

    Parameters:
      slugs - list of region keys to remove
    """
    for slug in slugs:
        p = os.path.join(MAP_DIR, slug + ".msg")
        if os.path.exists(p):
            os.remove(p)
            print(f"  {slug}.msg removed")
        else:
            print(f"  {slug}.msg was not present")


def upload():
    """
    upload() - push data/ to the device's flash via `pio run -t uploadfs`,
    after checking it actually fits.

    Refuses to run pio at all if the installed regions exceed the
    filesystem partition's capacity, since uploadfs would otherwise just
    fail (or worse, upload a truncated/inconsistent set) partway through.
    """
    cap, used = status(verbose=False)
    if cap and used > cap:
        print("Does not fit in LittleFS - remove some regions first.")
        return False
    print("\npushing data/ to flash ...")
    r = subprocess.run(["pio", "run", "-e", "esp32s3", "-t", "uploadfs"],
                       cwd=ROOT)
    return r.returncode == 0


# ---------------------------------------------------------------- Interactive
def menu():
    """
    menu() - interactive loop: list regions, let the user add/remove by
    number or trigger an upload, looping until 'q'.

    No parameters (reads/writes global state via installed()/status()).
    Input format: space-separated numbers to add (e.g. '4 13'), a leading
    '-' to remove (e.g. '-4'), 'u' to upload, 'q' to quit.
    """
    while True:
        have = {f[:-4] for f in installed()}
        cap, used = status()
        print()
        print("Available regions  (* = already converted)")
        print("-" * 58)
        keys = list(REGIONS)
        for i, slug in enumerate(keys, 1):
            mark = "*" if slug in have else " "
            est = estimate_mib(slug)
            pbf_mb, _, label = REGIONS[slug]
            print("%2d %s %-32s ~%5.2f MiB   PBF %4.0f MB"
                  % (i, mark, label, est, pbf_mb))
        print("-" * 58)
        print("Add by number (e.g. '4 13'), '-4' to remove,")
        print("'u' to upload, 'q' to quit")
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
                print(f"  '{tok}' is not a valid number")
                continue
            (deln if neg else addl).append(keys[int(tok) - 1])
        if deln:
            remove(deln)
        if addl:
            # show what this would mean for space, before actually doing it
            extra = sum(estimate_mib(s) for s in addl if s not in have)
            if cap:
                print("\n  estimated afterwards: %.2f of %.2f MiB"
                      % (used / MIB + extra, cap / MIB))
                if used / MIB + extra > cap / MIB:
                    print("  Warning: this will likely be too large.")
                    if input("  proceed anyway? [y/N] ").strip().lower() != "y":
                        continue
            add(addl)


def main():
    """
    main() - CLI entry point: parse arguments and dispatch to the
    corresponding action, falling back to the interactive menu() when no
    flags are given.
    """
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="list regions")
    ap.add_argument("--status", action="store_true", help="show usage")
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
