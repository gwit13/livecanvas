#include <Arduino.h>
#include <FastLED.h>

// =========================================================================
// Hardware Configuration
// =========================================================================
#define LED_PIN       13          // ESP32 GPIO pin connected to DIN (Data In)
#define LED_TYPE      WS2812B     // LED chipset
#define COLOR_ORDER   GRB         // Color order (WS2812B is typically GRB)
#define MATRIX_WIDTH  32          // Width of matrix in pixels
#define MATRIX_HEIGHT 8           // Height of matrix in pixels
#define NUM_LEDS      (MATRIX_WIDTH * MATRIX_HEIGHT) // 256 pixels

// -------------------------------------------------------------------------
// POWER SAFETY NOTE:
// 256 LEDs at full white can draw up to 15 Amps!
// When powered directly from the ESP32's VIN pin via USB, the USB port can
// only provide ~500mA. We cap the brightness and power below to avoid
// browning out or resetting the ESP32.
// -------------------------------------------------------------------------
#define BRIGHTNESS    20          // Safe brightness (scale 0-255, 20 is ~8%)
#define MAX_POWER_MA  500         // Maximum current in milliamps (USB safe)

// =========================================================================
// Matrix Wiring / Layout Options
// =========================================================================
// Most 8x32 flexible panels (e.g., BTF-LIGHTING) are wired in a
// COLUMN-MAJOR SERPENTINE (zigzag) layout:
//   Column 0: LEDs 0 -> 7 (top to bottom)
//   Column 1: LEDs 15 <- 8 (bottom to top)
//   Column 2: LEDs 16 -> 23 (top to bottom) ...
//
// If your panel is wired horizontally row-by-row, change CURRENT_LAYOUT:
#define LAYOUT_COL_SERPENTINE 1   // Vertical zigzag (most common 8x32)
#define LAYOUT_ROW_SERPENTINE 2   // Horizontal zigzag
#define LAYOUT_PROGRESSIVE    3   // Non-zigzag

#define CURRENT_LAYOUT LAYOUT_COL_SERPENTINE

CRGB leds[NUM_LEDS];

// =========================================================================
// 2D to 1D Mapping Function: XY(x, y) -> LED index
// =========================================================================
uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) {
    return 0; // Safe boundary check
  }

#if CURRENT_LAYOUT == LAYOUT_COL_SERPENTINE
  // Column-major zigzag (32 columns of 8 LEDs)
  if (x & 0x01) {
    // Odd columns run bottom to top
    return (x * MATRIX_HEIGHT) + (MATRIX_HEIGHT - 1 - y);
  } else {
    // Even columns run top to bottom
    return (x * MATRIX_HEIGHT) + y;
  }

#elif CURRENT_LAYOUT == LAYOUT_ROW_SERPENTINE
  // Row-major zigzag (8 rows of 32 LEDs)
  if (y & 0x01) {
    // Odd rows run right to left
    return (y * MATRIX_WIDTH) + (MATRIX_WIDTH - 1 - x);
  } else {
    // Even rows run left to right
    return (y * MATRIX_WIDTH) + x;
  }

#else
  // Progressive / linear (row by row)
  return (y * MATRIX_WIDTH) + x;
#endif
}

// Helper to set pixel by 2D coordinate
void setPixel(uint8_t x, uint8_t y, CRGB color) {
  if (x < MATRIX_WIDTH && y < MATRIX_HEIGHT) {
    leds[XY(x, y)] = color;
  }
}

// =========================================================================
// Individual Addressing Tests
// =========================================================================

// Test 1: Linear / Index Crawler (0 -> 255)
// Lights each LED one by one along the data chain with a fading trail.
void testLinearCrawler() {
  Serial.println(">>> [Test 1] Linear Index Crawler (0 to 255)...");
  FastLED.clear();

  for (int i = 0; i < NUM_LEDS; i++) {
    fadeToBlackBy(leds, NUM_LEDS, 64); // Fade previous LEDs to leave a comet tail
    leds[i] = CRGB::Cyan;
    FastLED.show();

    if (i % 32 == 0 || i == NUM_LEDS - 1) {
      Serial.printf("  LED Index: %d / %d\n", i, NUM_LEDS - 1);
    }
    delay(15);
  }
  delay(300);
}

