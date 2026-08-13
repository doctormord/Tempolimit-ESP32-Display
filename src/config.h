/*
 * config.h - alle Stellschrauben an einer Stelle.
 *
 * Plattformneutral: wird von Firmware (main.cpp) und Simulator (ui.c) gleicher-
 * massen eingebunden und enthaelt deshalb nur #defines, keine Includes.
 *
 * Regel fuers Projekt: was man ohne Codeverstaendnis verstellen koennen soll,
 * gehoert hierher und nicht mitten in eine Funktion.
 */

#pragma once

/* ==========================================================================
 * SCHRIFTSCHNITT
 * ==========================================================================
 * Mittelschrift fuellt die runde Flaeche besser aus, Engschrift laesst mehr
 * Luft. Beide Saetze liegen im Repo, umschalten per Build-Flag:
 *     PLATFORMIO_BUILD_FLAGS=-DUSE_DIN_ENG pio run -e esp32s3
 * Die Schriftgrade sind je Schnitt eigens ausgemessen - Mittelschrift ist
 * rund 30 % breiter, dieselben Grade wuerden den Kreis sprengen.
 */
#if !defined(USE_DIN_ENG) && !defined(USE_DIN_MITTEL)
#define USE_DIN_MITTEL
#endif

/*
 * Die Beschriftung sitzt jeweils mittig zwischen Ziffer bzw. Piktogramm und
 * dem Statusband. Nachgerechnet: beim Ziffernfall 19 px oben zu 20 px unten,
 * beim Piktogramm 25 zu 25.
 *
 * Wichtig beim Nachrechnen: die Ausrichtung addiert die optische Korrektur,
 * damit hebt sie sich wieder auf - die Ink-Mitte liegt schlicht bei
 * 180 + LABEL_Y bzw. 180 + NUM_Y_SHIFT. Wer die Korrektur beim Pruefen noch
 * einmal draufrechnet, bekommt ein um 13 px falsches Ergebnis.
 * Weil das Piktogramm (128 px) flacher ist als die Ziffer
 * (148 px), sind das zwei verschiedene Hoehen - LABEL_Y und LABEL_Y_PICTO.
 *
 * PICTO_Y ist so hoch wie moeglich: bei 208 px Breite passt die Oberkante
 * gerade noch bei y = 90 in den Kreis, hoeher wuerde das Bild seitlich
 * anecken.
 *
 * Optische Mitte: LVGL zentriert den Textkasten, nicht die Ziffern. Der
 * Kasten ist so hoch wie line_height und enthaelt Platz fuer Unterlaengen,
 * die Ziffern haben aber keine - sie sitzen dadurch sichtbar zu hoch. Die
 * Korrektur ist  base_line + Ziffernhoehe/2 - line_height/2  und je Font
 * ausgemessen, nicht geschaetzt.
 */
#if defined(USE_DIN_MITTEL)
/*
 * Textbreiten mit lv_text_get_size() nachgemessen (nicht geschaetzt) - die
 * Kommentare hier wichen vorher ab (197/233 statt tatsaechlich 208/252 px),
 * vermutlich aus einer frueheren Fassung vor einem erneuten
 * even_digit_spacing.py-Lauf. Die "verfuegbar"-Werte stammen dagegen aus
 * einer nicht mehr nachvollziehbaren Rechnung von vorher und sind NICHT neu
 * verifiziert - siehe backlog.md Punkt 4 fuer die Einordnung.
 *
 * "888" ist ein hypothetischer Schlimmstfall zur Schriftgroessenwahl, keine
 * reale Beschraenkung - echte dreistellige Limits (100/110/120/130) liegen
 * bei 168-203 px, deutlich darunter. Quelle liegt als
 * "DIN 1451 Std Mittelschrift.otf" im Repo, ein kleinerer Schnitt
 * liesse sich also erzeugen, falls es doch noetig wird.
 */
#define FONT_SIZE_2DIGIT 205   /* "88"   208 px breit von 273 verfuegbar */
#define FONT_SIZE_3DIGIT 162   /* "888"  252 px breit von 281 verfuegbar,
                                  reale Limits (100-130) 168-203 px */
