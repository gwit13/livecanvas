#include <Arduino.h>
#include <FastLED.h>
#include <math.h>

// =========================================================================
// Hardware — same 3x 8x32 daisy-chain as panel_test.cpp
// =========================================================================
#define LED_PIN          12
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB

#define PANEL_WIDTH      32
#define PANEL_HEIGHT     8
#define NUM_PANELS       3

#define MATRIX_WIDTH     PANEL_WIDTH
#define MATRIX_HEIGHT    (PANEL_HEIGHT * NUM_PANELS)  // 24
#define NUM_LEDS         (MATRIX_WIDTH * MATRIX_HEIGHT)

// Only the sun is driven; a handful of LEDs, but keep the USB cap.
#define BRIGHTNESS       80
#define MAX_POWER_MA     500

#define PANEL_1_FLIP_X   true
#define PANEL_1_FLIP_Y   true
#define PANEL_2_FLIP_X   false
#define PANEL_2_FLIP_Y   false
#define PANEL_3_FLIP_X   true
#define PANEL_3_FLIP_Y   true

CRGB leds[NUM_LEDS];

// =========================================================================
// Alignment: painting cell (ORIGIN_X, ORIGIN_Y) sits on panel LED (0, 0).
// Default (2, 0) is top-center of the 36x28 grid. Tweak these to match
// the physical painting on the panel (higher X = look further right).
// =========================================================================
#define PAINTING_WIDTH   36
#define PAINTING_HEIGHT  28
#define PAINTING_ORIGIN_X  2
#define PAINTING_ORIGIN_Y  0

// Arc rises/sets this many panel LEDs in from the left/right edges.
#define SET_INSET_LEFT   3
#define SET_INSET_RIGHT  3
#define SUN_START_COL    ((float)(PAINTING_ORIGIN_X + SET_INSET_LEFT))
#define SUN_END_COL      ((float)(PAINTING_ORIGIN_X + MATRIX_WIDTH - 1 - SET_INSET_RIGHT))
#define SUN_CENTER_ROW   20.0f
#define SUN_CENTER_COL   ((SUN_START_COL + SUN_END_COL) * 0.5f)
#define SUN_ARC_RADIUS   ((SUN_END_COL - SUN_START_COL) * 0.5f)
#define SUN_RADIUS       2.0f
#define MOON_RADIUS      (SUN_RADIUS * 0.5f)
#define BODY_AA          0.25f
#define SUN_DURATION_MS  10000u
#define MOON_DURATION_MS 10000u
#define CYCLE_MS         (SUN_DURATION_MS + MOON_DURATION_MS)

#define MASK_FOREGROUND_CLOUDS  true   // near-clouds sit in front of the body
#define MASK_BACKGROUND_CLOUDS  true   // far-clouds sit in front of the body
#define MASK_HORIZON            true   // foreground + background land form the horizon

static const CRGB SUN_COLOR(255, 214, 64);
static const CRGB MOON_COLOR(190, 210, 255);

// Bit x of row y is 1 for that occluder. Packed from categorized-human.csv.
static const uint64_t FG_CLOUD_MASK[PAINTING_HEIGHT] = {
  0x00031ffe00ULL,  // y= 0
  0x001f07fc00ULL,  // y= 1
  0x00000001c0ULL,  // y= 2
  0x0000001fffULL,  // y= 3
  0x00000000ffULL,  // y= 4
  0x0000000000ULL,  // y= 5
  0x0000000000ULL,  // y= 6
  0x0000000000ULL,  // y= 7
  0x0000000000ULL,  // y= 8
  0x0000000000ULL,  // y= 9
  0x0000000000ULL,  // y=10
  0x0000000000ULL,  // y=11
  0x0000000000ULL,  // y=12
  0x0000000030ULL,  // y=13
  0x0000003fffULL,  // y=14
  0x0000007fffULL,  // y=15
  0x000001ffe0ULL,  // y=16
  0x0000008000ULL,  // y=17
  0x0000000000ULL,  // y=18
  0x0000000000ULL,  // y=19
  0x0000000000ULL,  // y=20
  0x0000000000ULL,  // y=21
  0x0000000000ULL,  // y=22
  0x0000000000ULL,  // y=23
  0x0000000000ULL,  // y=24
  0x0000000000ULL,  // y=25
  0x0000000000ULL,  // y=26
  0x0000000000ULL,  // y=27
};

static const uint64_t BG_CLOUD_MASK[PAINTING_HEIGHT] = {
  0x0000000000ULL,  // y= 0
  0x0000000000ULL,  // y= 1
  0x0000000000ULL,  // y= 2
  0x0000000000ULL,  // y= 3
  0x0000000000ULL,  // y= 4
  0x0000000000ULL,  // y= 5
  0x0000000000ULL,  // y= 6
  0x0000000000ULL,  // y= 7
  0x0000000000ULL,  // y= 8
  0x0000000000ULL,  // y= 9
  0x0000000000ULL,  // y=10
  0x0000000000ULL,  // y=11
  0x0000000000ULL,  // y=12
  0x0000180000ULL,  // y=13
  0x00087e0000ULL,  // y=14
  0x003ffe8000ULL,  // y=15
  0x001c7e0000ULL,  // y=16
  0x0000661bf0ULL,  // y=17
  0x0000000000ULL,  // y=18
  0x0000000000ULL,  // y=19
  0x0000000000ULL,  // y=20
  0x0000000000ULL,  // y=21
  0x0000000000ULL,  // y=22
  0x0000000000ULL,  // y=23
  0x0000000000ULL,  // y=24
  0x0000000000ULL,  // y=25
  0x0000000000ULL,  // y=26
  0x0000000000ULL,  // y=27
};

