# Backlog

Offene Punkte, grob nach Nutzen sortiert. Erledigtes wandert nicht hierhin
zurück, sondern nach `history.md`.

---

## 1. Kartendaten ohne Toolchain aktualisieren

Heute braucht ein Kartenwechsel `osmium`, Python und `pio run -t uploadfs`
auf einem eingerichteten Rechner. Für ein Gerät, das im Auto hängt, ist das
zu viel.

### a) Weboberfläche über einen Access Point — umgesetzt, ungeprüft auf Hardware

`src/webupdate.h`/`.cpp`, siehe `history.md` (2026-08-14) für den Weg dahin
und die Partitionstabellen-Sackgasse unterwegs. Kurzfassung: AP
`Tempolimit-Setup` (offen, kein Passwort — für ein Gerät im Auto vertretbar,
und ein WPA2-Passwort müsste ohnehin am Gerät selbst nachzulesen sein),
Weboberfläche unter `http://192.168.4.1/`, Upload/Löschen legt Änderungen in
`/maps_pending` ab und übernimmt sie erst beim nächsten Neustart (Grund:
`SpeedLimitGrid` könnte gerade ein offenes File-Handle auf die betroffene
Regionsdatei halten).

**Nächster Schritt: erste Testrunde auf echter Hardware.** Zweimal sauber
gebaut, mehr nicht — auf dem Gerät weder AP-Verbindung noch Upload noch
Neustart-Übernahme geprüft. Zu beobachten:

- Verbindet sich ein Handy anstandslos mit dem offenen AP? Manche Geräte
  melden offene Netze ohne Internet als "kein Internet, trotzdem verbinden".
- Läuft der Upload einer echten `.msg`-Datei (Brandenburg, 2,6 MiB) über
  WLAN sauber durch, inklusive Fortschrittsanzeige im Browser?
- Schaltet der AP nach `AP_IDLE_TIMEOUT_MS` (5 min) wirklich ab, und startet
  `DEMO_PIN` 10 s halten (`AP_HOLD_TRIGGER_MS`) ihn zuverlässig neu?
- Wird `/maps_pending` nach einem Neustart korrekt geleert und `/maps`
  entsprechend aktualisiert (`applyPendingMapChanges()`, läuft vor
  `grid.begin()`)?
- Reicht der Sicherheitsabstand `AP_FREE_MARGIN_BYTES` (64 KiB) beim
  Platz-Check, oder ist `clientContentLength()` als Obergrenze für den
  multipart-Rahmen zu knapp/zu großzügig bemessen?
- Kein Captive-Portal-Redirect (DNS auf 192.168.4.1 umbiegen) — bewusst
  weggelassen, um keine ungeprüfte `DNSServer`-Abhängigkeit einzuführen. Falls
  das Verbinden in der Praxis umständlich ist, ist das der nächste Hebel.

### b) LittleFS als Massenspeicher am Rechner — weiterhin offen, nicht vordringlich

Mit (a) umgesetzt ist der eigentliche Bedarf gedeckt; dieser Weg bliebe eine
Alternative ohne Handy/Laptop-WLAN in der Nähe, falls sich das als nötig
erweist.

Der ESP32-S3 hat einen nativen USB-Anschluss und kann USB-MSC. Dann erscheint
die Karte als Laufwerk und man kopiert Dateien wie auf einen Stick.

Offene Fragen:

- **Dateisystem.** USB-MSC gibt Blockzugriff heraus; der Rechner erwartet FAT.
  LittleFS kann er nicht lesen. Also entweder eine zweite FAT-Partition (Platz
  ist da) und beim Start von FAT nach LittleFS kopieren, oder gleich alles auf
  FAT — dann ist die Frage, wie gut FAT mit den Lesezugriffen des Lookups
  zurechtkommt.
- **Der native USB-Port ist derzeit unbenutzt**, weil `ARDUINO_USB_CDC_ON_BOOT=0`
  gesetzt ist und Serial über die UART-Brücke läuft. Beides gleichzeitig geht,
  braucht aber Sorgfalt.

`tools/maps.py` bleibt in jedem Fall der Weg für Leute mit Rechner — es lädt
von Geofabrik, bereitet auf, zeigt die Belegung und lädt hoch.

## 2. Zweite Testfahrt auswerten

Die drei Befunde der ersten Fahrt sind eingebaut, aber **nur gebaut, nicht auf
Hardware geprüft** (Board war beim Fixen nicht angeschlossen):

- Begründung außerhalb der Geltungszeit unterdrückt
- Überschreitung ohne Blende, harter Farbwechsel
- `SWITCH_AHEAD_MS` 600 → 300

