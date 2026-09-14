#include <Arduino.h>
#include <FastLED.h>

// ESP-WROOM-32 DevKit pin labeled D12 / GPIO 12.
#define LED_PIN 27
#define NUM_LEDS 3
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

static void show_white_percent(uint8_t percent) {
  const uint8_t brightness = (uint16_t)percent * 255 / 100;
  FastLED.setBrightness(brightness);
  fill_solid(leds, NUM_LEDS, CRGB::White);
  FastLED.show();
  Serial.printf("power-test: %u%% white (brightness %u/255)\n", percent,
                brightness);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.clear(true);
  Serial.println("power-test: 256 WS2812B on GPIO 12");
}

void loop() {
  show_white_percent(10);
  delay(1000);
  show_white_percent(20);
  delay(1000);
  show_white_percent(30);
  delay(1000);
}
