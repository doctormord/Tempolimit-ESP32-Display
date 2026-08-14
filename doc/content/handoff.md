# Übergabe an die nächste Sitzung

Was eine neue Sitzung wissen muss, um weiterzuarbeiten. Bedienung und
Verkabelung stehen in `handover.md`, offene Aufgaben in `backlog.md`, der
Verlauf in `history.md`.

---

## Stand

Das Gerät funktioniert vollständig: Display, GPS mit 5 Hz, Karte aus LittleFS,
Map-Matching, zwei Anzeigemodi, zwei Schalter. Eine Testfahrt mit dem Fahrrad
ist gemacht.

**Nicht auf Hardware geprüft** sind die drei Korrekturen aus dieser Testfahrt —
das Board war beim Einbauen nicht angeschlossen. Sie bauen sauber und der
Simulator läuft, mehr nicht:

1. Begründung wird unterdrückt, wenn eine zeitliche Bedingung existiert und
   gerade nicht greift (`speedlimit_grid.h`, `scanCell`)
2. Überschreitung löst keine Blende mehr aus (`main.cpp`, `blf_*`)
3. `SWITCH_AHEAD_MS` 600 → 300

Das ist der erste Punkt, den die nächste Sitzung prüfen sollte.

**Kartenupdate per Access Point:** startet auf echter Hardware sauber
(Serial-Log: `[Update] AP "Tempolimit-Setup" gestartet,
http://192.168.4.1/`). Was das noch nicht abdeckt: eine echte
Browser-Verbindung, ein echter Upload oder eine echte Neustart-Übernahme.
Offene Fragen dazu in `backlog.md`, Punkt 1a.

**Aus der zweiten Testfahrt bestätigt und an der Quelle behoben:** nach dem
Abbiegen aus einer Tempo-30-Zone blieb das Limit auf 30 stehen. Ursache war
kein "die Hysterese hält halt lange fest" (das war ein erster, vom Nutzer
zu Recht angezweifelter Erklärungsversuch), sondern ein echter Fehler in
`tools/osm_to_grid.py`: `chain()` verschweißte an Kreuzungen innerhalb von
Tempo-30-Zonen unabhängige Straßen zu einer Kette, weil alle Straßen einer
Zone dieselbe Kennzeichnung tragen und nur Ende-trifft-Anfang plus gleiche
Kennzeichnung geprüft wurde, nicht die Richtung. Fix: `chain()` verkettet
nur noch bei ≤60° Richtungsänderung an der Naht; `brandenburg.msg` neu
erzeugt und ersetzt. Details und Zahlen in `history.md` 2026-08-14. Die
4-Sekunden-Hysterese-Grenze (`MATCH_HYSTERESIS_MAX_MS`) bleibt zusätzlich
als Sicherheitsnetz.

**Firmware und neue Karte sind geflasht** (2026-08-14, `pio run -t upload`
und `-t uploadfs` über den USB-UART-Port). Serial-Log bestätigt einen
sauberen Boot: `[FS] LittleFS: 2830336 von 13500416 Byte belegt` (die neue,
größere `brandenburg.msg`), `[Grid] brandenburg 222x178 Zellen, 13702
belegt, Index 107 KiB in 37 ms`, `[Grid] 1 Region(en): brandenburg`, dazu
der AP-Start (siehe oben). GPS fand die Baudrate (9600 auf GPIO18), aber
noch kein Fix (Test lief ohne Antennensicht). **Noch nicht geprüft: die
eigentliche Testfahrt an der Kreuzung selbst** (52.460744, 13.521135) mit
echtem GPS-Fix draußen — das ist der naheliegendste nächste Schritt.

## Wo was liegt

| | |
|---|---|
| `src/config.h` | **alle** Parameter, plattformneutral |
| `src/ui.c`, `ui.h` | Anzeige, gemeinsam mit dem Simulator |
| `src/main.cpp` | Firmware: GPS, Blende, Schalter, Hintergrundlicht |
| `src/speedlimit_grid.h` | Kartenlookup, Format MSG2 |
| `src/webupdate.h`, `.cpp` | Kartenupdate per Access Point, siehe Backlog 1a |
| `tools/osm_to_grid.py` | OSM → `.msg`, Format im Docstring |
| `tools/maps.py` | Regionen laden, aufbereiten, hochladen |
| `tools/even_digit_spacing.py` | Ziffernabstände vereinheitlichen |
| `tools/png_to_lvgl.py` | Piktogramme freistellen |
| `sim/sim_main.c` | PC-Simulator |

