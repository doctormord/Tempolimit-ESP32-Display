/*
 * Tempolimit-Anzeige - ESP32-S3-DevKitC (N16R8) + EstarDyn 1.53" ST77916
 *
 * Bindet dieselbe ui.c ein wie der PC-Simulator. Kein TCA9554 noetig,
 * weil das Breakout RST und BL direkt herausfuehrt.
 *
 * --------------------------------------------------------------------------
 * VERKABELUNG  (Displaypins wie auf der Platine beschriftet)
 * --------------------------------------------------------------------------
 *   Display        ESP32-S3      Bemerkung
 *   -----------------------------------------------------------------
 *   GND            GND
 *   VCC            3V3 oder 5V   Datenblatt nennt 3,3-5 V; das Modul hat einen
 *                                eigenen Regler. Die Logikpegel bleiben 3,3 V.
 *                                Bei beleuchtetem, aber schwarzem Bild lohnt
 *                                der Versuch mit 5V (VIN): das Hintergrundlicht
 *                                kann angehen, waehrend die Panel-Bias-Spannung
 *                                bei knapper Versorgung nicht hochkommt.
 *   SCL            GPIO12        QSPI-Takt
 *   SDA  (IO0)     GPIO11        QSPI Daten 0
 *   IO1            GPIO13        QSPI Daten 1
 *   IO2            GPIO14        QSPI Daten 2
 *   IO3            GPIO9         QSPI Daten 3
 *   CS             GPIO10
 *   RST            GPIO8
 *   BL             GPIO7         Hintergrundlicht, high = an
 *   TE             frei lassen   (Tearing-Signal, hier nicht genutzt)
 *
 *   NEO-6M         ESP32-S3
 *   -----------------------------------------------------------------
 *   VCC            5V (Pin VIN/5V des DevKit)
 *   GND            GND
 *   TX             GPIO18        GPS sendet -> ESP empfaengt
 *   RX             GPIO17        meist ungenutzt
 *
 * Finger weg von GPIO26-37: dort haengen Flash und das Octal-PSRAM des
 * N16R8-Moduls. GPIO19/20 sind USB, GPIO43/44 die UART-Bruecke.
 *
 * --------------------------------------------------------------------------
 * ARDUINO-EINSTELLUNGEN
 * --------------------------------------------------------------------------
 *   Board:       ESP32S3 Dev Module
 *   PSRAM:       OPI PSRAM            <- zwingend, sonst kein Framebuffer
 *   Flash Size:  16MB
 *   Partition:   16M Flash (3MB APP/9.9MB FATFS)
 *   USB CDC On Boot: Enabled          <- sonst keine Serial-Ausgabe
 *
 * Libraries: lvgl 9.x, Arduino_GFX_Library, TinyGPSPlus
 * Dateien im Sketch-Ordner: dieser Sketch, ui.c, ui.h, speedlimit_grid.h,
 * webupdate.h/.cpp (Kartenupdate per Access Point)
 * --------------------------------------------------------------------------
 */

#include <Arduino.h>   // in PlatformIO noetig, die Arduino-IDE ergaenzt das selbst
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <TinyGPSPlus.h>
#include <driver/gpio.h>   // gpio_set_drive_capability fuer das Hintergrundlicht
#include <lvgl.h>

#include "config.h"
#include "speedlimit_grid.h"
#include "ui.h"
#include "webupdate.h"

// ---------- Pins ----------
#define LCD_CS 10
#define LCD_SCK 12
#define LCD_D0 11
#define LCD_D1 13
#define LCD_D2 14
#define LCD_D3 9
#define LCD_RST 8
#define LCD_BL 7

#define GPS_RX 18   // an TX des NEO-6M
#define GPS_TX 17

// TE (Tearing Effect) ist ein AUSGANG des Panels. Normalerweise unbenutzt,
// fuer die Fehlersuche aber der einzige Rueckkanal, den QSPI hier bietet.
#define LCD_TE 16

// ---------- Display ----------
/*
 * Arduino_ST77916 nimmt als Default st77916_180_init_operations - die Sequenz
 * fuer ein 1,80"-Panel. Unseres ist das runde 1,53", dafuer ist die 150er
 * Sequenz gedacht. Mit der falschen Sequenz bleibt das Bild schwarz, obwohl
 * begin() "erfolgreich" meldet: ueber QSPI wird nichts zurueckgelesen.
 * Zum Gegentesten: -DST77916_INIT_180 setzen.
 */
#ifdef ST77916_INIT_180
#define ST77916_INIT st77916_180_init_operations
#else
#define ST77916_INIT st77916_150_init_operations
#endif

#define LCD_BL_HZ 5000
#define LCD_BL_BITS 8

Arduino_DataBus *bus =
    new Arduino_ESP32QSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX *gfx = new Arduino_ST77916(bus, LCD_RST, 0 /* rotation */,
                                       true /* IPS */, UI_SIZE, UI_SIZE,
                                       0, 0, 0, 0, ST77916_INIT,
                                       sizeof(ST77916_INIT));

/*
 * ---------------------------------------------------------------------------
 * Diagnose fuer die Inbetriebnahme - nur mit -DLCD_DIAG im Build.
 * Kostet rund 5 s Startzeit, deshalb im Normalbetrieb aus:
 *     PLATFORMIO_BUILD_FLAGS=-DLCD_DIAG pio run -e esp32s3 -t upload
 * ---------------------------------------------------------------------------
 */
#ifdef LCD_DIAG
/*
 * Verdrahtungstest ohne Messgeraet.
 *
 * Hintergrund: Arduino_ESP32QSPI sendet mit SPI_TRANS_MULTILINE_CMD und
 * SPI_TRANS_MULTILINE_ADDR - also gehen auch Kommando und Adresse ueber alle
 * vier Datenleitungen, nicht nur die Pixeldaten. Eine einzige schlechte
 * Verbindung an IO1/IO2/IO3 legt damit die gesamte Kommunikation lahm: das
 * Panel bleibt beleuchtet, aber schwarz, weil nicht einmal DISPON ankommt.
 *
 * Der Test findet Kurzschluesse gegen GND, gegen 3V3 und zwischen zwei Pins.
 * Was er NICHT finden kann, ist eine unterbrochene Leitung - eine offene
 * Leitung und eine saubere Leitung zu einem hochohmigen Panel-Eingang sehen
 * von hier aus gleich aus.
 */
