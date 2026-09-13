#include <Arduino.h>
#include <FastLED.h>

// =========================================================================
// Hardware Configuration
// =========================================================================
#define LED_PIN          12         // ESP32 GPIO pin connected to DIN of first panel
#define LED_TYPE         WS2812B    // LED chipset
#define COLOR_ORDER      GRB        // Color order

#define PANEL_WIDTH      32         // Width of each individual panel
#define PANEL_HEIGHT     8          // Height of each individual panel
#define NUM_PANELS       3          // Three panels daisy-chained vertically (Top, Middle, Bottom)

#define MATRIX_WIDTH     PANEL_WIDTH                 // 32 columns (X: 0..31)
#define MATRIX_HEIGHT    (PANEL_HEIGHT * NUM_PANELS) // 24 rows total (Y: 0..23)
#define NUM_LEDS         (MATRIX_WIDTH * MATRIX_HEIGHT) // 768 pixels total

// -------------------------------------------------------------------------
// POWER SAFETY NOTE:
// 768 LEDs at full white can draw over 45 Amps!
// When running from ESP32 VIN via USB, the USB port provides ~500mA.
// FastLED's active power manager strictly caps current to 500mA.
// -------------------------------------------------------------------------
#define BRIGHTNESS       20         // Safe brightness (scale 0-255, 20 is ~8%)
#define MAX_POWER_MA     500        // Maximum current in milliamps (USB safe)

// =========================================================================
// Independent Per-Panel Orientation Flags
// =========================================================================
// Because each daisy-chain link loops to the panel below, the orientation
// alternates (serpentine daisy-chaining):
//
// Panel 1 (Top, LEDs 0..255, Y: 0..7):
#define PANEL_1_FLIP_X   true
#define PANEL_1_FLIP_Y   true

// Panel 2 (Middle, LEDs 256..511, Y: 8..15):
// Rotated 180° so DOUT of Panel 1 meets DIN of Panel 2 on the right edge.
#define PANEL_2_FLIP_X   false
#define PANEL_2_FLIP_Y   false

// Panel 3 (Bottom, LEDs 512..767, Y: 16..23):
// DOUT of Panel 2 is on the left edge, meeting DIN of Panel 3 on the left.
// Matches the exact orientation of Panel 1.
#define PANEL_3_FLIP_X   true
#define PANEL_3_FLIP_Y   true

CRGB leds[NUM_LEDS];

// =========================================================================
// 2D to 1D Mapping: XY(x, y) -> LED index (0..767)
// =========================================================================
uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) {
    return 0; // Boundary safety check
  }

  uint8_t panelIndex = y / PANEL_HEIGHT; // 0 = Top, 1 = Middle, 2 = Bottom
  uint8_t py = y % PANEL_HEIGHT;         // 0..7 within the target panel
  uint8_t px = x;                        // 0..31 within the target panel

  // Apply specific panel orientation transformations
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

  // Column-Major Serpentine layout within each 8x32 panel
  uint16_t indexInPanel;
  if (px & 0x01) {
    // Odd columns run bottom to top
    indexInPanel = (px * PANEL_HEIGHT) + (PANEL_HEIGHT - 1 - py);
  } else {
    // Even columns run top to bottom
    indexInPanel = (px * PANEL_HEIGHT) + py;
  }

  return (panelIndex * (PANEL_WIDTH * PANEL_HEIGHT)) + indexInPanel;
}

// Helper to set pixel by 2D coordinate
void setPixel(int x, int y, CRGB color) {
  if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT) {
    leds[XY((uint8_t)x, (uint8_t)y)] = color;
  }
}

// =========================================================================
// Graphic Helpers for Orientation Diagnostics
// =========================================================================

// Draw Digit '1' (4 wide, 6 high)
void drawDigit1(int x, int y, CRGB color) {
  setPixel(x + 1, y + 0, color);
  setPixel(x + 0, y + 1, color);
  setPixel(x + 1, y + 1, color);
  setPixel(x + 1, y + 2, color);
  setPixel(x + 1, y + 3, color);
  setPixel(x + 1, y + 4, color);
  setPixel(x + 0, y + 5, color);
  setPixel(x + 1, y + 5, color);
  setPixel(x + 2, y + 5, color);
}

