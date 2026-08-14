# Backlog

Offene Punkte, grob nach Nutzen sortiert. Erledigtes wandert nicht hierhin
zurück, sondern nach `history.md`.

---

## 1. Kartendaten ohne Toolchain aktualisieren

Heute braucht ein Kartenwechsel `osmium`, Python und `pio run -t uploadfs`
auf einem eingerichteten Rechner. Für ein Gerät, das im Auto hängt, ist das
zu viel.

### a) Weboberfläche über einen Access Point — umgesetzt, AP-Start auf Hardware bestätigt

`src/webupdate.h`/`.cpp`, siehe `CLAUDE.md` ("Kartenupdate per Access
Point") für die Funktionsweise und `history.md` (2026-08-14) für den Weg
dahin und die Partitionstabellen-Sackgasse unterwegs.

**Auf dem Gerät bestätigt** (2026-08-14, Serial-Log): der AP startet sauber
(`[Update] AP "Tempolimit-Setup" gestartet, http://192.168.4.1/`). **Noch
offen** — eine echte Browser-Verbindung, ein echter Upload, eine echte
Neustart-Übernahme. Zu beobachten:

- Verbindet sich ein Handy anstandslos mit dem offenen AP? Manche Geräte
  melden offene Netze ohne Internet als "kein Internet, trotzdem verbinden".
- Läuft der Upload einer echten `.msg`-Datei (Brandenburg, 2,7 MiB) über
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

**Befund (2026-08-14):** rechts aus einer Tempo-30-Zone auf eine
unbeteiligte 50er-Straße abgebogen (Kreuzung 52.460744, 13.521135), Anzeige
blieb auf 30 hängen statt zügig auf 50 zu wechseln.

**Ursache gefunden und behoben, siehe `history.md` 2026-08-14:** ein echter
Fehler in `tools/osm_to_grid.py` — `chain()` verschweißte an Kreuzungen
innerhalb von Tempo-30-Zonen unabhängige Straßen zu einer einzigen, oft
zickzackenden Kette, weil alle Straßen einer Zone dieselbe Kennzeichnung
tragen und `chain()` nur auf Ende-trifft-Anfang plus gleiche Kennzeichnung
prüfte, nicht auf die Richtung. Fix: `chain()` verkettet nur noch, wenn die
Richtungsänderung an der Naht ≤60° bleibt. Um diese Kreuzung sank die Zahl
der Ketten mit >60°-Sprung von 26 auf 3. `brandenburg.msg` neu erzeugt und
im Repo ersetzt (157.251 → 164.693 Ketten, 2,61 → 2,68 MiB). Alle
Demo-Wegpunkte weiterhin ohne Abweichung. Als Sicherheitsnetz zusätzlich:
die Hysterese im Lookup ist jetzt auf `MATCH_HYSTERESIS_MAX_MS` (4 s)
begrenzt.

**Auf dem Gerät geflasht** (2026-08-14), Boot-Log bestätigt Größe und
Regionsliste. **Noch offen:**

- Die eigentliche Testfahrt an der Kreuzung mit echtem GPS-Fix draußen —
  kommt die Verkettungs-Korrektur tatsächlich an?
- Die drei Korrekturen der *ersten* Testfahrt (Begründung-Unterdrückung bei
  inaktiver Zeitbedingung, keine Blende bei Überschreitung,
  `SWITCH_AHEAD_MS` 600→300) liefen bei der zweiten Fahrt mit, ohne
  gemeldete Probleme — aber nicht einzeln bestätigt.
- Kommen die Limits jetzt zum richtigen Zeitpunkt? Wenn weiterhin zu früh,
  ist `MATCH_MAX_DIST_M` (30 m) der nächste Hebel — bei dem Radius gewinnt
  eine einmündende Straße, bevor man sie erreicht.
- Verhalten an Kreuzungen mit der Vorausschau.

## 3. SD-Karte für den Vollausbau

Deutschland ist als MSG2 rund 42 MiB — passt nie ins Flash. Der Reader nimmt
schon ein beliebiges `fs::FS`, es fehlt nur ein SPI-SD-Modul und das Mounten.
Achtung: die alten SD_MMC-Pins (14/17/16) kollidieren mit Display und GPS.
