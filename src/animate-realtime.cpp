#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>
#include "esp_sntp.h"
#include "noaa_solar.h"
#include "secrets.h"
#include "sky_time.h"


// =========================================================================
// Hardware Configuration (3x 8x32 daisy-chained panels -> 32x24 array)
// =========================================================================
#define LED_PIN          12
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB

#define PANEL_WIDTH      32
#define PANEL_HEIGHT     8
#define NUM_PANELS       3

#define MATRIX_WIDTH     PANEL_WIDTH                 // 32
#define MATRIX_HEIGHT    (PANEL_HEIGHT * NUM_PANELS) // 24
#define NUM_LEDS         (MATRIX_WIDTH * MATRIX_HEIGHT) // 768

#define BRIGHTNESS       80
#define MAX_POWER_MA     500

// Panel orientations (alternating serpentine)
#define PANEL_1_FLIP_X   true
#define PANEL_1_FLIP_Y   true
#define PANEL_2_FLIP_X   false
#define PANEL_2_FLIP_Y   false
#define PANEL_3_FLIP_X   true
#define PANEL_3_FLIP_Y   true

CRGB leds[NUM_LEDS];

// =========================================================================
// Real-Time Speed & Simulation Mode
// =========================================================================
// TIME_MULTIPLIER:
//   1.0f  = True 1x real-time (1 second in real life = 1 second on matrix).
//           Takes 24 real hours for a full day/night cycle.
//   60.0f = 1 minute per real second (full 24h day takes 24 minutes).
//  720.0f = Fast preview: full 24h day takes 2 minutes!
// Start with 720.0f to test the full sunrise->sunset->moonrise cycle,
// then set to 1.0f for your wall art!
#define TIME_MULTIPLIER  720.0f

// Re-query NTP on this real-time interval, then slew the 1x clock
// toward the result (never step). 0.001 = 1 ms of correction per
// real second, enough for typical ESP32 drift without a visible jump.
#define NTP_RESYNC_MS    (24UL * 60UL * 60UL * 1000UL)
#define NTP_SLEW_RATE    0.001f
#define NTP_WAIT_MS      15000u

// =========================================================================
// Painting Dimensions & Masking
// =========================================================================
#define PAINTING_WIDTH   36
#define PAINTING_HEIGHT  28
#define PAINTING_ORIGIN_X  2
#define PAINTING_ORIGIN_Y  0

#define SET_INSET_LEFT   3
#define SET_INSET_RIGHT  3
#define SUN_START_COL    ((float)(PAINTING_ORIGIN_X + SET_INSET_LEFT))
#define SUN_END_COL      ((float)(PAINTING_ORIGIN_X + MATRIX_WIDTH - 1 - SET_INSET_RIGHT))
#define SUN_CENTER_ROW   20.0f
#define SUN_CENTER_COL   ((SUN_START_COL + SUN_END_COL) * 0.5f)
#define SUN_ARC_RADIUS   ((SUN_END_COL - SUN_START_COL) * 0.5f)
#define SUN_RADIUS       2.0f
#define MOON_RADIUS      (SUN_RADIUS * 0.8f)
#define BODY_AA          0.25f

#define MASK_FOREGROUND_CLOUDS  true   // near-clouds: dim + diffuse the body
#define MASK_BACKGROUND_CLOUDS  true   // far-clouds: dim + diffuse the body
#define MASK_HORIZON            true   // land fully hides the body
#define CLOUD_DIM               0.35f  // brightness through clouds (1 = unchanged)
#define CLOUD_DIFFUSE           1.0f   // radius/falloff scale through clouds
#define SUN_HORIZON_WARM        0.70f  // 0 = no shift, 1 = full sunset color at horizon

#define STAR_COUNT       6

static const CRGB SUN_COLOR(255, 214, 64);
static const CRGB SUN_HORIZON_COLOR(255, 72, 12);
static const CRGB MOON_COLOR(190, 210, 255);
static const CRGB STAR_COLOR(220, 225, 255);