// Draw Digit '2' (5 wide, 6 high)
void drawDigit2(int x, int y, CRGB color) {
  setPixel(x + 1, y + 0, color);
  setPixel(x + 2, y + 0, color);
  setPixel(x + 3, y + 0, color);
  setPixel(x + 0, y + 1, color);
  setPixel(x + 4, y + 1, color);
  setPixel(x + 3, y + 2, color);
  setPixel(x + 2, y + 3, color);
  setPixel(x + 1, y + 4, color);
  setPixel(x + 0, y + 5, color);
  setPixel(x + 1, y + 5, color);
  setPixel(x + 2, y + 5, color);
  setPixel(x + 3, y + 5, color);
  setPixel(x + 4, y + 5, color);
}

// Draw Digit '3' (5 wide, 6 high)
void drawDigit3(int x, int y, CRGB color) {
  setPixel(x + 1, y + 0, color);
  setPixel(x + 2, y + 0, color);
  setPixel(x + 3, y + 0, color);
  setPixel(x + 4, y + 0, color);
  setPixel(x + 4, y + 1, color);
  setPixel(x + 2, y + 2, color);
  setPixel(x + 3, y + 2, color);
  setPixel(x + 4, y + 2, color);
  setPixel(x + 4, y + 3, color);
  setPixel(x + 4, y + 4, color);
  setPixel(x + 1, y + 5, color);
  setPixel(x + 2, y + 5, color);
  setPixel(x + 3, y + 5, color);
  setPixel(x + 4, y + 5, color);
  setPixel(x + 0, y + 4, color);
}

// Draw X-axis label and arrow: "X --->"
void drawXAxisArrow(int x, int y, CRGB color) {
  // Letter 'X' (3x3)
  setPixel(x + 0, y + 0, color);
  setPixel(x + 2, y + 0, color);
  setPixel(x + 1, y + 1, color);
  setPixel(x + 0, y + 2, color);
  setPixel(x + 2, y + 2, color);

  // Arrow shaft
  for (int xi = x + 5; xi <= x + 14; xi++) {
    setPixel(xi, y + 1, color);
  }

  // Arrowhead pointing right
  setPixel(x + 13, y + 0, color);
  setPixel(x + 14, y + 1, color);
  setPixel(x + 13, y + 2, color);
}

// Draw Y-axis label and arrow: "Y | v" pointing down across all 3 panels
void drawYAxisArrow(int x, CRGB color) {
  // Letter 'Y' (3x3) at top-left
  setPixel(x + 0, 0, color);
  setPixel(x + 2, 0, color);
  setPixel(x + 1, 1, color);
  setPixel(x + 1, 2, color);

  // Vertical arrow shaft running down through rows 4..21 (all 3 panels)
  for (int yi = 4; yi <= 21; yi++) {
    setPixel(x + 1, yi, color);
  }

  // Arrowhead pointing DOWN at row 22 on the bottom panel
  setPixel(x + 0, 20, color);
  setPixel(x + 2, 20, color);
  setPixel(x + 1, 22, color);
}

// =========================================================================
// Test Patterns
// =========================================================================

// Test 1: Alignment & Orientation Map ("1", "2", "3" & Directional Arrows)
void testOrientationMap() {
  Serial.println("==================================================");
  Serial.println(">>> [Test 1] Alignment Map: '1', '2', '3' & Axes (5 sec)");
  Serial.println("  Top Panel (1):    Digit '1' in CYAN   + Arrow (X --->) in GREEN");
  Serial.println("  Middle Panel (2): Digit '2' in ORANGE + Arrow (X --->) in GREEN");
  Serial.println("  Bottom Panel (3): Digit '3' in YELLOW + Arrow (X --->) in GREEN");
  Serial.println("  Left Edge:        Arrow (Y | v) in MAGENTA pointing DOWN (all 3 panels)");
  Serial.println("  Seam Lines:       Dotted RED lines at Y=7/8 and Y=15/16");
  Serial.println("==================================================");

  FastLED.clear();

  // 1. Draw Digit '1' and X-arrow on Top Panel (Y: 0..7)
  drawDigit1(6, 1, CRGB::Cyan);
  drawXAxisArrow(14, 2, CRGB::Green);

  // 2. Draw Digit '2' and X-arrow on Middle Panel (Y: 8..15)
  drawDigit2(6, 9, CRGB(255, 110, 0)); // Warm Amber/Orange
  drawXAxisArrow(14, 10, CRGB::Green);

  // 3. Draw Digit '3' and X-arrow on Bottom Panel (Y: 16..23)
  drawDigit3(6, 17, CRGB::Yellow);
  drawXAxisArrow(14, 18, CRGB::Green);

  // 4. Draw Y-axis arrow pointing DOWN across all 3 panels
  drawYAxisArrow(1, CRGB::Magenta);

  // 5. Draw panel seam indicators at Y=7 and Y=15
  for (int xi = 12; xi < MATRIX_WIDTH; xi += 2) {
    setPixel(xi, 7,  CRGB(50, 0, 0)); // Seam between Panel 1 & 2
    setPixel(xi, 15, CRGB(50, 0, 0)); // Seam between Panel 2 & 3
  }

  FastLED.show();
  delay(5000); // 5 seconds display hold
}

