/*
 * Speed-limit display - ESP32-S3-DevKitC (N16R8) + EstarDyn 1.53" ST77916
 *
 * Includes the same ui.c as the PC simulator. No TCA9554 needed, because
 * the breakout board brings RST and BL out directly.
 *
 * --------------------------------------------------------------------------
 * WIRING  (display pins as labeled on the board)
 * --------------------------------------------------------------------------
 *   Display        ESP32-S3      Note
 *   -----------------------------------------------------------------
 *   GND            GND
 *   VCC            3V3 or 5V     Datasheet allows 3.3-5V; the module has its
 *                                own regulator. Logic levels stay 3.3V. If
 *                                the backlight lights up but the image stays
 *                                black, trying 5V (VIN) is worth it: the
 *                                backlight can come on while the panel's
 *                                bias voltage doesn't reach full level on a
 *                                marginal supply.
 *   SCL            GPIO12        QSPI clock
 *   SDA  (IO0)     GPIO11        QSPI data 0
 *   IO1            GPIO13        QSPI data 1
 *   IO2            GPIO14        QSPI data 2
 *   IO3            GPIO9         QSPI data 3
 *   CS             GPIO10
 *   RST            GPIO8
 *   BL             GPIO7         Backlight, high = on
 *   TE             leave open    (tearing-effect signal, unused here)
 *
 *   NEO-6M         ESP32-S3
 *   -----------------------------------------------------------------
 *   VCC            5V (VIN/5V pin on the DevKit)
 *   GND            GND
 *   TX             GPIO18        GPS transmits -> ESP receives
 *   RX             GPIO17        mostly unused
 *
 * Stay away from GPIO26-37: that's where the N16R8 module's flash and
 * octal PSRAM live. GPIO19/20 are USB, GPIO43/44 the UART bridge.
 *
 * --------------------------------------------------------------------------
 * ARDUINO SETTINGS
 * --------------------------------------------------------------------------
 *   Board:       ESP32S3 Dev Module
 *   PSRAM:       OPI PSRAM            <- mandatory, otherwise no framebuffer
 *   Flash Size:  16MB
 *   Partition:   16M Flash (3MB APP/9.9MB FATFS)
 *   USB CDC On Boot: Enabled          <- otherwise no Serial output
 *
 * Libraries: lvgl 9.x, Arduino_GFX_Library, TinyGPSPlus
 * Files in the sketch folder: this sketch, ui.c, ui.h, speedlimit_grid.h,
 * webupdate.h/.cpp (map update via access point)
 * --------------------------------------------------------------------------
 */

#include <Arduino.h>   // required under PlatformIO, the Arduino IDE adds this automatically
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <TinyGPSPlus.h>
#include <driver/gpio.h>   // gpio_set_drive_capability, for the backlight
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

#define GPS_RX 18   // wired to the NEO-6M's TX
#define GPS_TX 17

// TE (Tearing Effect) is an OUTPUT of the panel. Normally unused, but for
// troubleshooting it's the only feedback channel QSPI offers here.
#define LCD_TE 16

// ---------- Display ----------
/*
 * Arduino_ST77916 defaults to st77916_180_init_operations - the init
 * sequence for a 1.80" panel. Ours is the round 1.53", which needs the 150
 * sequence instead. With the wrong sequence the image stays black even
 * though begin() reports "success": nothing gets read back over QSPI.
 * To test the wrong sequence deliberately: set -DST77916_INIT_180.
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
 * Bring-up diagnostics - only active with -DLCD_DIAG in the build.
 * Costs roughly 5s of startup time, hence off in normal operation:
 *     PLATFORMIO_BUILD_FLAGS=-DLCD_DIAG pio run -e esp32s3 -t upload
 * ---------------------------------------------------------------------------
 */
#ifdef LCD_DIAG
/*
 * lcdPinCheck() - wiring self-test without a multimeter, meant to run
 * before the QSPI bus is initialized while all pins are still free GPIOs.
 *
 * No parameters.
 *
 * Background: Arduino_ESP32QSPI sends with SPI_TRANS_MULTILINE_CMD and
 * SPI_TRANS_MULTILINE_ADDR - so command and address bytes also go out over
 * all four data lines, not just pixel data. A single bad connection on
 * IO1/IO2/IO3 therefore takes down all communication: the panel stays lit
 * but black, because not even DISPON arrives.
 *
 * The test finds shorts to GND, to 3V3, and between two pins. What it
 * CANNOT find is a broken/open line - an open line and a clean line into a
 * high-impedance panel input look identical from this side.
 */
static void lcdPinCheck() {
  static const struct {
    int gpio;
    const char *name;
  } P[] = {{LCD_SCK, "SCL/SCK"}, {LCD_D0, "SDA/IO0"}, {LCD_D1, "IO1"},
           {LCD_D2, "IO2"},      {LCD_D3, "IO3"},     {LCD_CS, "CS"},
           {LCD_RST, "RST"},     {LCD_BL, "BL"}};
  const int N = sizeof(P) / sizeof(P[0]);

  Serial.println("[Pin] --- wiring test ---");
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
      // Normal for the backlight: the driver on the module pulls the pin
      // toward GND harder than the internal pullup pulls it toward 3V3.
      v = (P[i].gpio == LCD_BL) ? "stuck at GND (normal for BL)"
                                : "<-- stuck at GND";
    } else if (up == 1 && dn == 1) {
      v = "<-- stuck at 3V3";
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
      // A pin that's permanently stuck low always reads 0 and would
      // otherwise be falsely reported as shorted against every driven pin.
      if (j == i || stuck[j]) continue;
      if (digitalRead(P[j].gpio) == 0) {
        Serial.printf("[Pin] SHORT GPIO%d (%s) <-> GPIO%d (%s)\n",
                      P[i].gpio, P[i].name, P[j].gpio, P[j].name);
        shorts++;
      }
    }
    pinMode(P[i].gpio, INPUT);
  }
  Serial.printf("[Pin] %d short(s) found\n", shorts);
}

