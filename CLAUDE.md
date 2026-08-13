# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Kommentare, Doku und UI-Texte sind auf Deutsch — bitte beibehalten.

## Dokumentation

Diese Datei beschreibt, **wie** das Projekt funktioniert. Vier weitere Dateien
in `doc/content/` beschreiben den Stand drumherum:

| | |
|---|---|
| `handoff.md` | **Zuerst lesen.** Stand, Regeln, Werkzeuge zum Pruefen, naechster Brocken |
| `backlog.md` | offene Aufgaben mit ihren offenen Fragen |
| `handover.md` | Bedienung, Verkabelung, Fehlersuche — fuer Menschen am Geraet |
| `history.md` | Verlauf, **nur anhaengen**. Auch Irrwege, damit sie niemand zweimal geht |

Wer etwas Grundsaetzliches aendert oder eine Sackgasse ausmisst, schreibt einen
Eintrag in `history.md` — die Messreihen dort haben mehrfach verhindert, dass
dieselbe Idee ein zweites Mal geprueft wird.

## Hardware

- ESP32-S3-DevKitC-1, Modul N16R8 (16 MB Flash, 8 MB Octal-PSRAM)
- Display: EstarDyn 1,53" rund, Controller ST77916, QSPI, 360x360
- GPS: NEO-6M, UART auf GPIO17/18
- **Kein SD-Kartenslot vorhanden** — nur die 16 MB Flash des Moduls

Verkabelung Display (Pins definiert in `src/main.cpp:57-67`):

| Displaypin | GPIO |
|---|---|
| SCL (Takt) | 12 |
| SDA / IO0 | 11 |
| IO1 | 13 |
| IO2 | 14 |
| IO3 | 9 |
| CS | 10 |
| RST | 8 |
| BL | 7 |

GPS: RX=18 (an TX des Moduls), TX=17.
Gesperrt: GPIO26-37 (Flash/PSRAM), GPIO19/20 (USB), GPIO43/44 (UART-Bridge).

Das Displaymodul ist die 11-Pin-QSPI-Variante (GND, VCC, SCL, SDA/IO0, IO1,
IO2, IO3, RST, CS, BL, TE). **TE bleibt unbeschaltet.** VCC vertraegt laut
Datenblatt 3,3-5 V, die Logikpegel bleiben 3,3 V. Es gibt vom selben Panel
auch eine 15-Pin-Variante mit normalem 3-Draht-SPI und **DC**-Pin — die
braucht einen anderen Bus und funktioniert an dieser Firmware nicht.