// Test 2: Downward Row Sweep (Top Y=0 down to Bottom Y=23)
// Confirms that horizontal bars smoothly cross both seams.
void testRowSweep() {
  Serial.println(">>> [Test 2] Row Sweep (Y: 0 down to 23)...");
  for (uint8_t repeat = 0; repeat < 2; repeat++) {
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      FastLED.clear();
      CRGB color;
      if (y < PANEL_HEIGHT) {
        color = CRGB::Cyan;
      } else if (y < PANEL_HEIGHT * 2) {
        color = CRGB(255, 110, 0); // Orange
      } else {
        color = CRGB::Yellow;
      }

      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        setPixel(x, y, color);
      }
      FastLED.show();
      delay(45);
    }
  }
}

// Test 3: Horizontal Column Sweep (Left X=0 to Right X=31)
// Confirms that 24-pixel tall vertical bars move left to right across all 3 panels.
void testColumnSweep() {
  Serial.println(">>> [Test 3] Column Sweep (X: 0 to 31 across all 3 panels)...");
  for (uint8_t repeat = 0; repeat < 2; repeat++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      FastLED.clear();
      for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
        setPixel(x, y, CRGB::Green);
      }
      FastLED.show();
      delay(30);
    }
  }
}

// Test 4: Bouncing Pixel across the Unified 32x24 Grid
void testBouncingPixel() {
  Serial.println(">>> [Test 4] Bouncing Pixel across 32x24 boundaries...");
  int x = 0, y = 0;
  int dx = 1, dy = 1;

  for (int step = 0; step < 180; step++) {
    fadeToBlackBy(leds, NUM_LEDS, 65);
    setPixel(x, y, CHSV((step * 3) % 255, 255, 255));
    FastLED.show();

    x += dx;
    y += dy;

    if (x <= 0 || x >= MATRIX_WIDTH - 1)  dx = -dx;
    if (y <= 0 || y >= MATRIX_HEIGHT - 1) dy = -dy;

    delay(25);
  }
}

// =========================================================================
// Setup and Main Loop
// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==================================================");
  Serial.println("  ESP32 WS2812B Triple Panel 32x24 Array (768 LEDs)");
  Serial.println("==================================================");
  Serial.printf("  Data Pin:       GPIO %d\n", LED_PIN);
  Serial.printf("  Dimensions:     %d x %d (%d LEDs total)\n", MATRIX_WIDTH, MATRIX_HEIGHT, NUM_LEDS);
  Serial.printf("  Panel 1 (Top):    FLIP_X=%d, FLIP_Y=%d\n", PANEL_1_FLIP_X, PANEL_1_FLIP_Y);
  Serial.printf("  Panel 2 (Middle): FLIP_X=%d, FLIP_Y=%d\n", PANEL_2_FLIP_X, PANEL_2_FLIP_Y);
  Serial.printf("  Panel 3 (Bottom): FLIP_X=%d, FLIP_Y=%d\n", PANEL_3_FLIP_X, PANEL_3_FLIP_Y);
  Serial.printf("  Power Limit:    %d mA @ 5V (USB Safe)\n", MAX_POWER_MA);
  Serial.printf("  Brightness:     %d / 255\n", BRIGHTNESS);
  Serial.println("==================================================");

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_POWER_MA);

  // Brief flash to confirm all 768 LEDs respond
  fill_solid(leds, NUM_LEDS, CRGB(0, 25, 0));
  FastLED.show();
  delay(250);
  FastLED.clear();
  FastLED.show();
  delay(200);

  Serial.println("Starting 3-panel test cycle...\n");
}

void loop() {
  // 1. Orientation map with "1", "2", "3", and directional arrows (5s hold)
  testOrientationMap();

  // 2. Downward row sweep (Top to Bottom across both seams)
  testRowSweep();

  // 3. Left-to-right column sweep (24px bar across all 3 panels)
  testColumnSweep();

  // 4. Bouncing pixel across all 3 panels
  testBouncingPixel();

  FastLED.clear();
  FastLED.show();
  Serial.println("\nCycle complete. Repeating in 1 second...\n");
  delay(1000);
}
