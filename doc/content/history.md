# Verlauf

**Nur anhängen, nie löschen oder umschreiben.** Neueste Einträge unten. Wer
etwas korrigiert, schreibt einen neuen Eintrag mit dem Grund — der alte bleibt
stehen, weil auch Irrwege Information sind.

Format: `## JJJJ-MM-TT — Überschrift`, darunter was geschah und *warum*.

---

## 2026-08-12 — Projektstand übernommen

Firmware für eine Tempolimit-Anzeige: ESP32-S3-DevKitC-1 (N16R8), rundes
1,53"-Display mit ST77916 über QSPI, NEO-6M-GPS. `ui.c` plattformneutral,
gemeinsam mit einem SDL-Simulator.

Kartenlookup (`speedlimit_grid.h`) existierte, war aber **in keiner
Übersetzungseinheit eingebunden** — nie kompiliert. `main.cpp` setzte
`snapshot.limit = -1` fest.

## 2026-08-12 — Karte aus LittleFS statt SD

Kein SD-Slot am Board. `SpeedLimitGrid::begin()` nimmt jetzt ein `fs::FS`
statt fest `SD_MMC`, das Mounten macht der Aufrufer. Damit läuft derselbe
Lookup über LittleFS und später über SD.

Partitionstabelle von `default_16MB.csv` auf `large_spiffs_16MB.csv`: die
Datenpartition hatte 3,375 MiB, Berlin brauchte 4,20 MiB.

Gemessen: Index 2,24 MiB in 741 ms geladen.

## 2026-08-12 — Cache-Fehler im Lookup

`loadCell()` hielt genau einen Block. Da ein Lookup die 3×3-Nachbarschaft der
Reihe nach durchgeht, war der Puffer am Ende immer mit der letzten Nachbarzelle
belegt — der nächste Lookup begann wieder oben links. **Der Cache traf nie.**

Ersetzt durch einen LRU-Cache; Puffer wachsen per `ps_realloc` in 4-KB-Schritten
statt fest 64 KB.

## 2026-08-12 — Demo-Fahrt ohne GPS

Am Schreibtisch bekommt der NEO-6M keinen Fix, damit war der Kartenpfad nicht
prüfbar. Die Demo fährt deshalb **echte Berliner Straßen** aus den Griddaten ab
statt erfundener Koordinaten, mit erwartetem Limit je Etappe im Serial-Log.

## 2026-08-12 — Display: schwarz trotz Beleuchtung

Lange Fehlersuche. Ausgeschlossen: Init-Sequenz (150 statt der Bibliotheks-
Vorgabe 180 war nötig, reichte aber nicht), QSPI-Takt 40/20/1 MHz, VCC 3,3
gegen 5 V, Kurzschlüsse.

**Ursache: CS (GPIO10) hatte keinen Kontakt.** Weil `Arduino_ESP32QSPI` mit
`SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR` sendet, gehen Kommando
*und* Adresse über alle vier Datenleitungen — eine einzige tote Leitung legt
alles still, auch `DISPON`. `gfx->begin()` meldet trotzdem Erfolg, weil über
QSPI nichts zurückgelesen wird.

Der eingebaute TE-Test hat korrekt gemeldet, dass kein Kommando ankommt (0
Flanken vorher, 60 Hz danach) — die Deutung der 187 Ω an TE war falsch, nicht
der Test.

## 2026-08-12 — GPS: keine Daten

Erst schien die Pinbelegung falsch. Tatsächlich hing das Modul an den mit
„RX/TX" beschrifteten Pins des DevKit — das sind **GPIO43/44, die
USB-UART-Brücke**, für eigene Peripherie gesperrt. Nach dem Umstecken auf
GPIO17/18 sofort Fix.

Die Suchautomatik (Pinbelegung × Baudrate) bestätigte GPIO18 bei 9600 als
richtig. Kriterium ist ein von TinyGPS akzeptierter Satz mit Prüfsumme, nicht
„es kommen Bytes" — bei falscher Baudrate kommen sehr wohl Bytes an.

## 2026-08-13 — GPS auf 5 Hz

`CFG-RATE` auf 200 ms, dazu `CFG-MSG` zum Abschalten von GSV/GSA/GLL/VTG/ZDA.
Das Abschalten ist keine Kosmetik: 9600 Baud sind 960 Byte/s, mit allen Sätzen
wären es bei 5 Hz rund 1900. Mit RMC+GGA sind es gemessen 717.

Einstellung wird bei **jedem** Sync neu gesendet — sie liegt im gepufferten RAM
des Moduls und geht ohne Batterie verloren.

## 2026-08-13 — Format MSG2