// Bitmasks from assets/categorized-human-more-clouds.csv
static const uint64_t FG_CLOUD_MASK[PAINTING_HEIGHT] = {
  0x00031ffe00ULL, 0x001f07fc00ULL, 0x00000001c0ULL, 0x0000001fffULL,
  0x00000000ffULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000000030ULL, 0x0000003fffULL, 0x0000007fffULL,
  0x000001ffe0ULL, 0x0000008000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
};

static const uint64_t BG_CLOUD_MASK[PAINTING_HEIGHT] = {
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000180000ULL, 0x00087e0000ULL, 0x003ffe8000ULL,
  0x007ffe0000ULL, 0x07ffff1bf0ULL, 0x0fffff0000ULL, 0x0ffffc0000ULL,
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
};

static const uint64_t HORIZON_MASK[PAINTING_HEIGHT] = {
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL, 0x0000000000ULL,
  0x0000000000ULL, 0x0000006000ULL, 0x0000007000ULL, 0x000002fc00ULL,
  0x0fffffffffULL, 0x0fffffffffULL, 0x0fffffffffULL, 0x0fffffffffULL,
  0x0fffffffffULL, 0x0fffffffffULL, 0x0fffffffffULL, 0x0fffffffffULL,
};

// Fixed star coordinates in panel space
static const int8_t STAR_POS[STAR_COUNT][2] = {
  {3, 6}, {8, 3}, {14, 7}, {19, 4}, {25, 9}, {28, 5},
};

// =========================================================================
// 2D to 1D Mapping: XY(x, y) -> LED index
// =========================================================================
uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) return 0;

  uint8_t panelIndex = y / PANEL_HEIGHT;
  uint8_t py = y % PANEL_HEIGHT;
  uint8_t px = x;

  if (panelIndex == 0) {
    if (PANEL_1_FLIP_X) px = (PANEL_WIDTH - 1) - px;
    if (PANEL_1_FLIP_Y) py = (PANEL_HEIGHT - 1) - py;
  } else if (panelIndex == 1) {
    if (PANEL_2_FLIP_X) px = (PANEL_WIDTH - 1) - px;
    if (PANEL_2_FLIP_Y) py = (PANEL_HEIGHT - 1) - py;
  } else if (panelIndex == 2) {
    if (PANEL_3_FLIP_X) px = (PANEL_WIDTH - 1) - px;
    if (PANEL_3_FLIP_Y) py = (PANEL_HEIGHT - 1) - py;
  }

  uint16_t indexInPanel;
  if (px & 0x01) {
    indexInPanel = (px * PANEL_HEIGHT) + (PANEL_HEIGHT - 1 - py);
  } else {
    indexInPanel = (px * PANEL_HEIGHT) + py;
  }

  return (panelIndex * (PANEL_WIDTH * PANEL_HEIGHT)) + indexInPanel;
}

void setPixel(int x, int y, CRGB color) {
  if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT) {
    leds[XY((uint8_t)x, (uint8_t)y)] = color;
  }
}

static bool maskBit(const uint64_t *mask, int x, int y) {
  if (x < 0 || x >= PAINTING_WIDTH || y < 0 || y >= PAINTING_HEIGHT) return false;
  return (mask[y] >> x) & 1ULL;
}

static bool behindHorizon(int x, int y) {
  return MASK_HORIZON && maskBit(HORIZON_MASK, x, y);
}

static bool behindCloud(int x, int y) {
  if (MASK_FOREGROUND_CLOUDS && maskBit(FG_CLOUD_MASK, x, y)) return true;
  if (MASK_BACKGROUND_CLOUDS && maskBit(BG_CLOUD_MASK, x, y)) return true;
  return false;
}

static bool bodyVisible(int x, int y) {
  if (x < 0 || x >= PAINTING_WIDTH || y < 0 || y >= PAINTING_HEIGHT) return false;
  if (behindHorizon(x, y) || behindCloud(x, y)) return false;
  return true;
}

static void skyPosition(float t, float *row, float *col) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  const float theta = PI * (1.0f - t);
  *col = SUN_CENTER_COL + SUN_ARC_RADIUS * cosf(theta);
  *row = SUN_CENTER_ROW - SUN_ARC_RADIUS * sinf(theta);
}