## Regeln, die man nicht verletzen sollte

**`ui.c` bleibt plattformneutral.** Kein Arduino, kein GPS, keine Dateizugriffe.
Die einzige Schnittstelle ist `ui_state_t` plus `ui_create()`, `ui_update()`,
`ui_tick()`. Wer ein Feld ergänzt, füllt es in **beiden** Aufrufern
(`main.cpp`, `sim/sim_main.c`) — sonst baut der Simulator nicht mehr oder zeigt
Müll.

**`ui_update()` und `ui_tick()` sind verschieden.** `ui_update()` nimmt
Messwerte im Datentakt, `ui_tick()` bewegt die Anzeige und gehört in jeden
Schleifendurchlauf. Wer beides zusammenlegt, bekommt den ruckelnden Balken
zurück.

**Das Grid-Format steht an zwei Stellen.** `tools/osm_to_grid.py` (Docstring)
und `src/speedlimit_grid.h` — Änderungen immer in beiden.

**Fonts:** immer mit `--no-kerning` erzeugen und danach
`even_digit_spacing.py` laufen lassen. Die Tabellenziffern-Varianten
(`*t.c`, für den Tacho) bewusst **ohne**.

**Reihenfolge der `REASON_*`-Werte** nicht ändern, ohne die Kartendaten neu zu
erzeugen — sie stecken in den Bits der `.msg`-Dateien.

**`applyPendingMapChanges()` muss vor `grid.begin()` laufen**, und zwar bevor
irgendein File-Handle auf eine Regionsdatei offen ist. Grund: die
Weboberfläche (`webupdate.h`) könnte sonst eine Datei ersetzen oder löschen,
während `SpeedLimitGrid` gerade mitten aus ihr liest.

## Werkzeuge zum Prüfen

**Demo-Route.** 14 Etappen auf echten Straßen, jede mit erwartetem Limit und
erwarteter Begründung. Das Log schreibt `ABWEICHUNG`, wenn es auseinanderläuft.
Erzwingen über den Schalter an GPIO5 oder:

```bash
PLATFORMIO_BUILD_FLAGS=-DFORCE_DEMO pio run -e esp32s3 -t upload
```

**Gegenrechnen auf dem PC.** Wenn ein Limit unterwegs falsch aussieht: Position
aus dem Log nehmen und die `.msg` mit einem kleinen Python-Leser prüfen. Das
trennt „Karte hat da nichts" von „Lookup trifft nicht". Das Format ist im
Docstring von `osm_to_grid.py` vollständig beschrieben.

**Zeichenlast.** Das Log zeigt Bildaufbauten/s, Buslast, LVGL-Anteil, längsten
Zyklus und TE-Wartezeit. Damit lässt sich jede Behauptung über Flüssigkeit
belegen statt vermuten.

## Was schon gemessen und verworfen wurde

Bevor jemand erneut an der Bildrate dreht — diese Wege sind gegangen und in
`history.md` mit Zahlen dokumentiert:

Zeichenpuffer im PSRAM, Kreismaske, `LV_USE_FLOAT`, QSPI-Takt 40 gegen 80 MHz,
zwei Zeicheneinheiten auf zwei Kernen (schlechter), größerer LVGL-Heap (passt
nicht), kleinere Blendflächen (bringt nichts), einfarbiges Rechteck statt
Ziffer (gleich teuer).

Kern: ein großflächiger Neuaufbau kostet 85–100 ms, davon nur 10 %
Übertragung. Deshalb die Blende über das Hintergrundlicht statt über Pixel.

## Nächster Brocken

Firmware und Karte sind bereits geflasht (siehe oben) — jetzt dieselbe
Kreuzung (52.460744, 13.521135) draußen mit echtem GPS-Fix abfahren: kommt
die Verkettungs-Korrektur in `chain()` tatsächlich an, wechselt das Limit
jetzt zügig auf 50? Siehe `backlog.md`, Punkt 2.

Danach: mit einem Handy tatsächlich mit dem AP "Tempolimit-Setup" verbinden,
eine `.msg`-Datei hochladen, neu starten, prüfen ob die Region ankommt.
Offene Einzelfragen dazu in `backlog.md`, Punkt 1a.