Richtige Init-Sequenz ist `st77916_150_init_operations` (1,53"), **nicht** der
Bibliotheks-Default `st77916_180_init_operations`. Mit der falschen Sequenz
bleibt das Bild schwarz, obwohl `gfx->begin()` Erfolg meldet — ueber QSPI wird
nichts zurueckgelesen, der Rueckgabewert sagt also nichts ueber das Panel aus.

### Inbetriebnahme-Diagnose

`-DLCD_DIAG` schaltet drei Tests beim Start frei (kostet ~5 s, daher normal aus):

```bash
PLATFORMIO_BUILD_FLAGS=-DLCD_DIAG pio run -e esp32s3 -t upload
```

- **Verdrahtungstest**: findet Kurzschluesse und festhaengende Pegel. Findet
  **keine** unterbrochenen Leitungen — eine offene Leitung und eine saubere
  Leitung zu einem hochohmigen Panel-Eingang sind von der ESP-Seite nicht
  unterscheidbar. Dafuer braucht es ein Multimeter.
- **TE-Test**: TE ist der einzige Rueckkanal, den QSPI hier bietet. Taktet er
  mit ~60 Hz, empfaengt das Panel Kommandos; 0 Flanken heisst, es erreicht es
  kein einziges. Die Messkette prueft sich vorher selbst an GPIO15.
- **Farbtestbild** vor LVGL: trennt Panelproblem von Oberflaechenproblem.

**Erfahrung aus der Inbetriebnahme:** ein komplett schwarzes Display bei
laufender Hintergrundbeleuchtung war ein **fehlender Kontakt an CS (GPIO10)**.
Weil Arduino_ESP32QSPI mit `SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR`
sendet, gehen Kommando und Adresse ueber alle vier Datenleitungen — eine
einzige schlechte Verbindung legt damit alles still, auch die Initialisierung.
`gfx->begin()` meldet trotzdem Erfolg, weil ueber QSPI nichts zurueckgelesen
wird. Bei schwarzem Bild also zuerst **Durchgang aller sieben Signalleitungen
messen**, bevor an der Software gesucht wird.

Weitere Fallstricke:
- **Serial haengt am UART-Port**, weil `platformio.ini`
  `ARDUINO_USB_CDC_ON_BOOT=0` setzt: `Serial` ist damit UART0 und laeuft ueber
  denselben Bruecken-Chip, ueber den auch geflasht wird (ein Kabel genuegt).
  Auf `1` gestellt landet die Ausgabe auf dem nativen USB-Port (GPIO19/20) und
  der Monitor am UART-Port bleibt stumm.
- Bei Bildfehlern `LCD_QSPI_HZ` in `main.cpp` stufenweise senken: 40 -> 20 -> 1
  MHz. Bei 1 MHz braucht ein Vollbild rund eine halbe Sekunde — das ist nur
  zur Fehlersuche brauchbar, nicht fuer den Betrieb.
- Das Hintergrundlicht laeuft ueber PWM (`LCD_BL_LEVEL`, 0-255) statt starr
  HIGH, und der GPIO ist auf maximale Treiberstaerke gesetzt
  (`GPIO_DRIVE_CAP_3`) — mit dem Standardwert faellt es sichtbar dunkler aus.
- `board_build.arduino.memory_type = qio_opi` ist Pflicht, sonst liefert
  `ps_malloc()` nichts und weder Framebuffer noch Kartenindex passen.

## Bauen

```bash
# Firmware
pio run -e esp32s3                 # uebersetzen
pio run -e esp32s3 -t upload       # flashen (USB-C an den UART-Port!)
pio run -e esp32s3 -t uploadfs     # Karte aus data/ ins Flash
pio device monitor                 # 115200 Baud

# PC-Simulator (SDL2, ueber CMake - NICHT ueber PlatformIO)
cmake -S sim -B build-sim && cmake --build build-sim -j
./build-sim/tempolimit-sim
```

Es gibt **kein** `[env:sim]` in `platformio.ini`. `pio run -e sim` in
`README.md` (Abschnitt 4), `TODO.md` und im Job `simulator` von
`.github/workflows/build.yml` ist veraltet und schlaegt fehl — der Simulator
laeuft ausschliesslich ueber CMake, weil PlatformIO SDL2 nicht findet.

Tests gibt es nicht. Der Simulator ist das Pruefmittel fuer UI-Aenderungen:
`sim/sim_main.c` faehrt eine feste Route (`ROUTE[]`) durch alle Anzeige-
zustaende (zweistellig, dreistellig, `frei` = 255, `?` = unbekannt, Limit
ueberschritten). Neue Zustaende dort als Etappe ergaenzen, statt Testcode in
`ui.c` einzubauen.

`include/lv_conf.h` liegt im Repo. Nur falls sie fehlt oder LVGL neu gezogen
wurde: `pio run -e esp32s3 && bash tools/setup_lvconf.sh`.

## Software-Architektur

**`src/ui.c` / `src/ui.h` sind plattformneutral** — dieselben Dateien werden
von Firmware und Simulator uebersetzt. Kein Arduino, kein GPS, keine Datei-
zugriffe duerfen dort hinein; die einzige Schnittstelle ist `ui_state_t`
(Limit, Tempo, Fix, Position, Ortszeit) plus `ui_create()`/`ui_update()`.
Wenn sich nur die Datenquelle aendert, bleibt `ui.c` unangetastet. Wer die
Anzeige um ein Datum erweitert, erweitert `ui_state_t` und fuellt es in
**beiden** Aufrufern (`src/main.cpp`, `sim/sim_main.c`).

Datenfluss auf dem Geraet: `gpsTask` auf Core 0 (FreeRTOS) parst NMEA in
`g_state` (per `gpsMutex` geschuetzt), `loop()` auf Core 1 zieht alle 500 ms
einen Snapshot und ruft `ui_update()`. `lv_timer_handler()` laeuft
ausschliesslich in `loop()` — LVGL ist mit `LV_USE_OS = LV_OS_NONE`
konfiguriert und nicht threadsicher.

Der GPS-Kurs steht bewusst **nicht** in `ui_state_t` (die Anzeige zeigt ihn
nicht), sondern in `g_course` — er wird nur fuer den Richtungsfilter des
Lookups gebraucht und unter demselben `gpsMutex` mitgelesen.

## Begruendung des Limits

Jede Kette traegt in **Bit 0-2 ihres flags-Byte** eine Begruendung — das Byte
gab es ohnehin (Bit 7 = zeitliche Bedingung), das Feld selbst kostet also
nichts. Werte: `REASON_*` in `osm_to_grid.py`, gespiegelt als `UI_REASON_*` in
`ui.h`. **Reihenfolge nicht aendern, ohne die Kartendaten neu zu erzeugen.**

Unter der Ziffer erscheint `ZONE`, `KINDER` oder `ZEIT`. Stumm bleiben
`SCHILD` (der Normalfall) und `NONE` — zusammen ueber drei Viertel aller
Strassen —, ausserdem `frei`, `?` und der Tachomodus.

**Die Begruendung gilt nur, solange die Bedingung greift.** Eine Strasse mit
"50, aber 30 von 6 bis 17 Uhr wegen Kindern" zeigte um 18 Uhr sonst
"50 KINDER" — was Unsinn ist, weil die Beschraenkung gerade nicht gilt.
Traegt eine Kette eine zeitliche Bedingung und greift diese nicht, faellt die
Begruendung weg (`scanCell` in `speedlimit_grid.h`).

Gemessen an Brandenburg (157.251 Ketten):

| | Anteil |
|---|---|
| ohne Angabe | 72,1 % |
| Zone | 17,1 % |
| Spielstrasse | 4,3 % |
| Einzelschild | 4,2 % |
| zeitlich | 1,4 % |
| Kinder | 0,5 % |
| Fahrradstrasse | 0,4 % |

**Ganz gratis war es doch nicht:** die Begruendung gehoert zur Identitaet einer
Kette, dadurch verschmelzen weniger Teilstuecke (153.929 -> 157.251 Ketten) und
die Datei wuchs von 2,61 auf 2,63 MiB. Das Feld kostet null Byte, die
schlechtere Verkettung 0,8 %.

Die OSM-Tags dahinter sind uneinheitlich: `DE:zone30`, `DE:zone:30`,
`de:zone30`, `DE:30`, verteilt auf die drei redundanten Schluessel
`maxspeed:type`, `zone:maxspeed` und `source:maxspeed`. `reason_code()`
normalisiert das. Schule als solche gibt es in OSM nicht — am naechsten kommt
`hazard=children`.

Die Beschriftung nutzt nur Grossbuchstaben A-Z, Umlaute, Leerzeichen und
Bindestrich. Neue Woerter brauchen keinen neuen Konverterlauf, solange sie aus
diesen Zeichen bestehen — die Breite aber schon: auf Hoehe `LABEL_Y` ist der
Kreis noch rund 210 px breit.

### Piktogramme

Fahrrad- und Spielstrasse zeigen **statt der Ziffer** das freigestellte
Piktogramm des amtlichen Schildes (Zeichen 244.1 und 325.1). Das Tempo rutscht
dann nach unten an die Stelle der Beschriftung — bei 7 km/h als Wort
`SCHRITT`, weil Schrittgeschwindigkeit die gemeinte Aussage ist und nicht die
Zahl.

Quellbilder liegen als `src/fahrrad.png` und `src/spielstrasse.png`,
umgewandelt mit:

```bash
python3 tools/png_to_lvgl.py src/fahrrad.png src/img_fahrrad.c \
        --name img_fahrrad --polarity hell --height 128
python3 tools/png_to_lvgl.py src/spielstrasse.png src/img_spielstrasse.c \
        --name img_spielstrasse --polarity dunkel --height 128
```

`--polarity` sagt, welche Flaeche das Motiv ist: `hell` beim weissen Fahrrad
auf blau, `dunkel` bei den schwarzen Figuren auf weiss.

**Format A8** — ein Byte Deckkraft je Pixel, keine Farbe. Die Farbe kommt beim
Zeichnen aus `image_recolor`, dasselbe Bild laesst sich also weiss auf blau und
in jeder anderen Farbe darstellen, ohne es neu zu erzeugen. Halber Speicher
gegenueber RGB565: 208x128 sind 26 KiB.

**Fahrradstrasse und Spielstrasse faerben die Flaeche blau** (`COL_BLUE`,
RAL 5017 Verkehrsblau — die Farbe der echten Schilder 244.1 und 325.1),
Ziffer und Beschriftung dann weiss, und der Fuellbalken ebenfalls blau.
Ueberschreitung schlaegt die Schildfarbe: die Flaeche wird rot, damit eine
Warnung nie mehrdeutig ist.

## Stellschrauben: src/config.h

**Alle Parameter stehen in `src/config.h`** — Schriftgrade, Farbschwellen,
Takte, Matching, Dimmen, Pins. Plattformneutral, wird von Firmware und
Simulator eingebunden. Regel: was man ohne Codeverstaendnis verstellen koennen
soll, gehoert dorthin und nicht mitten in eine Funktion.

Gruppen: Schriftschnitt, Anzeige, Hintergrundlicht, Betriebsart, Vorausschau,
Map-Matching, Takte, GPS.

`UI_UPDATE_MS` (60 ms, rund 17 Hz) ist der Takt von **Lookup und
Anzeigewerten**, nicht die Bildrate — der Fuellbalken laeuft ueber `ui_tick()`
in jedem Schleifendurchlauf und ist davon unabhaengig. Gemessen: 27-62
Bildaufbauten/s.

`COURSE_MIN_KMH` gilt fuer den Richtungsfilter **und** die Vorausschau - es ist
dieselbe Aussage ("unter diesem Tempo ist der GPS-Kurs Rauschen"), deshalb eine
Konstante statt zweier.

### Schriftschnitt umschalten

Beide DIN-Schnitte liegen fertig konvertiert im Repo. **Mittelschrift ist
Standard**, sie fuellt die runde Flaeche besser:

```bash
PLATFORMIO_BUILD_FLAGS=-DUSE_DIN_ENG pio run -e esp32s3   # schmaler Schnitt
```

Die Schriftgrade sind je Schnitt eigens ausgemessen — Mittelschrift ist rund
30 % breiter, dieselben Grade wuerden den Kreis sprengen:

| | Mittelschrift | Engschrift |
|---|---|---|
| zweistellig | 205 px | 197 px |
| dreistellig | 162 px | 171 px |
| Beschriftung | 52 px | 56 px |

Grenze bei Mittelschrift: `88` traegt bis 227 px, `888` bis 168 px.

### Tachomodus

Im Tacho steht **nur** das gefahrene Tempo: keine Beschriftung, kein
Piktogramm, keine Einfaerbung von Schrift oder Flaeche. Das gehoert zum Schild,
nicht zum Tacho. Einzig der Balken bleibt auf das Limit skaliert, damit
sichtbar ist, wieviel der erlaubten Geschwindigkeit ausgeschoepft wird.

Dafuer gibt es einen **zweiten Ziffernsatz mit Tabellenziffern**
(`lv_font_din_m205t`, `m162t`, bewusst ohne `even_digit_spacing.py`). Die
Tachozahl wechselt staendig, und mit den proportionalen Ziffern des Schilds
springt sie seitlich — die 1 ist halb so breit wie die uebrigen. Kostet 22 KiB
Flash.

### Schalter

Zwei entprellte Schalter gegen Masse, beide mit internem Pullup:

| Pin | offen | gegen GND |
|---|---|---|
| **GPIO21** (`MODE_PIN`) | Tempolimit | Tacho: in der Mitte steht das gefahrene Tempo, der Balken bleibt auf das Limit skaliert |
| **GPIO5** (`DEMO_PIN`) | normal | simulierte Fahrt erzwingen, auch bei gueltigem Fix |

Beide GPIOs sind beim ESP32-S3 frei von Sonderfunktionen. GPIO15 waere
ebenfalls frei, wird aber unter `-DLCD_DIAG` als Pruefpin der Messkette
benutzt.

Mit dem Demo-Schalter braucht es fuer eine Vorfuehrung keine eigene Firmware
mehr; `-DFORCE_DEMO` bleibt fuer automatisierte Laeufe. Jeder Wechsel steht
im Log.

### Hintergrundlicht

Unter `DIM_BELOW_KMH` (2 km/h) wird auf `LCD_BL_DIM_LEVEL` (20) gedimmt, ueber
`DIM_ABOVE_KMH` (5 km/h) wieder voll. Zwei Schwellen, damit es an der Ampel
nicht springt; Ueberblendung ueber `DIM_FADE_MS`.

**Grundhelligkeit und Blende liegen uebereinander und werden beide in
wahrgenommener Helligkeit gerechnet**, nicht im PWM-Wert. Eine linear
gefahrene PWM faellt oben kaum und unten schlagartig - das sah beim Abdimmen
wie Flackern aus. Beide Rampen laufen auf demselben `esp_timer`
(`FADE_BL_STEP_MS`), nie in der Hauptschleife.

Der Timer haengt **nicht** an `FADE_MODE`: sonst stuende das Abdimmen still,
sobald jemand die Blendeart umstellt.

### Schriftsatz: Ziffernabstaende

DIN 1451 hat Tabellenziffern - alle mit gleichem Vorschub, aber sehr
unterschiedlichen Strichbreiten und Seitenabstaenden (links 1 bis 8 px). Das
ergibt ungleiche Luecken:

    100 -> 11,8 / 11,8      120 -> 10,8 / 12,8      130 -> 7,8 / 13,8

`tools/even_digit_spacing.py` gibt allen Ziffern denselben Seitenabstand,
danach ist jede Luecke gleich. Die Ziffern werden dadurch proportional statt
tabellarisch - fuer eine einzelne grosse Zahl ist der gleiche optische Abstand
richtig, gleicher Vorschub waere es fuer eine Tabelle.

```bash
python3 tools/even_digit_spacing.py src/lv_font_din_m205.c src/lv_font_din_m162.c
```

**Nach jedem Lauf von lv_font_conv erneut aufrufen** - und die Fonts immer mit
`--no-kerning` erzeugen, sonst ziehen Kerning-Paare die Abstaende wieder
schief (sichtbar bei "120" und an den beiden T in "SCHRITT").

### Optische Mitte

LVGL zentriert den **Textkasten**, nicht die Ziffern. Der Kasten ist so hoch
wie `line_height` und enthaelt Platz fuer Unterlaengen, die Ziffern haben aber
keine - sie sitzen dadurch zu hoch. `NUM_OPT_2DIGIT` / `NUM_OPT_3DIGIT` /
`LABEL_OPT` korrigieren das und sind je Font ausgemessen:

    Korrektur = base_line + Ziffernhoehe/2 - line_height/2

Wer einen Font neu konvertiert, muss diese Werte nachrechnen.

### Ueberblendung

`FADE_MODE` waehlt die Art:

| | |
|---|---|
| 0 | hart, kein Uebergang |
| 1 | Pixelblende ueber die Deckkraft der Ziffer (`FADE_MS`) |
| **2** | **Hintergrundlicht (Standard)** |

Variante 2 kostet **kein Zeichnen**: das Licht faehrt herunter, im Dunkeln
werden Limit und Begruendung getauscht, dann faehrt es wieder hoch.
Gammakorrigiert (`FADE_BL_GAMMA` 2,2), sonst wirkt linear gefahrene PWM oben
flach und unten wie ein Sprung.

**Zu schnell entscheidet der Aufrufer**, nicht `ui.c` (`ui_state_t.over`) -
mit Hysterese (`OVER_HYSTERESIS_KMH`), damit es an der Schwelle nicht pendelt.

**Die Ueberschreitung loest keine Blende aus** und wird auch nicht
zurueckgehalten: eine Warnung muss in dem Moment erscheinen, in dem sie gilt,
nicht eine Viertelsekunde spaeter. Zurueckgehalten werden nur Limit und
Begruendung.

Drei Details, ohne die es nicht funktioniert:

- **`FADE_BL_FLOOR` = 0.** Jeder Restwert laesst den Neuaufbau durchscheinen.
- **Die Rampe laeuft auf einem eigenen `esp_timer`** (`FADE_BL_STEP_MS` 5 ms),
  nicht in der Hauptschleife - die blockiert waehrend eines Neuaufbaus bis zu
  90 ms.
- **Im Dunkelpunkt wird gehalten**, bis `lv_refr_now()` den Bildwechsel
  wirklich abgeschlossen hat. Ohne das faehrt das Licht schon wieder hoch,
  waehrend noch gezeichnet wird - dann sieht man den Aufbau samt
  Streifenteilung.

Variante 1 ist gemessen teuer und nicht zu retten: ein Blendschritt fasst
22.000 Pixel an, bei 30-40 Bildern/s ergeben 200 ms nur sechs Stufen. Eine
Messreihe dazu steht in `config.h` - auch, dass kleinere Blendflaechen nichts
bringen und ein einfarbiges Rechteck genauso teuer ist wie die Ziffer.

### Vorausschauend umschalten

Der Lookup laeuft nicht an der aktuellen Position, sondern `SWITCH_AHEAD_MS`
(600 ms) weiter vorn - das neue Schild steht damit schon da, wenn man es
erreicht. Die Strecke waechst mit dem Tempo und ist genau deshalb sinnvoll:
bei 10 km/h sind es 1,7 m, bei 100 km/h 17 m. `SWITCH_AHEAD_MAX_M` (25 m)
deckelt nach oben.

### Vorausschau beim Matching

An Kreuzungen sprang die Anzeige auf die Querstrasse, weil deren Segment
kurzzeitig naeher lag. Gegenmittel: aus Kurs und Tempo die Position in
`PREDICT_AHEAD_MS` (700 ms) koppelnavigieren und Kandidaten danach bewerten,
wie nah sie an **beiden** Punkten liegen:

    score = d_jetzt + PREDICT_WEIGHT * d_voraus

Eine Strasse, die man nur quert, faellt damit zurueck — man ist gleich nicht
mehr auf ihr. Fuer die 30-m-Reichweite zaehlt weiterhin der echte Abstand
jetzt, nicht die Bewertung. Unter `PREDICT_MIN_KMH` ist der GPS-Kurs
unbrauchbar, dann entfaellt die Vorausschau.

### Warum es zwei Funktionen gibt

`ui_update(state)` uebernimmt neue Messwerte — im Datentakt, also 5 Hz.
`ui_tick(dt_ms)` bewegt die Anzeige darauf zu und gehoert in **jeden**
Schleifendurchlauf, direkt neben `lv_timer_handler()`. Beide Aufrufer muessen
das tun (`main.cpp`, `sim/sim_main.c`).

Ohne die Trennung sprang der Fuellbalken im Datentakt — sichtbar fuenfmal pro
Sekunde. Mit ihr laeuft er zwischen den Messwerten weiter.

`ARC_TAU_MS` rechnet **zeitbasiert**, ist also unabhaengig von der Aufrufrate;
Simulator und Geraet sehen dadurch gleich aus.

### Zeichenlast, gemessen

`lv_arc_set_value()` invalidiert nur den geaenderten Kreisausschnitt - rund
1700 Pixel je Aufbau, 1,3 % eines Vollbilds. Haeufige kleine Schritte sind
damit billiger als seltene grosse.

**TE wird vor jeder Teilflaeche abgewartet**, nicht nur einmal je Bildzyklus.
Mit nur einer Wartung landeten alle weiteren Streifen mitten im Bildaufbau -
sichtbar als waagerechter Strich auf der Puffergrenze.

**`LCD_BUF_DIV` = 2** (ein halbes Bild, 126 KiB, ein einzelner Puffer im
internen RAM). Der Puffer bestimmt, in wieviele Streifen LVGL eine Aenderung
zerlegt; zu klein heisst sichtbare Naht. LVGL teilt dabei immer in **Zeilen** -
eine senkrechte Teilung gibt es nicht.

`LV_DEF_REFR_PERIOD` in `include/lv_conf.h` steht auf 16 ms (Obergrenze
62 fps, vorher 33 ms = 30 fps).

Die **Statuszeilen laufen in Festbreitenschrift** (`lv_font_mono16`, Menlo
16 px, 1 bpp also ohne Kantenglaettung). Mit Proportionalschrift wanderten die
Ziffern bei jedem Wechsel seitlich. Zeile 1: Tempo, Quelle mit Satellitenzahl,
Ortszeit. Zeile 2: Breite, Laenge, Fahrtrichtung.

Zwischen Ring und weisser Flaeche stand eine dunkle Linie aus der
Kantenglaettung beider Kreise — die Flaeche wird deshalb um `DISC_OVERLAP`
(3 px) groesser gezeichnet, als der Ring innen frei laesst.

**Alles ausserhalb des Balkens wird nur bei echter Aenderung gezeichnet.**
Ziffer, Font, Farben, Beschriftung und Statuszeilen haben je einen
`shown_*`-Merker. Der Grund ist die Groesse: eine 197-px-Ziffer ist rund
147x142 px und wuerde sonst fuenfmal pro Sekunde unveraendert neu uebertragen
werden — und sich dabei die Uebertragungszeit mit dem Balken teilen.

Wer `OVER_TOLERANCE_PCT` aendert, muss die Demo-Etappen in `main.cpp`
gegenpruefen: die Etappe "Autobahn, zu schnell" faehrt 145 bei Limit 120,
weil 130 bei 10 % Toleranz nicht mehr ausgeloest haette.

## Kartendaten: Format MSG2, eine Datei je Region

`tools/osm_to_grid.py` (braucht `osmium`) erzeugt **eine** Datei je Region.
Das Format ist im Docstring der Datei vollstaendig beschrieben und in
`speedlimit_grid.h` gespiegelt — **Aenderungen immer in beiden Dateien**.

```bash
python3 tools/osm_to_grid.py berlin-latest.osm.pbf data/maps --name berlin
pio run -e esp32s3 -t uploadfs
```

Mehrere Regionen liegen einfach nebeneinander in `data/maps/`. `begin()` liest
beim Start alle Koepfe und Indizes; ein Lookup fragt nur die Regionen, deren
Bounding-Box die Position enthaelt. Ueberlappungen sind erlaubt, dann gewinnt
der naechstgelegene Treffer. Obergrenze `MAX_REGIONS` (8).

**Ueberlappende Regionen kosten doppelt.** Jede zusaetzliche Region an einer
Position bringt 9 weitere Zellen je Lookup — `CELL_CACHE_SLOTS` muss das
abdecken, sonst verdraengt ein einziger Lookup seinen eigenen Cacheinhalt.
Mit 12 Slots und zwei ueberlappenden Regionen fiel die Trefferquote auf
**null** (0 Treffer / 7140 Lesezugriffe); mit 27 Slots sind es 5248 / 110.
Faustregel: 9 Slots je gleichzeitig ueberlappender Region plus Reserve.

Geofabrik liefert manche Regionen ineinander: **Brandenburg enthaelt Berlin**,
Niedersachsen enthaelt Bremen. Beide zu laden ist reine Verschwendung —
`tools/maps.py` warnt davor.

### Regionen verwalten

`tools/maps.py` ist die Konsole dafuer: Auswahl, Download von Geofabrik,
Aufbereitung, Belegungsanzeige und Upload.

```bash
python3 tools/maps.py                      # interaktives Menue
python3 tools/maps.py --add brandenburg
python3 tools/maps.py --remove berlin
python3 tools/maps.py --status
python3 tools/maps.py --upload
```

Die Kapazitaet wird nicht geraten: das Werkzeug liest `board_build.partitions`
aus `platformio.ini`, findet die Partitionstabelle (Projektordner oder
Framework-Paket) und nimmt die Groesse der letzten Data-Partition mit Subtype
`spiffs`/`fat`/`littlefs` — dieselbe Regel, nach der auch `uploadfs` sucht.
Der Platzbedarf wird blockweise gerechnet (LittleFS belegt in 4-KiB-Bloecken).

Das Zellraster (0,010 x 0,020 Grad) ist global verankert, damit die Zellgrenzen
verschiedener Regionen aufeinander passen. Jede Region speichert nur ihren
eigenen Ausschnitt.

### Warum das Format so aussieht

Gegenueber dem Vorgaenger MSG1 (Berlin: 4,20 MiB) gemessen:

| Aenderung | Berlin |
|---|---|
| eigene Bounding-Box je Region statt Index ueber ganz DE | Index 2,24 MiB -> 5,8 KiB |
| Teilstuecke verketten (104.625 -> 44.593 Ketten) | der eigentliche Hebel |
| Douglas-Peucker, klassenabhaengig 8 / 3 / 0,5 m | |
| Koordinatenraster 0,5 m statt 0,11 m | |
| Zickzack-Varint auf Differenzen statt absolute uint16 | |
| **Summe** | **4,20 MiB -> 0,65 MiB** |

Der grosse Posten war der Index: MSG1 beschrieb immer ganz Deutschland
(390.796 Zellen a 6 Byte), auch wenn nur 728 davon belegt waren.

Die Verkettung ist deshalb so wirksam, weil Wege an jeder Zellgrenze
zerschnitten werden. Vorher hatte ein Segment im Schnitt 4,4 Stuetzpunkte —
daran findet Douglas-Peucker nichts. Erst die Ketten geben ihm Material.

**Innerorts wird bewusst nur auf 0,5 m vereinfacht** (Autobahn 8 m,
Landstrasse 3 m). Der Vereinfachungsfehler geht direkt vom Suchradius
`MATCH_MAX_DIST_M` (30 m) ab, und innerorts liegen Parallelstrassen 20 m
auseinander. Die grobe Variante (15 / 5 / 1,5 m) spart nur 5 % und halbiert
das Budget — nicht lohnend.

**MSG1 hatte einen Geometriefehler**, den MSG2 nebenbei behebt: Stuetzpunkte
aus Nachbarzellen wurden auf `0..65535` geklemmt und rutschten damit auf die
Zellecke. Ein Vergleich ueber 3000 Punkte zeigte 1,83 % abweichende
Ergebnisse, davon 53 % durch genau diese geklemmte Geometrie (MSG1 lag
falsch), 33 % Grenzfaelle an der 30-m-Schwelle. Nicht erklaerte Differenz:
0,27 %.

### Groessen planen

`.dat` betraegt rund 2,2 % der PBF-Groesse (bei MSG1 gemessen, Berlin wie
Deutschland), MSG2 davon nochmal 28 % (Stadt) bis 39 % (Flaechenland). Damit
laesst sich jede Geofabrik-Region vorab abschaetzen:

| Region | PBF | MSG2 geschaetzt |
|---|---|---|
| Berlin | 94 MB | 0,58 MiB (gemessen: 0,65) |
| Berlin + Brandenburg | 284 MB | 2,5 MiB |
| Mecklenburg-Vorpommern | 121 MB | 1,1 MiB |
| Sachsen | 254 MB | 2,2 MiB |
| Niedersachsen + Bremen | 478 MB | 4,1 MiB |
| Bayern | 809 MB | 7,0 MiB |
| ganz Deutschland | 4,6 GB | ~42 MiB |

In die 6,875-MiB-Partition passt damit etwa Norddeutschland oder
Berlin+Brandenburg mit reichlich Reserve. **Ganz Deutschland passt weiterhin
nicht ins Flash** — dafuer bleibt SD noetig.

Ein Lookup liest 3x3 Zellen. Die Bloecke liegen in einem LRU-Cache mit
`CELL_CACHE_SLOTS` (12) Slots im PSRAM, gemeinsam ueber alle Regionen, die
Puffer wachsen in 4-KB-Schritten. `cacheHits()`/`cacheReads()` zum
Mitschneiden auf der Testfahrt; im Betrieb steht es bei rund 50:1.

Beim Lookup werden 3x3 Zellen geprueft (die Strasse kann knapp jenseits der
Zellgrenze liegen), Kandidaten ueber Punkt-Segment-Abstand bewertet, per
GPS-Kurs gefiltert (`COURSE_TOLERANCE_DEG`, Gegenrichtung erlaubt) und mit
Hysterese (`MATCH_HYSTERESIS`) stabilisiert, damit die Anzeige an Kreuzungen
nicht flackert. Zeitliche Limits (`maxspeed:conditional`) stecken als 4 Byte
im Segment und ersetzen das Grundlimit, wenn `condActive()` zutrifft.

## Karte aus LittleFS

Die Karte liegt im Flash-Dateisystem, nicht auf SD (das Board hat keinen
Slot). `SpeedLimitGrid::begin(fs::FS &fs)` bekommt ein **bereits gemountetes**
Dateisystem — LittleFS heute, SD ueber SPI spaeter, ohne Aenderung am Lookup.
Gemountet wird beim Aufrufer (`main.cpp`), weil nur das sich unterscheidet.

```bash
python3 tools/maps.py            # Regionen waehlen, laden, aufbereiten, hochladen
```

- Partitionstabelle ist die eigene `partitions_maps_16MB.csv`: App-Slots auf
  1,5 MiB geschrumpft (Firmware ist 0,75 MiB), Dateisystem dafuer
  **12,875 MiB**. Begruendung steht in der CSV. OTA wird nicht genutzt; wer
  app1 auch noch opfert, gewinnt weitere 1,5 MiB.
- **Nichts umbenennen**: `LittleFS.begin()` mountet per Default das Label
  `"spiffs"`, `uploadfs` nimmt die letzte Data-Partition mit Subtype
  `spiffs`/`fat`/`littlefs`. Die Tabelle passt genau dazu.
- `board_build.filesystem` ist nicht gesetzt und muss es nicht sein —
  pioarduino nutzt `littlefs` als Default.
- `LittleFS.begin(false)`: formatOnFail bleibt aus, sonst loescht ein
  Mountfehler die hochgeladene Karte.
- Deutschland passt auch als MSG2 nicht ins Flash (~42 MiB). LittleFS bleibt
  die Regional-Loesung, fuer den Vollausbau wird SD gebraucht.

Gemessen auf dem Geraet: Brandenburg (2,61 MiB, enthaelt Berlin) belegt
2.760.704 von 13.500.416 Byte, sein Index von 107 KiB ist in **38 ms** geladen.
Unter MSG1 waren allein fuer Berlin 2,24 MiB Index und 741 ms faellig.

## GPS

`gpsTask` sucht die Verbindung selbst: es probiert beide Pinbelegungen
(RX/TX vertauscht) mal vier Baudraten (9600, 38400, 115200, 4800) durch, je
4 s. Als Erfolg gilt **nicht** "es kommen Bytes", sondern ein von TinyGPS
akzeptierter Satz mit gueltiger Pruefsumme — bei falscher Baudrate kommen sehr
wohl Bytes an, nur eben Datenmuell. Die gefundene Kombination steht im Log.

Nach dem Sync stellt `gpsConfigure()` das Modul per UBX auf **5 Hz**
(`CFG-RATE`, 200 ms) und schaltet GSV, GSA, GLL, VTG und ZDA ab
(`CFG-MSG`) — TinyGPS braucht nur RMC und GGA.

**Das Abschalten ist nicht optional, sondern eine Bandbreitenfrage.** 9600 8N1
sind 960 Byte/s. Mit allen Saetzen waeren es bei 5 Hz rund 1900 Byte/s, der
Puffer liefe ueber. Mit RMC+GGA sind es gemessen 717 Byte/s (75 % Auslastung).
Wer weitere Saetze braucht, muss zuerst die Baudrate hochsetzen (`CFG-PRT`) —
die Suchautomatik findet die neue Rate von selbst wieder.

Die Einstellung liegt im gepufferten RAM des Moduls und geht ohne Batterie
beim Trennen der Versorgung verloren; sie wird deshalb bei **jedem**
erfolgreichen Sync neu gesendet, nicht einmalig beim ersten Start.

`UI_UPDATE_MS` (200 ms) passt dazu — bei den frueheren 500 ms waeren drei von
fuenf Positionen ungenutzt verfallen. Das Serial-Log bleibt per
`LOG_INTERVAL_MS` bei 1 Hz, sonst ist der Mitschnitt unlesbar.

Alle 5 s kommt eine Statistikzeile `Bytes= ok= (Saetze/s) Pruefsummenfehler=
Sat=`. Die Satzrate zeigt direkt, ob 5 Hz greift: **10/s** bei RMC+GGA, vorher
waren es 2/s. Die Zeile trennt ausserdem die Fehlerfaelle sauber:

| Bild | Bedeutung |
|---|---|
| Bytes bleibt ~0 | nichts kommt an — Leitung TX(Modul) -> GPIO18 pruefen |
| viele Bytes, ok=0, viele Pruefsummenfehler | falsche Baudrate |
| ok steigt, Sat=-1 oder 0 | Empfang laeuft, aber noch kein Fix (drinnen normal) |
| Fix da, aber `Limit -1` | normal, solange keine Strasse innerhalb von
  `MATCH_MAX_DIST_M` (30 m) liegt. Im Gebaeude sind 100 m und mehr ueblich —
  vor dem Debuggen mit `tools/`-Daten gegenrechnen, ob dort ueberhaupt etwas
  in Reichweite ist. |

## Demo-Fahrt ohne GPS

Bleibt der Fix nach `GPS_GRACE_MS` (15 s) aus, schaltet `loop()` auf eine
simulierte Fahrt (`ui_state_t.demo`, Statuszeile zeigt blau `DEMO`). Kommt
spaeter ein echter Fix, uebernehmen sofort wieder die GPS-Daten.

Die Stuetzpunkte in `DEMO[]` (`main.cpp`) liegen auf **echten Berliner
Strassen** aus den Griddaten, nicht auf erfundenen Koordinaten. Dadurch prueft die Demo
den kompletten Pfad LittleFS -> Zellblock -> Map-Matching und nicht nur die
Anzeige — am Schreibtisch bekommt der NEO-6M ohnehin keinen Fix. Jede Etappe
traegt ihr erwartetes Limit; das Serial-Log stellt Ist und Soll nebeneinander
und schreibt `ABWEICHUNG`, wenn es auseinanderlaeuft.

Etappen: 30 (zu schnell), 50, 60, 80, 100, 120 (zu schnell), 255 = `frei`,
sowie ein Punkt ausserhalb des Extrakts fuer `?`. Ein Durchlauf dauert rund
96 s (`DEMO_LEG_MS` = 12 s je Etappe).

Sobald GPS einen Fix hat, laeuft die Demo nicht mehr an. Erzwingen geht ueber
den Schalter an `DEMO_PIN` oder, fuer automatisierte Laeufe, ueber:

```bash
PLATFORMIO_BUILD_FLAGS=-DFORCE_DEMO pio run -e esp32s3 -t upload
```

Neue Etappen brauchen verifizierte Koordinaten. Der Weg dahin: Segmente aus
`maxspeed.dat` dekodieren, einen langen Strassenzug je Wunschlimit heraus-
suchen und mit einer Nachbildung des Lookups auf dem PC gegenpruefen — sonst
steht im Log ein erwarteter Wert, den die Karte gar nicht hergibt.

## Fonts

`src/lv_font_din_197.c` (zweistellig) und `lv_font_din_171.c` (dreistellig,
auch fuer `frei`) sind konvertierte DIN-1451-Fonts, aktiv ueber
`-DUSE_DIN_FONT` (gesetzt in `platformio.ini` und `sim/CMakeLists.txt`).
Ohne das Define faellt `ui.c` auf Montserrat 48 zurueck.

**Die Groesse steckt im Font, nicht im Code.** Ein anderer Schriftgrad heisst
neu konvertieren. Quelle ist `src/DIN 1451 Std Engschrift.ttf`, das Werkzeug
braucht Node:

```bash
npx lv_font_conv --bpp 4 --size 197 \
  --font "src/DIN 1451 Std Engschrift.ttf" \
  --symbols '0123456789?!-_ #/\frei' \
  --format lvgl -o src/lv_font_din_197.c
```

Danach `ui.c` (die beiden `extern`-Zeilen und `FONT_2DIGIT`/`FONT_3DIGIT`)
und `sim/CMakeLists.txt` auf die neuen Dateinamen ziehen — der Simulator
listet die Fontdateien einzeln auf, die Firmware nimmt ueber
`build_src_filter` ohnehin alles aus `src/`.

Der Zeichensatz muss `0123456789?!-_ #/\frei` bleiben: ohne die Buchstaben
`f r e i` fehlt die Anzeige fuer "unbegrenzt", ohne `?` die fuer "unbekannt".
Aeltere Fassungen des Konverters kannten `--stride` und `--align`, die
aktuelle nicht mehr — die Angaben im Dateikopf der Fonts stammen daher.

Vor dem Vergroessern nachrechnen, ob es passt: die weisse Flaeche ist rund,
die nutzbare Breite nimmt zur Ober- und Unterkante hin ab. Bei Radius 144 px
stehen auf Hoehe der Textkante nur `2*sqrt(144^2 - (Texthoehe/2)^2)` px zur
Verfuegung. Bei 197/171 px belegt der breiteste Fall `888` 191 px von 260 px.

## Sonstiges

- `tools/schild-simulator.html` ist ein eigenstaendiger HTML/SVG-Entwurf der
  Anzeige zum Ausprobieren von Layout und Farben im Browser — kein Bestandteil
  des Builds und nicht mit `ui.c` synchronisiert.
- `.devcontainer/` startet einen Codespace mit noVNC-Desktop auf Port 6080
  (Passwort `lvgl`), damit das SDL-Fenster sichtbar wird. Codespaces hat
  keinen USB-Zugriff — geflasht wird lokal oder ueber das
  `merged-firmware.bin`-Artifact des CI-Builds.