static CRGB sunColorAtRow(float row) {
  float height = 1.0f;
  if (SUN_ARC_RADIUS > 0.0f) {
    height = (SUN_CENTER_ROW - row) / SUN_ARC_RADIUS;
    if (height < 0.0f) height = 0.0f;
    if (height > 1.0f) height = 1.0f;
  }
  const float warm = (1.0f - height) * SUN_HORIZON_WARM;
  return SUN_COLOR.lerp8(SUN_HORIZON_COLOR, (uint8_t)(warm * 255.0f + 0.5f));
}

static void renderBody(float row, float col, float radius, CRGB color) {
  const float reach = radius * CLOUD_DIFFUSE;
  const int r0 = (int)floorf(row - reach - 1.0f);
  const int r1 = (int)ceilf(row + reach + 1.0f);
  const int c0 = (int)floorf(col - reach - 1.0f);
  const int c1 = (int)ceilf(col + reach + 1.0f);

  for (int y = r0; y <= r1; ++y) {
    for (int x = c0; x <= c1; ++x) {
      if (x < 0 || x >= PAINTING_WIDTH || y < 0 || y >= PAINTING_HEIGHT) continue;
      if (behindHorizon(x, y)) continue;

      const bool cloud = behindCloud(x, y);
      const float r = cloud ? (radius * CLOUD_DIFFUSE) : radius;
      const float aa = cloud ? (BODY_AA * CLOUD_DIFFUSE) : BODY_AA;
      const float dist = hypotf((float)x - col, (float)y - row);
      float alpha = (r - dist) / aa;
      if (alpha <= 0.0f) continue;
      if (alpha > 1.0f) alpha = 1.0f;
      if (cloud) alpha *= CLOUD_DIM;

      const int px = x - PAINTING_ORIGIN_X;
      const int py = y - PAINTING_ORIGIN_Y;
      CRGB c = color;
      c.nscale8((uint8_t)(alpha * 255.0f + 0.5f));
      setPixel(px, py, c);
    }
  }
}

static void renderStars() {
  for (uint8_t i = 0; i < STAR_COUNT; ++i) {
    const int px = STAR_POS[i][0];
    const int py = STAR_POS[i][1];
    const int x = px + PAINTING_ORIGIN_X;
    const int y = py + PAINTING_ORIGIN_Y;
    if (!bodyVisible(x, y)) continue;

    CRGB &pix = leds[XY((uint8_t)px, (uint8_t)py)];
    if (pix.r | pix.g | pix.b) continue; // Don't overwrite moon
    pix = STAR_COLOR;
  }
}

// =========================================================================
// NOAA Astronomical Solar Calculator & Real-Time Engine
// =========================================================================
// 1x UTC wall clock in milliseconds. NTP offsets are queued in
// pendingSlewMs and absorbed at NTP_SLEW_RATE so animation never steps.
static int64_t wallMs = 0;
static int64_t wallOriginMs = 0;
static int64_t pendingSlewMs = 0;
static uint32_t lastWallMillis = 0;
static uint32_t ntpLastDoneMs = 0;
static uint32_t ntpWaitStartMs = 0;
static bool ntpWaiting = false;
static bool wallRunning = false;

static int64_t tickWall() {
  const uint32_t nowMs = millis();
  if (!wallRunning) {
    return wallMs;
  }
  const uint32_t dt = nowMs - lastWallMillis;
  lastWallMillis = nowMs;
  int64_t absorb = 0;
  if (pendingSlewMs != 0 && dt > 0) {
    int64_t maxAbs = (int64_t)((double)dt * (double)NTP_SLEW_RATE);
    if (maxAbs < 1) {
      maxAbs = 1;
    }
    absorb = pendingSlewMs;
    if (absorb > maxAbs) absorb = maxAbs;
    if (absorb < -maxAbs) absorb = -maxAbs;
    pendingSlewMs -= absorb;
  }
  wallMs += (int64_t)dt + absorb;
  return wallMs;
}

