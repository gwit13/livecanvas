#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "noaa_solar.h"
#include "secrets.h"

// Austin, TX coordinates (from include/secrets.h)
// Latitude: 30.2672 N, Longitude: -97.7431 W
// Timezone: Central Daylight Time (CDT) = UTC - 5 hours
#define TIMEZONE_OFFSET_HOURS (-5)

void printPadded(int val) {
  if (val < 10) Serial.print("0");
  Serial.print(val);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==================================================");
  Serial.println("   LiveCanvas: Wi-Fi & NOAA Solar Calculation Test");
  Serial.println("==================================================");
  Serial.println("Target Location: Austin, Texas");
  Serial.printf("  Latitude:   %.4f N\n", LOCATION_LATITUDE);
  Serial.printf("  Longitude:  %.4f W\n", LOCATION_LONGITUDE);
  Serial.printf("  Timezone:   UTC %d (CDT)\n", TIMEZONE_OFFSET_HOURS);
  Serial.println("==================================================\n");

  // 1. Connect to Wi-Fi
  Serial.printf("[1/3] Connecting to Wi-Fi: '%s' ...", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" CONNECTED!");
    Serial.printf("      ESP32 Local IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("      Signal (RSSI):  %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("\n[ERROR] Wi-Fi connection timed out. Please verify SSID and password.");
    return;
  }

  // 2. Fetch Time from NTP Endpoint
  Serial.println("\n[2/3] Fetching real-world time from NTP endpoint (pool.ntp.org)...");
  // Configure NTP: 0 offset so C library internal clock is UTC
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

  time_t now = time(nullptr);
  uint32_t ntpStart = millis();
  while (now < 100000000 && millis() - ntpStart < 15000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }

  if (now < 100000000) {
    Serial.println("\n[ERROR] NTP time fetch timed out.");
    return;
  }

  Serial.println(" SUCCESS!");

  // 3. Compute NOAA Solar Times for Austin, TX
  Serial.println("\n[3/3] Running NOAA Astronomical Solar Algorithm for Austin, TX...\n");

  struct tm utcTm;
  gmtime_r(&now, &utcTm);

  int year  = utcTm.tm_year + 1900;
  int month = utcTm.tm_mon + 1;
  int day   = utcTm.tm_mday;

  // Run the NOAA equations
  double riseMinUTC = 0, setMinUTC = 0;
  NOAA::calculateSunriseSunsetUTC(year, month, day,
                                 LOCATION_LATITUDE, LOCATION_LONGITUDE,
                                 riseMinUTC, setMinUTC);

  // Convert UTC minutes to local Austin CDT (UTC - 5 hours)
  double riseMinLocal = riseMinUTC + (TIMEZONE_OFFSET_HOURS * 60.0);
  double setMinLocal  = setMinUTC  + (TIMEZONE_OFFSET_HOURS * 60.0);

  if (riseMinLocal < 0) riseMinLocal += 1440.0;
  if (setMinLocal < 0)  setMinLocal  += 1440.0;

  int riseHour = (int)(riseMinLocal / 60.0);
  int riseMin  = (int)fmod(riseMinLocal, 60.0);

  int setHour = (int)(setMinLocal / 60.0);
  int setMin  = (int)fmod(setMinLocal, 60.0);

  double dayLengthHours = (setMinUTC - riseMinUTC) / 60.0;

  // Current Local Time in Austin
  time_t localEpoch = now + (TIMEZONE_OFFSET_HOURS * 3600);
  struct tm localTm;
  gmtime_r(&localEpoch, &localTm);

  Serial.println("--------------------------------------------------");
  Serial.printf("  Date (Austin, TX):    %04d-%02d-%02d\n", year, month, day);
  Serial.printf("  Current Local Time:   %02d:%02d:%02d CDT\n",
                localTm.tm_hour, localTm.tm_min, localTm.tm_sec);
  Serial.printf("  Current UTC Time:     %02d:%02d:%02d UTC\n",
                utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec);
  Serial.println("--------------------------------------------------");
  Serial.printf("  NOAA Sunrise Today:   %02d:%02d AM CDT  (%02d:%02d UTC)\n",
                riseHour, riseMin,
                (int)(riseMinUTC / 60.0), (int)fmod(riseMinUTC, 60.0));
  Serial.printf("  NOAA Sunset Today:    %02d:%02d PM CDT  (%02d:%02d UTC)\n",
                (setHour > 12) ? setHour - 12 : setHour, setMin,
                (int)(setMinUTC / 60.0), (int)fmod(setMinUTC, 60.0));
  Serial.printf("  Total Day Length:     %.2f hours (%d hrs, %d mins)\n",
                dayLengthHours, (int)dayLengthHours, (int)((dayLengthHours - (int)dayLengthHours) * 60.0));
  Serial.println("--------------------------------------------------");

  // Real-time position status
  double currentMinOfDay = (localTm.tm_hour * 60.0) + localTm.tm_min + (localTm.tm_sec / 60.0);
  bool isDay = (currentMinOfDay >= riseMinLocal && currentMinOfDay < setMinLocal);

  if (isDay) {
    double progress = (currentMinOfDay - riseMinLocal) / (setMinLocal - riseMinLocal);
    Serial.printf("  Current Status:       DAYTIME (Sun is UP)\n");
    Serial.printf("  Sun Progress (0..1):  %.3f (%.1f%% across the sky arc)\n",
                  progress, progress * 100.0);
  } else {
    Serial.printf("  Current Status:       NIGHTTIME (Moon is UP)\n");
  }
  Serial.println("==================================================\n");
}

void loop() {
  // Print live time update every 10 seconds
  time_t now = time(nullptr);
  time_t localEpoch = now + (TIMEZONE_OFFSET_HOURS * 3600);
  struct tm localTm;
  gmtime_r(&localEpoch, &localTm);

  Serial.printf("[%02d:%02d:%02d CDT] ESP32 clock running normally.\n",
                localTm.tm_hour, localTm.tm_min, localTm.tm_sec);
  delay(10000);
}
