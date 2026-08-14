/*
 * config.h - every tunable in one place.
 *
 * Platform-neutral: included by both the firmware (main.cpp) and the
 * simulator (ui.c) alike, so it holds only #defines, no includes.
 *
 * Project rule: anything you should be able to adjust without reading code
 * belongs here, not buried inside a function.
 *
 * PERMANENT NOTE - do not strip these comments:
 * The rationale, measurements and dead-end warnings below are load-bearing
 * documentation, not clutter. Several of them exist because a "cleaner"
 * value was tried on real hardware and made things worse (see
 * doc/content/history.md for the write-ups). A future cleanup/simplification
 * pass must NOT remove or shorten these comments to "tidy up" the file -
 * that would silently throw away the reason the current value was chosen
 * and invite someone to re-run the same failed experiment. If a comment
 * looks redundant, that is very likely because you already know the "why" -
 * the next reader may not.
 */

#pragma once

/* ==========================================================================
 * TYPEFACE
 * ==========================================================================
 * Medium fills the round display area better, Condensed leaves more
 * breathing room. Both sets ship in the repo, switch via build flag:
 *     PLATFORMIO_BUILD_FLAGS=-DUSE_DIN_ENG pio run -e esp32s3
 * The point sizes were measured separately per cut - Medium is roughly
 * 30% wider, so the same sizes would overflow the circle.
 */
#if !defined(USE_DIN_ENG) && !defined(USE_DIN_MITTEL)
#define USE_DIN_MITTEL
#endif

/*
 * The reason label sits centered between the digit (or pictogram) and the
 * status band. Measured: 19px above / 20px below for the digit case, 25/25
 * for the pictogram case.
 *
 * Important when re-measuring: the alignment offset already includes the
 * optical correction, so it cancels out - the ink center simply sits at
 * 180 + LABEL_Y resp. 180 + NUM_Y_SHIFT. Adding the correction again while
 * checking this gives a result that is off by 13px.
 * Because the pictogram (128px) is shorter than the digit (148px), these
 * are two different heights - LABEL_Y and LABEL_Y_PICTO.
 *
 * PICTO_Y is as high as possible: at 208px width the top edge just barely
 * fits inside the circle at y = 90; any higher and the image clips the
 * sides.
 *
 * Optical center: LVGL centers the text BOX, not the glyphs. The box is as
 * tall as line_height and reserves room for descenders, but digits have
 * none - so they visually sit too high. The correction is
 * base_line + digit_height/2 - line_height/2, measured per font, not
 * estimated.
 */
#if defined(USE_DIN_MITTEL)
/*
 * Text widths measured with lv_text_get_size() (not estimated) - the
 * comments here used to be off (197/233 instead of the actual 208/252px),
 * presumably left over from an earlier version predating a rerun of
 * even_digit_spacing.py. The "available" figures, on the other hand, come
 * from an older calculation that can no longer be reconstructed and are
 * NOT re-verified - see backlog.md item 4 for how to weigh that.
 *
 * "888" is a hypothetical worst case used to pick the font size, not a
 * real-world limit - actual three-digit limits (100/110/120/130) come out
 * at 168-203px, well below that. The source typeface ships in the repo as
 * "DIN 1451 Std Mittelschrift.otf", so a smaller cut could be generated if
 * one is ever needed.
 */
#define FONT_SIZE_2DIGIT 205   /* "88"  208px wide, 273px available        */
#define FONT_SIZE_3DIGIT 162   /* "888" 252px wide, 281px available;
                                  real-world limits (100-130) are 168-203px */
#define FONT_SIZE_LABEL 48     /* "SCHRITT" 175px wide, 213px available     */
#define FONT_SIZE_PICNUM 72    /* speed shown below the pictogram           */
#define NUM_OPT_2DIGIT 13      /* optical correction, see explanation above */
#define NUM_OPT_3DIGIT 10
#define LABEL_OPT (-4)
#define NUM_Y_SHIFT (-25)      /* push digit up when a reason label is shown */
#define PICTO_Y (-26)          /* pictogram - as high as its width allows   */
#define LABEL_Y 85             /* reason label below the digit              */
#define LABEL_Y_PICTO 80       /* reason label below the pictogram          */
#define PICNUM_OPT 0           /* optical correction for the 72px font      */
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
 * DISPLAY (LOGIC)
 * ========================================================================== */