static void armWallClock(time_t utcEpoch) {
  wallMs = (int64_t)utcEpoch * 1000;
  wallOriginMs = wallMs;
  lastWallMillis = millis();
  pendingSlewMs = 0;
  wallRunning = true;
  ntpLastDoneMs = millis();
  ntpWaiting = false;
}

// Simulated (or 1x) epoch. TIME_MULTIPLIER is applied to slewed 1x wall
// elapsed since boot, so a later NTP correction eases in instead of jumping.
time_t getCurrentEpoch() {
  const int64_t wall = tickWall();
  const int64_t elapsed = wall - wallOriginMs;
  return (time_t)(wallOriginMs / 1000 + (elapsed * (double)TIME_MULTIPLIER) / 1000.0);
}

static void queueNtpSlew(time_t ntpEpoch) {
  if (ntpEpoch < 100000000 || !wallRunning) {
    return;
  }
  const int64_t ntpMs = (int64_t)ntpEpoch * 1000;
  const int64_t wall = tickWall();
  const int64_t delta = ntpMs - wall;
  pendingSlewMs += delta;
  Serial.printf("NTP: offset %+lld ms, slewing %+lld ms remaining\n",
                (long long)delta, (long long)pendingSlewMs);
}

static void pollNtp() {
  if (!wallRunning || WiFi.status() != WL_CONNECTED) {
    return;
  }
  const uint32_t nowMs = millis();
  if (!ntpWaiting) {
    if (nowMs - ntpLastDoneMs < NTP_RESYNC_MS) {
      return;
    }
    sntp_stop();
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    sntp_init();
    ntpWaiting = true;
    ntpWaitStartMs = nowMs;
    Serial.println("NTP: daily resync started");
    return;
  }

  const bool done = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
  const bool timeout = (nowMs - ntpWaitStartMs) > NTP_WAIT_MS;
  if (!done && !timeout) {
    return;
  }
  if (done) {
    queueNtpSlew(time(nullptr));
  } else {
    Serial.println("NTP: daily resync timed out");
  }
  sntp_stop();
  ntpWaiting = false;
  ntpLastDoneMs = nowMs;
}

// Helper: computes UTC epoch of midnight (00:00:00) for a given UTC day offset
time_t getMidnightEpoch(time_t epoch, int dayOffset) {
  time_t target = epoch + (dayOffset * 86400);
  struct tm tm_info;
  gmtime_r(&target, &tm_info);
  tm_info.tm_hour = 0;
  tm_info.tm_min = 0;
  tm_info.tm_sec = 0;
  // Convert UTC tm back to time_t using standard UTC epoch formula:
  int y = tm_info.tm_year + 1900;
  int m = tm_info.tm_mon + 1;
  int d = tm_info.tm_mday;
  if (m <= 2) { y--; m += 12; }
  long long days = (365LL * y) + (y / 4) - (y / 100) + (y / 400) + ((153 * (m - 3) + 2) / 5) + d - 719469;
  return (time_t)(days * 86400LL);
}

// Gets sunrise and sunset epoch for a given date offset from current epoch
void getSolarTimes(time_t epoch, int dayOffset, time_t *sunriseEpoch, time_t *sunsetEpoch) {
  time_t midnight = getMidnightEpoch(epoch, dayOffset);
  time_t target = midnight + 43200; // midday for date extraction
  struct tm tm_info;
  gmtime_r(&target, &tm_info);

  int y = tm_info.tm_year + 1900;
  int m = tm_info.tm_mon + 1;
  int d = tm_info.tm_mday;

  double riseMin = 0, setMin = 0;
  NOAA::calculateSunriseSunsetUTC(y, m, d, LOCATION_LATITUDE, LOCATION_LONGITUDE, riseMin, setMin);

  *sunriseEpoch = midnight + (time_t)(riseMin * 60.0 + 0.5);
  *sunsetEpoch  = midnight + (time_t)(setMin * 60.0 + 0.5);
}