Neues Kartenformat, eine Datei je Region. Berlin von 4,20 auf 0,65 MiB.

Hebel, gemessen: eigene Bounding-Box je Region (Index 2,24 MiB → 5,8 KiB),
Teilstücke verketten (104.625 → 44.593 Ketten), Douglas-Peucker
klassenabhängig, 0,5-m-Raster, Zickzack-Varints.

Die Verkettung war der eigentliche Durchbruch: Wege werden an jeder Zellgrenze
zerschnitten, im Schnitt blieben 4,4 Stützpunkte je Segment — daran findet
Douglas-Peucker nichts.

**MSG1 hatte einen Geometriefehler**, den MSG2 nebenbei behebt: Stützpunkte aus
Nachbarzellen wurden auf `0..65535` geklemmt und rutschten auf die Zellecke.
Von 1,83 % abweichenden Ergebnissen gingen 53 % darauf zurück.

## 2026-08-13 — Brandenburg, Mehr-Regionen-Betrieb

Geofabriks Brandenburg enthält Berlin — `berlin.msg` war redundant und wurde
entfernt.

**Cache-Kollaps entdeckt:** mit zwei überlappenden Regionen fasst ein Lookup 18
Zellen an, bei 12 Cache-Plätzen fiel die Trefferquote auf **null** (0 Treffer /
7140 Lesezugriffe). `CELL_CACHE_SLOTS` auf 27. Danach 5248 / 110.

## 2026-08-13 — Begründung des Limits

Zone, Kinder, Spielstraße, Fahrradstraße, Einzelschild, zeitlich — in den
unteren drei Bit des `flags`-Byte, das ohnehin da war. Nicht ganz gratis: die
Begründung gehört zur Identität einer Kette, dadurch verschmelzen weniger
Teilstücke (+0,8 % Dateigröße).

Später ersetzt für Fahrrad- und Spielstraße durch freigestellte Piktogramme der
amtlichen Zeichen 244.1 und 325.1, Format A8.

## 2026-08-13 — Schriftsatz

Umstellung auf DIN 1451 Mittelschrift (Engschrift bleibt über `-DUSE_DIN_ENG`).

Drei Anläufe beim Ziffernabstand, zweimal an der falschen Stelle repariert:

1. Die `1` hatte in der Tabellenziffern-Metrik 56 px Luft → Vorschub gekürzt.
2. Immer noch ungleich → Median-Seitenabstände statt Prozentwert.
3. Immer noch ungleich bei `130` → **die Ursache lag nie bei der Eins.** Die
   Lücke hängt vom linken Seitenabstand der *Folgeziffer* ab, und der schwankt
   von 1 bis 8 px. `even_digit_spacing.py` vereinheitlicht seitdem alle zehn
   Ziffern.

Fonts müssen mit `--no-kerning` erzeugt werden, sonst ziehen Kerning-Paare die
Abstände wieder schief (sichtbar bei „120" und den beiden T in „SCHRITT").

## 2026-08-13 — Flüssigkeit: eine lange Messreihe

Der Balken lief mit 5 fps, die Überblendungen ruckelten. Der Reihe nach geprüft
und **ausgeschlossen**:

| Verdacht | Ergebnis |
|---|---|
| Zeichenpuffer im PSRAM | intern verschoben — keine Änderung |
| Kreismaske der Fläche | rechteckig getestet — keine Änderung |
| `LV_USE_FLOAT` | keine Änderung |
| QSPI-Takt 40 → 80 MHz | 12 % → 9 % Buslast, irrelevant |
| 2 Zeicheneinheiten auf 2 Kernen | **schlechter** (128 statt 100 ms) |
| LVGL-Heap 64 → 128/256 KiB | passt nicht bzw. Board bleibt stehen |
| kleinere Blendfläche (37k → 6k px) | **keine höhere Bildrate** |
| einfarbiges Rechteck statt Ziffer | 668 gegen 682 ns/px — gleich |

Gefunden wurden dabei drei echte Ursachen:

- **Der Balken konnte gar nicht flüssig laufen**: `LV_USE_FLOAT` stand auf 0,
  Bogenwinkel waren ganzzahlige Grad = 2,8 px Sprünge.
- **`ui_update()` und `ui_tick()` waren dasselbe**: der Balken bekam nur im
  Datentakt neue Werte. Getrennt, Glättung zeitbasiert.
- **Alles wurde bei jedem Aufruf neu gezeichnet**, auch unverändert. Merker je
  Element.

Ergebnis: 27–62 Bildaufbauten/s bei 1,3 % Vollbildfläche je Aufbau.

