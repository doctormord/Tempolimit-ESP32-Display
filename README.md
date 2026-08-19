<!-- Note for maintainers: this is the only file in the project that stays in
     English on purpose — everything else (comments, docs, UI text) is
     German. Keep this file in sync whenever hardware, wiring, the build/
     flash workflow, the map-data workflow, or a src/config.h parameter
     changes. See CLAUDE.md for the rule. -->

# Tempolimit-Anzeige — Speed Limit Display

A round, dashboard-mounted speed-limit sign for a bike or car. An ESP32-S3
reads GPS position, matches it against an offline map derived from
OpenStreetMap, and shows the current speed limit on a 360x360 round display
— including the reason (zone, school, time-restricted), pictograms for
bicycle/play streets, and a red warning when you're over the limit. No
phone, no internet connection while driving: the map lives on the device's
own flash.

There is also a PC simulator for developing the UI without hardware, and a
Wi-Fi based way to update the map data without a laptop.

## Contents

1. [Hardware](#1-hardware)
2. [Wiring](#2-wiring)
3. [Features](#3-features)
4. [Building & flashing](#4-building--flashing)
5. [PC simulator](#5-pc-simulator)
6. [Map data: where it comes from and how to build it](#6-map-data-where-it-comes-from-and-how-to-build-it)
7. [Updating maps on the device](#7-updating-maps-on-the-device)
8. [Configuration reference (`src/config.h`)](#8-configuration-reference-srcconfigh)
9. [Switches](#9-switches)
10. [Diagnostics & the serial log](#10-diagnostics--the-serial-log)
11. [Repository layout](#11-repository-layout)
12. [Further documentation](#12-further-documentation)
13. [License](#13-license)

## 1. Hardware

| Part | Notes |
|---|---|
| **ESP32-S3-DevKitC-1**, module **N16R8** | 16 MB flash, 8 MB Octal PSRAM. The PSRAM is required — the framebuffer and the map's cell cache both live there. |
| **Display**: EstarDyn 1.53" round, controller **ST77916**, QSPI, 360x360 | Must be the **11-pin QSPI variant** (GND, VCC, SCL, SDA/IO0, IO1, IO2, IO3, RST, CS, BL, TE). The same panel also exists as a 15-pin 3-wire-SPI variant with a **DC** pin — that one needs a different bus and does not work with this firmware. |
| **GPS**: NEO-6M module | Any breakout with a 9600-115200 baud NMEA UART output works; the firmware auto-detects both pin order and baud rate. |
| 2x momentary/toggle switch | Against ground, wired to internal pull-ups — no external resistors needed. |

There is **no SD card slot** on this board. The map lives entirely in the
module's 16 MB flash (see [§6](#6-map-data-where-it-comes-from-and-how-to-build-it)).

## 2. Wiring

Display (pin names as printed on the breakout):

| Display pin | ESP32-S3 GPIO |
|---|---|
| GND | GND |
| VCC | 3V3 or 5V (module has its own regulator; logic stays 3.3 V) |
| SCL (clock) | 12 |
| SDA / IO0 | 11 |
| IO1 | 13 |
| IO2 | 14 |
| IO3 | 9 |
| CS | 10 |
| RST | 8 |
| BL (backlight) | 7 |
| TE | leave unconnected (only used for the optional `-DLCD_DIAG` diagnostics) |

GPS (NEO-6M):

| GPS pin | ESP32-S3 GPIO |
|---|---|
| TX (module sends) | 18 (`GPS_RX` in firmware) |
| RX | 17 (`GPS_TX`, mostly unused) |
| VCC | 5V (VIN pin) |
| GND | GND |

Switches (see [§9](#9-switches)): **GPIO21** and **GPIO5**, both against GND.

**Reserved / do not use**: GPIO26-37 (flash & PSRAM), GPIO19/20 (native
USB), GPIO43/44 (USB-UART bridge — this is what `Serial` actually uses,
see [§4](#4-building--flashing)).

Init sequence note: the firmware uses `st77916_150_init_operations` (for
the 1.53" panel), not the GFX library's default 180-series sequence. With
the wrong sequence the screen stays black even though `gfx->begin()`
reports success — QSPI has no read-back channel, so a successful `begin()`
call proves nothing about whether the panel actually received anything.

## 3. Features

- **Speed-limit sign display.** Shows the current limit as a large digit,
  styled after the real German sign. Two- and three-digit limits use
  different, individually-tuned font sizes so the numeral always fills the
  circle well.
- **Reason label.** Under the number: `ZONE` (Tempo-30-Zone), `KINDER`
  (school/children hazard), or `ZEIT` (time-restricted limit, e.g. school
  hours). Left blank for a plain sign or "no data" — together over 75% of
  all roads, so the common case stays uncluttered. A time-restricted
  reason only shows while the restriction is actually in effect.
- **Extra hint.** Independent of the reason label and of the limit itself:
  `NÄSSE` (lower limit `maxspeed:conditional` when wet), `WILD` (game
  crossing), `KURVE` (curve warning sign), `GEFAHR` (generic hazard sign),
  or `ENG` (narrowing road), sourced from OSM's `hazard`/`traffic_sign`
  tags. Takes priority over the reason label when both apply (it's the
  safety-relevant one), and — unlike the reason label — still shows even
  when the limit itself is unknown (`?`) or unrestricted (`frei`), since a
  curve is a curve regardless of what the sign says.
- **Pictograms.** Bicycle streets and play streets (`verkehrsberuhigter
  Bereich`) show the real sign's pictogram (Zeichen 244.1 / 325.1) instead
  of a digit, colored to match the real sign (Verkehrsblau). The speed
  moves below as a number, or as the word `SCHRITT` for walking pace.
- **Over-limit warning.** The whole display turns red the moment you
  exceed the limit (with a small tolerance and hysteresis so it doesn't
  flicker right at the threshold) — this never waits for a transition
  effect, it has to be immediate.
- **Smooth transitions.** Limit/reason changes fade via the *backlight*
  (dim to black, swap the content, fade back up) rather than redrawing
  pixels — this costs no rendering time at all and looks the smoothest of
  several approaches that were measured and rejected (see `CLAUDE.md`).
- **Speedometer mode.** A switch flips the display to show your actual
  speed instead of the limit; the fill ring stays scaled to the limit so
  you can see how much of it you're using.
- **Demo drive.** Without a GPS fix (e.g. on a desk), the device drives a
  simulated route over real Berlin/Brandenburg streets pulled straight
  from the map data, cycling through every display state. Useful for
  demos and for regression-checking the map lookup itself.
- **Map matching.** GPS position is matched to the nearest tagged road
  within 30 m, filtered by direction of travel (so a crossing side street
  doesn't win just because it's briefly closer), with a short hysteresis
  window so the display doesn't flicker right at a junction, and a small
  look-ahead so a new sign is already showing by the time you reach it.
- **Multi-region maps.** Several regions can sit side by side on the
  device (e.g. two federal states); the lookup picks whichever region's
  bounding box contains your position, and the nearest match wins where
  regions overlap.
- **Wi-Fi map updates.** The device can open its own Wi-Fi hotspot and
  serve a small web page to upload/remove map regions — no laptop or
  toolchain needed in the field. See [§7](#7-updating-maps-on-the-device).
- **PC simulator.** The same UI code runs in an SDL2 window on your
  computer, for fast UI iteration without flashing hardware.

## 4. Building & flashing

Install [PlatformIO](https://platformio.org/) (CLI, or the VS Code
extension — either works from a plain shell).

```bash
pio run -e esp32s3                 # compile the firmware
pio run -e esp32s3 -t upload       # flash it
pio run -e esp32s3 -t uploadfs     # upload data/maps/*.msg to the device's flash
pio device monitor                 # serial log, 115200 baud
```

Plug the USB-C cable into the DevKit's **UART port**, not the native USB
port — `Serial` in this firmware is routed to UART0 (`platformio.ini` sets
`ARDUINO_USB_CDC_ON_BOOT=0`), which goes out over the same USB-UART bridge
chip used for flashing. If upload fails, hold **BOOT**, tap **RESET**,
release BOOT.

The device runs fine without GPS or map data attached yet — it just shows
a `?` sign and "no fix", which is enough to check the display wiring first.

Useful build-time flags (`PLATFORMIO_BUILD_FLAGS=-Dxxx pio run -e esp32s3 -t upload`):

| Flag | Effect |
|---|---|
| `-DUSE_DIN_ENG` | switch to the narrower DIN 1451 Engschrift font (default is Mittelschrift) |
| `-DLCD_DIAG` | run wiring/TE self-tests and a color test pattern at boot (~5 s) |
| `-DFORCE_DEMO` | force the simulated demo drive even with a valid GPS fix (for unattended test runs; normally use the `DEMO_PIN` switch instead) |
| `-DST77916_INIT_180` | use the *wrong* (180-series) init sequence, for comparison while debugging a black screen |

`include/lv_conf.h` is checked in and ready to use. Only regenerate it if
it's missing or LVGL was re-fetched: `pio run -e esp32s3 && bash
tools/setup_lvconf.sh`.

There are no automated tests. The simulator (below) and the demo drive's
`ABWEICHUNG` (deviation) log are the tools used to check correctness.

## 5. PC simulator

```bash
cmake -S sim -B build-sim && cmake --build build-sim -j
./build-sim/tempolimit-sim
```

This is **not** a PlatformIO environment — there is no `[env:sim]` in
`platformio.ini`, because PlatformIO can't find SDL2 reliably. CMake finds
it via `find_package`/`pkg-config` on macOS, Linux, and inside the
`.devcontainer` Codespace alike.

```bash
sudo apt install libsdl2-dev cmake     # Debian/Ubuntu
brew install sdl2 cmake                # macOS
```

`src/ui.c`/`ui.h` are platform-neutral and compiled unchanged into both the
firmware and the simulator — no Arduino, GPS, or file-system code is
allowed in there. The simulator drives a fixed route (`sim/sim_main.c`)
through every display state (two-digit, three-digit, `frei` = unlimited,
`?` = unknown, over-limit).

## 6. Map data: where it comes from and how to build it

Map data is derived from **OpenStreetMap**, via regional extracts from
[Geofabrik](https://download.geofabrik.de/). It is converted into a
compact custom binary format (**MSG2**, one file per region, e.g.
`brandenburg.msg`) that the device reads directly from its own flash
(LittleFS) at lookup time — no parsing on the device.

```bash
pip install osmium

python3 tools/maps.py                      # interactive menu: pick a region,
                                            # download, build, check size, upload
# or non-interactively:
python3 tools/maps.py --add brandenburg
python3 tools/maps.py --status             # how full is the device's flash
python3 tools/maps.py --upload

# equivalent by hand, one region:
wget https://download.geofabrik.de/europe/germany/brandenburg-latest.osm.pbf
python3 tools/osm_to_grid.py brandenburg-latest.osm.pbf data/maps --name brandenburg
pio run -e esp32s3 -t uploadfs
```

Notes:

- **Don't load a region and the smaller region(s) it contains** —
  Geofabrik regions overlap (Brandenburg contains Berlin, Niedersachsen
  contains Bremen, ...); `tools/maps.py` warns about this. Overlapping
  regions are supported but cost extra cache slots for no benefit if one
  fully contains the other.
- **Capacity isn't guessed.** `tools/maps.py` reads the actual partition
  table (`board_build.partitions` in `platformio.ini`) to know how much
  space is really available, the same way `uploadfs` does.
- **Rough size budget**, `.msg` as a fraction of the source `.pbf`:

  | Region | `.pbf` | `.msg` (approx.) |
  |---|---|---|
  | Berlin | 94 MB | 0.65 MiB |
  | Berlin + Brandenburg | 284 MB | 2.5-2.7 MiB |
  | Mecklenburg-Vorpommern | 121 MB | 1.1 MiB |
  | Sachsen | 254 MB | 2.2 MiB |
  | Niedersachsen + Bremen | 478 MB | 4.1 MiB |
  | Bayern | 809 MB | 7.0 MiB |
  | all of Germany | 4.6 GB | ~42 MiB |

  All of Germany does **not** fit in flash (the filesystem partition is
  12.875 MiB, see `partitions_maps_16MB.csv`). Northern Germany or
  Berlin+Brandenburg fit comfortably; a full national rollout would need
  an SD card, which this board doesn't have a slot for.
- The exact on-disk format is documented in full in the `tools/osm_to_grid.py`
  docstring and mirrored in `src/speedlimit_grid.h` — the two are kept in
  sync by hand, so any format change touches both.
- Up to `MAX_REGIONS` (8) regions can sit side by side on the device.

## 7. Updating maps on the device

Two ways to get a `.msg` file onto the device:

**a) With a computer** (works today): `python3 tools/maps.py`, then
`pio run -e esp32s3 -t uploadfs` as above.

**b) Over Wi-Fi, no computer needed in the field:** the device can open its
own access point and serve a small upload page.

1. The AP starts automatically on boot, or — if it already timed out — by
   holding the `DEMO_PIN` switch (GPIO5) closed for 10 seconds.
2. Connect a phone or laptop to the open Wi-Fi network **`Tempolimit-Setup`**
   (no password).
3. Browse to **http://192.168.4.1/**. The page shows installed regions
   with their size, free space, an upload form (drag in a `.msg` file
   built with `tools/maps.py`/`tools/osm_to_grid.py` as above — this page
   does not process raw OSM data, only the finished file), and a delete
   link per region.
4. Uploads and deletions are staged, not applied immediately — the page
   says so, and there's a "restart now" button. This is deliberate: while
   the device is running, its map reader may have a file open for reading
   from the very region you're replacing, so changes are only applied at
   the very start of the *next* boot, before anything is opened.
5. Without any connection, the AP shuts itself off after 5 minutes to save
   power in a parked vehicle; it stays open as long as something's
   connected, plus the same grace period afterwards.

There is no captive-portal redirect (the browser won't pop up
automatically) — navigate to the address by hand.

## 8. Configuration reference (`src/config.h`)

Every tunable value in the project lives in this one file — font sizes,
color thresholds, timing, map-matching, dimming, pins. It's
platform-neutral (no Arduino/ESP32 includes) so both the firmware and the
simulator use exactly the same values. The rule for contributors: **if
something should be adjustable without reading code, it belongs here, not
inline in a function.** Every constant's inline comment explains *why* its
value is what it is (often backed by a measurement) — this table is a
map, not a replacement for reading those comments.

<!-- Table below mirrors src/config.h's own section grouping. Keep both
     in sync when a constant is added, removed, renamed, or its default
     changes meaningfully. -->

### Font

| Constant | Default | Meaning |
|---|---|---|
| `USE_DIN_MITTEL` / `USE_DIN_ENG` | Mittelschrift | which DIN 1451 cut to use; set via build flag `-DUSE_DIN_ENG`, not edited here directly |
| `FONT_SIZE_2DIGIT` / `_3DIGIT` | 205 / 162 (Mittelschrift) | point size of the main numeral, per digit count |
| `FONT_SIZE_LABEL` | 48 | point size of the reason label (`ZONE`, `SCHRITT`, ...) |
| `FONT_SIZE_PICNUM` | 72 | point size of the speed number shown under a pictogram |
| `NUM_OPT_2DIGIT` / `_3DIGIT`, `LABEL_OPT`, `PICNUM_OPT` | per-font | optical centering correction (LVGL centers the text box, not the ink) — must be re-measured for any newly converted font |
| `NUM_Y_SHIFT` | -25 | moves the numeral up when a reason label is shown below it |
| `PICTO_Y` | -26 | vertical position of the bicycle/play-street pictogram |
| `LABEL_Y` / `LABEL_Y_PICTO` | 85 / 80 | vertical position of the reason label, under the numeral vs. under a pictogram |

### Display / appearance

| Constant | Default | Meaning |
|---|---|---|
| `OVER_TOLERANCE_PCT` | 10 | how far above the limit counts as "too fast" |
| `OVER_HYSTERESIS_KMH` | 3.0 | fallback margin so the over-limit warning doesn't flicker right at the threshold |
| `OVER_STYLE_INVERT` | 1 | 1 = whole area turns red on over-limit; 0 = only the digits do |
| `ARC_TAU_MS` | 250 | smoothing time constant of the fill ring |
| `FADE_MODE` | 2 (backlight fade) | 0 = hard cut, 1 = pixel-opacity fade (measured expensive, see `CLAUDE.md`), 2 = fade via the backlight (default, costs no rendering) |
| `FADE_MS` | 1000 | duration for `FADE_MODE 1` only |
| `FADE_BL_MS` | 500 | total backlight fade duration (half down, half up) |
| `FADE_BL_GAMMA` | 2.2 | gamma-correct the backlight ramp so it looks linear to the eye |
| `FADE_COLOR_MS` | 0 (hard cut) | color-change fade — measured not worth doing, see `CLAUDE.md` |
| `LABEL_LETTER_SPACE` | 3 | extra letter spacing in the reason label (DIN 1451 caps nearly touch otherwise) |
| `DISC_OVERLAP` | 3 | how far the white disc overdraws the ring, to hide anti-aliasing seams |
| `BAND_COLOR` / `BAND_OPA` / `BAND_TEXT` | — | status band color, opacity, and text color |

### Display hardware

| Constant | Default | Meaning |
|---|---|---|
| `LCD_QSPI_HZ` | 80000000 | QSPI bus clock; lower it (40 -> 20 -> 1 MHz) if the picture is corrupted, e.g. on loose wiring |
| `LCD_TE_SYNC` | 1 | sync each redraw to the panel's tearing-effect signal |
| `LCD_TE_TIMEOUT_US` | 20000 | give up waiting for TE after this long (keeps things working if TE isn't wired) |
| `LCD_BUF_DIV` | 2 | draw buffer size as a fraction of a full frame (1/2 = largest that still fits internal RAM) |

### Backlight

| Constant | Default | Meaning |
|---|---|---|
| `LCD_BL_LEVEL` / `LCD_BL_DIM_LEVEL` | 255 / 20 | brightness while moving vs. standing still |
| `DIM_BELOW_KMH` / `DIM_ABOVE_KMH` | 2.0 / 5.0 | two thresholds (not one) so it doesn't flicker at a red light |
| `DIM_FADE_MS` | 400 | dim/undim transition time |

### Operating mode / switches

| Constant | Default | Meaning |
|---|---|---|
| `MODE_PIN` | GPIO21 | switch to ground = speedometer mode instead of the sign |
| `DEMO_PIN` | GPIO5 | switch to ground = force the simulated demo drive; held 10 s = (re)start the Wi-Fi AP |
| `MODE_DEBOUNCE_MS` | 80 | switch debounce time |

### Map-matching look-ahead

| Constant | Default | Meaning |
|---|---|---|
| `PREDICT_AHEAD_MS` / `PREDICT_WEIGHT` | 700 / 0.6 | dead-reckon this far ahead and factor it into candidate scoring, so a street you're only crossing loses to the one you're actually on |
| `SWITCH_AHEAD_MS` / `SWITCH_AHEAD_MAX_M` | 300 / 25 | look this far ahead (time x speed, capped in meters) so a new sign is already showing when you reach it |

### Map-matching

| Constant | Default | Meaning |
|---|---|---|
| `MATCH_MAX_DIST_M` | 30 | max. distance to a road for it to count as a match |
| `MATCH_HYSTERESIS` | 1.6 | preference for staying on the previously-matched road, to avoid flicker at a junction |
| `MATCH_HYSTERESIS_MAX_MS` | 4000 | hard cap on how long that preference can hold — without it, a real-world bug let a stale limit persist for hundreds of meters (see `CLAUDE.md`/`doc/content/history.md`, 2026-08-14) |
| `COURSE_MIN_KMH` | 8.0 | below this speed, GPS heading is treated as noise and ignored (also applies to look-ahead) |
| `COURSE_TOLERANCE_DEG` | 45 | allowed heading deviation for a road to count as a match (opposite direction allowed too) |
| `CELL_CACHE_SLOTS` | 27 | shared LRU cache for map grid cells, in PSRAM — rule of thumb: 9 slots per simultaneously overlapping region, plus headroom |
| `CELL_ALLOC_GRAN` | 4096 | cache buffer growth step |
| `MAX_REGIONS` | 8 | max. number of region files loaded at once |
| `GRID_DIR` | `/maps` | where installed region files live on the device |

### Wi-Fi map update (`webupdate.h`/`.cpp`)

| Constant | Default | Meaning |
|---|---|---|
| `AP_SSID` | `Tempolimit-Setup` | open network, no password (deliberate — see `CLAUDE.md`) |
| `AP_IDLE_TIMEOUT_MS` | 5 min | AP shuts off after this long with no client connected; the same timer also gives a grace period after the last client disconnects |
| `AP_HOLD_TRIGGER_MS` | 10 s | how long to hold `DEMO_PIN` to restart the AP after it timed out |
| `AP_FREE_MARGIN_BYTES` | 64 KiB | safety margin when checking free space before accepting an upload |
| `PENDING_DIR` | `/maps_pending` | staging area for uploads/deletions until the next restart applies them |

### Timing

| Constant | Default | Meaning |
|---|---|---|
| `UI_UPDATE_MS` | 60 (~17 Hz) | rate of map lookups and display *values* — not the frame rate, which the fill ring's own `ui_tick()` drives independently every loop iteration |
| `LOG_INTERVAL_MS` | 1000 | serial log line rate |
| `GPS_GRACE_MS` | 15000 | how long to wait for a GPS fix before falling back to the demo drive |
| `DEMO_LEG_MS` | 12000 | duration of one demo-route leg |

### GPS

| Constant | Default | Meaning |
|---|---|---|
| `GPS_BAUD_TARGET` | 9600 | baud rate the module is configured to after auto-detection |
| `GPS_RATE_HZ` | 5 | fix rate requested via UBX `CFG-RATE` |
| `GPS_PROBE_MS` | 4000 | how long to try each pin/baud combination while searching |

## 9. Switches

Two debounced switches against ground, both using internal pull-ups (no
external resistors):

| Pin | Open | Closed (to GND) |
|---|---|---|
| **GPIO21** (`MODE_PIN`) | speed-limit sign | speedometer: shows your actual speed, fill ring stays scaled to the limit |
| **GPIO5** (`DEMO_PIN`) | normal | forces the simulated demo drive even with a valid GPS fix; held 10 s restarts the Wi-Fi AP if it had timed out |

## 10. Diagnostics & the serial log

`pio device monitor` at 115200 baud. A few things to look for:

- **GPS status line**, every 5 s: `Bytes=... ok=... (Saetze/s)
  Pruefsummenfehler=... Sat=...`. Sentence rate should settle at **10/s**
  once the module is configured (5 Hz x RMC+GGA). Bytes staying near 0
  means nothing is arriving (check the TX(module) -> GPIO18 wire); many
  bytes with many checksum errors means wrong baud rate.
- **Demo log line**: position, matched limit vs. expected, reason, extra
  hint, and `ok`/`ABWEICHUNG` (deviation) — the regression check for the
  map lookup path, since a desk has no real GPS fix to test against.
- **Draw stats**: frames/s, bus load, LVGL time, longest frame, TE wait
  time — a concrete way to back up any claim about smoothness instead of
  eyeballing it.
- **`-DLCD_DIAG`** (see [§4](#4-building--flashing)) adds a wiring
  continuity test and a TE signal test at boot, plus a color test pattern
  before LVGL starts — useful for isolating a black-screen problem to
  wiring vs. software.

## 11. Repository layout

| Path | What's there |
|---|---|
| `src/config.h` | every tunable parameter, platform-neutral |
| `src/ui.c`, `ui.h` | the display itself — shared verbatim with the simulator |
| `src/main.cpp` | firmware: GPS task, display driver, switches, backlight, Wi-Fi map update wiring |
| `src/speedlimit_grid.h` | map lookup, reads the MSG2 format |
| `src/webupdate.h`, `.cpp` | Wi-Fi access point + map upload web UI |
| `src/lv_font_din_*.c`, `*.ttf`/`.otf` | converted fonts and their source files |
| `src/img_fahrrad.c`, `img_spielstrasse.c` | converted pictograms |
| `tools/osm_to_grid.py` | OSM `.pbf` -> `.msg` converter; the on-disk format is documented in its docstring |
| `tools/maps.py` | interactive console for downloading, building, and uploading regions |
| `tools/even_digit_spacing.py` | normalizes digit spacing after converting a font |
| `tools/png_to_lvgl.py` | converts a pictogram PNG into an LVGL image source |
| `sim/` | PC simulator (CMake, SDL2) |
| `data/maps/` | shipped map region(s) |
| `partitions_maps_16MB.csv` | flash partition table (large filesystem partition for map data) |
| `doc/content/` | German working docs — history, backlog, handoff notes (see [§12](#12-further-documentation)) |

## 12. Further documentation

This README covers what a new user or contributor needs to get started.
The deeper *why* behind design decisions, measurements, and dead ends —
in German, like the rest of the project's comments — lives in:

| | |
|---|---|
| `CLAUDE.md` | the technical reference: how everything works, with the reasoning and measurements behind it |
| `doc/content/handoff.md` | current status, hard rules, next steps — read this first when picking the project back up |
| `doc/content/backlog.md` | open tasks and their open questions |
| `doc/content/handover.md` | operation, wiring, troubleshooting — for someone at the device |
| `doc/content/history.md` | append-only project history, including dead ends, so they aren't re-investigated |

## 13. License

GNU General Public License v3.0 — see [`LICENSE`](LICENSE).
