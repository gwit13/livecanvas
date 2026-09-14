# livecanvas

Dynamic, living wall art synchronized with the sky outside.

`livecanvas` illuminates physical artwork using an ESP32 microcontroller driving a 32x24 grid of WS2812B RGB LEDs (768 pixels) mounted behind the canvas. By synchronizing with Wi-Fi NTP and calculating real-time astronomical events using the NOAA Solar Calculator, the display mirrors the actual sun, moon, and stars outside in real time, factoring in realistic cloud diffusion and terrain horizon occlusions.

---

## Hardware Overview

- **Microcontroller**: ESP-WROOM-32 / ESP32 DevKit (30-pin or 38-pin).
- **Display**: Three daisy-chained 8x32 flexible WS2812B LED panels stacked vertically to create a 32x24 pixel matrix.
  - **Panel 1 (Top)**: 32x8 (X-flipped, Y-flipped)
  - **Panel 2 (Middle)**: 32x8 (Standard orientation)
  - **Panel 3 (Bottom)**: 32x8 (X-flipped, Y-flipped)
- **Power**: 5V / VIN. FastLED software power limiter defaults to 500 mA with brightness at 80 for safe USB-powered operation. An external 5V supply (3A–4A) can be connected to the matrix power rails for higher brightness.

### Wiring

| ESP32 Pin | Matrix Connection | Notes |
|:---:|:---:|:---|
| **GPIO 27 (`D27`)** | DIN (Panel 1) | LED Data signal. Free of boot strapping traps. |
| **VIN / 5V** | 5V / VCC | Panel power input. |
| **GND** | GND | Common ground. |

> [!NOTE]
> **Pin Selection (Avoid GPIO 12)**:
> Earlier iterations used GPIO 12 (`MTDI`). Because GPIO 12 is an ESP32 bootstrapping pin that sets flash voltage to 1.8V if pulled HIGH during boot, connecting it directly to WS2812B data lines causes boot hangs on standalone power. **GPIO 27 (`D27`)** is free of strapping conflicts and provides ample physical clearance from VIN and GND for hand soldering.

---

## Key Features

- **NOAA Solar Engine (`include/noaa_solar.h`)**:
  - Implements the exact NOAA Solar Calculator astronomical algorithm entirely on-chip.
  - Computes solar noon, equation of time, solar declination, sunrise, and sunset based on your exact latitude and longitude.
- **Continuous Local-Day Time Tracking (`src/animate-realtime.cpp`)**:
  - Automatically resolves daylight and nighttime intervals based on local calendar dates (`epoch + TIMEZONE_OFFSET_SEC`).
  - Eliminates UTC-rollover discontinuities (such as early dusk or freeze when UTC crosses midnight during local evening).
- **Art-Aware Occlusion & Diffusion**:
  - **Horizon Masking**: Prevents the sun and moon from shining through solid terrain.
  - **Cloud Diffusion**: Near and far cloud layers soften and diffuse celestial bodies as they pass behind clouds.
  - **Color Shifting**: Sun automatically warms toward a deep orange/red as it approaches the horizon.
- **Robust Standalone Operation**:
  - Wi-Fi NTP synchronization with internal time-slewing to prevent sudden jumps.
  - Background Wi-Fi auto-reconnect with continuous local clock fallback during network interruptions.

---

## Project Structure

```
livecanvas/
├── assets/                  # Artwork masks, bitmasks, and preprocessing scripts
│   ├── categorized-human-more-clouds.csv # Cloud and terrain bitmasks
│   └── scripts/             # Python tools for image discretization and alignment
├── include/
│   ├── noaa_solar.h         # On-device NOAA Solar Calculator implementation
│   ├── secrets.example.h    # Wi-Fi and location configuration template
│   └── sky_time.h           # Day/night astronomical state machine & progress mapping
├── src/
│   ├── animate-realtime.cpp # Primary firmware: 24/7 real-time astronomical sync
│   ├── animate-sun.cpp      # Standalone day-only sun animation
│   ├── animate-sun-moon-stars.cpp # Standalone full celestial cycle animation
│   ├── endpoint-test.cpp    # Test utility: Wi-Fi & NOAA solar calculations
│   ├── panel_test.cpp       # Matrix alignment, orientation, and serpentine test
│   └── power-test.cpp       # FastLED power limiter verification
└── platformio.ini           # PlatformIO project environments and build configurations
```

---

## Quick Start

### 1. Configure Secrets

Copy the example secrets file and edit it with your Wi-Fi credentials and coordinates:

```powershell
cp include/secrets.example.h include/secrets.h
```

Edit `include/secrets.h`:

```cpp
#pragma once

#define WIFI_SSID            "YourNetworkName"
#define WIFI_PASSWORD        "YourNetworkPassword"

// Coordinates (e.g. Austin, TX)
#define LOCATION_LATITUDE    30.2672f
#define LOCATION_LONGITUDE  -97.7431f

// Timezone offset in seconds (e.g. UTC-5 for CDT: -5 * 3600)
#define TIMEZONE_OFFSET_SEC  (-5 * 3600)
#define DAYLIGHT_OFFSET_SEC  (0)
```

> [!TIP]
> `include/secrets.h` is excluded from git tracking by `.gitignore`.

---

### 2. Build & Upload

Using [PlatformIO](https://platformio.org/):

#### Main Firmware: Real-Time Astronomical Sync
```powershell
pio run -e animate-realtime -t upload
```

Monitor serial output with local timestamp and solar progress logs:
```powershell
pio device monitor -e animate-realtime
```

#### Speed / Preview Mode
In `src/animate-realtime.cpp`:
- `TIME_MULTIPLIER 1.0f`: Standard 1x real-time (24 hours in real life = 24 hours on matrix).
- `TIME_MULTIPLIER 720.0f`: Fast preview mode (complete 24-hour cycle in 2 minutes).

---

### 3. Diagnostic Environments

- **Matrix Test**:
  Verify panel serpentine mappings, alignment arrows, and panel orientations:
  ```powershell
  pio run -e panel_test -t upload
  ```

- **NOAA & Wi-Fi Check**:
  Verify network connection and print today's sunrise/sunset UTC and local times:
  ```powershell
  pio run -e endpoint-test -t upload
  ```

- **Sun / Moon Animation (Offline Preview)**:
  Run looping celestial animations without Wi-Fi or NTP:
  ```powershell
  pio run -e animate-sun-moon-stars -t upload
  ```