static void lcdPinCheck() {
  static const struct {
    int gpio;
    const char *name;
  } P[] = {{LCD_SCK, "SCL/SCK"}, {LCD_D0, "SDA/IO0"}, {LCD_D1, "IO1"},
           {LCD_D2, "IO2"},      {LCD_D3, "IO3"},     {LCD_CS, "CS"},
           {LCD_RST, "RST"},     {LCD_BL, "BL"}};
  const int N = sizeof(P) / sizeof(P[0]);

  Serial.println("[Pin] --- Verdrahtungstest ---");
  bool stuck[N];
  for (int i = 0; i < N; i++) {
    pinMode(P[i].gpio, INPUT_PULLUP);
    delay(2);
    int up = digitalRead(P[i].gpio);
    pinMode(P[i].gpio, INPUT_PULLDOWN);
    delay(2);
    int dn = digitalRead(P[i].gpio);
    stuck[i] = (up == dn);
    const char *v = "ok";
    if (up == 0 && dn == 0) {
      // Beim Hintergrundlicht ist das normal: der Treiber auf dem Modul zieht
      // den Pin staerker nach GND als der interne Pullup nach 3V3.
      v = (P[i].gpio == LCD_BL) ? "fest auf GND (bei BL normal)"
                                : "<-- haengt fest auf GND";
    } else if (up == 1 && dn == 1) {
      v = "<-- haengt fest auf 3V3";
    }
    Serial.printf("[Pin] GPIO%-2d %-8s  %s\n", P[i].gpio, P[i].name, v);
    pinMode(P[i].gpio, INPUT);
  }

  int shorts = 0;
  for (int i = 0; i < N; i++) {
    if (stuck[i]) continue;
    for (int j = 0; j < N; j++)
      if (j != i) pinMode(P[j].gpio, INPUT_PULLUP);
    pinMode(P[i].gpio, OUTPUT);
    digitalWrite(P[i].gpio, LOW);
    delay(3);
    for (int j = 0; j < N; j++) {
      // Ein dauerhaft tiefer Pin liest immer 0 und wuerde sonst gegen jeden
      // getriebenen Pin faelschlich als Kurzschluss gemeldet.
      if (j == i || stuck[j]) continue;
      if (digitalRead(P[j].gpio) == 0) {
        Serial.printf("[Pin] KURZSCHLUSS GPIO%d (%s) <-> GPIO%d (%s)\n",
                      P[i].gpio, P[i].name, P[j].gpio, P[j].name);
        shorts++;
      }
    }
    pinMode(P[i].gpio, INPUT);
  }
  Serial.printf("[Pin] %d Kurzschluesse gefunden\n", shorts);
}

/*
 * Lebenszeichen des Panels ueber TE.
 *
 * Bei QSPI gibt es keinen brauchbaren Rueckkanal - deshalb meldet
 * gfx->begin() auch dann Erfolg, wenn gar kein Panel angeschlossen ist. TE
 * ist die Ausnahme: ein Ausgang des Panels, den die 150er Initsequenz mit
 * Kommando 0x35 einschaltet. Er pulst mit der Bildwiederholrate.
 *
 *   Flanken > 0  ->  Kommandos kommen an, das Panel laeuft. Der Fehler liegt
 *                    dann hinter der Initialisierung (Pixelpfad, Adressfenster,
 *                    Farbformat).
 *   Flanken = 0  ->  kein einziges Kommando erreicht das Panel.
 *
 * Voraussetzung: TE des Moduls mit LCD_TE verbinden. Ohne Draht bleibt der
 * Pin dank Pulldown ruhig und meldet ebenfalls 0 - deshalb wird der Ruhepegel
 * mitgemeldet, um "nicht verdrahtet" von "stumm" zu unterscheiden.
 */
static volatile uint32_t te_edges = 0;
static void IRAM_ATTR teISR() { te_edges++; }

static void teCheck() {
  // 1. Erst die Messkette selbst pruefen. Ein Ergebnis von 0 Flanken ist
  //    wertlos, solange nicht feststeht, dass ueberhaupt gezaehlt wird.
  //    Das laeuft auf einem freien Pin - auf LCD_TE wuerde man gegen den
  //    Ausgang des Panels treiben.
  const int probe = 15;
  pinMode(probe, OUTPUT);
  digitalWrite(probe, LOW);
  te_edges = 0;
  attachInterrupt(digitalPinToInterrupt(probe), teISR, RISING);
  for (int i = 0; i < 50; i++) {
    digitalWrite(probe, HIGH);
    delayMicroseconds(200);
    digitalWrite(probe, LOW);
    delayMicroseconds(200);
  }
  detachInterrupt(digitalPinToInterrupt(probe));
  pinMode(probe, INPUT);
  Serial.printf("[TE] Messkette: %lu von 50 Flanken erkannt%s\n",
                (unsigned long)te_edges,
                te_edges >= 45 ? "" : "  <-- Zaehlung unzuverlaessig!");

  // 2. TE passiv messen, beide Pull-Richtungen. Waere TE ein Open-Drain-
  //    Ausgang, bliebe er mit Pulldown dauerhaft tief und der Test blind.
  for (int mode = 0; mode < 2; mode++) {
    pinMode(LCD_TE, mode ? INPUT_PULLUP : INPUT_PULLDOWN);
    delay(5);
    int idle = digitalRead(LCD_TE);
    te_edges = 0;
    attachInterrupt(digitalPinToInterrupt(LCD_TE), teISR, CHANGE);
    delay(500);
    detachInterrupt(digitalPinToInterrupt(LCD_TE));
    Serial.printf("[TE] GPIO%d %-8s: %lu Wechsel/0,5s, Ruhepegel %d\n", LCD_TE,
                  mode ? "Pullup" : "Pulldown", (unsigned long)te_edges, idle);
  }
}

// Vollflaechige Farben vor LVGL: zeigt das Panel die, liegt es an der
// Oberflaeche - bleibt es schwarz, an Panel, Verkabelung oder Hintergrundlicht.
static void lcdSelfTest() {
  static const struct {
    uint16_t rgb565;
    const char *name;
  } STEPS[] = {{0xF800, "rot"}, {0x07E0, "gruen"},
               {0x001F, "blau"}, {0xFFFF, "weiss"}};
  for (auto &s : STEPS) {
    Serial.printf("[LCD] Testbild %s\n", s.name);
    gfx->fillScreen(s.rgb565);
    delay(800);
  }
  gfx->fillScreen(0x0000);
}
#endif  // LCD_DIAG

static uint32_t lv_prev_ms = 0;

static uint32_t g_flushes = 0, g_flush_px = 0, g_flush_us = 0, g_flush_max = 0;
static uint32_t g_refr_us = 0, g_refr_max_us = 0;

#if LCD_TE_SYNC
/*
 * Auf den Beginn der Austastluecke warten. TE ist ein Ausgang des Panels und
 * pulst mit der Bildwiederholrate; wer dort mit dem Schreiben beginnt, ist
 * fertig, bevor der Bildaufbau die geschriebene Stelle erreicht.
 *
 * Erst auf Low warten, dann auf die steigende Flanke - sonst wuerde ein
 * gerade anliegendes High sofort als Flanke durchgehen. Der Timeout sorgt
 * dafuer, dass ein fehlendes TE-Signal die Anzeige nicht anhaelt.
 */
static uint32_t g_te_timeouts = 0;
static uint32_t g_te_us = 0, g_te_waits = 0;

static void waitForTE() {
  uint32_t t0 = micros();
  g_te_waits++;
  while (digitalRead(LCD_TE) == HIGH) {
    if (micros() - t0 > LCD_TE_TIMEOUT_US) { g_te_timeouts++; return; }
  }
  while (digitalRead(LCD_TE) == LOW) {
    if (micros() - t0 > LCD_TE_TIMEOUT_US) { g_te_timeouts++; g_te_us += micros() - t0; return; }
  }
  g_te_us += micros() - t0;
}

#endif

static void lv_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
#if LCD_TE_SYNC
  /*
   * Vor JEDER Teilflaeche auf die Austastluecke warten, nicht nur vor der
   * ersten des Bildzyklus.
   *
   * Mit nur einer Wartung je Zyklus landeten alle weiteren Teilflaechen
   * mitten im Bildaufbau - sichtbar als waagerechter Strich genau auf der
   * Puffergrenze bei y=180. Je Teilflaeche kostet das eine Bildperiode; bei
   * zwei Streifen also 33 statt 17 ms. Fuer eine Blende von 1000 ms sind das
   * immer noch rund 30 Stufen.
   */
  waitForTE();