**Erste echte Rückmeldung von der Straße (2026-08-14):** rechts aus einer
Tempo-30-Zone auf eine unbeteiligte 50er-Straße abgebogen (Kreuzung
52.460744, 13.521135), Anzeige blieb ~400 m auf 30 statt sofort auf 50 zu
wechseln — Neustart an derselben Stelle korrigierte es sofort. Mit den
Kartendaten gegengerechnet: die Kreuzung liegt in einem dichten
Zonen-Gebiet, eine lange parallel verlaufende Zonen-Kette blieb die ganze
Strecke im Suchradius. Genau das war hier schon als Verdacht notiert
("`MATCH_HYSTERESIS` auf parallelen Straßen") — jetzt mit echten Daten
bestätigt. Fix: Hysterese zusätzlich zeitlich auf `MATCH_HYSTERESIS_MAX_MS`
(4 s) begrenzt, siehe `history.md`. **Nicht auf Hardware geprüft** — die
nächste Testfahrt sollte gezielt an dieser Kreuzung gegenprüfen, ob 4 s
reichen, ohne an echten Kreuzungen neues Flackern einzuführen. Falls die
Zonen-Kette sehr viel länger als ~4 s Fahrzeit ist (schnelles Fahren auf der
Straße), könnte selbst 4 s noch zu großzügig oder zu knapp sein — das zeigt
erst die nächste Fahrt.

Weiter zu beobachten:

- Kommen die Limits jetzt zum richtigen Zeitpunkt? Wenn weiterhin zu früh, ist
  `MATCH_MAX_DIST_M` (30 m) der nächste Hebel — bei dem Radius gewinnt eine
  einmündende Straße, bevor man sie erreicht.
- Verhalten an Kreuzungen mit der Vorausschau.

## 3. Datenformat weiter verkleinern

Gemessen und beschrieben in `history.md`. Umgesetzt ist MSG2; nicht umgesetzt:

- **Bitmap-Index** statt sparse: bei dicht belegten Rastern (Deutschland,
  44,7 % belegt) 0,71 statt 1,33 MiB. Bei Regionsdateien bedeutungslos, weil
  der Index dort ohnehin klein ist.
- **Innerorts-Regelfall folgern** statt speichern: 45,3 % aller Stützpunkte
  gehören zu Tempo-50-Straßen. Bräuchte eine Ortslagen-Ebene und tauscht
  Genauigkeit genau dort ein, wo sie am wichtigsten ist. **Nicht empfohlen.**

## 4. Dreistellig mit Beschriftung

**Nachgemessen statt geschätzt (mit `lv_text_get_size()`, nicht von Hand):**
Die Sorge, `888` sei bei vorhandener Beschriftung an der Grenze, bezog sich
auf den hypothetischen Schlimmstfall `888` — der aber **nie als echtes Limit
vorkommt**. Reale dreistellige Limits sind ausschließlich 100/110/120/130,
und die sind in Mittelschrift deutlich schmaler:

| Text | Mittelschrift (162) | Engschrift (171) |
|---|---|---|
| `100` | 201 px | 177 px |
| `110` | 168 px | 159 px |
| `120` | 201 px | 177 px |
| `130` | 203 px | 177 px |
| `888` (Schlimmstfall) | 252 px | 195 px |

`130 ZEIT` ist mit 203 px rund 49 px schmaler als der `888`-Schlimmstfall, mit
dem die Schriftgröße gewählt wurde — vergleichbar mit dem Sicherheitsabstand,
den die zweistelligen Limits mit Beschriftung längst im echten Betrieb ohne
Beanstandung durchlaufen (Testfahrt, `ZONE`/`KINDER`-Etappen der Demo-Route).
**Vermutlich kein Problem in der Praxis.**

Offen bleibt die genaue verfügbare Breite an der verschobenen Position (mit
`NUM_Y_SHIFT`) — der alte Rechenweg dafür ist nicht mehr nachvollziehbar, und
ein Nachbau mit der Kreissehnenformel (`2*sqrt(144² - Versatz²)`) widerspricht
sich mit dem bekannt funktionierenden Zweistelligen-Fall (sagt dort nur noch
1 px Rand voraus, obwohl das im echten Betrieb längst unauffällig läuft) —
**dem Rechenweg ist also nicht zu trauen, ohne visuell nachzuprüfen.** Falls
in der Praxis doch mal ein `130 ZEIT` o.ä. am Rand abgeschnitten aussieht:
zuerst mit dem Simulator (Bildschirm nötig, nicht headless) gegenprüfen, erst
dann an `NUM_Y_SHIFT` oder der Schriftgröße drehen. Die Mittelschrift-Quelle
liegt als `src/DIN 1451 Std Mittelschrift.otf` im Repo — ein kleinerer
Schnitt für diesen Fall ließe sich also erzeugen, falls es doch nötig wird.

## 5. SD-Karte für den Vollausbau

Deutschland ist als MSG2 rund 42 MiB — passt nie ins Flash. Der Reader nimmt
schon ein beliebiges `fs::FS`, es fehlt nur ein SPI-SD-Modul und das Mounten.
Achtung: die alten SD_MMC-Pins (14/17/16) kollidieren mit Display und GPS.

## 6. Aufräumen

- `README.md` und `TODO.md` beschreiben teils den Stand von vor der
  LittleFS-Umstellung.
- Der Job `simulator` in `.github/workflows/build.yml` ruft `pio run -e sim`
  auf — das Env gibt es nicht mehr, der Simulator läuft über CMake. Der Job
  schlägt fehl.
- `tools/out-berlin/` und `tools/out-germany/` sind Daten im alten MSG1-Format
  und werden von nichts mehr gelesen. 108 MiB.