Der Rest ist Physik: ein großflächiger Neuaufbau kostet 85–100 ms, davon nur
10 % Übertragung. Eine mehrstufige Farbüberblendung ist damit nicht machbar.

## 2026-08-13 — Blende über das Hintergrundlicht

Idee des Nutzers, und die einzige, die **nichts kostet**: das Licht fährt
herunter, im Dunkeln wird das Bild getauscht, dann fährt es wieder hoch. Kein
Rendering beteiligt.

Drei Bedingungen, ohne die es nicht funktioniert:

- Restwert **0** — jeder Rest lässt den Neuaufbau durchscheinen.
- Rampe auf eigenem `esp_timer`, nicht in der Hauptschleife (die blockiert bis
  zu 90 ms).
- Im Dunkelpunkt halten, bis `lv_refr_now()` durch ist.

Dazu: TE wird vor **jeder** Teilfläche abgewartet, nicht einmal je Bildzyklus —
sonst blieb ein waagerechter Strich auf der Puffergrenze stehen.

## 2026-08-13 — Erste Testfahrt (Fahrrad)

Drei Befunde, alle behoben, **noch nicht auf Hardware gegengeprüft**:

1. **„50 KINDER" / „50 ZEIT" außerhalb der Geltungszeit.** Die Begründung
   gehört zur Bedingung, nicht zum Grundlimit. Wird jetzt unterdrückt, wenn
   eine Bedingung existiert und gerade nicht greift.
2. **Blende beim Überschreiten.** Eine Warnung muss sofort erscheinen — die
   Überschreitung löst keine Blende mehr aus und wird nicht zurückgehalten.
3. **Limits kamen zu früh.** `SWITCH_AHEAD_MS` von 600 auf 300 halbiert.

## 2026-08-13 — Testfahrt-Korrekturen auf Hardware bestätigt

Demo-Route um eine 13. Etappe erweitert: eine Straße mit `maxspeed=50` und
`maxspeed:conditional=30 @ (06:00-18:00)`, Begründung KINDER — genau der Fall,
der auf der Fahrt „50 KINDER" anzeigte. Davon gibt es in Brandenburg 172.

Die Erwartung hängt an der Uhrzeit: tagsüber 30 mit KINDER, sonst 50 ohne
Beschriftung. Nachts um 22 Uhr gemessen:

    [Demo] 52.43175,13.22990  Limit 50 (erwartet -2)  -  ok

50 ohne Beschriftung — die Unterdrückung greift. 144 Messpunkte über 12
Etappen, 0 Abweichungen.

Eine erste Suche hatte eine Straße mit Grundlimit 30 *und* bedingt 30
gefunden — als Testfall wertlos, weil sich das Limit dort nie ändert. Verworfen
und gezielt nach 50→30 gesucht.

## 2026-08-13 — Dimmen im Stand flackerte

Zwei Ursachen, beide dieselben wie zuvor bei der Blende:

- Die Rampe lief in der **Hauptschleife**, die waehrend eines Neuaufbaus bis zu
  90 ms blockiert. Bei 60 ms Takt und 400 ms Rampe waren das sechs
  unregelmaessige Spruenge.
- Sie interpolierte **linear im PWM-Wert**. Wahrgenommen faellt das oben kaum
  und unten schlagartig.

Beides umgestellt: Grundhelligkeit und Blende liegen jetzt uebereinander,
werden in wahrgenommener Helligkeit gerechnet und laufen gemeinsam auf dem
5-ms-Timer. Der Timer ist dabei bewusst von `FADE_MODE` entkoppelt - in der
ersten Fassung haette das Umstellen der Blendeart das Abdimmen stillgelegt.

## 2026-08-14 — Kartenupdate per Access Point (Backlog Punkt 1a)

Weg (a) aus Backlog Punkt 1 umgesetzt: das Geraet spannt beim Start (und
erneut, wenn `DEMO_PIN` 10 s gehalten wird) einen offenen AP
(`Tempolimit-Setup`) auf und stellt unter `http://192.168.4.1/` eine
Weboberflaeche bereit - Regionen mit Groesse anzeigen, `.msg`-Dateien
hochladen, installierte Regionen loeschen, Speicherbelegung sehen. Neu in
`src/webupdate.h`/`.cpp`, dazu vier Konstanten in `config.h`.

