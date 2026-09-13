#pragma once

#include <time.h>
#include <stdint.h>

// Clock snapshot for one animation tick. Fill this from NTP/NOAA
// (see getSkyTime in animate-realtime.cpp). sunrise/sunset are today's
// civil times; sunrise_next / sunset_prev cover the night wrap.
struct SkyTime {
  time_t now;
  time_t sunrise;
  time_t sunset;
  time_t sunrise_next;
  time_t sunset_prev;
};

struct SkyPhase {
  bool visible;
  float t;  // 0 at rise, 1 at set
};

struct SkyFrame {
  SkyPhase sun;
  SkyPhase moon;
};

inline float skyProgress(time_t now, time_t start, time_t end) {
  time_t span = end - start;
  if (span <= 0) {
    return 0.0f;
  }
  float t = (float)(now - start) / (float)span;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t;
}

// Day: now in [sunrise, sunset). Night: otherwise, moon 0 at sunset
// of the evening that started this night, 1 at the following sunrise.
inline SkyFrame mapSky(SkyTime t) {
  SkyFrame f;
  f.sun.visible = false;
  f.sun.t = 0.0f;
  f.moon.visible = false;
  f.moon.t = 0.0f;

  if (t.now >= t.sunrise && t.now < t.sunset) {
    f.sun.visible = true;
    f.sun.t = skyProgress(t.now, t.sunrise, t.sunset);
    return f;
  }

  f.moon.visible = true;
  if (t.now >= t.sunset) {
    f.moon.t = skyProgress(t.now, t.sunset, t.sunrise_next);
  } else {
    f.moon.t = skyProgress(t.now, t.sunset_prev, t.sunrise);
  }
  return f;
}