/* Percentage over the limit that counts as "too fast".
   10% means: at a 30 limit, triggers from 33km/h; at 100, from 110km/h. */
#define OVER_TOLERANCE_PCT 10

/* Fallback margin so a speed sitting exactly on the threshold doesn't
   flicker between white and red forever - and doesn't retrigger one fade
   right after another. */
#define OVER_HYSTERESIS_KMH 3.0f

/* 1 = the whole area turns red, digits turn white (default, reads better in
   peripheral vision). 0 = only the digits turn red. */
#define OVER_STYLE_INVERT 1

/* Time constant of the fill-bar smoothing, in milliseconds. Computed on
   elapsed time, so it stays independent of how often ui_tick() is called. */
#define ARC_TAU_MS 250.0f

/*
 * Crossfade when the number changes. 0 = hard cut (default).
 *
 * The measurement series behind this, so nobody re-walks the same dead
 * ends:
 *
 *   fading digit opacity            682 ns/pixel
 *   fading a solid-color rectangle  668 ns/pixel  (practically identical)
 *
 * So the cost isn't the antialiased digit glyph, it's simply touching any
 * area of the screen at all. And going smaller doesn't help either:
 *
 *   fade area 37,400px  ->  38 frames/s
 *   fade area 16,000px  ->  27 frames/s
 *   fade area  6,000px  ->  31 frames/s
 *
 * With the animation running, frame rate isn't driven by area but by a
 * fixed per-frame overhead - 19-27% of which is just waiting on TE alone.
 * At 30-40 frames/s, 200ms only buys six to eight visible steps, and you
 * can see each one.
 *
 * If you do want a crossfade: 400ms gives roughly twelve steps and looks
 * noticeably smoother, at the cost of feeling sluggish. Splitting the
 * number into one label per digit does not help, per the measurements
 * above.
 */
#define FADE_MS 1000

/*
 * Crossfade style:
 *   0 = hard cut, no transition
 *   1 = pixel fade (digit opacity, over FADE_MS) - expensive, see the
 *       measurement series above. FADE_MS is only used at all when
 *       FADE_MODE is 1.
 *   2 = backlight fade (default): dim down, swap the frame, dim back up
 *
 * Mode 2 costs no drawing at all - the fade happens in the backlight's PWM
 * channel, not on screen. That makes it arbitrarily fine and stepless, but
 * it dims the whole display, not just the digit.
 *
 * The ramp is gamma-corrected (FADE_BL_GAMMA); otherwise a linearly driven
 * PWM looks flat at the top and jumps at the bottom - the eye perceives
 * brightness roughly as the square root of power.
 *
 * The ramp runs on its own timer (FADE_BL_STEP_MS), not in the main loop:
 * the main loop can block for up to 90ms during a frame redraw, and a ramp
 * with 90ms gaps isn't a ramp. At the dark point it holds until the frame
 * swap has actually completed - otherwise the backlight starts climbing
 * again while the screen is still being drawn.
 */
#define FADE_MODE 2
#define FADE_BL_MS 500          /* total duration, half down / half up      */
#define FADE_BL_FLOOR 0         /* fully dark - any nonzero floor lets the
                                    redraw show through                     */
#define FADE_BL_GAMMA 2.2f
#define FADE_BL_STEP_MS 5       /* ramp tick rate                           */

/*
 * Color crossfade: 0 = hard cut. This is a measurement-driven decision, not
 * laziness.
 *
 * A color step repaints the whole disc. Measured on the device, a
 * large-area LVGL redraw costs 85-100ms - only about 10% of that is
 * transfer, the rest is drawing. Checked and ruled out as the cause: draw
 * buffer in PSRAM instead of internal RAM, a circular mask instead of a
 * rectangle, LV_USE_FLOAT, QSPI clock at 40 vs 80MHz, the TE wait. The arc
 * itself only accounts for a fifth of it.
 *
 * At 90ms per step, 400ms only fits four steps - and four steps is visibly
 * choppy. A hard cut is a single 90ms stutter and therefore reads as
 * instantaneous rather than jerky.
 *
 * If you still want it to fade: raise the value and accept the choppiness.
 */
#define FADE_COLOR_MS 0