/*
 * teISR() - interrupt handler counting edges on the TE probe/test pin.
 *
 * No parameters. Bumps the shared te_edges counter; used both by the probe
 * self-check and the real TE measurement in teCheck().
 */
static volatile uint32_t te_edges = 0;
static void IRAM_ATTR teISR() { te_edges++; }

/*
 * teCheck() - panel life sign via the TE (tearing-effect) output.
 *
 * No parameters.
 *
 * QSPI offers no usable feedback channel - which is why gfx->begin() reports
 * success even with no panel attached at all. TE is the exception: an
 * output of the panel that the 150 init sequence enables with command 0x35.
 * It pulses at the refresh rate.
 *
 *   edges > 0  ->  commands are arriving, the panel is running. The fault
 *                  then lies past initialization (pixel path, address
 *                  window, color format).
 *   edges = 0  ->  not a single command reaches the panel.
 *
 * Precondition: the module's TE must be wired to LCD_TE. Without a wire the
 * pin stays quiet thanks to the pulldown and also reports 0 - hence the
 * idle level is reported alongside, to distinguish "not wired" from
 * "silent".
 */
static void teCheck() {
  // 1. First verify the measurement chain itself. A result of 0 edges is
  //    worthless unless it's established that counting works at all. This
  //    runs on a free pin - on LCD_TE it would drive against the panel's
  //    own output.
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
  Serial.printf("[TE] measurement chain: %lu of 50 edges detected%s\n",
                (unsigned long)te_edges,
                te_edges >= 45 ? "" : "  <-- count unreliable!");

  // 2. Measure TE passively, both pull directions. If TE were an
  //    open-drain output, it would stay permanently low with a pulldown and
  //    the test would be blind.
  for (int mode = 0; mode < 2; mode++) {
    pinMode(LCD_TE, mode ? INPUT_PULLUP : INPUT_PULLDOWN);
    delay(5);
    int idle = digitalRead(LCD_TE);
    te_edges = 0;
    attachInterrupt(digitalPinToInterrupt(LCD_TE), teISR, CHANGE);
    delay(500);
    detachInterrupt(digitalPinToInterrupt(LCD_TE));
    Serial.printf("[TE] GPIO%d %-8s: %lu transitions/0.5s, idle level %d\n",
                  LCD_TE, mode ? "Pullup" : "Pulldown",
                  (unsigned long)te_edges, idle);
  }
}

/*
 * lcdSelfTest() - flash full-screen color fields before LVGL starts.
 *
 * No parameters. If the panel shows these, the problem is in the software
 * layer above; if it stays black, the fault is in the panel, wiring, or
 * backlight.
 */
