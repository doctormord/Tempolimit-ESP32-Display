# Übergabe an die Bedienung

Für Menschen, die das Gerät benutzen, verkabeln oder flashen — nicht für die
Weiterentwicklung. Dafür ist `handoff.md` da.

---

## Verkabelung

Display (EstarDyn 1,53" rund, ST77916, 11-Pin-QSPI-Variante):

| Displaypin | GPIO |
|---|---|
| GND | GND |
| VCC | 3V3 (5 V ginge laut Datenblatt auch) |
| SCL | 12 |
| SDA / IO0 | 11 |
| IO1 | 13 |
| IO2 | 14 |
| IO3 | 9 |
| RST | 8 |
| CS | 10 |
| BL | 7 |
| TE | 16 |

GPS (NEO-6M): VCC an 5 V, GND an GND, **TX des Moduls an GPIO18**, RX an GPIO17.

> Die mit „RX/TX" beschrifteten Pins des DevKit sind GPIO43/44 — die
> USB-UART-Brücke. Dort darf das GPS **nicht** hin. Genau dieser Fehler hat
> beim Aufbau Stunden gekostet.

Schalter, jeweils gegen Masse (interne Pullups sind aktiv):

| Pin | offen | geschlossen |
|---|---|---|
| GPIO21 | Tempolimit-Anzeige | Tacho: Mitte zeigt das gefahrene Tempo |
| GPIO5 | normal | erzwingt die simulierte Fahrt |

Gesperrt: GPIO26–37 (Flash/PSRAM), GPIO19/20 (USB), GPIO43/44 (UART-Brücke),
GPIO0/3/45/46 (Strapping).

## Bauen und flashen

```bash
pio run -e esp32s3 -t upload     # Firmware
pio run -e esp32s3 -t uploadfs   # Kartendaten aus data/
pio device monitor               # 115200 Baud
```

USB-C an den **UART-Port** des DevKit. Serial läuft über denselben Port wie das
Flashen, ein Kabel genügt.

## Karten verwalten

```bash
python3 tools/maps.py            # interaktives Menü
python3 tools/maps.py --status   # was liegt drauf, wieviel Platz
```

Lädt von Geofabrik, bereitet auf, zeigt die Belegung, lädt hoch. Braucht
`osmium` (`pip install osmium`).

Achtung: Geofabriks Brandenburg **enthält Berlin**, Niedersachsen enthält
Bremen. Beide zu laden verschwendet Platz und halbiert die Cache-Trefferquote.
Das Werkzeug warnt.

## Was die Anzeige zeigt

Große Zahl in der Mitte: das geltende Limit. `frei` bei unbegrenzt, `?` wenn
keine Kartendaten vorliegen.

Darunter, wenn die Karte einen Grund kennt: `ZONE`, `KINDER`, `ZEIT`. Bei
Fahrrad- und Spielstraße steht statt der Zahl das Piktogramm des echten
Schildes, das Tempo rutscht darunter — bei 7 km/h als `SCHRITT`.

Fläche blau bei Fahrrad- und Spielstraße (RAL 5017, wie die echten Schilder).
Fläche rot, sobald du mehr als 10 % über dem Limit bist.

Ring außen: gefahrenes Tempo im Verhältnis zum Limit.

Statusband unten, Festbreitenschrift:
Zeile 1 Tempo, Quelle (`FIX` / `DEM` / `...`) mit Satellitenzahl, Ortszeit.
Zeile 2 Breite, Länge, Fahrtrichtung.

Im Stand (unter 2 km/h) dimmt das Display auf 20 von 255.

## Wenn etwas nicht geht

**Display bleibt schwarz, Beleuchtung an.** Zuerst **Durchgang aller sieben
Signalleitungen messen** (SCL, SDA, IO1–3, CS, RST), nicht in der Software
suchen. Kommando und Adresse gehen über alle vier Datenleitungen — eine einzige
tote Leitung legt alles still, und `gfx->begin()` meldet trotzdem Erfolg.

Diagnose beim Start einschalten:

```bash
PLATFORMIO_BUILD_FLAGS=-DLCD_DIAG pio run -e esp32s3 -t upload
```

Prüft Kurzschlüsse, testet TE (60 Hz = Panel empfängt Kommandos) und zeigt ein
Farbtestbild. Findet **keine** unterbrochenen Leitungen — dafür das Multimeter.

**Bildfehler.** `LCD_QSPI_HZ` in `src/config.h` stufenweise senken: 80 → 40 →
20 → 1 MHz.

**Kein GPS.** Die Statistikzeile im Log trennt die Fälle:

| Bild | Bedeutung |
|---|---|
| `Bytes=` bleibt ~0 | nichts kommt an — Leitung TX → GPIO18 prüfen |
| viele Bytes, `ok=0`, viele Prüfsummenfehler | falsche Baudrate |
| `ok` steigt, `Sat=0` | Empfang läuft, noch kein Fix (drinnen normal) |

**Limit `-1` im Stand.** Normal, wenn keine Straße innerhalb von 30 m liegt. Im
Gebäude sind 100 m und mehr üblich.
