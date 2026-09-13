#pragma once
#include <math.h>

namespace NOAA {

constexpr double DEG2RAD = 0.017453292519943295;
constexpr double RAD2DEG = 57.29577951308232;

inline double degToRad(double deg) { return deg * DEG2RAD; }
inline double radToDeg(double rad) { return rad * RAD2DEG; }

// Julian Day from year, month, day
inline double getJulianDay(int year, int month, int day) {
  if (month <= 2) {
    year -= 1;
    month += 12;
  }
  double A = floor(year / 100.0);
  double B = 2.0 - A + floor(A / 4.0);
  return floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) + day + B - 1524.5;
}

// Julian Century from Julian Day
inline double getJulianCentury(double jd) {
  return (jd - 2451545.0) / 36525.0;
}

// Geometric Mean Longitude of Sun (deg)
inline double getGeomMeanLongSun(double t) {
  double l0 = 280.46646 + t * (36000.76983 + 0.0003032 * t);
  while (l0 > 360.0) l0 -= 360.0;
  while (l0 < 0.0) l0 += 360.0;
  return l0;
}

// Geometric Mean Anomaly of Sun (deg)
inline double getGeomMeanAnomalySun(double t) {
  return 357.52911 + t * (35999.05029 - 0.0001537 * t);
}

// Eccentricity of Earth's Orbit
inline double getEccentricityEarthOrbit(double t) {
  return 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
}

// Sun Equation of Center (deg)
inline double getSunEqOfCenter(double t) {
  double m = getGeomMeanAnomalySun(t);
  double mrad = degToRad(m);
  return sin(mrad) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
         sin(2.0 * mrad) * (0.019993 - 0.000101 * t) +
         sin(3.0 * mrad) * 0.000289;
}

// Sun True Longitude (deg)
inline double getSunTrueLong(double t) {
  return getGeomMeanLongSun(t) + getSunEqOfCenter(t);
}

// Sun Apparent Longitude (deg)
inline double getSunApparentLong(double t) {
  double o = getSunTrueLong(t);
  double omega = 125.04 - 1934.136 * t;
  return o - 0.00569 - 0.00478 * sin(degToRad(omega));
}

// Mean Obliquity of Ecliptic (deg)
inline double getMeanObliquityOfEcliptic(double t) {
  double seconds = 21.448 - t * (46.8150 + t * (0.00059 - t * 0.001813));
  return 23.0 + (26.0 + (seconds / 60.0)) / 60.0;
}

// Obliquity Correction (deg)
inline double getObliquityCorrection(double t) {
  double e0 = getMeanObliquityOfEcliptic(t);
  double omega = 125.04 - 1934.136 * t;
  return e0 + 0.00256 * cos(degToRad(omega));
}

// Sun Declination (deg)
inline double getSunDeclination(double t) {
  double e = getObliquityCorrection(t);
  double lambda = getSunApparentLong(t);
  double sint = sin(degToRad(e)) * sin(degToRad(lambda));
  return radToDeg(asin(sint));
}

// Equation of Time (minutes)
inline double getEquationOfTime(double t) {
  double epsilon = getObliquityCorrection(t);
  double l0 = getGeomMeanLongSun(t);
  double e = getEccentricityEarthOrbit(t);
  double m = getGeomMeanAnomalySun(t);

  double y = tan(degToRad(epsilon) / 2.0);
  y *= y;

  double sin2l0 = sin(2.0 * degToRad(l0));
  double sinm = sin(degToRad(m));
  double cos2l0 = cos(2.0 * degToRad(l0));
  double sin4l0 = sin(4.0 * degToRad(l0));
  double sin2m = sin(2.0 * degToRad(m));

  double eTime = y * sin2l0 - 2.0 * e * sinm + 4.0 * e * y * sinm * cos2l0 -
                 0.5 * y * y * sin4l0 - 1.25 * e * e * sin2m;
  return radToDeg(eTime) * 4.0;
}

// Hour Angle for Sunrise/Sunset (deg)
// 90.833 deg accounts for standard 34 arcmin refraction + 16 arcmin solar semi-diameter
inline double getHourAngleSunrise(double lat, double solarDec) {
  double latRad = degToRad(lat);
  double sdRad = degToRad(solarDec);
  double HAarg = (cos(degToRad(90.833)) / (cos(latRad) * cos(sdRad)) - tan(latRad) * tan(sdRad));
  if (HAarg > 1.0) HAarg = 1.0;
  if (HAarg < -1.0) HAarg = -1.0;
  return radToDeg(acos(HAarg));
}

// Returns UTC sunrise and sunset in minutes from UTC midnight
// lat: degrees North (positive), lon: degrees East (positive, West is negative!)
inline void calculateSunriseSunsetUTC(int year, int month, int day, double lat, double lon,
                                     double &sunriseMinUTC, double &sunsetMinUTC) {
  double jd = getJulianDay(year, month, day);
  double t = getJulianCentury(jd);

  double eqTime = getEquationOfTime(t);
  double solarDec = getSunDeclination(t);
  double ha = getHourAngleSunrise(lat, solarDec);

  // Solar noon in UTC minutes:
  double solarNoonUTC = 720.0 - (4.0 * lon) - eqTime;

  sunriseMinUTC = solarNoonUTC - (ha * 4.0);
  sunsetMinUTC  = solarNoonUTC + (ha * 4.0);
}

} // namespace NOAA