static SkyTime getSkyTime() {
  SkyTime t;
  t.now = getCurrentEpoch();
  getSolarTimes(t.now, 0, &t.sunrise, &t.sunset);
  time_t dummy;
  getSolarTimes(t.now, 1, &t.sunrise_next, &dummy);
  getSolarTimes(t.now, -1, &dummy, &t.sunset_prev);
  return t;
}


// =========================================================================
// Setup: Wi-Fi, NTP & System Init
// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==================================================");
  Serial.println("  LiveCanvas: Real-Time NOAA Astronomical Sync");
  Serial.println("==================================================");
  Serial.printf("  Location:      Lat %.4f, Lon %.4f\n", LOCATION_LATITUDE, LOCATION_LONGITUDE);
  Serial.printf("  Speed:         %.1fx (%s)\n",
                TIME_MULTIPLIER, (TIME_MULTIPLIER == 1.0f) ? "Real-Time 1x" : "Simulation Preview");
  Serial.printf("  Clouds:        dim=%.2f diffuse=%.2f  sun_warm=%.2f\n",
                CLOUD_DIM, CLOUD_DIFFUSE, SUN_HORIZON_WARM);
  Serial.println("==================================================");

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_POWER_MA);
  FastLED.clear(true);

  // 1. Connect to Wi-Fi
  Serial.printf("Connecting to Wi-Fi SSID '%s'...", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connected!");
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WARNING] Wi-Fi connection timed out. Check credentials.");
  }

  // 2. Synchronize NTP Real-Time Clock
  Serial.println("Synchronizing Real-Time Clock via NTP (pool.ntp.org)...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

  time_t now = time(nullptr);
  uint32_t ntpStart = millis();
  while (now < 100000000 && millis() - ntpStart < 12000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }

  if (now >= 100000000) {
    armWallClock(now);
    sntp_stop();

    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    Serial.printf("\nNTP Synced! Current UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                  tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);

    time_t sRise, sSet;
    getSolarTimes(now, 0, &sRise, &sSet);
    struct tm rTm, sTm;
    gmtime_r(&sRise, &rTm);
    gmtime_r(&sSet, &sTm);
    Serial.printf("  Today Sunrise (UTC): %02d:%02d:%02d\n", rTm.tm_hour, rTm.tm_min, rTm.tm_sec);
    Serial.printf("  Today Sunset  (UTC): %02d:%02d:%02d\n", sTm.tm_hour, sTm.tm_min, sTm.tm_sec);
    Serial.printf("  Daylight Duration:   %.2f hours\n", (float)(sSet - sRise) / 3600.0f);
  } else {
    Serial.println("\n[WARNING] NTP sync timed out. Falling back to default baseline time.");
    armWallClock(1726246800); // Fallback Sept 13, 2026 ~17:00 UTC
  }

  Serial.println("Starting real-time animation loop...\n");
}

// =========================================================================
// Main Loop: Speed-Modulated Astronomical Tracker
// =========================================================================
void loop() {
  pollNtp();
  const SkyTime sky = getSkyTime();
  const SkyFrame frame = mapSky(sky);

  FastLED.clear();
  float t = 0.0f;
  if (frame.sun.visible) {
    t = frame.sun.t;
    float row, col;
    skyPosition(t, &row, &col);
    renderBody(row, col, SUN_RADIUS, sunColorAtRow(row));
  }
  if (frame.moon.visible) {
    t = frame.moon.t;
    float row, col;
    skyPosition(t, &row, &col);
    renderBody(row, col, MOON_RADIUS, MOON_COLOR);
    renderStars();
  }
  FastLED.show();

  static uint32_t lastLog = 0;
  if (millis() - lastLog > 2000) {
    lastLog = millis();
    struct tm curTm;
    gmtime_r(&sky.now, &curTm);
    Serial.printf("[%02d:%02d:%02d UTC] %s | Progress t=%.3f\n",
                  curTm.tm_hour, curTm.tm_min, curTm.tm_sec,
                  frame.sun.visible ? "DAY (Sun)" : "NIGHT (Moon+Stars)", t);
  }

  delay(20);
}