**Aenderungen wirken erst nach einem Neustart.** Grund: waehrend das Geraet
laeuft, kann `SpeedLimitGrid` gerade ein offenes File-Handle auf genau die
Regionsdatei halten, die ersetzt oder geloescht werden soll - ein Schreiben
mitten hinein waere ein Wettlauf mit dem laufenden Lookup. Hochladen und
Loeschen legen deshalb nur Dateien in `/maps_pending` an (neue/ersetzte
Regionen als `.msg`, Loeschungen als leere `.msg.del`-Marker);
`applyPendingMapChanges()` wendet das erst ganz am Anfang des naechsten
`setup()` an, bevor `grid.begin()` auch nur eine Datei geoeffnet hat. Damit
entfaellt jede Nebenlaeufigkeitsfrage, kostet dafuer den einen zusaetzlichen
Klick auf "Jetzt neu starten".

**Sackgasse unterwegs, mit Zahlen:** WiFi + `WebServer` allein kosteten rund
634 KiB Flash. Mit der bisherigen Partitionstabelle (zwei App-Slots a
1,5 MiB fuer OTA, obwohl OTA im Projekt nie genutzt wird) sprang die
Firmware von 54,8 % auf **95,1 %** eines Slots - keine Reserve mehr fuer
irgendetwas. Behoben nicht durch Sparen am Code, sondern am
Partitionslayout: ein einziger `factory`-Slot statt `app0`/`app1` +
`otadata` verdoppelt den nutzbaren Platz auf 3 MiB, ohne die `spiffs`-
Partition anzufassen (Offset und Groesse blieben exakt gleich - wer schon
eine Karte hochgeladen hat, muss nicht neu `uploadfs` laufen lassen). Danach
8,9 % von 16 MB Flash bzw. 47,5 % des neuen App-Slots.

Zweimal sauber gebaut und gelinkt (vor und nach der Partitionsaenderung,
mit den erwarteten Flash-Zahlen). **Nicht auf Hardware geprueft** - kein
Geraet fuer diese Sitzung angeschlossen. Offene Fragen fuer die naechste
Testrunde stehen in `backlog.md`, Punkt 1.

## 2026-08-14 — Dreistellig mit Beschriftung nachgerechnet: vermutlich kein Problem

Backlog Punkt 4 nachgemessen statt weiter geschaetzt - mit `lv_text_get_size()`
gegen die tatsaechlich verbauten Fonts, nicht von Hand am Bildschirm. Dafuer
ein kleines Host-Programm gegen die vorhandene `liblvgl.a` des Simulator-Builds
gelinkt (headless, ohne SDL-Fenster - reine Textvermessung, kein Rendern).

**Die Kommentare in `config.h` waren stale:** "88" sollte 197 px breit sein,
gemessen sind es 208; "888" sollte 233 sein, gemessen 252. Vermutlich ein
Rest aus einer Fassung vor einem spaeteren `even_digit_spacing.py`-Lauf, der
nie in den Kommentar zurueckgeflossen ist. Korrigiert.

**Der eigentliche Befund:** "888" ist ein hypothetischer Schlimmstfall, der
als echtes Tempolimit nie vorkommt. Reale dreistellige Limits (100/110/120/130)
sind in Mittelschrift 168-203 px breit - bis zu 49 px schmaler als der
Schlimmstfall, an dem die Schriftgroesse bemessen wurde. Vergleichbar eng mit
dem laengst unauffaellig laufenden zweistelligen Fall mit Beschriftung (`ZONE`
etc., durch Testfahrt und Demo-Route abgedeckt).

**Sackgasse dabei:** der Versuch, die tatsaechlich verfuegbare Breite an der
verschobenen Position (mit `NUM_Y_SHIFT`) ueber die Kreissehnenformel
`2*sqrt(144² - Versatz²)` nachzurechnen. Auf den bekannt unauffaelligen
zweistelligen Fall angewendet, sagt dieselbe Formel nur noch 1 px Rand voraus
- ein Widerspruch zur Praxis, also ist der Rechenweg selbst nicht mehr
vertrauenswuerdig (die urspruengliche Herleitung der "verfuegbar"-Zahlen in
den alten Kommentaren ist nicht mehr nachvollziehbar). **Nicht weiter an
`NUM_Y_SHIFT` oder der Schriftgroesse drehen, ohne das Ergebnis am Bildschirm
zu sehen** - dieser Rechenweg taugt nicht als Ersatz dafuer.

Keine Aenderung an `ui.c` oder den Layout-Konstanten - nur die Kommentare in
`config.h` korrigiert und Backlog Punkt 4 mit den echten Zahlen neu gefasst.
Auch festgestellt: die Mittelschrift-Ausgangs-TTF liegt nicht im Repo (nur
Engschrift) - ein neuer, kleinerer Mittelschrift-Schnitt liesse sich in dieser
Form ohnehin nicht erzeugen.

## 2026-08-14 — Korrektur: Mittelschrift-Quelle ist doch im Repo