/*
 * Extra letter spacing for the reason label. DIN 1451 has essentially no
 * side bearing on its capitals - in "SCHRITT" the two T's touch at the top.
 * That's not a kerning issue, it's the font metrics themselves.
 */
#define LABEL_LETTER_SPACE 3

/* The white disc is drawn this many pixels larger than the ring's inner
   edge leaves free. Without this overlap, a roughly one-pixel dark seam
   remains between ring and disc - that's the antialiasing of both circles
   blending against the background. */
#define DISC_OVERLAP 3

/* Status band: darker and more opaque than the earlier version, white text.
   The old, more transparent version was barely readable against the bright
   sign area. */
#define BAND_COLOR 0x05080C
#define BAND_OPA 217            /* out of 255, i.e. roughly 85%             */
#define BAND_TEXT 0xF2F4F7

/* ==========================================================================
 * DISPLAY (HARDWARE)
 * ==========================================================================
 * QSPI bus clock. If you see glitches, step it down: 40 -> 20 -> 1MHz.
 * Going above 40MHz is worth trying but buys little: at 40MHz bus load is
 * already only 1.5%, one arc-segment redraw takes 0.17ms out of a 16.7ms
 * frame period.
 *
 * Careful, 80MHz can fail: Arduino_ESP32QSPI forces the bus through the
 * GPIO matrix (SPICOMMON_BUSFLAG_GPIO_PINS) instead of IO_MUX. Through the
 * matrix, roughly 40MHz is the ceiling - even though this project's pins
 * happen to be exactly SPI2's IO_MUX pins.
 */
#define LCD_QSPI_HZ 80000000

/*
 * Frame sync via the panel's TE pin.
 *
 * Without it, the transfer lands at a random point in the panel's own
 * scanout - that's tearing. TE signals the start of the blanking interval;
 * starting the write there finishes before scanout reaches that spot.
 *
 * Waited on once per LVGL frame cycle, not once per partial redraw region -
 * otherwise every partial redraw would cost a full frame period. The 60Hz
 * ceiling matches LV_DEF_REFR_PERIOD (16ms).
 *
 * TE has to actually be wired up (LCD_TE) for this to work. If the signal
 * is missing, the wait simply times out after LCD_TE_TIMEOUT_US and
 * rendering continues without sync.
 */
/*
 * Draw buffer size as a fraction of a full frame.
 *
 * The buffer determines how many horizontal strips LVGL splits a redraw
 * into, and each strip is its own transfer. Too small means a visible seam
 * at the buffer boundary. A half frame (64,800px) holds the fade area
 * (37,400px) and the status lines each in one piece; a full-disc redraw
 * (86,400px) still needs two.
 *
 * 1/2 is the largest size that still fits into internal RAM as a single
 * buffer (129.6 KiB). Anything bigger has to live in PSRAM - not slower to
 * draw from, but measured no faster either.
 */
#define LCD_BUF_DIV 2

#define LCD_TE_SYNC 1
#define LCD_TE_TIMEOUT_US 20000

/* ==========================================================================
 * BACKLIGHT
 * ==========================================================================
 * Dimmed while stationary: saves power and doesn't glare at night. Two
 * thresholds so it doesn't flip back and forth every second at a red light.
 */
#define LCD_BL_LEVEL 255        /* brightness while driving, 0-255          */
#define LCD_BL_DIM_LEVEL 20     /* brightness while stationary               */
#define DIM_BELOW_KMH 2.0f      /* dims below this speed                     */
#define DIM_ABOVE_KMH 5.0f      /* back to full above this speed             */
#define DIM_FADE_MS 400         /* crossfade duration                        */

/* ==========================================================================
 * OPERATING MODE
 * ==========================================================================
 * A switch to ground on this pin flips the display to a plain speedometer:
 * the center then shows the driven speed instead of the limit, while the
 * ring stays scaled to the limit.
 *
 * GPIO21 has no special function on the ESP32-S3 - not a strapping pin (0,
 * 3, 45, 46), not USB (19/20), not Flash/PSRAM (26-37), not the UART bridge
 * (43/44). Open = speed-limit mode, tied to GND = speedometer mode.
 */
#define MODE_PIN 21