// Test 2: Four Corners Test (2D Coordinate Addressing)
// Proves (X, Y) coordinate addressing and helps verify panel orientation.
void testFourCorners() {
  Serial.println(">>> [Test 2] Four Corners 2D Test (3 seconds)...");
  Serial.println("  Top-Left     (0, 0)   = RED");
  Serial.println("  Top-Right    (31, 0)  = GREEN");
  Serial.println("  Bottom-Left  (0, 7)   = BLUE");
  Serial.println("  Bottom-Right (31, 7)  = YELLOW");

  FastLED.clear();
  setPixel(0, 0, CRGB::Red);
  setPixel(MATRIX_WIDTH - 1, 0, CRGB::Green);
  setPixel(0, MATRIX_HEIGHT - 1, CRGB::Blue);
  setPixel(MATRIX_WIDTH - 1, MATRIX_HEIGHT - 1, CRGB::Yellow);
  FastLED.show();
  delay(3000);
}

// Test 3: Column Sweep (X = 0 -> 31)
// Sweeps a vertical line across each column.
void testColumnSweep() {
  Serial.println(">>> [Test 3] Column Sweep (X: 0 to 31)...");
  for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
    FastLED.clear();
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      setPixel(x, y, CRGB::Magenta);
    }
    FastLED.show();
    delay(40);
  }
}

// Test 4: Row Sweep (Y = 0 -> 7)
// Sweeps a horizontal line across each row.
void testRowSweep() {
  Serial.println(">>> [Test 4] Row Sweep (Y: 0 to 7)...");
  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    FastLED.clear();
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      setPixel(x, y, CRGB::Orange);
    }
    FastLED.show();
    delay(100);
  }
}

// Test 5: Bouncing Single Pixel
// Moves an individually addressed pixel around the 32x8 grid.
void testBouncingPixel() {
  Serial.println(">>> [Test 5] Bouncing Pixel across (X, Y)...");
  int x = 0, y = 0;
  int dx = 1, dy = 1;

  for (int step = 0; step < 120; step++) {
    fadeToBlackBy(leds, NUM_LEDS, 80);
    setPixel(x, y, CHSV((step * 4) % 255, 255, 255));
    FastLED.show();

    x += dx;
    y += dy;

    if (x <= 0 || x >= MATRIX_WIDTH - 1)  dx = -dx;
    if (y <= 0 || y >= MATRIX_HEIGHT - 1) dy = -dy;

    delay(30);
  }
}

// Test 6: 2D Rainbow Color Wave
// Verifies all 256 pixels can produce clean RGB hues smoothly.
void testRainbowWave() {
  Serial.println(">>> [Test 6] 2D Rainbow Wave...");
  for (uint16_t frame = 0; frame < 200; frame++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
        uint8_t hue = (frame * 2) + (x * 6) + (y * 10);
        setPixel(x, y, CHSV(hue, 255, 255));
      }
    }
    FastLED.show();
    delay(20);
  }
}

// =========================================================================
// Setup and Main Loop
// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(1000); // Allow serial monitor to open

  Serial.println();
  Serial.println("==================================================");
  Serial.println("  ESP32 WS2812B 8x32 Matrix Test (256 Pixels)");
  Serial.println("==================================================");
  Serial.printf("  Data Pin:     GPIO %d\n", LED_PIN);
  Serial.printf("  Dimensions:   %d x %d (%d LEDs)\n", MATRIX_WIDTH, MATRIX_HEIGHT, NUM_LEDS);
  Serial.printf("  Power Limit:  %d mA @ 5V (USB safe)\n", MAX_POWER_MA);
  Serial.printf("  Brightness:   %d / 255\n", BRIGHTNESS);
  Serial.println("==================================================");

  // Initialize FastLED on GPIO 13
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_POWER_MA); // Protect USB power rail

  // Flash all LEDs green briefly to confirm power & signal
  fill_solid(leds, NUM_LEDS, CRGB::Green);
  FastLED.show();
  delay(300);
  FastLED.clear();
  FastLED.show();
  delay(200);

  Serial.println("Initialization complete. Starting test cycles...\n");
}

void loop() {
  // Run test sequence
  testLinearCrawler();
  testFourCorners();
  testColumnSweep();
  testRowSweep();
  testBouncingPixel();
  testRainbowWave();

  // Clear and pause before repeating
  FastLED.clear();
  FastLED.show();
  Serial.println("\nAll tests completed. Repeating cycle in 1 second...\n");
  delay(1000);
}