Der vorige Eintrag ("Dreistellig mit Beschriftung nachgerechnet") und der
Backlog-Punkt 4 behaupteten, die Mittelschrift-Ausgangsschrift fehle im Repo
und ein kleinerer Schnitt sei deshalb nicht erzeugbar. **Das war falsch** -
die Suche danach lief nur nach `*.ttf`, gefunden wurde nur die
Engschrift-TTF. Die Mittelschrift liegt als `src/DIN 1451 Std
Mittelschrift.otf` im selben Ordner, `lv_font_conv` liest OTF genauso wie
TTF. Ein kleinerer Mittelschrift-Schnitt für den 3-stellig-mit-Beschriftung-
Fall wäre also technisch möglich, falls er doch mal gebraucht wird.

Der eigentliche Befund von vorhin (reale Limits 100-130 passen mit
ordentlich Rand, verglichen mit dem `888`-Schlimmstfall) bleibt davon
unberührt - nur die Prämisse "geht technisch nicht" war falsch, nicht die
Einschätzung "vermutlich kein Problem".

## 2026-08-14 — Zweite Testfahrt: Limit blieb 400 m zu lange stehen

Erste echte Rückmeldung aus `backlog.md` Punkt 2. Auf der Straße: von einer
Tempo-30-Zone rechts auf eine unbeteiligte 50er-Straße abgebogen (Kreuzung
52.460744, 13.521135) - die Anzeige blieb rund 400 m auf 30, statt sofort
auf 50 zu wechseln. Ein Neustart des ESP32 an derselben Stelle korrigierte es
augenblicklich.

**Mit `data/maps/brandenburg.msg` gegengerechnet** statt geraten (kleiner
Python-Leser fürs MSG2-Format, siehe `handoff.md`): die Kreuzung liegt in
einem dichten Tempo-30-Zonen-Gebiet, und direkt daneben verläuft eine lange
Zonen-Kette (mehrere hundert Meter) recht nah an der eigentlich befahrenen
50er-Straße entlang - nah genug, um durchgehend im 30-m-Suchradius zu bleiben
und score-mäßig konkurrenzfähig zu `MATCH_HYSTERESIS` (1,6) zu sein.