#define FONT_SIZE_LABEL 48     /* "SCHRITT" 175 px breit von 213         */
#define FONT_SIZE_PICNUM 72    /* Tempo unter dem Piktogramm             */
#define NUM_OPT_2DIGIT 13      /* optische Korrektur, siehe oben          */
#define NUM_OPT_3DIGIT 10
#define LABEL_OPT (-4)
#define NUM_Y_SHIFT (-25)      /* Ziffer hoch, wenn Beschriftung da ist   */
#define PICTO_Y (-26)          /* Piktogramm - so hoch wie die Breite zulaesst */
#define LABEL_Y 85             /* Beschriftung unter der Ziffer           */
#define LABEL_Y_PICTO 80       /* Beschriftung unter dem Piktogramm       */
#define PICNUM_OPT 0           /* optische Korrektur des 72-px-Fonts      */
#else
#define FONT_SIZE_2DIGIT 197
#define FONT_SIZE_3DIGIT 171
#define FONT_SIZE_LABEL 56
#define NUM_OPT_2DIGIT 12
#define NUM_OPT_3DIGIT 11
#define LABEL_OPT (-4)
#define FONT_SIZE_PICNUM 88
#define NUM_Y_SHIFT (-25)
#define PICTO_Y (-26)
#define LABEL_Y 85
#define LABEL_Y_PICTO 80
#define PICNUM_OPT 0
#endif

/* ==========================================================================
 * ANZEIGE
 * ========================================================================== */

/* Ab wieviel Prozent ueber dem Limit gilt "zu schnell".
   10 % heisst: bei Tempo 30 ab 33 km/h, bei Tempo 100 ab 110 km/h. */
#define OVER_TOLERANCE_PCT 10

/* Rueckfallabstand, damit ein Tempo genau an der Schwelle nicht dauernd
   zwischen weiss und rot pendelt - und damit keine Blende nach der anderen
   ausloest. */
#define OVER_HYSTERESIS_KMH 3.0f

/* 1 = Flaeche wird rot, Ziffern weiss (Standard, wirkt aus dem Augenwinkel
   staerker). 0 = nur die Ziffern werden rot. */
#define OVER_STYLE_INVERT 1

/* Zeitkonstante der Balkenglaettung in Millisekunden, zeitbasiert gerechnet
   und damit unabhaengig von der Aufrufrate von ui_tick(). */
#define ARC_TAU_MS 250.0f

/*
 * Ueberblendung beim Wechsel der Zahl. 0 = harter Wechsel (Standard).
 *
 * Messreihe dahinter, damit niemand dieselben Sackgassen nochmal laeuft:
 *
 *   Zifferndeckkraft blenden        682 ns/Pixel
 *   einfarbiges Rechteck blenden    668 ns/Pixel   (praktisch gleich)
 *
 * Es kostet also nicht die kantengeglaettete Ziffer, sondern schlicht jede
 * angefasste Flaeche. Und kleiner hilft nicht:
 *
 *   Blendflaeche 37.400 px  ->  38 Bilder/s
 *   Blendflaeche 16.000 px  ->  27 Bilder/s
 *   Blendflaeche  6.000 px  ->  31 Bilder/s
 *
 * Die Bildrate haengt bei laufender Animation nicht an der Flaeche, sondern
 * an festem Aufwand je Bildzyklus - davon sind allein 19-27 % Warten auf TE.
 * Bei 30-40 Bildern/s ergeben 200 ms sechs bis acht Stufen, und die sieht
 * man einzeln.
 *
 * Wer blenden will: 400 ms geben rund zwoelf Stufen und wirken deutlich
 * weicher, dafuer traeger. Die Ziffer in einzelne Labels je Stelle zu
 * zerlegen bringt nach obiger Messreihe nichts.
 */
#define FADE_MS 1000