static void lcdSelfTest() {
  static const struct {
    uint16_t rgb565;
    const char *name;
  } STEPS[] = {{0xF800, "red"}, {0x07E0, "green"},
               {0x001F, "blue"}, {0xFFFF, "white"}};
  for (auto &s : STEPS) {
    Serial.printf("[LCD] test pattern %s\n", s.name);
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
 * waitForTE() - wait for the start of the vertical blanking interval.
 *
 * No parameters. TE is a panel output pulsing at the refresh rate; starting
 * a write right after that edge guarantees the write finishes before the
 * panel's own scan-out reaches the area just written.
 *
 * Waits for low first, then for the rising edge - otherwise an
 * already-high level would be misread as an edge immediately. The timeout
 * ensures a missing TE signal never hangs the display.
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

/*
 * lv_flush(disp, area, px) - LVGL flush callback: push one dirty rectangle
 * to the panel.
 *
 * Parameters:
 *   disp - the LVGL display object; used to signal flush completion
 *   area - pixel rectangle that changed
 *   px   - RGB565 pixel data for that rectangle
 *
 * Waits for TE (if LCD_TE_SYNC), issues the blit via draw16bitRGBBitmap(),
 * and tracks timing/pixel-count statistics used by the periodic log line.
 */
static void lv_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
#if LCD_TE_SYNC
  /*
   * Wait for the blanking interval before EVERY partial area, not just the
   * first one of a frame cycle.
   *
   * With only one wait per cycle, all further partial areas landed mid
   * scan-out - visible as a horizontal line right on the buffer boundary at
   * y=180. Each partial area costs one frame period this way; with two
   * stripes that's 33ms instead of 17ms. For a 1000ms fade that's still
   * around 30 steps.
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

// ---------- GPS on core 0 ----------
TinyGPSPlus gps;
HardwareSerial GPSSerial(1);
static SemaphoreHandle_t gpsMutex;
// Order must follow the declaration in ui.h - C++ is stricter about this
// than C
static ui_state_t g_state = {.limit = -1, .fix = false, .sats = 0};
// Course doesn't belong in ui_state_t (the display doesn't show it), but is
// needed for the lookup's direction filter. <0 = unknown.
static float g_course = -1.0f;
// Counts received NMEA bytes: 0 after the wait period = no module attached
static uint32_t g_gps_bytes = 0;

// ---------- Map lookup ----------
static SpeedLimitGrid grid;
static bool gridReady = false;

/*
 * dayOfWeek(y, m, d) - Zeller-congruence-style weekday for a Gregorian
 * date.
 *
 * Parameters:
 *   y - year (full, e.g. 2026)
 *   m - month, 1-12
 *   d - day of month
 *
 * Returns 0 = Sunday .. 6 = Saturday. Used only by isSummerTime() to find
 * the last Sunday of March/October for the DST switch-over.
 */
static int dayOfWeek(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

/*
 * isSummerTime(y, mo, d, h) - is CEST (not CET) in effect at this moment?
 *
 * Parameters:
 *   y  - year
 *   mo - month, 1-12
 *   d  - day of month
 *   h  - hour, 0-23
 *
 * EU rule: DST runs from the last Sunday in March 01:00 UTC to the last
 * Sunday in October 01:00 UTC. Outside March/October it's a plain yes/no;
 * within those two months, the switch-over day itself is resolved by
 * comparing against the last Sunday.
 */
static bool isSummerTime(int y, int mo, int d, int h) {
  if (mo < 3 || mo > 10) return false;
  if (mo > 3 && mo < 10) return true;
  int s = 31 - dayOfWeek(y, mo, 31);   // last Sunday of the month
  if (mo == 3) return d > s ? true : (d < s ? false : h >= 1);
  return d < s ? true : (d > s ? false : h < 1);
}

/*
 * Pin AND baud-rate autodetection.
 *
 * Two mistakes are common by default: RX/TX swapped (the ESP must connect
 * to the module's TX) and a baud rate other than the original's 9600 -
 * NEO-6M clones also show up at 38400 or 115200.
 *
 * "Working" is not defined as "bytes are arriving" but as "TinyGPS accepts
 * a sentence with a valid checksum". At the wrong baud rate, bytes do
 * arrive - they're just garbage.
 */
static const uint32_t GPS_BAUDS[] = {9600, 38400, 115200, 4800};
#define N_GPS_BAUDS (sizeof(GPS_BAUDS) / sizeof(GPS_BAUDS[0]))

/*
 * ubxSend(cls, id, payload, len) - build and transmit one UBX protocol
 * frame.
 *
 * Parameters:
 *   cls     - UBX message class (e.g. 0x06 = CFG)
 *   id      - UBX message ID within the class
 *   payload - message body, may be NULL if len == 0
 *   len     - payload length in bytes
 *
 * Frames as 0xB5 0x62 <cls> <id> <len_lo> <len_hi> <payload...> <ck_a>
 * <ck_b>, with the 8-bit Fletcher checksum computed over everything from
 * class onward (not the 0xB5 0x62 sync bytes).
 */
static void ubxSend(uint8_t cls, uint8_t id, const uint8_t *payload,
                    uint16_t len) {
  uint8_t head[6] = {0xB5, 0x62, cls, id, (uint8_t)(len & 0xFF),
                     (uint8_t)(len >> 8)};
  uint8_t a = 0, b = 0;
  for (int i = 2; i < 6; i++) {   // checksum starts at class, excluding 0xB5 0x62
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

/*
 * gpsConfigure() - switch the NEO-6M to 5Hz via the UBX protocol.
 *
 * No parameters. Two steps, and the first one is not optional: at 9600
 * baud the full NMEA sentence set doesn't fit five times a second. 9600 8N1
 * is 960 byte/s, one complete sentence block is roughly 380 bytes - at 5Hz
 * that's 1900 byte/s. So everything TinyGPS doesn't need gets disabled
 * first (it only needs RMC and GGA), then the rate goes up. What's left is
 * roughly 730 byte/s.
 *
 * This setting lives in the module's battery-backed RAM and is lost
 * without backup power when the supply is disconnected - it is therefore
 * resent on every successful sync, not just once at startup.
 */
static void gpsConfigure() {
  // NMEA class 0xF0: 00=GGA 01=GLL 02=GSA 03=GSV 04=RMC 05=VTG 08=ZDA
  static const uint8_t QUIET[] = {0x01, 0x02, 0x03, 0x05, 0x08};
  for (uint8_t id : QUIET) {
    const uint8_t msg[3] = {0xF0, id, 0x00};   // rate 0 = off
    ubxSend(0x06, 0x01, msg, 3);               // CFG-MSG
    delay(UBX_CMD_DELAY_MS);
  }
  // CFG-RATE: 200ms measurement interval, navRate 1, time reference GPS
  const uint8_t rate[6] = {0xC8, 0x00, 0x01, 0x00, 0x01, 0x00};
  ubxSend(0x06, 0x08, rate, 6);
  Serial.println("[GPS] GSV/GSA/GLL/VTG/ZDA off, rate set to 5Hz");
}

/*
 * gpsTask(pv) - FreeRTOS task on core 0: read raw NMEA from the GPS UART,
 * try baud rates and swapped RX/TX pins until real sentences show up, then
 * feed TinyGPS and publish the results into g_state/g_course under
 * gpsMutex.
 *
 * Parameters:
 *   pv - unused (required by the FreeRTOS task signature)
 *
 * Runs forever, never returns. See the comment above GPS_BAUDS for the
 * autodetection strategy. Also prints a 5s statistics line that
 * distinguishes "nothing arriving" from "arriving but no checksum-valid
 * sentences yet" from "sentences valid but no GPS fix" - see the GPS
 * troubleshooting table in CLAUDE.md for how to read it.
 */
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
        Serial.printf("[GPS] valid NMEA sentences on RX=GPIO%d, %lu baud\n",
                      rx, (unsigned long)GPS_BAUDS[bi]);
        gpsConfigure();
      } else if (millis() - probe_start > GPS_PROBE_MS) {
        // Try all baud rates first, then start over with swapped pins
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
        Serial.printf("[GPS] nothing valid - trying RX=GPIO%d, %lu baud\n",
                      rx, (unsigned long)GPS_BAUDS[bi]);
      }
    }

    // Periodic statistics: separates "no reception" from "reception without
    // a fix". The sentence rate also shows whether the 5Hz switch-over took
    // effect: with RMC+GGA, 10 sentences/s are expected, previously 2.
    if (millis() - last_stat > GPS_STAT_INTERVAL_MS) {
      uint32_t dt = millis() - last_stat;
      last_stat = millis();
      uint32_t ok = gps.passedChecksum();
      Serial.printf(
          "[GPS] bytes=%lu ok=%lu (%.1f sentences/s) checksum errors=%lu "
          "sat=%d\n",
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
    vTaskDelay(pdMS_TO_TICKS(GPS_TASK_POLL_MS));
  }
}

// ---------- Simulated drive, when no GPS is available ----------
/*
 * The waypoints lie on real streets from tools/out-berlin. This way the
 * demo doesn't just exercise the display - it exercises the whole path:
 * LittleFS -> cell block -> map matching. The expected limits come from a
 * re-implementation of the lookup run on the PC, and are logged to Serial
 * for comparison. If "Limit" diverges from "expected", something is wrong
 * on the device.
 *
 * Why bother at all: at a desk, the NEO-6M never gets a fix. Without these
 * legs, the map couldn't be tested indoors at all.
 */
typedef struct {
  float lat, lon;
} demo_pt_t;

typedef struct {
  const demo_pt_t *pts;
  uint8_t n;
  float speed_kmh;   // speed driven on this leg
  int expect;        // expected limit (255 = frei/unrestricted, -1 = no data)
  uint8_t why;       // expected reason, 0xFF = don't care
  const char *note;
} demo_leg_t;

#define WHY_ANY 0xFF

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
static const demo_pt_t DP_UNRESTRICTED[] = {
    {52.626942f, 13.480943f}, {52.627301f, 13.479750f}, {52.627730f, 13.478057f},
    {52.627997f, 13.476792f}, {52.628209f, 13.475561f}, {52.628466f, 13.473928f}};
// Berlin streets with a stored reason - exercise the display below the
// digit and bits 0-2 of the flags byte.
static const demo_pt_t DP_ZONE[] = {
    {52.505100f, 13.488393f}, {52.504060f, 13.486349f}, {52.503608f, 13.485474f},
    {52.502304f, 13.492173f}, {52.502340f, 13.492376f}, {52.505148f, 13.494007f}};
static const demo_pt_t DP_CHILDREN[] = {
    {52.466728f, 13.507763f}, {52.466800f, 13.508015f}, {52.466932f, 13.508876f},
    {52.466820f, 13.508883f}, {52.466408f, 13.509058f}, {52.465992f, 13.509212f}};
static const demo_pt_t DP_PLAY_STREET[] = {
    {52.493348f, 13.420258f}, {52.493808f, 13.418508f}, {52.494468f, 13.418844f},
    {52.495828f, 13.419404f}, {52.495672f, 13.417983f}, {52.494944f, 13.414546f}};
static const demo_pt_t DP_BICYCLE[] = {
    {52.560000f, 13.325726f}, {52.558828f, 13.325740f}, {52.556548f, 13.325313f},
    {52.554736f, 13.324655f}, {52.550592f, 13.323220f}, {52.550112f, 13.322947f}};

/*
 * Time-limited limit: 50, but 30 from 6am to 6pm because of children.
 * Verifies that the reason label only appears while the condition is
 * active - at night this must show "50" with no label, during the day
 * "30 KINDER". The expectation depends on the time of day, hence WHY_ANY
 * and expect = -2 meaning "depends on time of day" (see the evaluation in
 * the log).
 */
static const demo_pt_t DP_TIME_LIMITED[] = {
    {52.431736f, 13.229898f}, {52.433076f, 13.229765f}, {52.433856f, 13.229828f},
    {52.434564f, 13.229996f}, {52.435216f, 13.230262f}, {52.435820f, 13.230619f}};

// Southern Brandenburg near Cottbus - lies outside Berlin's bounding box,
// specifically to verify that the second region is found and read.
static const demo_pt_t DP_BB[] = {
    {51.877376f, 14.552160f}, {51.884296f, 14.566069f}, {51.884828f, 14.567525f},
    {51.885668f, 14.570528f}, {51.887688f, 14.574896f}, {51.889532f, 14.579306f}};
// Outside all regions: empty cells -> question mark
static const demo_pt_t DP_NO_DATA[] = {{52.200000f, 13.000000f},
                                    {52.205000f, 13.005000f}};

#define DL(arr) arr, (uint8_t)(sizeof(arr) / sizeof(arr[0]))
static const demo_leg_t DEMO[] = {
    {DL(DP_30), 45.0f, 30, WHY_ANY, "30 zone, speeding"},
    {DL(DP_ZONE), 28.0f, 30, UI_REASON_ZONE, "30 zone (Tempo-30-Zone)"},
    {DL(DP_CHILDREN), 25.0f, 30, UI_REASON_CHILDREN, "children / school"},
    {DL(DP_PLAY_STREET), 6.0f, 7, UI_REASON_PLAY_STREET, "play street"},
    {DL(DP_BICYCLE), 22.0f, 30, UI_REASON_BICYCLE_STREET, "bicycle street"},
    {DL(DP_TIME_LIMITED), 40.0f, -2, WHY_ANY, "time-limited (6am-6pm)"},
    {DL(DP_50), 42.0f, 50, WHY_ANY, "urban through-road"},
    {DL(DP_60), 58.0f, 60, WHY_ANY, "main road"},
    {DL(DP_80), 80.0f, 80, WHY_ANY, "arterial road, bar full"},
    {DL(DP_100), 75.0f, 100, WHY_ANY, "expressway"},
    // 145 instead of 130: with OVER_TOLERANCE_PCT=10 the threshold sits at 132
    {DL(DP_120), 145.0f, 120, WHY_ANY, "motorway, speeding"},
    {DL(DP_UNRESTRICTED), 160.0f, 255, WHY_ANY, "unrestricted -> frei"},
    {DL(DP_BB), 95.0f, 100, WHY_ANY, "Brandenburg, second region"},
    {DL(DP_NO_DATA), 90.0f, -1, WHY_ANY, "no map data -> ?"},
};
#define N_DEMO (sizeof(DEMO) / sizeof(DEMO[0]))



static size_t demo_leg = 0;
static float demo_pos_m = 0.0f;
static uint32_t demo_leg_ms = 0;

/*
 * demoSegLen(a, b) - straight-line distance between two demo waypoints, in
 * meters.
 *
 * Parameters:
 *   a - start point (lat/lon, degrees)
 *   b - end point (lat/lon, degrees)
 *
 * Flat-earth approximation using EARTH_M_PER_DEG_LAT for latitude and a
 * cosine-corrected scale for longitude - accurate enough over the
 * few-hundred-meter legs used by the demo route.
 */
static float demoSegLen(const demo_pt_t &a, const demo_pt_t &b) {
  float mlat = EARTH_M_PER_DEG_LAT;
  float mlon = EARTH_M_PER_DEG_LAT * cosf(a.lat * (float)DEG_TO_RAD);
  float dy = (b.lat - a.lat) * mlat;
  float dx = (b.lon - a.lon) * mlon;
  return sqrtf(dx * dx + dy * dy);
}

/*
 * demoStep(dt_ms, st, course) - advance the simulated drive by dt_ms and
 * write the resulting position/speed/course into the caller's state.
 *
 * Parameters:
 *   dt_ms  - milliseconds elapsed since the previous call
 *   st     - ui_state_t to update (lat/lon/speed_kmh are written)
 *   course - output: heading in degrees, computed from the current segment
 *
 * Walks the current DEMO[] leg's polyline at leg.speed_kmh, advances to the
 * next leg after DEMO_LEG_MS or once the polyline is exhausted, and
 * interpolates position/heading linearly within the current segment.
 */
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
    // the last segment catches anything left over from rounding
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
  // Speed wobble, see DEMO_SPEED_WOBBLE_PCT/DEMO_SPEED_WOBBLE_PERIOD_MS in
  // config.h for why this exists.
  st->speed_kmh = leg.speed_kmh *
                  (1.0f + DEMO_SPEED_WOBBLE_PCT *
                              sinf((float)millis() / DEMO_SPEED_WOBBLE_PERIOD_MS));
}

/*
 * Switch - debounced switch to ground. State lives in the passed-in struct
 * so multiple switches can share the same debounce code.
 */
struct Switch {
  int pin;
  bool stable = false;
  bool last_raw = false;
  uint32_t since = 0;
};

/*
 * readSwitch(sw) - read one debounced switch, updating its stored state.
 *
 * Parameters:
 *   sw - the Switch instance to read/update (by reference)
 *
 * Returns the debounced (stable) state: true = pulled to GND. A raw
 * reading only becomes "stable" once it has held steady for
 * MODE_DEBOUNCE_MS - this must be called regularly (every loop iteration)
 * for the debounce timer to advance.
 */
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

static Switch sw_mode{MODE_PIN};   // open = speed-limit mode, to GND = speedometer
static Switch sw_demo{DEMO_PIN};   // to GND = force simulated drive

/*
 * Backlight with a smooth crossfade. Two thresholds (DIM_BELOW_KMH /
 * DIM_ABOVE_KMH) so it doesn't jump back and forth every second at a red
 * light.
 */
/*
 * Backlight.
 *
 * Two effects are layered on top of each other: the base brightness (full
 * while driving, dimmed when stationary) and the crossfade during a
 * content swap. Both are computed in *perceived* brightness, not raw PWM
 * value - the eye reads brightness roughly as the square root of power, so
 * a linearly driven PWM barely drops at the top and falls off a cliff at
 * the bottom. That's exactly what looked like flicker while dimming down.
 *
 * The ramp runs on the same timer as the crossfade, not in the main loop:
 * the main loop blocks for up to 90ms during a redraw, and a ramp with
 * gaps like that would visibly stutter.
 */
static float bl_target_p = 1.0f;   /* target, perceived 0..1   */
static float bl_cur_p = 1.0f;      /* current, perceived 0..1  */
static float g_bl_fade = 1.0f;     /* fade 0..1, 1 = normal     */
static int g_bl_level = LCD_BL_LEVEL;   /* last PWM value set, for logging only */

/*
 * bl_perc(duty) - convert a raw PWM duty cycle (0-255) to perceived
 * brightness (0..1).
 *
 * Parameters:
 *   duty - PWM duty cycle, 0-255
 *
 * Inverse-gamma mapping (see FADE_BL_GAMMA in config.h): perceived
 * brightness is roughly power^(1/gamma).
 */
static inline float bl_perc(int duty) {   /* PWM -> perceived */
  return powf((float)duty / 255.0f, 1.0f / FADE_BL_GAMMA);
}

/*
 * applyBacklight() - combine the base-brightness ramp and the fade factor
 * into one PWM value and write it out.
 *
 * No parameters. Reads bl_cur_p (base ramp position) and g_bl_fade (0..1
 * crossfade factor), multiplies them in perceived-brightness space, then
 * converts back to a 0-255 PWM duty with the gamma curve before calling
 * ledcWrite().
 */
static void applyBacklight() {
  float p = bl_cur_p * g_bl_fade;
  if (p < 0.0f) p = 0.0f;
  int v = (int)(255.0f * powf(p, FADE_BL_GAMMA) + 0.5f);
  if (v > 255) v = 255;
  g_bl_level = v;
  ledcWrite(LCD_BL, v);
}

/*
 * backlightRamp() - advance the base-brightness ramp by one timer step.
 *
 * No parameters. Belongs on the timer (see the backlight comment above),
 * not the main loop. Steps bl_cur_p toward bl_target_p in
 * perceived-brightness space at a rate of FADE_BL_STEP_MS / DIM_FADE_MS
 * per call, and applies the result.
 */
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
 * updateBacklight(speed_kmh) - decide the base-brightness target from the
 * current speed.
 *
 * Parameters:
 *   speed_kmh - current driven speed
 *
 * Only decides the target; backlightRamp() (on the timer) does the actual
 * moving. Two thresholds (DIM_BELOW_KMH / DIM_ABOVE_KMH) so it doesn't
 * flip back and forth at a red light.
 */
static void updateBacklight(float speed_kmh) {
  static bool dimmed = false;
  if (!dimmed && speed_kmh < DIM_BELOW_KMH) dimmed = true;
  if (dimmed && speed_kmh > DIM_ABOVE_KMH) dimmed = false;
  bl_target_p = dimmed ? bl_perc(LCD_BL_DIM_LEVEL) : bl_perc(LCD_BL_LEVEL);
}

/*
 * deadReckon(lat, lon, course_deg, kmh, ms, out_lat, out_lon) - dead
 * reckoning: where will you be in `ms` milliseconds if course and speed
 * stay unchanged?
 *
 * Parameters:
 *   lat, lon         - current position, degrees
 *   course_deg       - current heading, degrees (0 = north, clockwise)
 *   kmh              - current speed
 *   ms               - time to project forward, milliseconds
 *   out_lat, out_lon - output position, degrees
 *
 * Flat-earth approximation, entirely adequate over the few-hundred-meter
 * distances used here (map-matching lookahead).
 */
static void deadReckon(double lat, double lon, float course_deg, float kmh,
                       uint32_t ms, double *out_lat, double *out_lon) {
  float d = kmh / 3.6f * (ms / 1000.0f);            // distance in meters
  float rad = course_deg * (float)DEG_TO_RAD;
  double dlat = (d * cosf(rad)) / EARTH_M_PER_DEG_LAT;
  double dlon = (d * sinf(rad)) / (EARTH_M_PER_DEG_LAT * cos(lat * DEG_TO_RAD));
  *out_lat = lat + dlat;
  *out_lon = lon + dlon;
}

/*
 * Fade via the backlight.
 *
 * A change to limit or reason is held back: the light ramps down first,
 * the image is swapped while dark, then the light ramps back up. The image
 * swap itself still costs a redraw exactly as before - nobody sees it,
 * because it happens in the dark.
 *
 * Everything except limit and reason (speed, fill bar, status line) keeps
 * running normally throughout.
 */
static volatile int blf_state = 0;  /* 0 idle, 1 falling, 2 holding, 3 rising */
static float blf_t = 0.0f;
static int blf_limit = -999;       /* what's currently displayed */
static uint8_t blf_reason = 0;
static int blf_want_limit = -999;
static uint8_t blf_want_reason = 0;


/*
 * backlightTimer(pv) - esp_timer periodic callback (FADE_BL_STEP_MS):
 * drives both the base-brightness ramp and, in FADE_MODE 2, the
 * fade-to-dark state machine.
 *
 * Parameters:
 *   pv - unused (esp_timer callback signature)
 *
 * Runs on its own timer, independent of the drawing loop. One timer for
 * both ramps: the base brightness always runs, the fade only under
 * FADE_MODE 2 - the timer itself is therefore not gated on the fade mode,
 * otherwise dimming would stop the moment someone switches FADE_MODE.
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
      blf_state = 2;        // hold, until the loop has swapped the image
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

/*
 * setup() - Arduino entry point, runs once at boot.
 *
 * No parameters (Arduino framework calls it with none). Brings up Serial,
 * the backlight PWM channel, the QSPI display + LVGL, mounts LittleFS and
 * loads the speed-limit grid, starts the backlight fade timer, configures
 * the mode/demo switch pins, starts the map-update access point, and
 * spawns gpsTask() pinned to core 0.
 */
void setup() {
  Serial.begin(115200);
  delay(300);

#ifdef LCD_DIAG
  lcdPinCheck();   // before bus initialization, while the pins are still free
#endif

  // Full drive strength before the channel is attached - afterward it has
  // no effect
  gpio_set_drive_capability((gpio_num_t)LCD_BL, GPIO_DRIVE_CAP_3);
  ledcAttach(LCD_BL, LCD_BL_HZ, LCD_BL_BITS);
  bl_cur_p = bl_target_p = bl_perc(LCD_BL_LEVEL);
  applyBacklight();
  Serial.printf("[LCD] backlight GPIO%d = %d/255\n", LCD_BL,
                LCD_BL_LEVEL);

  if (!gfx->begin(LCD_QSPI_HZ)) {
    Serial.println("[LCD] init failed - check wiring");
  }
  Serial.printf("[LCD] ST77916 %dx%d, QSPI %d MHz, init sequence %s\n", UI_SIZE,
                UI_SIZE, LCD_QSPI_HZ / 1000000,
#ifdef ST77916_INIT_180
                "180"
#else
                "150"
#endif
  );
#ifdef LCD_DIAG
  teCheck();                // after init, TEON is part of the sequence
#endif
  gfx->fillScreen(0x0000);  // black
#ifdef LCD_DIAG
  lcdSelfTest();
#endif

  lv_init();
  lv_display_t *disp = lv_display_create(UI_SIZE, UI_SIZE);
  lv_display_set_flush_cb(disp, lv_flush);
#if LCD_TE_SYNC
  pinMode(LCD_TE, INPUT);
  Serial.printf("[LCD] frame sync via TE on GPIO%d\n", LCD_TE);
#endif

  /*
   * Draw buffer at full frame size.
   *
   * With an eighth-frame buffer, the white disc alone (288x288) needed
   * five passes and therefore five flushes. Only the first one waits for
   * TE; the other four land mid scan-out on the panel - visible as
   * flicker, especially on a color change, where the whole disc has to
   * redraw.
   *
   * At full size, every update is a single transfer: wait for TE once,
   * write once, done. Costs 2 x 259 KiB of PSRAM, out of the 8 MiB
   * available.
   */
  /*
   * Draw buffer preferably in internal RAM.
   *
   * Measured: the same buffer in PSRAM costs LVGL roughly 740ns per pixel,
   * making a full-disc frame cycle take 96ms - visible as a stutter on
   * color changes. Internal DMA-capable memory is several times faster,
   * but only a partial buffer fits there.
   */
  /*
   * One single large buffer instead of two smaller ones.
   *
   * The buffer size determines how many stripes LVGL splits a change
   * into - and each stripe is its own transfer with its own TE wait. With
   * a quarter-frame buffer (32,400px), the fade area (37,400px) narrowly
   * didn't fit and got split right on the buffer boundary at y=180: the
   * horizontal line in the middle of the image.
   *
   * A half-frame buffer fits it in one piece. Two buffers of that size
   * don't fit in internal RAM, but one does - and since the transfer is
   * blocking anyway, a second buffer wouldn't help.
   */
  size_t buf_px = (size_t)UI_SIZE * UI_SIZE / LCD_BUF_DIV;
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(
      buf_px * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  uint8_t *buf2 = NULL;
  if (buf1) {
    Serial.printf("[LVGL] draw buffer %u KiB in internal RAM (1/%d frame)\n",
                  (unsigned)(buf_px * 2 / 1024), LCD_BUF_DIV);
  } else {
    buf_px = (size_t)UI_SIZE * UI_SIZE;
    buf1 = (uint8_t *)ps_malloc(buf_px * 2);
    Serial.println("[LVGL] internal RAM too tight - full-frame buffer in PSRAM");
  }
  lv_display_set_buffers(disp, buf1, buf2, buf_px * 2,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  ui_create();
  lv_prev_ms = millis();

  // ---------- Map from flash ----------
  // formatOnFail stays false: a mount failure must not format the
  // uploaded map, otherwise it's gone on the next boot.
  if (!LittleFS.begin(false)) {
    Serial.println("[FS] LittleFS mount failed - has "
                   "'pio run -t uploadfs' been run yet?");
  } else {
    Serial.printf("[FS] LittleFS: %u of %u bytes used\n",
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)LittleFS.totalBytes());
    // Before grid.begin(): apply changes from the web UI (webupdate.h)
    // while no file handle on a region file is open yet. See the rationale
    // for PENDING_DIR in config.h.
    applyPendingMapChanges(LittleFS);
    gridReady = grid.begin(LittleFS);
    if (!gridReady) {
      Serial.println("[Grid] map unavailable - display stays at ?");
    } else {
      Serial.printf("[Grid] %u region(s):", grid.regionCount());
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
    Serial.printf("[LCD] backlight timer %d ms, fade mode %d\n",
                  FADE_BL_STEP_MS, FADE_MODE);
  }

  pinMode(MODE_PIN, INPUT_PULLUP);
  pinMode(DEMO_PIN, INPUT_PULLUP);
  Serial.printf("[Mode] GPIO%d to GND = speedometer, GPIO%d = force demo\n",
                MODE_PIN, DEMO_PIN);

  // Map update via access point (webupdate.h) - after the pinMode() calls
  // above, because webupdateLoop() also reads DEMO_PIN to re-enable the AP.
  webupdateBegin();

  gpsMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(gpsTask, "GPS", 4096, NULL, 1, NULL, 0);

  Serial.printf("[CPU] %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
  Serial.println("[OK] ready");
}

/*
 * loop() - Arduino main loop, runs continuously on core 1.
 *
 * No parameters. Drives the LVGL tick and timer handler on every
 * iteration (must happen every iteration regardless of the data rate),
 * then at UI_UPDATE_MS intervals takes a snapshot of g_state/g_course,
 * resolves demo vs. real GPS, runs the map lookup with predictive
 * lookahead, applies the backlight-fade hold-back for limit/reason
 * changes, calls ui_update(), and writes the periodic Serial log line.
 */
void loop() {
  uint32_t now = millis();
  uint32_t lv_dt = now - lv_prev_ms;
  lv_tick_inc(lv_dt);
  lv_prev_ms = now;
  ui_tick(lv_dt);       // keep the fill bar moving, independent of the data rate
  webupdateLoop();      // AP/web UI - outside an active maintenance session
                        // this costs only a pin read + millis()

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

    // Switch to the simulated drive if there's still no fix after the grace
    // period. If a fix arrives later, immediately switch back to real data.
    // -DFORCE_DEMO forces the simulated drive even with a valid fix -
    // otherwise the map path couldn't be tested once GPS starts providing
    // data.
    // Read the switch before deciding; both switches are debounced.
    bool demo_forced = readSwitch(sw_demo);
#ifdef FORCE_DEMO
    snapshot.demo = true;
    (void)demo_forced;
#else
    // switch, or no fix after the grace period
    snapshot.demo = demo_forced || (!snapshot.fix && now > GPS_GRACE_MS);
#endif
    static bool demo_last = false;
    if (snapshot.demo != demo_last) {
      demo_last = snapshot.demo;
      Serial.printf("[Demo] %s\n", snapshot.demo
                        ? "on (switch or no fix)"
                        : "off, real GPS data");
    }
    if (snapshot.demo) {
      static bool announced = false;
      if (!announced) {
        announced = true;
        Serial.printf("[Demo] No GPS fix, %lu NMEA bytes received "
                      "(0 = module not connected) - simulated drive\n",
                      (unsigned long)g_gps_bytes);
      }
      demoStep(dt, &snapshot, &course);
    }

    snapshot.course = course;
    snapshot.speedo = readSwitch(sw_mode);

    if (gridReady && (snapshot.fix || snapshot.demo)) {
      /*
       * Two separate lookaheads:
       *  - The lookup runs SWITCH_AHEAD_MS further ahead, so the new sign
       *    is already in place by the time you reach it (capped at
       *    SWITCH_AHEAD_MAX_M, otherwise it would trigger too early on a
       *    motorway).
       *  - Additionally, PREDICT_AHEAD_MS is looked ahead to score
       *    candidates: a cross street you're merely crossing falls behind.
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
     * Decide "too fast" here, so the fade can hold it back together with
     * limit and reason. With hysteresis, otherwise a speed sitting right
     * at the threshold would trigger one fade after another.
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
    real_limit = snapshot.limit;      // for the log comparison
    real_reason = snapshot.reason;
    /*
     * The "too fast" warning deliberately triggers NO fade and is not
     * held back either: a warning must appear the moment it applies, not
     * a quarter second later after a crossfade. Only limit and reason are
     * held back.
     */
    if (snapshot.limit != blf_want_limit || snapshot.reason != blf_want_reason) {
      blf_want_limit = snapshot.limit;
      blf_want_reason = snapshot.reason;
      if (blf_limit == -999) {          // first run: show immediately
        blf_limit = snapshot.limit;
        blf_reason = snapshot.reason;
      } else if (blf_state == 0) {
        blf_state = 1;
        blf_t = 0.0f;
      }
    }
    bool at_dark = (blf_state == 2);
    if (at_dark) {                      // switch at the dark point
      blf_limit = blf_want_limit;
      blf_reason = blf_want_reason;
    }
    snapshot.limit = blf_limit;
    snapshot.reason = blf_reason;
#endif
    ui_update(&snapshot);
#if FADE_MODE == 2
    if (at_dark) {
      // Force a redraw right here, while the light is still off -
      // otherwise it would ramp back up while drawing is still in
      // progress.
      lv_refr_now(NULL);
      blf_state = 3;
      blf_t = 0.0f;
    }
#endif

    // Log the reason too, ordered the same as UI_REASON_* in ui.h
    static const char *WHY[8] = {"-", "ZONE", "KINDER", "SPIEL",
                                 "RAD", "SCHILD", "ZEIT", "-"};
    static uint32_t last_log = 0;
    bool do_log = (now - last_log >= LOG_INTERVAL_MS);
    if (do_log) last_log = now;

    if (!do_log) {
      // nothing to log - display is already updated
    } else if (snapshot.demo) {
      int exp = DEMO[demo_leg].expect;
      uint8_t expw = DEMO[demo_leg].why;
      // Check against the computed value, not against what's currently
      // shown - the fade briefly holds back the switch-over.
      /* -2 = depends on time of day: 30 with KINDER during the day, 50
         without otherwise */
      bool ok;
      if (exp == -2) {
        bool day = snapshot.time_valid && snapshot.hour >= 6 && snapshot.hour < 18;
        ok = day ? (real_limit == 30 && real_reason == UI_REASON_CHILDREN)
                 : (real_limit == 50 && real_reason == UI_REASON_NONE);
      } else {
        ok = (real_limit == exp) &&
             (expw == WHY_ANY || real_reason == expw);
      }
      Serial.printf("[Demo] %.5f,%.5f k=%3.0f %3.0f km/h  limit %3d "
                    "(expected %3d) %-7s %s  cache %lu/%lu\n",
                    snapshot.lat, snapshot.lon, course, snapshot.speed_kmh,
                    real_limit, exp, WHY[real_reason & 7],
                    ok ? "ok" : "MISMATCH",
                    (unsigned long)grid.cacheHits(),
                    (unsigned long)grid.cacheReads());
      Serial.printf("[Draw] %lu/s, %lu kPixel/s, bus %lu%%, LVGL %lu%%, "
"longest cycle %lu us, TE %lu%% in %lu waits, largest %lu kPixel\n",
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
                    "%02u:%02u  limit %d  %lu fps  bus %lu%%  LVGL %lu%%  "
                    "longest cycle %lu us  BL %d\n",
                    snapshot.fix, snapshot.sats, snapshot.lat, snapshot.lon,
                    snapshot.speed_kmh, course, snapshot.hour, snapshot.minute,
                    snapshot.limit, (unsigned long)g_flushes,
                    (unsigned long)(g_flush_us / 10000),
                    (unsigned long)(g_refr_us / 10000),
                    (unsigned long)g_refr_max_us, g_bl_level);
      g_flush_us = 0; g_flush_max = 0; g_refr_us = 0; g_refr_max_us = 0;
#if LCD_TE_SYNC
      if (g_te_timeouts) {
        Serial.printf("[LCD] TE timeouts: %lu - is the signal wired?\n",
                      (unsigned long)g_te_timeouts);
        g_te_timeouts = 0;
      }
#endif
      g_flushes = 0; g_flush_px = 0;
    }
  }

  delay(5);
}