**Warum der Neustart sofort half, war der entscheidende Hinweis:** er setzt
`last_speed_` auf -1 zurück, und der erste Lookup danach entscheidet ganz
ohne Hysterese rein nach Abstand/Kurs. Das schließt einen falschen Treffer
als Ursache aus - die Karte und der Kurs-Filter lagen die ganze Zeit richtig,
nur die Hysterese hielt zu lange am alten Wert fest. Genau das stand als
offene Frage schon in `backlog.md` Punkt 2 ("MATCH_HYSTERESIS auf parallelen
Straßen").

**Fix:** Hysterese jetzt zusätzlich zeitlich begrenzt auf
`MATCH_HYSTERESIS_MAX_MS` (4 s, `speedlimit_grid.h`/`config.h`). Das reicht
für eine kurze Ambiguität an einer Kreuzung, aber nicht mehr für hunderte
Meter auf einer parallel verlaufenden Straße. Dafür bekommt `lookup()` jetzt
`now_ms` als Parameter (`millis()` vom Aufrufer) - einziger Aufrufer ist
`main.cpp`, entsprechend angepasst.

Sauber gebaut (Firmware, +32 Byte Flash). **Nicht auf Hardware geprüft** -
die 4 s sind eine erste, plausible Schätzung, keine Messung. Nächste
Testfahrt sollte gezielt an derselben Kreuzung gegenprüfen, ob 4 s reichen,
ohne an echten Kreuzungen neues Flackern einzuführen.

## 2026-08-14 — Korrektur: eigentliche Ursache war chain(), nicht die Hysterese

Der Nutzer widersprach zu Recht dem vorigen Eintrag: "das darf nicht
passieren, ein Reset hat es behoben - also muss bei uns was falsch laufen."
Und: "wir nutzen Kurs, an dieser Stelle sind die 30er immer Kreuzungen, keine
parallelen Straßen." Beides zusammen war der richtige Hinweis, dass die
4-Sekunden-Hysterese-Grenze vom letzten Eintrag nur ein Symptom behandelt
hätte, nicht die Ursache.

**Nachgeschaut statt geglaubt:** ein Skript entlang der tatsächlich
befahrenen 50er-Straße gebaut (mehrere aneinandergehängte `.msg`-Ketten
verfolgt, ~1 km) und geprüft, wie oft eine Tempo-30-Kette in Konkurrenz-
Reichweite kommt - das zeigte ein durchgehend verzahntes Bild, aber noch
keine Ursache.

**Der eigentliche Fehler steckt in `chain()` in `tools/osm_to_grid.py`:**
die Funktion hängt zwei Teilstücke aneinander, wenn Ende und Anfang
zusammentreffen UND Tempo/Bedingung/Begründung übereinstimmen - ohne zu
prüfen, ob die Richtung an der Naht überhaupt zusammenpasst. In einer
Tempo-30-Zone tragen praktisch **alle** Straßen dieselbe Kennzeichnung
(`30, Zone`), und an jeder Kreuzung innerhalb der Zone treffen zwei
komplett unabhängige Straßen an einem gemeinsamen OSM-Knoten aufeinander.
`chain()` verschweißte sie trotzdem zu einer einzigen, oft zickzackenden
Kette quer durch mehrere echte Straßen.

Nachgemessen an den Ketten um die Kreuzung aus dem letzten Eintrag: von 85
Zonen-Ketten mit ≥4 Punkten hatten **26 einen Richtungssprung über 60 Grad**
zwischen zwei benachbarten Segmenten - einige über 90, eine sogar 150 Grad.
Kein echter Straßenverlauf macht das, das sind zusammengeschweißte
Kreuzungen. Genau das erklärt, warum der Kurs-Filter (`COURSE_TOLERANCE_DEG`)
nicht rettet: er prüft nur, ob IRGENDEIN Segment der (zu langen) Kette zur
Fahrtrichtung passt - bei einer durch mehrere echte Straßen gezickzackten
Kette trifft das öfter zufällig zu, als man denkt, unabhängig davon, welche
der verschweißten Straßen tatsächlich in Reichweite liegt.

**Fix an der Quelle:** `chain()` hängt zwei Teilstücke nur noch zusammen,
wenn die Richtungsänderung an der Naht höchstens `CHAIN_MAX_TURN_DEG`
(60°) beträgt; bei mehreren Kandidaten gewinnt der mit der kleinsten
Abweichung. `brandenburg.msg` neu erzeugt: 157.251 → 164.693 Ketten
(+4,7 %), 2,61 → 2,68 MiB - ein kleiner, erwarteter Preis für weniger
Verschweißung. Um dieselbe Kreuzung: die Ketten mit Richtungssprung >60°
gingen von 26 auf 3 zurück (die verbliebenen 3 sind vermutlich echte
Kurven innerhalb einer einzelnen OSM-Way, keine Kreuzungs-Verschweißung
mehr). Alle zehn Demo-Wegpunkte (`DEMO[]` in `main.cpp`) treffen mit der
neu erzeugten Karte weiterhin ihr erwartetes Limit - keine Regression.

Die 4-Sekunden-Hysterese-Grenze aus dem vorigen Eintrag bleibt drin, ist
aber vom Haupt- zum Sicherheitsnetz herabgestuft: sie hilft, falls
irgendwo anders im Land noch eine vergleichbare Verschweißung übersehen
wurde oder eine wirklich parallele Straße lange in Reichweite bleibt -
verhindert aber nicht mehr das eigentliche, jetzt behobene Problem.

**Nicht auf Hardware geprüft** - die neue `brandenburg.msg` muss noch per
`uploadfs` auf ein Gerät und an derselben Kreuzung nachgefahren werden.

## 2026-08-14 — Firmware und neue Karte geflasht, Boot bestätigt

Geraet war ueber die USB-UART-Bruecke erreichbar (`/dev/cu.usbmodem...`).
`pio run -t upload` und `-t uploadfs` liefen durch, beide mit Hash-Verifikation
bestaetigt. Serial-Log (per pyserial mitgelesen, `pio device monitor`
scheitert ohne echtes TTY):

    [FS] LittleFS: 2830336 von 13500416 Byte belegt
    [Grid] brandenburg  222x178 Zellen, 13702 belegt, Index 107 KiB in 37 ms
    [Grid] 1 Region(en): brandenburg
    [Update] AP "Tempolimit-Setup" gestartet, http://192.168.4.1/
    [GPS] gueltige NMEA-Saetze auf RX=GPIO18, 9600 Baud

Damit bestaetigt: die neue, groessere `brandenburg.msg` ist tatsaechlich auf
dem Geraet (Byte-Zahl passt zur neuen Dateigroesse), die neue
Partitionstabelle mountet die FS-Partition weiterhin korrekt, und der
Access Point aus Backlog 1a startet auf echter Hardware. Kein Fix beim Test
(drinnen, keine Antennensicht) - GPS-Automatik fand aber sofort die richtige
Verbindung.

Noch offen: eine echte Testfahrt an der Kreuzung (52.460744, 13.521135) mit
Fix draussen, und eine echte Browser-Verbindung zum AP mit Upload.

## 2026-08-14 — Aufgeraeumt: veraltete Doku, kaputter CI-Job, alte Kartendaten

Reine Aufraeumarbeit, kein Verhaltensaenderung:

- `README.md` und `TODO.md` beschrieben noch den Stand von vor der
  LittleFS-Umstellung (Projektordner-von-Hand-anlegen, `pio run -e sim`).
  `README.md` seitdem komplett neu geschrieben (siehe Eintrag weiter unten),
  `TODO.md` auf einen Verweis zu `backlog.md` eingedampft.
- Der Job `simulator` in `.github/workflows/build.yml` rief `pio run -e sim`
  auf - das Env gibt es nicht, der Job schlug immer fehl. Umgestellt auf den
  echten Weg ueber CMake, lokal nachgebaut und gruen.
- `tools/out-berlin/` und `tools/out-germany/` waren Daten im alten
  MSG1-Format, von nichts mehr gelesen. 112 MiB geloescht.

## 2026-08-14 — Entschieden: Datenformat nicht weiter verkleinern

Zwei Wege prophylaktisch durchgerechnet, beide **nicht umgesetzt**:

- **Bitmap-Index statt sparse Liste.** Bei dicht belegten Rastern (ganz
  Deutschland, 44,7 % belegt) 0,71 statt 1,33 MiB. Bei Regionsdateien
  bedeutungslos, weil der Index dort ohnehin klein ist (Brandenburg: 107 KiB
  von 2,68 MiB Gesamtgroesse) - der Hebel greift nur, wenn man doch wieder
  einen Index ueber ganz Deutschland vorhaelt, und genau das hat MSG2
  bewusst abgeschafft (eigene Bounding-Box je Region).
- **Innerorts-Regelfall (Tempo 50) folgern statt speichern.** 45,3 % aller
  Stuetzpunkte gehoeren zu Tempo-50-Strassen - theoretisch Sparpotential,
  wenn man sie implizit annimmt statt zu speichern. Braeuchte dafuer aber
  eine eigene Ortslagen-Ebene und tauscht Genauigkeit ausgerechnet dort ein,
  wo sie am wichtigsten ist (Ortsdurchfahrten, wo Limits am haeufigsten
  wechseln). **Nicht empfohlen** - das Risiko falscher Grundannahmen wiegt
  schwerer als der Platzgewinn.

Format bleibt MSG2 wie es ist. Nicht erneut pruefen, ohne einen neuen Grund.

## 2026-08-20 — Zusatzhinweise (Naesse/Wildwechsel/Kurve/Gefahr/Eng), unabhaengig von REASON_*

Frage aus der Sitzung: OSM kennt neben Zone/Kinder/Spiel/Rad/Zeit noch mehr
Warnhinweise (Naesse, Kurve, Wildwechsel, Verengung, allgemeine Gefahrstelle)
- gibt es die, und lohnt sich das? Gegen die Brandenburg-PBF gescannt statt
geraten: `hazard=*` und `traffic_sign(:forward/:backward)=*` sitzen ueberwiegend
direkt am Way (1819/1923 bzw. 61962/77236 Vorkommen), nicht an einem
Punktknoten - waeren also genauso wie REASON_* an eine Kette anheftbar.
`maxspeed:conditional` traegt vereinzelt "wet" ohne Uhrzeit (145 Vorkommen in
Brandenburg), das `parse_conditional()` bisher still verschluckte, weil es nur
auf eine Uhrzeit-Bedingung geprueft hat.

**Bewusster Unterschied zu REASON_\*:** ein Zusatzhinweis ist keine
Begruendung fuer die Zahl, sondern gilt unabhaengig davon - eine Kurve bleibt
eine Kurve, ob das Limit 30 oder 100 ist. Deshalb eigenes Feld (Bit 3-5 im
selben flags-Byte, `EXTRA_*` in `osm_to_grid.py` / `UI_EXTRA_*` in `ui.h`),
nicht in die 3 verbliebenen REASON-Werte gequetscht. Drei Entscheidungen dazu,
alle vom Nutzer bestaetigt:

- **Zusatz hat Vorrang vor der Begruendung**, wenn beide auf derselben Kette
  stehen (nur eine Beschriftungszeile vorhanden) - Sicherheitsrelevantes zuerst,
  kein Risiko mit ungetesteten Breiten fuer eine gemeinsame Zeile.
- **Wege ohne bekanntes Limit, aber mit Zusatzhinweis, bleiben jetzt im Grid**
  (vorher verwarf `way()` jeden Way mit `speed==0 and cond is None` komplett).
  Betraf in Brandenburg 18 von 175 relevanten Hazard-Ways (~10 %), ueberwiegend
  `secondary` ohne eigenes `maxspeed`-Tag. Die Anzeige zeigt dann "?" mit
  Beschriftung darunter - dafuer musste `scanCell()` in `speedlimit_grid.h`
  auch Ketten mit `speed==0` als Kandidaten zulassen (vorher generell
  uebersprungen) und am Ende von `lookup()` `speed==0` wieder auf den
  bestehenden Vertrag "-1 = kein Limit" zurueckmappen, waehrend `last_extra_`
  unabhaengig davon stehen bleibt.
- **Rangfolge bei seltener Ueberschneidung auf derselben Kette:**
  Naesse > Wildwechsel > Kurve > Gefahrstelle > Verengung - Naesse zuerst,
  weil sie als einzige Kategorie tatsaechlich an einem Limit haengt
  (`maxspeed:conditional`), der Rest nach Unfallschwere.

**Verteilung in Brandenburg** (164.801 Ketten): 187 Wildwechsel, 112 Naesse,
22 Kurve, 15 Gefahrstelle, 0 Verengung. Fuer `EXTRA_ENG` also bewusst keine
Demo-Etappe - eine Karte ohne einzige Verengung-Kette kann keinen erwarteten
Wert bestaetigen.

Vier neue Demo-Etappen (`main.cpp`) mit echten, aus der neu erzeugten
`brandenburg.msg` dekodierten Koordinaten, eine davon (Gefahrstelle in einer
Tempo-30-Zone, 52.5366/13.4416) mit nativ vorkommender REASON+EXTRA-
Ueberschneidung - testet die Vorrang-Regel also mit echten Daten, nicht
konstruiert. Fuenf neue ROUTE-Etappen im Simulator (`sim/sim_main.c`),
darunter der "?"+Kurve-Fall.

`brandenburg.msg` neu erzeugt: 164.693 → 164.801 Ketten (das neue Feld ist
Teil der Ketten-Identitaet, genau wie REASON_* schon), Dateigroesse
unveraendert bei 2,68 MiB. Firmware und Karte geflasht, Boot-Log bestaetigt:
`[Grid] brandenburg 222x178 Zellen, 13703 belegt, Index 107 KiB in 39 ms`.

### Nebenbei gefundener und behobener Altbug: Kettenzahl im Zellkopf zu hoch

Beim Bau eines Python-Decoders zum Heraussuchen echter Demo-Koordinaten stieg
ein `IndexError` auf, weil ein Zellblock weniger Ketten enthielt, als sein
Kopf (`varint(buf, len(chains))`) versprach. Ursache: dieser Wert wurde aus
`len(chains)` (Ausgabe von `chain()`, **vor** dem Filtern) geschrieben, aber
`write_region()` uebersprang dahinter einzelne Ketten per `continue`, wenn sie
nach Quantisierung/Entdoppeln auf unter 2 Punkte zusammenfielen. Bestand seit
MSG2 (nicht durch die Zusatzhinweis-Aenderung verursacht) und betraf jede
`.msg`-Datei, nicht nur Brandenburg.

**Nicht katastrophal, weil `scanCell()` in `speedlimit_grid.h` das ohnehin per
Bounds-Check abfaengt** (`p < end` in der Schleifenbedingung, `if (p + 2 > end)
return`) - eine betroffene Zelle brach die Auswertung einfach vorzeitig ab und
verlor die restlichen, in Wahrheit gar nicht vorhandenen "Ketten" stillschweigend.
Auf dem Geraet also nie als Absturz sichtbar, hoechstens als ein paar zu wenig
ausgewertete Kandidaten in einzelnen Zellen.

**Fix:** Kettenbytes werden jetzt in einen eigenen `chain_buf` geschrieben,
die tatsaechlich geschriebene Anzahl mitgezaehlt (`n_written`) und der Header
erst danach aus diesem echten Wert gebaut. Gegengeprueft mit einem strikten
Decoder (kein Try/Except, keine Toleranz) ueber alle 13.703 Zellen der neu
erzeugten `brandenburg.msg`: **0 Abweichungen zwischen Kopf und Inhalt**,
vorher brach der Decoder bei Zelle 836 (cell_id 4734) ab. Ketten- und
Punktzahl in der Statistikausgabe unveraendert (164.801 Ketten, 737.979
Stuetzpunkte) - der Bug betraf nur den Kopf-Zaehler, nie die Nutzdaten selbst.