#endif
  uint32_t t0 = micros();
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
  g_flush_us += micros() - t0;
  g_flushes++;
  g_flush_px += w * h;
  if (w * h > g_flush_max) g_flush_max = w * h;
  lv_display_flush_ready(disp);
}

// ---------- GPS auf Core 0 ----------
TinyGPSPlus gps;
HardwareSerial GPSSerial(1);
static SemaphoreHandle_t gpsMutex;
// Reihenfolge muss der Deklaration in ui.h folgen - C++ ist da strenger als C
static ui_state_t g_state = {.limit = -1, .fix = false, .sats = 0};
// Kurs gehoert nicht in ui_state_t (die Anzeige zeigt ihn nicht), wird aber
// fuer den Richtungsfilter des Lookups gebraucht. <0 = unbekannt.
static float g_course = -1.0f;
// Zaehlt empfangene NMEA-Bytes: 0 nach der Wartezeit = kein Modul angeschlossen
static uint32_t g_gps_bytes = 0;

// ---------- Kartenlookup ----------
static SpeedLimitGrid grid;
static bool gridReady = false;

static int dayOfWeek(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static bool isSummerTime(int y, int mo, int d, int h) {
  if (mo < 3 || mo > 10) return false;
  if (mo > 3 && mo < 10) return true;
  int s = 31 - dayOfWeek(y, mo, 31);   // letzter Sonntag im Monat
  if (mo == 3) return d > s ? true : (d < s ? false : h >= 1);
  return d < s ? true : (d > s ? false : h < 1);
}

/*
 * Automatik fuer Pinbelegung UND Baudrate.
 *
 * Zwei Fehler passieren hier standardmaessig: RX/TX vertauscht (der ESP muss
 * an den TX des Moduls) und eine andere Baudrate als die 9600 des Originals -
 * NEO-6M-Nachbauten kommen auch mit 38400 oder 115200.
 *
 * Als "geht" gilt nicht "es kommen Bytes", sondern "TinyGPS akzeptiert einen
 * Satz mit gueltiger Pruefsumme". Bei falscher Baudrate kommen naemlich sehr
 * wohl Bytes an, nur eben Datenmuell.
 */
static const uint32_t GPS_BAUDS[] = {9600, 38400, 115200, 4800};
#define N_GPS_BAUDS (sizeof(GPS_BAUDS) / sizeof(GPS_BAUDS[0]))

/*
 * NEO-6M auf 5 Hz stellen (UBX-Protokoll).
 *
 * Zwei Schritte, und der erste ist nicht optional: bei 9600 Baud passen die
 * vollen NMEA-Saetze fuenfmal pro Sekunde nicht durch. 9600 8N1 sind 960
 * Byte/s, ein kompletter Satz-Block ist rund 380 Byte - macht bei 5 Hz 1900
 * Byte/s. Deshalb erst alles abschalten, was TinyGPS nicht braucht (es
 * genuegen RMC und GGA), dann die Rate hochsetzen. Uebrig bleiben rund
 * 730 Byte/s.
 *
 * Die Einstellung liegt im batteriegepufferten RAM des Moduls und geht ohne
 * Puffer beim Trennen der Versorgung verloren - sie wird daher bei jedem
 * erfolgreichen Sync neu gesendet, nicht nur einmalig.
 */
static void ubxSend(uint8_t cls, uint8_t id, const uint8_t *payload,
                    uint16_t len) {
  uint8_t head[6] = {0xB5, 0x62, cls, id, (uint8_t)(len & 0xFF),
                     (uint8_t)(len >> 8)};
  uint8_t a = 0, b = 0;
  for (int i = 2; i < 6; i++) {   // Pruefsumme ab Klasse, ohne 0xB5 0x62
    a += head[i];
    b += a;
  }
  for (uint16_t i = 0; i < len; i++) {
    a += payload[i];
    b += a;
  }
  GPSSerial.write(head, 6);
  if (len) GPSSerial.write(payload, len);
  GPSSerial.write(a);
  GPSSerial.write(b);
  GPSSerial.flush();
}

static void gpsConfigure() {
  // NMEA-Klasse 0xF0: 00=GGA 01=GLL 02=GSA 03=GSV 04=RMC 05=VTG 08=ZDA
  static const uint8_t QUIET[] = {0x01, 0x02, 0x03, 0x05, 0x08};
  for (uint8_t id : QUIET) {
    const uint8_t msg[3] = {0xF0, id, 0x00};   // Rate 0 = aus
    ubxSend(0x06, 0x01, msg, 3);               // CFG-MSG
    delay(20);
  }
  // CFG-RATE: 200 ms Messintervall, navRate 1, Zeitbezug GPS
  const uint8_t rate[6] = {0xC8, 0x00, 0x01, 0x00, 0x01, 0x00};
  ubxSend(0x06, 0x08, rate, 6);
  Serial.println("[GPS] GSV/GSA/GLL/VTG/ZDA aus, Rate auf 5 Hz gesetzt");
}

static void gpsTask(void *) {
  int rx = GPS_RX, tx = GPS_TX;
  size_t bi = 0;
  GPSSerial.begin(GPS_BAUDS[bi], SERIAL_8N1, rx, tx);
  uint32_t probe_start = millis();
  uint32_t base_ok = 0;
  uint32_t last_stat = 0;
  uint32_t last_ok = 0;
  bool found = false;

  for (;;) {
    if (!found) {
      if (gps.passedChecksum() > base_ok) {
        found = true;
        Serial.printf("[GPS] gueltige NMEA-Saetze auf RX=GPIO%d, %lu Baud\n",
                      rx, (unsigned long)GPS_BAUDS[bi]);
        gpsConfigure();
      } else if (millis() - probe_start > GPS_PROBE_MS) {
        // Erst alle Baudraten, dann mit getauschten Pins von vorn
        if (++bi >= N_GPS_BAUDS) {
          bi = 0;
          int t = rx;
          rx = tx;
          tx = t;
        }
        GPSSerial.end();
        GPSSerial.begin(GPS_BAUDS[bi], SERIAL_8N1, rx, tx);
        base_ok = gps.passedChecksum();
        probe_start = millis();
        Serial.printf("[GPS] nichts Gueltiges - probiere RX=GPIO%d, %lu Baud\n",
                      rx, (unsigned long)GPS_BAUDS[bi]);
      }
    }

    // Regelmaessige Statistik: trennt "kein Empfang" von "Empfang ohne Fix".
    // Die Satzrate zeigt ausserdem, ob die 5-Hz-Umstellung gegriffen hat:
    // mit RMC+GGA sind 10 Saetze/s zu erwarten, vorher waren es 2.
    if (millis() - last_stat > 5000) {
      uint32_t dt = millis() - last_stat;
      last_stat = millis();
      uint32_t ok = gps.passedChecksum();
      Serial.printf(
          "[GPS] Bytes=%lu ok=%lu (%.1f Saetze/s) Pruefsummenfehler=%lu "
          "Sat=%d\n",
          (unsigned long)g_gps_bytes, (unsigned long)ok,
          (ok - last_ok) * 1000.0f / dt, (unsigned long)gps.failedChecksum(),
          gps.satellites.isValid() ? (int)gps.satellites.value() : -1);
      last_ok = ok;
    }

    while (GPSSerial.available()) {
      g_gps_bytes++;
      if (!gps.encode(GPSSerial.read())) continue;

      xSemaphoreTake(gpsMutex, portMAX_DELAY);
      if (gps.location.isValid()) {
        g_state.lat = gps.location.lat();
        g_state.lon = gps.location.lng();
        g_state.fix = true;
      }
      if (gps.speed.isValid()) g_state.speed_kmh = gps.speed.kmph();
      if (gps.course.isValid()) g_course = gps.course.deg();
      if (gps.satellites.isValid()) g_state.sats = gps.satellites.value();
      if (gps.date.isValid() && gps.time.isValid() && gps.date.year() > 2020) {
        int off = isSummerTime(gps.date.year(), gps.date.month(),
                               gps.date.day(), gps.time.hour())
                      ? 2
                      : 1;
        int h = gps.time.hour() + off;
        int d = gps.date.day();
        if (h >= 24) {
          h -= 24;
          d += 1;
        }
        g_state.hour = h;
        g_state.minute = gps.time.minute();
        g_state.weekday =
            (dayOfWeek(gps.date.year(), gps.date.month(), d) + 6) % 7;
        g_state.time_valid = true;
      }
      xSemaphoreGive(gpsMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------- Demo-Fahrt, wenn kein GPS da ist ----------
/*
 * Die Stuetzpunkte liegen auf echten Strassen aus tools/out-berlin. Damit
 * prueft die Demo nicht nur die Anzeige, sondern den ganzen Weg
 * LittleFS -> Zellblock -> Map-Matching. Die erwarteten Limits stammen aus
 * einer Nachbildung des Lookups auf dem PC und stehen zum Abgleich im
 * Serial-Log. Weicht "Limit" von "erwartet" ab, stimmt etwas am Geraet nicht.
 *
 * Warum ueberhaupt: am Schreibtisch bekommt der NEO-6M keinen Fix. Ohne diese
 * Etappen liesse sich die Karte drinnen gar nicht testen.
 */
typedef struct {
  float lat, lon;
} demo_pt_t;

typedef struct {
  const demo_pt_t *pts;
  uint8_t n;
  float speed_kmh;   // Tempo auf dieser Etappe
  int expect;        // erwartetes Limit (255 = frei, -1 = keine Daten)
  uint8_t why;       // erwartete Begruendung, 0xFF = egal
  const char *note;
} demo_leg_t;

#define WHY_EGAL 0xFF

static const demo_pt_t DP_30[] = {
    {52.564815f, 13.300000f}, {52.565114f, 13.303592f}, {52.565435f, 13.305962f},
    {52.564914f, 13.309158f}, {52.564155f, 13.313430f}, {52.562995f, 13.317742f},
    {52.562629f, 13.318487f}, {52.561390f, 13.318878f}};
static const demo_pt_t DP_50[] = {
    {52.483589f, 13.401198f}, {52.483467f, 13.402116f}, {52.483292f, 13.403175f},
    {52.482667f, 13.406162f}, {52.482284f, 13.407934f}, {52.482080f, 13.408886f},
    {52.482036f, 13.409090f}, {52.481876f, 13.409828f}};
static const demo_pt_t DP_60[] = {
    {52.483930f, 13.306886f}, {52.483757f, 13.307060f}, {52.483513f, 13.307334f},
    {52.482673f, 13.308385f}, {52.481701f, 13.309735f}, {52.481033f, 13.310714f}};
static const demo_pt_t DP_80[] = {
    {52.537277f, 13.320754f}, {52.537141f, 13.319775f}, {52.536750f, 13.317010f},
    {52.536002f, 13.311816f}, {52.535253f, 13.306460f}};
static const demo_pt_t DP_100[] = {
    {52.451742f, 13.214369f}, {52.449744f, 13.211849f}, {52.447494f, 13.209003f},
    {52.445041f, 13.205904f}, {52.444719f, 13.205499f}, {52.442666f, 13.202903f}};
static const demo_pt_t DP_120[] = {
    {52.396671f, 13.180372f}, {52.396106f, 13.179383f}, {52.395597f, 13.178316f},
    {52.395288f, 13.177561f}, {52.394969f, 13.176720f}, {52.394676f, 13.175818f}};
static const demo_pt_t DP_FREI[] = {
    {52.626942f, 13.480943f}, {52.627301f, 13.479750f}, {52.627730f, 13.478057f},
    {52.627997f, 13.476792f}, {52.628209f, 13.475561f}, {52.628466f, 13.473928f}};
// Berliner Strassen mit hinterlegter Begruendung - pruefen die Anzeige
// unter der Ziffer und die Bits 0-2 des flags-Byte.
static const demo_pt_t DP_ZONE[] = {
    {52.505100f, 13.488393f}, {52.504060f, 13.486349f}, {52.503608f, 13.485474f},
    {52.502304f, 13.492173f}, {52.502340f, 13.492376f}, {52.505148f, 13.494007f}};
static const demo_pt_t DP_KIND[] = {
    {52.466728f, 13.507763f}, {52.466800f, 13.508015f}, {52.466932f, 13.508876f},
    {52.466820f, 13.508883f}, {52.466408f, 13.509058f}, {52.465992f, 13.509212f}};
static const demo_pt_t DP_SPIEL[] = {
    {52.493348f, 13.420258f}, {52.493808f, 13.418508f}, {52.494468f, 13.418844f},
    {52.495828f, 13.419404f}, {52.495672f, 13.417983f}, {52.494944f, 13.414546f}};
static const demo_pt_t DP_RAD[] = {
    {52.560000f, 13.325726f}, {52.558828f, 13.325740f}, {52.556548f, 13.325313f},
    {52.554736f, 13.324655f}, {52.550592f, 13.323220f}, {52.550112f, 13.322947f}};

/*
 * Zeitlich begrenztes Limit: 50, aber 30 von 6 bis 18 Uhr wegen Kindern.
 * Prueft, dass die Begruendung nur waehrend der Bedingung erscheint -
 * nachts muss hier "50" ohne Beschriftung stehen, tagsueber "30 KINDER".
 * Die Erwartung haengt an der Uhrzeit, deshalb WHY_EGAL und Limit -2 als
 * "je nach Tageszeit" (siehe Auswertung im Log).
 */
static const demo_pt_t DP_ZEIT[] = {
    {52.431736f, 13.229898f}, {52.433076f, 13.229765f}, {52.433856f, 13.229828f},
    {52.434564f, 13.229996f}, {52.435216f, 13.230262f}, {52.435820f, 13.230619f}};

// Suedbrandenburg bei Cottbus - liegt ausserhalb der Berlin-Bounding-Box und
// prueft damit gezielt, dass die zweite Region gefunden und gelesen wird.
static const demo_pt_t DP_BB[] = {
    {51.877376f, 14.552160f}, {51.884296f, 14.566069f}, {51.884828f, 14.567525f},
    {51.885668f, 14.570528f}, {51.887688f, 14.574896f}, {51.889532f, 14.579306f}};
// Ausserhalb aller Regionen: leere Zellen -> Fragezeichen
static const demo_pt_t DP_LEER[] = {{52.200000f, 13.000000f},
                                    {52.205000f, 13.005000f}};

#define DL(arr) arr, (uint8_t)(sizeof(arr) / sizeof(arr[0]))
static const demo_leg_t DEMO[] = {
    {DL(DP_30), 45.0f, 30, WHY_EGAL, "Tempo-30, zu schnell"},
    {DL(DP_ZONE), 28.0f, 30, UI_REASON_ZONE, "Tempo-30-Zone"},
    {DL(DP_KIND), 25.0f, 30, UI_REASON_KINDER, "Kinder / Schule"},
    {DL(DP_SPIEL), 6.0f, 7, UI_REASON_SPIEL, "Spielstrasse"},
    {DL(DP_RAD), 22.0f, 30, UI_REASON_RAD, "Fahrradstrasse"},
    {DL(DP_ZEIT), 40.0f, -2, WHY_EGAL, "zeitlich begrenzt (6-18 Uhr)"},
    {DL(DP_50), 42.0f, 50, WHY_EGAL, "Ortsdurchfahrt"},
    {DL(DP_60), 58.0f, 60, WHY_EGAL, "Hauptstrasse"},
    {DL(DP_80), 80.0f, 80, WHY_EGAL, "Ausfallstrasse, Balken voll"},
    {DL(DP_100), 75.0f, 100, WHY_EGAL, "Schnellstrasse"},
    // 145 statt 130: mit OVER_TOLERANCE_PCT=10 liegt die Schwelle bei 132
    {DL(DP_120), 145.0f, 120, WHY_EGAL, "Autobahn, zu schnell"},
    {DL(DP_FREI), 160.0f, 255, WHY_EGAL, "unbegrenzt -> frei"},
    {DL(DP_BB), 95.0f, 100, WHY_EGAL, "Brandenburg, zweite Region"},
    {DL(DP_LEER), 90.0f, -1, WHY_EGAL, "keine Kartendaten -> ?"},
};
#define N_DEMO (sizeof(DEMO) / sizeof(DEMO[0]))



static size_t demo_leg = 0;
static float demo_pos_m = 0.0f;
static uint32_t demo_leg_ms = 0;

static float demoSegLen(const demo_pt_t &a, const demo_pt_t &b) {
  float mlat = 111320.0f;
  float mlon = 111320.0f * cosf(a.lat * (float)DEG_TO_RAD);
  float dy = (b.lat - a.lat) * mlat;
  float dx = (b.lon - a.lon) * mlon;
  return sqrtf(dx * dx + dy * dy);
}

// Setzt Position, Tempo und Kurs auf den aktuellen Punkt der simulierten Fahrt
static void demoStep(uint32_t dt_ms, ui_state_t *st, float *course) {
  demo_leg_ms += dt_ms;
  demo_pos_m += DEMO[demo_leg].speed_kmh / 3.6f * (dt_ms / 1000.0f);

  float total = 0.0f;
  for (uint8_t i = 0; i + 1 < DEMO[demo_leg].n; i++)
    total += demoSegLen(DEMO[demo_leg].pts[i], DEMO[demo_leg].pts[i + 1]);

  if (demo_pos_m > total || demo_leg_ms >= DEMO_LEG_MS) {
    demo_leg = (demo_leg + 1) % N_DEMO;
    demo_pos_m = 0.0f;
    demo_leg_ms = 0;
    Serial.printf("[Demo] -> %s\n", DEMO[demo_leg].note);
  }

  const demo_leg_t &leg = DEMO[demo_leg];
  float acc = 0.0f;
  for (uint8_t i = 0; i + 1 < leg.n; i++) {
    float L = demoSegLen(leg.pts[i], leg.pts[i + 1]);
    // letztes Teilstueck faengt alles ab, was durch Rundung ueberhaengt
    if (demo_pos_m <= acc + L || i + 2 == leg.n) {
      float t = (L > 0.01f) ? (demo_pos_m - acc) / L : 0.0f;
      t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
      st->lat = leg.pts[i].lat + t * (leg.pts[i + 1].lat - leg.pts[i].lat);
      st->lon = leg.pts[i].lon + t * (leg.pts[i + 1].lon - leg.pts[i].lon);

      float mlon = cosf(leg.pts[i].lat * (float)DEG_TO_RAD);
      float dx = (leg.pts[i + 1].lon - leg.pts[i].lon) * mlon;
      float dy = leg.pts[i + 1].lat - leg.pts[i].lat;
      float deg = atan2f(dx, dy) * 180.0f / (float)PI;
      *course = deg < 0.0f ? deg + 360.0f : deg;
      break;
    }
    acc += L;
  }
  /*
   * Tempo leicht schwanken lassen (+-8 %). In der Demo waere es sonst je
   * Etappe konstant und der Fuellbalken stuende still - man saehe nicht, ob
   * er ruckelt. Die Schwankung ist klein genug, dass keine Etappe ueber oder
   * unter die Toleranzschwelle von OVER_TOLERANCE_PCT rutscht.
   */
  st->speed_kmh = leg.speed_kmh *
                  (1.0f + 0.08f * sinf((float)millis() / 1500.0f));
}

/*
 * Entprellter Schalter gegen Masse. Der Zustand haengt an der uebergebenen
 * Struktur, damit sich mehrere Schalter denselben Code teilen.
 */
struct Switch {
  int pin;
  bool stable = false;
  bool last_raw = false;
  uint32_t since = 0;
};

static bool readSwitch(Switch &sw) {
  bool raw = (digitalRead(sw.pin) == LOW);
  if (raw != sw.last_raw) {
    sw.last_raw = raw;
    sw.since = millis();
  } else if (millis() - sw.since >= MODE_DEBOUNCE_MS) {
    sw.stable = raw;
  }
  return sw.stable;
}

static Switch sw_mode{MODE_PIN};   // offen = Tempolimit, gegen GND = Tacho
static Switch sw_demo{DEMO_PIN};   // gegen GND = simulierte Fahrt erzwingen

/*
 * Hintergrundlicht mit weicher Ueberblendung. Zwei Schwellen (DIM_BELOW_KMH /
 * DIM_ABOVE_KMH), damit es an der Ampel nicht im Sekundentakt springt.
 */
/*
 * Hintergrundlicht.
 *
 * Zwei Einfluesse liegen uebereinander: die Grundhelligkeit (voll im
 * Fahrbetrieb, gedimmt im Stand) und die Blende beim Bildwechsel. Beide
 * werden in *wahrgenommener* Helligkeit gerechnet, nicht im PWM-Wert - das
 * Auge sieht Helligkeit ungefaehr als Wurzelfunktion der Leistung, eine
 * linear gefahrene PWM faellt oben kaum und unten schlagartig. Genau das
 * sah beim Abdimmen wie Flackern aus.
 *
 * Die Rampe laeuft auf demselben Timer wie die Blende, nicht in der
 * Hauptschleife: die blockiert waehrend eines Neuaufbaus bis zu 90 ms, und
 * eine Rampe mit solchen Luecken ruckelt sichtbar.
 */
static float bl_target_p = 1.0f;   /* Ziel, wahrgenommen 0..1   */
static float bl_cur_p = 1.0f;      /* Ist, wahrgenommen 0..1    */
static float g_bl_fade = 1.0f;     /* Blende 0..1, 1 = normal   */
static int g_bl_level = LCD_BL_LEVEL;   /* zuletzt gesetzte PWM, nur fuers Log */

static inline float bl_perc(int duty) {   /* PWM -> wahrgenommen */
  return powf((float)duty / 255.0f, 1.0f / FADE_BL_GAMMA);
}

static void applyBacklight() {
  float p = bl_cur_p * g_bl_fade;
  if (p < 0.0f) p = 0.0f;
  int v = (int)(255.0f * powf(p, FADE_BL_GAMMA) + 0.5f);
  if (v > 255) v = 255;
  g_bl_level = v;
  ledcWrite(LCD_BL, v);
}

/* Grundhelligkeit weiterfahren - gehoert auf den Timer, siehe oben. */
static void backlightRamp() {
  if (bl_cur_p == bl_target_p) return;
  float step = (float)FADE_BL_STEP_MS / DIM_FADE_MS;
  if (bl_cur_p < bl_target_p)
    bl_cur_p = (bl_cur_p + step > bl_target_p) ? bl_target_p : bl_cur_p + step;
  else
    bl_cur_p = (bl_cur_p - step < bl_target_p) ? bl_target_p : bl_cur_p - step;
  applyBacklight();
}

/*
 * Entscheidet nur das Ziel, gefahren wird es vom Timer. Zwei Schwellen
 * (DIM_BELOW_KMH / DIM_ABOVE_KMH), damit es an der Ampel nicht springt.
 */
static void updateBacklight(float speed_kmh) {
  static bool dimmed = false;
  if (!dimmed && speed_kmh < DIM_BELOW_KMH) dimmed = true;
  if (dimmed && speed_kmh > DIM_ABOVE_KMH) dimmed = false;
  bl_target_p = dimmed ? bl_perc(LCD_BL_DIM_LEVEL) : bl_perc(LCD_BL_LEVEL);
}

/*
 * Koppelnavigation: wo ist man in ms Millisekunden, wenn Kurs und Tempo so
 * bleiben? Ebene Naeherung, ueber ein paar hundert Meter voellig ausreichend.
 */
static void deadReckon(double lat, double lon, float course_deg, float kmh,
                       uint32_t ms, double *out_lat, double *out_lon) {
  float d = kmh / 3.6f * (ms / 1000.0f);            // Strecke in Metern
  float rad = course_deg * (float)DEG_TO_RAD;
  double dlat = (d * cosf(rad)) / 111320.0;
  double dlon = (d * sinf(rad)) / (111320.0 * cos(lat * DEG_TO_RAD));
  *out_lat = lat + dlat;
  *out_lon = lon + dlon;
}

/*
 * Blende ueber das Hintergrundlicht.
 *
 * Ein Wechsel von Limit oder Begruendung wird zurueckgehalten: erst faehrt
 * das Licht herunter, im Dunkeln wird das Bild getauscht, dann faehrt es
 * wieder hoch. Der Bildwechsel selbst kostet einen Neuaufbau wie bisher -
 * nur sieht ihn niemand, weil er im Dunkeln passiert.
 *
 * Alles ausser Limit und Begruendung (Tempo, Balken, Statuszeile) laeuft
 * waehrenddessen normal weiter.
 */
static volatile int blf_state = 0;  /* 0 ruhig, 1 runter, 2 halten, 3 hoch */
static float blf_t = 0.0f;
static int blf_limit = -999;       /* was gerade gezeigt wird */
static uint8_t blf_reason = 0;
static int blf_want_limit = -999;
static uint8_t blf_want_reason = 0;


/* Laeuft auf einem eigenen Timer, unabhaengig von der Zeichenschleife. */
/*
 * Ein Timer fuer beides: die Grundhelligkeit laeuft immer, die Blende nur bei
 * FADE_MODE 2. Der Timer selbst ist deshalb nicht an die Blende gebunden -
 * sonst stuende das Abdimmen still, sobald jemand die Blende umstellt.
 */
static void backlightTimer(void *) {
  backlightRamp();
#if FADE_MODE == 2
  if (blf_state == 0 || blf_state == 2) return;
  blf_t += (float)FADE_BL_STEP_MS / (FADE_BL_MS / 2.0f);
  if (blf_state == 1) {
    g_bl_fade = 1.0f - blf_t;
    if (blf_t >= 1.0f) {
      g_bl_fade = 0.0f;
      blf_state = 2;        // halten, bis die Schleife das Bild getauscht hat
      blf_t = 0.0f;
    }
  } else {
    g_bl_fade = blf_t;
    if (blf_t >= 1.0f) {
      g_bl_fade = 1.0f;
      blf_state = 0;
    }
  }
  applyBacklight();
#endif
}

void setup() {
  Serial.begin(115200);
  delay(300);

#ifdef LCD_DIAG
  lcdPinCheck();   // vor der Businitialisierung, solange die Pins frei sind
#endif

  // Volle Treiberstaerke, bevor der Kanal haengt - danach greift es nicht mehr
  gpio_set_drive_capability((gpio_num_t)LCD_BL, GPIO_DRIVE_CAP_3);
  ledcAttach(LCD_BL, LCD_BL_HZ, LCD_BL_BITS);
  bl_cur_p = bl_target_p = bl_perc(LCD_BL_LEVEL);
  applyBacklight();
  Serial.printf("[LCD] Hintergrundlicht GPIO%d = %d/255\n", LCD_BL,
                LCD_BL_LEVEL);

  if (!gfx->begin(LCD_QSPI_HZ)) {
    Serial.println("[LCD] Init fehlgeschlagen - Verkabelung pruefen");
  }
  Serial.printf("[LCD] ST77916 %dx%d, QSPI %d MHz, Initsequenz %s\n", UI_SIZE,
                UI_SIZE, LCD_QSPI_HZ / 1000000,
#ifdef ST77916_INIT_180
                "180"
#else
                "150"
#endif
  );
#ifdef LCD_DIAG
  teCheck();                // nach der Init, TEON steckt in der Sequenz
#endif
  gfx->fillScreen(0x0000);  // schwarz
#ifdef LCD_DIAG
  lcdSelfTest();
#endif

  lv_init();
  lv_display_t *disp = lv_display_create(UI_SIZE, UI_SIZE);
  lv_display_set_flush_cb(disp, lv_flush);
#if LCD_TE_SYNC
  pinMode(LCD_TE, INPUT);
  Serial.printf("[LCD] Bildsynchronisation ueber TE an GPIO%d\n", LCD_TE);
#endif

  /*
   * Zeichenpuffer in voller Bildgroesse.
   *
   * Mit einem Achtelpuffer brauchte allein die weisse Scheibe (288x288)
   * fuenf Durchgaenge und damit fuenf Fluesche. Nur der erste wartet auf TE,
   * die uebrigen vier landen mitten im Bildaufbau des Panels - sichtbar als
   * Blitzer, besonders beim Farbwechsel, wo die ganze Scheibe neu muss.
   *
   * Mit voller Groesse ist jede Aktualisierung ein einziger Transfer: einmal
   * auf TE warten, einmal schreiben, fertig. Kostet 2 x 259 KiB PSRAM, davon
   * sind 8 MiB da.
   */
  /*
   * Zeichenpuffer bevorzugt im internen RAM.
   *
   * Gemessen: derselbe Puffer im PSRAM kostet LVGL rund 740 ns je Pixel, ein
   * Bildzyklus mit voller Scheibe dauerte damit 96 ms - sichtbar als Stocken
   * beim Farbwechsel. Interner DMA-faehiger Speicher ist um ein Vielfaches
   * schneller, dafuer passt nur ein Teilpuffer hinein.
   */
  /*
   * Ein einzelner grosser Puffer statt zweier kleiner.
   *
   * Der Puffer bestimmt, in wieviele Streifen LVGL eine Aenderung zerlegt -
   * und jeder Streifen ist ein eigener Transfer mit eigener TE-Wartung. Bei
   * einem Viertelbild (32.400 px) passte die Blendflaeche (37.400 px) knapp
   * nicht hinein und wurde genau auf der Puffergrenze bei y=180 geteilt: der
   * waagerechte Strich mitten im Bild.
   *
   * Ein halbes Bild fasst sie in einem Stueck. Zwei Puffer dieser Groesse
   * passen nicht ins interne RAM, einer schon - und da der Transfer ohnehin
   * blockierend ist, bringt der zweite nichts.
   */
  size_t buf_px = (size_t)UI_SIZE * UI_SIZE / LCD_BUF_DIV;
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(
      buf_px * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  uint8_t *buf2 = NULL;
  if (buf1) {
    Serial.printf("[LVGL] Zeichenpuffer %u KiB im internen RAM (1/%d Bild)\n",
                  (unsigned)(buf_px * 2 / 1024), LCD_BUF_DIV);
  } else {
    buf_px = (size_t)UI_SIZE * UI_SIZE;
    buf1 = (uint8_t *)ps_malloc(buf_px * 2);
    Serial.println("[LVGL] internes RAM zu knapp - Vollbildpuffer im PSRAM");
  }
  lv_display_set_buffers(disp, buf1, buf2, buf_px * 2,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  ui_create();
  lv_prev_ms = millis();

  // ---------- Karte aus dem Flash ----------
  // formatOnFail bleibt false: ein Fehlschlag darf die hochgeladene Karte
  // nicht formatieren, sonst ist sie beim naechsten Start weg.
  if (!LittleFS.begin(false)) {
    Serial.println("[FS] LittleFS-Mount fehlgeschlagen - schon "
                   "'pio run -t uploadfs' gelaufen?");
  } else {
    Serial.printf("[FS] LittleFS: %u von %u Byte belegt\n",
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)LittleFS.totalBytes());
    // Vor grid.begin(): Aenderungen aus der Weboberflaeche (webupdate.h)
    // uebernehmen, solange noch kein File-Handle auf eine Regionsdatei
    // offen ist. Siehe Begruendung bei PENDING_DIR in config.h.
    applyPendingMapChanges(LittleFS);
    gridReady = grid.begin(LittleFS);
    if (!gridReady) {
      Serial.println("[Grid] Karte nicht verfuegbar - Anzeige bleibt auf ?");
    } else {
      Serial.printf("[Grid] %u Region(en):", grid.regionCount());
      for (uint8_t i = 0; i < grid.regionCount(); i++)
        Serial.printf(" %s", grid.regionName(i));
      Serial.println();
    }
  }

  {
    const esp_timer_create_args_t a = {.callback = backlightTimer,
                                       .arg = NULL,
                                       .dispatch_method = ESP_TIMER_TASK,
                                       .name = "blfade",
                                       .skip_unhandled_events = true};
    esp_timer_handle_t h;
    esp_timer_create(&a, &h);
    esp_timer_start_periodic(h, FADE_BL_STEP_MS * 1000);
    Serial.printf("[LCD] Hintergrundlicht-Timer %d ms, Blendeart %d\n",
                  FADE_BL_STEP_MS, FADE_MODE);
  }

  pinMode(MODE_PIN, INPUT_PULLUP);
  pinMode(DEMO_PIN, INPUT_PULLUP);
  Serial.printf("[Mode] GPIO%d gegen GND = Tacho, GPIO%d = Demo erzwingen\n",
                MODE_PIN, DEMO_PIN);

  // Kartenupdate per Access Point (webupdate.h) - nach den pinMode()-Aufrufen
  // oben, weil webupdateLoop() DEMO_PIN fuer das Wiedereinschalten mitliest.
  webupdateBegin();

  gpsMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(gpsTask, "GPS", 4096, NULL, 1, NULL, 0);

  Serial.printf("[CPU] %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
  Serial.println("[OK] bereit");
}

void loop() {
  uint32_t now = millis();
  uint32_t lv_dt = now - lv_prev_ms;
  lv_tick_inc(lv_dt);
  lv_prev_ms = now;
  ui_tick(lv_dt);       // Balken weiterbewegen, unabhaengig vom Datentakt
  webupdateLoop();      // AP/Weboberflaeche, kostet ausserhalb einer
                        // Wartungssitzung nur einen Pin-Read + millis()

  uint32_t t_refr = micros();
  lv_timer_handler();
  t_refr = micros() - t_refr;
  g_refr_us += t_refr;
  if (t_refr > g_refr_max_us) g_refr_max_us = t_refr;

  static uint32_t last = 0;
  if (now - last >= UI_UPDATE_MS) {
    uint32_t dt = now - last;
    last = now;

    xSemaphoreTake(gpsMutex, portMAX_DELAY);
    ui_state_t snapshot = g_state;
    float course = g_course;
    xSemaphoreGive(gpsMutex);

    // Ohne Fix nach der Wartezeit auf die simulierte Fahrt umschalten. Kommt
    // spaeter doch ein Fix, uebernehmen sofort wieder die echten Daten.
    // -DFORCE_DEMO erzwingt die simulierte Fahrt auch bei gueltigem Fix -
    // sonst laesst sich der Kartenpfad nicht pruefen, sobald GPS liefert.
    // Schalter vor der Entscheidung lesen, beide sind entprellt
    bool demo_forced = readSwitch(sw_demo);
#ifdef FORCE_DEMO
    snapshot.demo = true;
    (void)demo_forced;
#else
    // Schalter, oder kein Fix nach der Wartezeit
    snapshot.demo = demo_forced || (!snapshot.fix && now > GPS_GRACE_MS);
#endif
    static bool demo_last = false;
    if (snapshot.demo != demo_last) {
      demo_last = snapshot.demo;
      Serial.printf("[Demo] %s\n", snapshot.demo
                        ? "eingeschaltet (Schalter oder kein Fix)"
                        : "aus, echte GPS-Daten");
    }
    if (snapshot.demo) {
      static bool announced = false;
      if (!announced) {
        announced = true;
        Serial.printf("[Demo] Kein GPS-Fix, %lu NMEA-Bytes empfangen "
                      "(0 = Modul nicht angeschlossen) - simulierte Fahrt\n",
                      (unsigned long)g_gps_bytes);
      }
      demoStep(dt, &snapshot, &course);
    }

    snapshot.course = course;
    snapshot.speedo = readSwitch(sw_mode);

    if (gridReady && (snapshot.fix || snapshot.demo)) {
      /*
       * Zwei getrennte Vorausschauen:
       *  - Der Lookup laeuft SWITCH_AHEAD_MS weiter vorn, damit das neue
       *    Schild schon steht, wenn man es erreicht (gedeckelt auf
       *    SWITCH_AHEAD_MAX_M, sonst waere es auf der Autobahn zu frueh).
       *  - Zusaetzlich wird PREDICT_AHEAD_MS weit geschaut, um Kandidaten zu
       *    bewerten: eine Querstrasse, die man nur kreuzt, faellt zurueck.
       */
      double qlat = snapshot.lat, qlon = snapshot.lon;
      double alat = 0.0, alon = 0.0;
      if (course >= 0.0f && snapshot.speed_kmh >= COURSE_MIN_KMH) {
        uint32_t ahead = SWITCH_AHEAD_MS;
        float dist = snapshot.speed_kmh / 3.6f * (ahead / 1000.0f);
        if (dist > SWITCH_AHEAD_MAX_M)
          ahead = (uint32_t)(SWITCH_AHEAD_MAX_M * 3600.0f / snapshot.speed_kmh);
        deadReckon(snapshot.lat, snapshot.lon, course, snapshot.speed_kmh,
                   ahead, &qlat, &qlon);
        deadReckon(qlat, qlon, course, snapshot.speed_kmh, PREDICT_AHEAD_MS,
                   &alat, &alon);
      }
      snapshot.limit =
          grid.lookup(qlat, qlon, course, snapshot.speed_kmh,
                      snapshot.hour, snapshot.weekday, snapshot.time_valid,
                      alat, alon, now);
      snapshot.reason = grid.reason();
    } else {
      snapshot.limit = -1;
      snapshot.reason = UI_REASON_NONE;
    }

    int real_limit = snapshot.limit;
    uint8_t real_reason = snapshot.reason;

    /*
     * Ueberschreitung hier entscheiden, damit die Blende sie zusammen mit
     * Limit und Begruendung zurueckhalten kann. Mit Hysterese, sonst loest
     * ein Tempo genau an der Schwelle eine Blende nach der anderen aus.
     */
    static bool over_state = false;
    bool has_ref = (snapshot.limit > 0 && snapshot.limit != 255);
    if (has_ref) {
      float on = snapshot.limit * (1.0f + OVER_TOLERANCE_PCT / 100.0f);
      if (!over_state && snapshot.speed_kmh > on) over_state = true;
      if (over_state && snapshot.speed_kmh < on - OVER_HYSTERESIS_KMH)
        over_state = false;
    } else {
      over_state = false;
    }
    snapshot.over = over_state;
    updateBacklight(snapshot.speed_kmh);
#if FADE_MODE == 2
    real_limit = snapshot.limit;      // fuer die Pruefung im Log
    real_reason = snapshot.reason;
    /*
     * Die Ueberschreitung loest bewusst KEINE Blende aus und wird auch nicht
     * zurueckgehalten: eine Warnung muss in dem Moment erscheinen, in dem sie
     * gilt, nicht eine Viertelsekunde spaeter nach einer Ueberblendung.
     * Zurueckgehalten werden nur Limit und Begruendung.
     */
    if (snapshot.limit != blf_want_limit || snapshot.reason != blf_want_reason) {
      blf_want_limit = snapshot.limit;
      blf_want_reason = snapshot.reason;
      if (blf_limit == -999) {          // erster Durchlauf: sofort zeigen
        blf_limit = snapshot.limit;
        blf_reason = snapshot.reason;
      } else if (blf_state == 0) {
        blf_state = 1;
        blf_t = 0.0f;
      }
    }
    bool at_dark = (blf_state == 2);
    if (at_dark) {                      // im Dunkelpunkt umschalten
      blf_limit = blf_want_limit;
      blf_reason = blf_want_reason;
    }
    snapshot.limit = blf_limit;
    snapshot.reason = blf_reason;
#endif
    ui_update(&snapshot);
#if FADE_MODE == 2
    if (at_dark) {
      // Neuaufbau hier und jetzt erzwingen, solange das Licht aus ist -
      // sonst faehrt es wieder hoch, waehrend noch gezeichnet wird.
      lv_refr_now(NULL);
      blf_state = 3;
      blf_t = 0.0f;
    }
#endif

    // Begruendung im Log mitschreiben, Reihenfolge wie UI_REASON_* in ui.h
    static const char *WHY[8] = {"-", "ZONE", "KINDER", "SPIEL",
                                 "RAD", "SCHILD", "ZEIT", "-"};
    static uint32_t last_log = 0;
    bool do_log = (now - last_log >= LOG_INTERVAL_MS);
    if (do_log) last_log = now;

    if (!do_log) {
      // nichts zu protokollieren - Anzeige ist bereits aktualisiert
    } else if (snapshot.demo) {
      int exp = DEMO[demo_leg].expect;
      uint8_t expw = DEMO[demo_leg].why;
      // Gegen den ermittelten Wert pruefen, nicht gegen den gerade
      // angezeigten - die Blende haelt den Wechsel kurz zurueck.
      /* -2 = haengt an der Uhrzeit: tagsueber 30 mit KINDER, sonst 50 ohne */
      bool ok;
      if (exp == -2) {
        bool day = snapshot.time_valid && snapshot.hour >= 6 && snapshot.hour < 18;
        ok = day ? (real_limit == 30 && real_reason == UI_REASON_KINDER)
                 : (real_limit == 50 && real_reason == UI_REASON_NONE);
      } else {
        ok = (real_limit == exp) &&
             (expw == WHY_EGAL || real_reason == expw);
      }
      Serial.printf("[Demo] %.5f,%.5f k=%3.0f %3.0f km/h  Limit %3d "
                    "(erwartet %3d) %-7s %s  Cache %lu/%lu\n",
                    snapshot.lat, snapshot.lon, course, snapshot.speed_kmh,
                    real_limit, exp, WHY[real_reason & 7],
                    ok ? "ok" : "ABWEICHUNG",
                    (unsigned long)grid.cacheHits(),
                    (unsigned long)grid.cacheReads());
      Serial.printf("[Draw] %lu/s, %lu kPixel/s, Bus %lu%%, LVGL %lu%%, "
"laengster Zyklus %lu us, TE %lu%% in %lu Wartungen, groesster %lu kPixel\n",
                    (unsigned long)g_flushes, (unsigned long)(g_flush_px / 1000),
                    (unsigned long)(g_flush_us / 10000),
                    (unsigned long)(g_refr_us / 10000),
                    (unsigned long)g_refr_max_us,
                    (unsigned long)(g_te_us / 10000), (unsigned long)g_te_waits,
                    (unsigned long)(g_flush_max / 1000));
      g_flushes = 0; g_flush_px = 0; g_flush_us = 0; g_flush_max = 0;
      g_refr_us = 0; g_refr_max_us = 0; g_te_us = 0; g_te_waits = 0;
    } else {
      Serial.printf("[GPS] fix=%d sat=%u %.5f,%.5f %.1f km/h k=%.0f "
                    "%02u:%02u  Limit %d  %lu fps  Bus %lu%%  LVGL %lu%%  "
                    "laengster Zyklus %lu us  BL %d\n",
                    snapshot.fix, snapshot.sats, snapshot.lat, snapshot.lon,
                    snapshot.speed_kmh, course, snapshot.hour, snapshot.minute,
                    snapshot.limit, (unsigned long)g_flushes,
                    (unsigned long)(g_flush_us / 10000),
                    (unsigned long)(g_refr_us / 10000),
                    (unsigned long)g_refr_max_us, g_bl_level);
      g_flush_us = 0; g_flush_max = 0; g_refr_us = 0; g_refr_max_us = 0;
#if LCD_TE_SYNC
      if (g_te_timeouts) {
        Serial.printf("[LCD] TE-Timeouts: %lu - Signal verdrahtet?\n",
                      (unsigned long)g_te_timeouts);
        g_te_timeouts = 0;
      }
#endif
      g_flushes = 0; g_flush_px = 0;
    }
  }

  delay(5);
}