/*
 * Art der Ueberblendung:
 *   0 = hart, kein Uebergang
 *   1 = Pixelblende (Deckkraft der Ziffer, FADE_MS) - teuer, siehe Messreihe
 *       Nur bei FADE_MODE 1 wird FADE_MS ueberhaupt ausgewertet.
 *   2 = Hintergrundlicht (Standard): runterdimmen, Bild wechseln, hochdimmen
 *
 * Variante 2 kostet ueberhaupt kein Zeichnen - die Blende passiert im
 * PWM-Kanal des Hintergrundlichts, nicht im Bild. Sie ist dadurch beliebig
 * fein und stufenlos, faerbt aber die ganze Anzeige, nicht nur die Ziffer.
 *
 * Der Verlauf ist gammakorrigiert (FADE_BL_GAMMA), sonst wirkt eine linear
 * gefahrene PWM oben flach und unten wie ein Sprung - das Auge sieht
 * Helligkeit ungefaehr als Wurzelfunktion der Leistung.
 *
 * Die Rampe laeuft auf einem eigenen Timer (FADE_BL_STEP_MS), nicht in der
 * Hauptschleife: die blockiert waehrend eines Neuaufbaus bis zu 90 ms, und
 * eine Rampe mit 90-ms-Luecken ist keine Rampe. Im Dunkelpunkt wird gehalten,
 * bis der Bildwechsel wirklich durch ist - sonst faehrt das Licht schon
 * wieder hoch, waehrend noch gezeichnet wird.
 */
#define FADE_MODE 2
#define FADE_BL_MS 500          /* gesamte Dauer, halb runter halb hoch */
#define FADE_BL_FLOOR 0         /* ganz dunkel - alles darueber laesst
                                   den Neuaufbau durchscheinen */
#define FADE_BL_GAMMA 2.2f
#define FADE_BL_STEP_MS 5       /* Taktung der Rampe */

/*
 * Ueberblendung der Farbe: 0 = harter Wechsel. Das ist eine Messentscheidung,
 * keine Bequemlichkeit.
 *
 * Ein Farbschritt faerbt die ganze Scheibe neu. Gemessen auf dem Geraet
 * kostet ein grossflaechiger Neuaufbau in LVGL 85-100 ms - davon nur 10 %
 * Uebertragung, der Rest ist Zeichnen. Geprueft und ausgeschlossen als
 * Ursache: Zeichenpuffer im PSRAM statt intern, Kreismaske statt Rechteck,
 * LV_USE_FLOAT, QSPI-Takt 40 gegen 80 MHz, TE-Wartezeit. Der Bogen macht
 * nur ein Fuenftel aus.
 *
 * Bei 90 ms je Schritt passen in 400 ms vier Stufen - und vier Stufen sieht
 * man einzeln. Ein harter Wechsel ist ein einziger Ruckler von 90 ms und
 * wirkt dadurch sofort statt stockend.
 *
 * Wer es doch verlaufen lassen will: Wert hochsetzen und die Stufigkeit in
 * Kauf nehmen.
 */
#define FADE_COLOR_MS 0

/*
 * Zusaetzlicher Zeichenabstand in der Beschriftung. DIN 1451 hat bei den
 * Grossbuchstaben praktisch keinen Seitenabstand - in "SCHRITT" beruehren
 * sich die beiden T oben. Das ist keine Kerning-Frage, sondern die
 * Schriftmetrik selbst.
 */
#define LABEL_LETTER_SPACE 3

/* Die weisse Flaeche wird um so viele Pixel groesser gezeichnet, als der Ring
   innen frei laesst. Ohne diese Ueberlappung bleibt zwischen Ring und Flaeche
   eine dunkle Kante von rund einem Pixel stehen - das ist die Kantenglaettung
   beider Kreise, die sich auf dem Hintergrund mischt. */
#define DISC_OVERLAP 3

/* Statusband: dunkler und deckender als frueher, Schrift weiss. Auf der
   hellen Schildflaeche war die alte halbtransparente Variante kaum lesbar. */
#define BAND_COLOR 0x05080C
#define BAND_OPA 217            /* von 255, also rund 85 % */
#define BAND_TEXT 0xF2F4F7