static const uint64_t HORIZON_MASK[PAINTING_HEIGHT] = {
  0x0000000000ULL,  // y= 0
  0x0000000000ULL,  // y= 1
  0x0000000000ULL,  // y= 2
  0x0000000000ULL,  // y= 3
  0x0000000000ULL,  // y= 4
  0x0000000000ULL,  // y= 5
  0x0000000000ULL,  // y= 6
  0x0000000000ULL,  // y= 7
  0x0000000000ULL,  // y= 8
  0x0000000000ULL,  // y= 9
  0x0000000000ULL,  // y=10
  0x0000000000ULL,  // y=11
  0x0000000000ULL,  // y=12
  0x0000000000ULL,  // y=13
  0x0000000000ULL,  // y=14
  0x0000000000ULL,  // y=15
  0x0000000000ULL,  // y=16
  0x0000006000ULL,  // y=17
  0x0000007000ULL,  // y=18
  0x000002fc00ULL,  // y=19
  0x0fffffffffULL,  // y=20
  0x0fffffffffULL,  // y=21
  0x0fffffffffULL,  // y=22
  0x0fffffffffULL,  // y=23
  0x0fffffffffULL,  // y=24
  0x0fffffffffULL,  // y=25
  0x0fffffffffULL,  // y=26
  0x0fffffffffULL,  // y=27
};

uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) {
    return 0;
  }

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
  if (x < 0 || x >= PAINTING_WIDTH || y < 0 || y >= PAINTING_HEIGHT) {
    return false;
  }
  return (mask[y] >> x) & 1ULL;
}

static bool bodyVisible(int x, int y) {
  if (x < 0 || x >= PAINTING_WIDTH || y < 0 || y >= PAINTING_HEIGHT) {
    return false;
  }
  if (MASK_FOREGROUND_CLOUDS && maskBit(FG_CLOUD_MASK, x, y)) {
    return false;
  }
  if (MASK_BACKGROUND_CLOUDS && maskBit(BG_CLOUD_MASK, x, y)) {
    return false;
  }
  if (MASK_HORIZON && maskBit(HORIZON_MASK, x, y)) {
    return false;
  }
  return true;
}

static void skyPosition(float t, float *row, float *col) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  const float theta = PI * (1.0f - t);
  *col = SUN_CENTER_COL + SUN_ARC_RADIUS * cosf(theta);
  *row = SUN_CENTER_ROW - SUN_ARC_RADIUS * sinf(theta);
}

static void renderBody(float row, float col, float radius, CRGB color) {
  FastLED.clear();

  const int r0 = (int)floorf(row - radius - 1.0f);
  const int r1 = (int)ceilf(row + radius + 1.0f);
  const int c0 = (int)floorf(col - radius - 1.0f);
  const int c1 = (int)ceilf(col + radius + 1.0f);

  for (int y = r0; y <= r1; ++y) {
    for (int x = c0; x <= c1; ++x) {
      if (!bodyVisible(x, y)) {
        continue;
      }
      const float dist = hypotf((float)x - col, (float)y - row);
      float alpha = (radius - dist) / BODY_AA;
      if (alpha <= 0.0f) {
        continue;
      }
      if (alpha > 1.0f) {
        alpha = 1.0f;
      }
      const int px = x - PAINTING_ORIGIN_X;
      const int py = y - PAINTING_ORIGIN_Y;
      CRGB c = color;
      c.nscale8((uint8_t)(alpha * 255.0f + 0.5f));
      setPixel(px, py, c);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("animate-sun: sun then moon");
  Serial.printf("  origin=(%d,%d) inset L=%d R=%d\n",
                PAINTING_ORIGIN_X, PAINTING_ORIGIN_Y,
                SET_INSET_LEFT, SET_INSET_RIGHT);
  Serial.printf("  MASK_FG_CLOUDS=%d MASK_BG_CLOUDS=%d MASK_HORIZON=%d moon_r=%.2f\n",
                MASK_FOREGROUND_CLOUDS, MASK_BACKGROUND_CLOUDS, MASK_HORIZON,
                MOON_RADIUS);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_POWER_MA);
  FastLED.clear(true);
}

void loop() {
  const uint32_t elapsed = millis() % CYCLE_MS;
  const bool isMoon = elapsed >= SUN_DURATION_MS;
  const uint32_t phaseMs = isMoon ? (elapsed - SUN_DURATION_MS) : elapsed;
  const uint32_t phaseDur = isMoon ? MOON_DURATION_MS : SUN_DURATION_MS;
  const float t = (float)phaseMs / (float)phaseDur;

  float row, col;
  skyPosition(t, &row, &col);
  if (isMoon) {
    renderBody(row, col, MOON_RADIUS, MOON_COLOR);
  } else {
    renderBody(row, col, SUN_RADIUS, SUN_COLOR);
  }
  FastLED.show();
  delay(30);
}