/*
 * A second switch to ground: forces the simulated demo drive even while GPS
 * has a fix. Handy for demos and for checking the display without having to
 * build a separate firmware for it (-DFORCE_DEMO still exists, but is now
 * meant only for automated runs).
 *
 * GPIO5 is likewise free on the ESP32-S3: no strapping, no USB, no
 * Flash/PSRAM, no UART bridge. GPIO15 would also be free, but is used as
 * the diagnostic probe pin under -DLCD_DIAG.
 */
#define DEMO_PIN 5

#define MODE_DEBOUNCE_MS 80

/* ==========================================================================
 * LOOK-AHEAD FOR MAP MATCHING
 * ==========================================================================
 * At intersections the display used to jump to the cross street because its
 * segment was briefly closer. Countermeasure: dead-reckon the position
 * PREDICT_AHEAD_MS into the future from course and speed, and score
 * candidates by how close they are to BOTH points. A street you're merely
 * crossing falls behind this way - you're already off it a moment later.
 *
 * The score is  distance_now + PREDICT_WEIGHT * distance_ahead.
 * The 30m match radius still applies to the real distance right now, not to
 * the scored value.
 */
#define PREDICT_AHEAD_MS 700
#define PREDICT_WEIGHT 0.6f

/*
 * Look-ahead switching: the lookup doesn't run at the current position but
 * SWITCH_AHEAD_MS further along. That way the new sign is already showing
 * by the time you actually reach it, instead of just after.
 *
 * The look-ahead distance grows with speed (time x speed): at 25km/h, 300ms
 * is about 2m; at 100km/h it's 8m. SWITCH_AHEAD_MAX_M additionally caps it.
 *
 * Halved from 600 to 300ms after the first test drive - the new limits were
 * noticeably too early otherwise. If it's still too early, the next lever
 * is MATCH_MAX_DIST_M: at a 30m search radius, a side street can already
 * win before you actually reach it.
 */
#define SWITCH_AHEAD_MS 300
#define SWITCH_AHEAD_MAX_M 25.0f

/* ==========================================================================
 * MAP MATCHING
 * ========================================================================== */
#define MATCH_MAX_DIST_M 30.0f      /* max. distance to a candidate road    */
#define MATCH_HYSTERESIS 1.6f       /* bonus for the currently chosen chain */
/*
 * Without a time limit, hysteresis held on to the limit of a Tempo-30 zone
 * already left behind, on a completely different, roughly parallel street,
 * for up to 400m - a long zone chain stayed within the search radius for
 * the entire stretch and scored close enough. A restart (which resets
 * last_speed_ to -1) fixed it instantly - evidence that the holding
 * duration was the problem, not a wrong match to begin with. Real test
 * drive, see history.md. 4s is enough to bridge a brief ambiguity at an
 * intersection, but not a parallel street held for several hundred meters.
 */
#define MATCH_HYSTERESIS_MAX_MS 4000UL

/* Below this speed, GPS course is noise. Applies to both the matching
   direction filter AND the look-ahead - it's the same statement, hence one
   constant. */
#define COURSE_MIN_KMH 8.0f
#define COURSE_TOLERANCE_DEG 45.0f  /* allowed heading deviation             */
#define CELL_CACHE_SLOTS 27         /* 9 per simultaneously overlapping region */
#define CELL_ALLOC_GRAN 4096
#define MAX_REGIONS 8
#define GRID_DIR "/maps"

/*
 * Flat-earth approximation: meters per degree of latitude (WGS84, roughly
 * constant everywhere). Meters per degree of longitude shrinks toward the
 * poles and is computed at each use site as EARTH_M_PER_DEG_LAT * cos(lat).
 *
 * Used both by the real map-matching lookup (speedlimit_grid.h, scaled down
 * to meters per micro-degree since positions there are stored as 1e-6
 * degree integers) and by the demo route's distance/dead-reckoning math in
 * main.cpp - same physical constant, so it lives here once instead of as a
 * literal duplicated in both files.
 */
#define EARTH_M_PER_DEG_LAT 111320.0f