/* ==========================================================================
 * DISPLAY
 * ==========================================================================
 * Taktfrequenz des QSPI-Busses. Bei Bildfehlern stufenweise senken:
 * 40 -> 20 -> 1 MHz. Hoeher als 40 MHz ist einen Versuch wert, bringt aber
 * kaum etwas: bei 40 MHz liegt die Buslast schon bei 1,5 %, ein
 * Balkenausschnitt dauert 0,17 ms von 16,7 ms Bildperiode.
 *
 * Achtung, 80 MHz koennen scheitern: Arduino_ESP32QSPI erzwingt mit
 * SPICOMMON_BUSFLAG_GPIO_PINS die Leitung ueber die GPIO-Matrix statt ueber
 * IO_MUX. Ueber die Matrix sind rund 40 MHz die Grenze - obwohl die Pins
 * dieses Projekts zufaellig genau die IO_MUX-Pins von SPI2 sind.
 */
#define LCD_QSPI_HZ 80000000

/*
 * Bildsynchronisation ueber den TE-Pin des Panels.
 *
 * Ohne sie liegt der Transfer zufaellig im Bildaufbau des Panels und man
 * sieht die Naht - genau das Tearing. TE meldet den Beginn der Austastluecke;
 * wer dort mit dem Schreiben beginnt, ist fertig, bevor der Bildaufbau die
 * Stelle erreicht.
 *
 * Gewartet wird einmal je LVGL-Bildzyklus, nicht je Teilflaeche - sonst
 * kostete jede Teilflaeche eine ganze Bildperiode. Die Obergrenze von 60 Hz
 * deckt sich mit LV_DEF_REFR_PERIOD (16 ms).
 *
 * TE muss dafuer verdrahtet sein (LCD_TE). Fehlt das Signal, laeuft die
 * Wartezeit nach LCD_TE_TIMEOUT_US aus und es geht ohne weiter.
 */
/*
 * Groesse des Zeichenpuffers als Bruchteil eines Vollbilds.
 *
 * Der Puffer bestimmt, in wieviele Streifen LVGL eine Aenderung zerlegt, und
 * jeder Streifen ist ein eigener Transfer. Zu klein heisst sichtbare Naht auf
 * der Puffergrenze. Ein halbes Bild (64.800 px) fasst die Blendflaeche
 * (37.400 px) und die Statuszeilen jeweils am Stueck; die volle Scheibe
 * (86.400 px) braucht weiterhin zwei.
 *
 * 1/2 ist das Groesste, was als einzelner Puffer noch ins interne RAM passt
 * (129,6 KiB). Wer mehr will, landet im PSRAM - das ist beim Zeichnen nicht
 * langsamer, war aber gemessen auch nicht schneller.
 */
#define LCD_BUF_DIV 2

#define LCD_TE_SYNC 1
#define LCD_TE_TIMEOUT_US 20000

/* ==========================================================================
 * HINTERGRUNDLICHT
 * ==========================================================================
 * Im Stand gedimmt: spart Strom und blendet nachts nicht. Zwei Schwellen,
 * damit es an der Ampel nicht im Sekundentakt hin und her springt.
 */
#define LCD_BL_LEVEL 255        /* Helligkeit im Fahrbetrieb, 0-255 */
#define LCD_BL_DIM_LEVEL 20     /* Helligkeit im Stand               */
#define DIM_BELOW_KMH 2.0f      /* darunter wird gedimmt             */
#define DIM_ABOVE_KMH 5.0f      /* darueber wieder voll              */
#define DIM_FADE_MS 400         /* Ueberblendzeit                    */

/* ==========================================================================
 * BETRIEBSART
 * ==========================================================================
 * Schalter gegen Masse an diesem Pin schaltet auf reinen Tacho um: in der
 * Mitte steht dann das gefahrene Tempo statt des Limits, der Balken bleibt
 * auf das Limit skaliert.
 *
 * GPIO21 ist beim ESP32-S3 frei von Sonderfunktionen - kein Strapping (0, 3,
 * 45, 46), kein USB (19/20), kein Flash/PSRAM (26-37), keine UART-Bruecke
 * (43/44). Offen = Tempolimit, gegen GND = Tacho.
 */
#define MODE_PIN 21

