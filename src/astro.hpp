#pragma once
// chronos::astro — self-contained sun & moon math.
//
// Sunrise/sunset use the standard NOAA solar-position approximation.
// Moon phase uses the synodic-month age since a known new moon.
// No external deps — just <cmath> and <ctime>.

#include <cmath>
#include <ctime>
#include <optional>
#include <string>
#include <algorithm>

namespace chronos::astro {

constexpr double PI  = 3.14159265358979323846;
constexpr double D2R = PI / 180.0;
constexpr double R2D = 180.0 / PI;

inline double norm360(double x) {
    x = std::fmod(x, 360.0);
    return x < 0 ? x + 360.0 : x;
}

// Day of year (1-366) for a civil date.
inline int day_of_year(int y, int m, int d) {
    static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int doy = cum[m - 1] + d;
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    if (leap && m > 2) doy++;
    return doy;
}

struct SunTimes {
    bool valid       = false;   // false → polar day/night (no rise/set)
    bool always_up   = false;   // sun never sets
    bool always_down = false;   // sun never rises
    double sunrise_h = 0;       // local solar hours (0-24), in the given tz
    double sunset_h  = 0;
    double daylight_h = 0;      // length of day in hours
};

// NOAA sunrise/sunset for a date at (lat, lon).
// tz_offset_hours is the local UTC offset (e.g. +5.5 for IST).
// Returns times in LOCAL clock hours.
inline SunTimes sun_times(int year, int mon, int day,
                          double lat, double lon, double tz_offset_hours) {
    SunTimes out;
    const double zenith = 90.833;  // official sunrise/sunset (incl. refraction)

    auto compute = [&](bool rising) -> std::optional<double> {
        int N = day_of_year(year, mon, day);
        double lngHour = lon / 15.0;
        double t = rising ? N + ((6.0 - lngHour) / 24.0)
                          : N + ((18.0 - lngHour) / 24.0);

        double M = (0.9856 * t) - 3.289;                       // mean anomaly
        double L = M + (1.916 * std::sin(M * D2R))
                     + (0.020 * std::sin(2 * M * D2R)) + 282.634;
        L = norm360(L);

        double RA = R2D * std::atan(0.91764 * std::tan(L * D2R));
        RA = norm360(RA);
        double Lquad  = std::floor(L / 90.0) * 90.0;
        double RAquad = std::floor(RA / 90.0) * 90.0;
        RA = (RA + (Lquad - RAquad)) / 15.0;                   // into hours

        double sinDec = 0.39782 * std::sin(L * D2R);
        double cosDec = std::cos(std::asin(sinDec));

        double cosH = (std::cos(zenith * D2R) - (sinDec * std::sin(lat * D2R)))
                      / (cosDec * std::cos(lat * D2R));
        if (cosH > 1)  { out.always_down = true; return std::nullopt; }
        if (cosH < -1) { out.always_up   = true; return std::nullopt; }

        double H = rising ? 360.0 - R2D * std::acos(cosH)
                          : R2D * std::acos(cosH);
        H /= 15.0;

        double T  = H + RA - (0.06571 * t) - 6.622;            // local mean time
        double UT = std::fmod(T - lngHour + 24.0, 24.0);       // to UTC
        double local = std::fmod(UT + tz_offset_hours + 24.0, 24.0);
        return local;
    };

    auto rise = compute(true);
    auto set  = compute(false);
    if (out.always_up)   { out.valid = true; out.daylight_h = 24; return out; }
    if (out.always_down) { out.valid = true; out.daylight_h = 0;  return out; }
    if (!rise || !set)   return out;

    out.valid      = true;
    out.sunrise_h  = *rise;
    out.sunset_h   = *set;
    double dl = *set - *rise;
    if (dl < 0) dl += 24.0;
    out.daylight_h = dl;
    return out;
}

// ── Continuous solar position ─────────────────────────────────────────────
// Returns the sun's altitude (degrees above horizon, negative below) and
// azimuth (degrees, 0=N, 90=E, 180=S, 270=W) for any UTC instant. This is
// what drives the animated sky colours and the sun's screen position —
// far smoother than just rise/set times.

struct SunPos {
    double altitude = 0;   // degrees, +up
    double azimuth  = 0;   // degrees from north, clockwise
};

inline SunPos sun_position(std::time_t utc, double lat, double lon) {
    // Days since J2000.0
    double d = (static_cast<double>(utc) - 946728000.0) / 86400.0; // 2000-01-01 12:00 UTC

    double g = norm360(357.529 + 0.98560028 * d);          // mean anomaly
    double q = norm360(280.459 + 0.98564736 * d);          // mean longitude
    double L = norm360(q + 1.915 * std::sin(g * D2R)
                         + 0.020 * std::sin(2 * g * D2R));  // ecliptic longitude
    double e = 23.439 - 0.00000036 * d;                    // obliquity

    double sinL = std::sin(L * D2R);
    double RA = std::atan2(std::cos(e * D2R) * sinL, std::cos(L * D2R)) * R2D;
    RA = norm360(RA);
    double dec = std::asin(std::sin(e * D2R) * sinL) * R2D;

    // Greenwich mean sidereal time → local sidereal time → hour angle
    double GMST = norm360(280.46061837 + 360.98564736629 * d);
    double LST  = norm360(GMST + lon);
    double HA   = norm360(LST - RA);
    double HAr  = HA * D2R, decr = dec * D2R, latr = lat * D2R;

    double sinAlt = std::sin(latr) * std::sin(decr)
                  + std::cos(latr) * std::cos(decr) * std::cos(HAr);
    double alt = std::asin(std::clamp(sinAlt, -1.0, 1.0)) * R2D;

    double cosAz = (std::sin(decr) - std::sin(latr) * sinAlt)
                 / (std::cos(latr) * std::cos(std::asin(sinAlt)) + 1e-9);
    double az = std::acos(std::clamp(cosAz, -1.0, 1.0)) * R2D;
    if (std::sin(HAr) > 0) az = 360.0 - az;

    return {alt, az};
}

struct MoonPhase {
    double age_days = 0;     // 0..29.53 days into the synodic month
    double illum    = 0;     // 0..1 fraction illuminated
    double frac     = 0;     // 0..1 position in cycle (0=new, .5=full)
    bool   waxing   = true;
    std::string name;        // "Waxing crescent" etc.
    std::string glyph;       // unicode moon glyph
};

// Moon phase for a given UTC instant.
inline MoonPhase moon_phase(std::time_t utc) {
    constexpr double SYNODIC = 29.530588853;
    // Reference new moon: 2000-01-06 18:14 UTC (Julian 2451550.1).
    constexpr double ref_unix = 947182440.0;  // 2000-01-06 18:14:00 UTC
    double days = (static_cast<double>(utc) - ref_unix) / 86400.0;
    double age  = std::fmod(days, SYNODIC);
    if (age < 0) age += SYNODIC;

    MoonPhase m;
    m.age_days = age;
    m.frac     = age / SYNODIC;
    // Illumination from phase angle.
    m.illum    = (1.0 - std::cos(2 * PI * m.frac)) / 2.0;
    m.waxing   = m.frac < 0.5;

    // Eight named phases with glyphs.
    struct P { const char* name; const char* glyph; };
    static const P phases[8] = {
        {"New moon",        "\xF0\x9F\x8C\x91"},  // 🌑
        {"Waxing crescent", "\xF0\x9F\x8C\x92"},  // 🌒
        {"First quarter",   "\xF0\x9F\x8C\x93"},  // 🌓
        {"Waxing gibbous",  "\xF0\x9F\x8C\x94"},  // 🌔
        {"Full moon",       "\xF0\x9F\x8C\x95"},  // 🌕
        {"Waning gibbous",  "\xF0\x9F\x8C\x96"},  // 🌖
        {"Last quarter",    "\xF0\x9F\x8C\x97"},  // 🌗
        {"Waning crescent", "\xF0\x9F\x8C\x98"},  // 🌘
    };
    int idx = static_cast<int>(std::floor(m.frac * 8.0 + 0.5)) % 8;
    m.name  = phases[idx].name;
    m.glyph = phases[idx].glyph;
    return m;
}

} // namespace chronos::astro