/* ==========================================================================
 * MAP UPDATES OVER WI-FI (webupdate.h/.cpp)
 * ==========================================================================
 * Backlog item 1a: update regions without a PC toolchain. The device opens
 * an access point and serves a small web UI at http://192.168.4.1/ for
 * uploading finished .msg files and deleting installed regions. Turning a
 * PBF into a .msg file still happens on a PC with tools/maps.py - the web UI
 * only accepts the finished file.
 *
 * Open AP with no password: reasonable for a device that lives in a car
 * (see backlog.md); a deliberate choice over WPA2 with a password that
 * would have to be looked up on the device itself anyway.
 *
 * Uploaded regions and regions marked for deletion first land in
 * PENDING_DIR, not directly in GRID_DIR: while the device is running,
 * SpeedLimitGrid may be holding an open file handle on exactly the region
 * file being replaced or deleted - a lookup in progress would then read in
 * the middle of a write. applyPendingMapChanges() therefore applies the
 * pending changes right at the start of the next setup(), before any file
 * handle into GRID_DIR exists. Changes only take effect after a restart -
 * the web UI has its own button for that.
 */
#define AP_SSID "Tempolimit-Setup"
/* Without a connection, the AP shuts itself off after this long (saves
   power in a parked car). Once someone connects, the same timer restarts
   on every check as long as at least one station is connected - so the AP
   stays up for the whole session, plus one more full period as a grace
   window afterward. One constant covers both cases because it's the same
   trade-off either way. */
#define AP_IDLE_TIMEOUT_MS (5UL * 60UL * 1000UL)
/* Holding DEMO_PIN to GND this long restarts the AP if it has already shut
   down, without needing a reboot. The switch also still forces the
   simulated drive as usual; that's harmless during a maintenance session on
   a parked vehicle. */
#define AP_HOLD_TRIGGER_MS (10UL * 1000UL)
/* Safety margin for the free-space check before an upload - the request's
   Content-Length is an upper bound (it includes the multipart framing), not
   the exact file size. */
#define AP_FREE_MARGIN_BYTES (64UL * 1024UL)
#define PENDING_DIR "/maps_pending"

/* ==========================================================================
 * TICK RATES
 * ========================================================================== */
/*
 * Rate of lookups and display-value updates. Not to be confused with the
 * frame rate: the fill bar runs via ui_tick() on every loop iteration,
 * independent of this. 60ms is roughly 17Hz - faster than the GPS's 5Hz,
 * because dead reckoning fills the gaps between fixes and the number
 * crossfade would otherwise look coarse. Thanks to the cache, a lookup
 * costs almost nothing.
 */
#define UI_UPDATE_MS 60
#define LOG_INTERVAL_MS 1000    /* serial log rate, otherwise unreadable       */
#define GPS_GRACE_MS 15000      /* how long to wait for a fix before demo mode */
#define DEMO_LEG_MS 12000       /* duration of one demo leg                    */
/*
 * The demo route's speed wobbles by +-DEMO_SPEED_WOBBLE_PCT with a period of
 * DEMO_SPEED_WOBBLE_PERIOD_MS. Without this, speed would be constant for the
 * whole leg and the fill bar would sit still - you couldn't tell it's a live
 * animation rather than a frozen screen. Kept small enough that no demo leg
 * crosses the OVER_TOLERANCE_PCT threshold in either direction.
 */
#define DEMO_SPEED_WOBBLE_PCT 0.08f
#define DEMO_SPEED_WOBBLE_PERIOD_MS 1500.0f

/* ==========================================================================
 * GPS
 * ==========================================================================
 * 9600 baud is 960 bytes/s. Measured with RMC+GGA at 5Hz: about 717 bytes/s,
 * i.e. 75% utilization - it fits, but with no headroom. Anyone who needs
 * additional sentences (GSV for satellite quality, say) has to raise this
 * first; the auto-detect logic will find the new rate on its own.
 */
#define GPS_BAUD_TARGET 9600
#define GPS_RATE_HZ 5
#define GPS_PROBE_MS 4000

/* Delay between two UBX config messages (CFG-MSG) sent back-to-back while
   silencing unused NMEA sentences - gives the module time to process each
   one before the next arrives. */
#define UBX_CMD_DELAY_MS 20

/* How often gpsTask() prints its bytes/sentences/checksum-errors/satellites
   statistics line (see the GPS troubleshooting table in CLAUDE.md). Same
   idea as LOG_INTERVAL_MS above, just for the GPS task's own log line. */
#define GPS_STAT_INTERVAL_MS 5000

/* gpsTask()'s own poll period once a fix source is running - separate from
   UI_UPDATE_MS because this task only reads the UART and publishes into
   g_state, it doesn't do any lookup work. */
#define GPS_TASK_POLL_MS 10