/*
 * Zweiter Schalter gegen Masse: erzwingt die simulierte Fahrt, auch wenn GPS
 * einen Fix hat. Praktisch zum Vorfuehren und zum Pruefen der Anzeige, ohne
 * dafuer eine eigene Firmware bauen zu muessen (-DFORCE_DEMO gibt es
 * weiterhin, ist aber nur noch fuer automatisierte Laeufe gedacht).
 *
 * GPIO5 ist beim ESP32-S3 frei: kein Strapping, kein USB, kein Flash/PSRAM,
 * keine UART-Bruecke. GPIO15 waere ebenfalls frei, wird aber unter
 * -DLCD_DIAG als Pruefpin der Messkette benutzt.
 */
#define DEMO_PIN 5

#define MODE_DEBOUNCE_MS 80

/* ==========================================================================
 * VORAUSSCHAU BEIM MAP-MATCHING
 * ==========================================================================
 * An Kreuzungen sprang die Anzeige auf die Querstrasse, weil deren Segment
 * kurzzeitig naeher lag. Gegenmittel: aus Kurs und Tempo die Position in
 * PREDICT_AHEAD_MS berechnen und Kandidaten danach bewerten, wie nah sie an
 * BEIDEN Punkten liegen. Eine Strasse, die man nur quert, faellt damit
 * zurueck - man ist gleich nicht mehr auf ihr.
 *
 * Die Bewertung ist  d_jetzt + PREDICT_WEIGHT * d_voraus.
 * Fuer die 30-m-Schwelle zaehlt weiterhin der echte Abstand jetzt.
 */
#define PREDICT_AHEAD_MS 700
#define PREDICT_WEIGHT 0.6f

/*
 * Vorausschauend umschalten: der Lookup laeuft nicht an der aktuellen
 * Position, sondern SWITCH_AHEAD_MS weiter vorn. Das neue Schild steht damit
 * schon da, wenn man es erreicht, statt kurz danach.
 *
 * Die Strecke waechst mit dem Tempo (Zeit x Geschwindigkeit): bei 25 km/h
 * sind 300 ms rund 2 m, bei 100 km/h 8 m. SWITCH_AHEAD_MAX_M deckelt es
 * zusaetzlich nach oben.
 *
 * Nach der ersten Testfahrt von 600 auf 300 ms halbiert - die neuen Limits
 * kamen sonst spuerbar zu frueh. Wenn es weiterhin zu frueh kommt, ist der
 * naechste Hebel MATCH_MAX_DIST_M: bei 30 m Suchradius gewinnt eine
 * einmuendende Strasse schon, bevor man sie erreicht.
 */
#define SWITCH_AHEAD_MS 300
#define SWITCH_AHEAD_MAX_M 25.0f

/* ==========================================================================
 * MAP-MATCHING
 * ========================================================================== */
#define MATCH_MAX_DIST_M 30.0f      /* max. Abstand zur Strasse            */
#define MATCH_HYSTERESIS 1.6f       /* Vorzug fuers zuletzt gewaehlte Segment */

/* Unterhalb dieser Geschwindigkeit ist der GPS-Kurs Rauschen. Gilt fuer den
   Richtungsfilter beim Matching UND fuer die Vorausschau - es ist dieselbe
   Aussage, deshalb eine Konstante. */
#define COURSE_MIN_KMH 8.0f
#define COURSE_TOLERANCE_DEG 45.0f  /* zulaessige Winkelabweichung         */
#define CELL_CACHE_SLOTS 27         /* 9 je gleichzeitig ueberlappender Region */
#define CELL_ALLOC_GRAN 4096
#define MAX_REGIONS 8
#define GRID_DIR "/maps"

/* ==========================================================================
 * KARTEN-UPDATE UEBER WLAN (webupdate.h/.cpp)
 * ==========================================================================
 * Backlog Punkt 1a: Regionen ohne Toolchain aktualisieren. Das Geraet spannt
 * einen Access Point auf und stellt unter http://192.168.4.1/ eine
 * Weboberflaeche bereit, ueber die fertige .msg-Dateien hochgeladen und
 * installierte Regionen geloescht werden koennen. Aufbereitung (PBF -> msg)
 * bleibt auf dem PC bei tools/maps.py - hier wird nur die fertige Datei
 * entgegengenommen.
 *
 * Offener AP ohne Passwort: fuer ein Geraet im Auto vertretbar (siehe
 * backlog.md), bewusst so entschieden statt WPA2 mit einem Passwort, das
 * ohnehin am Geraet selbst stuende.
 *
 * Hochgeladene und zum Loeschen vorgemerkte Regionen landen zunaechst in
 * PENDING_DIR, nicht direkt in GRID_DIR: waehrend das Geraet laeuft, haelt
 * SpeedLimitGrid unter Umstaenden gerade ein offenes File-Handle auf genau
 * die Regionsdatei, die ersetzt oder geloescht werden soll - der laufende
 * Lookup wuerde dann mitten im Schreiben lesen. applyPendingMapChanges()
 * uebernimmt die Aenderungen deshalb erst ganz am Anfang des naechsten
 * setup(), bevor irgendein File-Handle auf GRID_DIR existiert. Aenderungen
 * wirken also erst nach einem Neustart - dafuer gibt es in der Weboberflaeche
 * einen eigenen Knopf.
 */
#define AP_SSID "Tempolimit-Setup"
/* Ohne Verbindung schaltet der AP nach dieser Zeit wieder ab (Stromverbrauch
   im geparkten Auto). Verbindet sich jemand, wird dieselbe Frist bei jeder
   Pruefung neu gestartet, solange mindestens eine Station verbunden ist -
   der AP bleibt also offen, solange die Sitzung laeuft, und danach noch
   einmal so lange als Kulanz. Eine einzige Konstante fuer beide Faelle, weil
   es dieselbe Abwaegung ist. */
#define AP_IDLE_TIMEOUT_MS (5UL * 60UL * 1000UL)
/* DEMO_PIN so lange gegen GND gehalten, startet den AP erneut, falls er
   schon abgeschaltet ist - ohne dafuer neu starten zu muessen. Der Schalter
   erzwingt dabei wie gewohnt auch die simulierte Fahrt; das ist waehrend
   einer Wartungssitzung am geparkten Fahrzeug ohne Belang. */
#define AP_HOLD_TRIGGER_MS (10UL * 1000UL)
/* Sicherheitsabstand beim Platzcheck vor einem Upload - der Content-Length
   der Anfrage ist eine Obergrenze (multipart-Rahmen ist mit drin), keine
   exakte Dateigroesse. */
#define AP_FREE_MARGIN_BYTES (64UL * 1024UL)
#define PENDING_DIR "/maps_pending"

/* ==========================================================================
 * TAKTE
 * ========================================================================== */
/*
 * Takt von Lookup und Anzeigewerten. Nicht zu verwechseln mit der Bildrate:
 * der Fuellbalken laeuft ueber ui_tick() in jedem Schleifendurchlauf und ist
 * davon unabhaengig. 60 ms sind rund 17 Hz - schneller als die 5 Hz des GPS,
 * weil zwischen den Fixes koppelnavigiert wird und die Ueberblendung der
 * Zahlen sonst grob aussaehe. Der Lookup kostet dank Cache fast nichts.
 */
#define UI_UPDATE_MS 60
#define LOG_INTERVAL_MS 1000    /* Serial-Log, sonst unlesbar                  */
#define GPS_GRACE_MS 15000      /* so lange auf einen Fix warten, dann Demo    */
#define DEMO_LEG_MS 12000       /* Dauer einer Demo-Etappe                     */

/* ==========================================================================
 * GPS
 * ==========================================================================
 * 9600 Baud sind 960 Byte/s. Mit RMC+GGA bei 5 Hz sind rund 717 Byte/s
 * gemessen, also 75 % ausgelastet - es passt, aber ohne Reserve. Wer weitere
 * Saetze braucht (etwa GSV fuer die Satellitenqualitaet), muss zuerst hier
 * hoch; die Suchautomatik findet die neue Rate von allein wieder.
 */
#define GPS_BAUD_TARGET 9600
#define GPS_RATE_HZ 5
#define GPS_PROBE_MS 4000
