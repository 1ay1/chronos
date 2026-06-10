#pragma once
// chronos::timeutil — world clocks, tz offsets, holiday countdowns.

#include <ctime>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>

namespace chronos::timeutil {

struct Zone {
    std::string label;   // "NYC"
    std::string tzname;  // POSIX TZ string ("America/New_York")
};

// Default world clocks. Edit freely — these are the "rice" defaults.
inline std::vector<Zone> default_zones() {
    return {
        {"UTC", "UTC"},
        {"NYC", "America/New_York"},
        {"LON", "Europe/London"},
        {"BER", "Europe/Berlin"},
        {"TOK", "Asia/Tokyo"},
        {"SYD", "Australia/Sydney"},
    };
}

struct ClockReading {
    std::string label;
    int hour = 0, minute = 0;
    int day = 0, month = 0;       // local date in that zone
    std::string weekday;          // "Wed"
    std::string month_abbr;       // "Jun"
    bool is_day = true;           // rough day/night by hour
};

// Render a UTC instant into a named zone using the OS tz database.
inline ClockReading read_zone(std::time_t now, const Zone& z) {
    ClockReading r;
    r.label = z.label;

    // setenv TZ then localtime — restore afterwards.
    const char* old = getenv("TZ");
    std::string saved = old ? old : "";
    setenv("TZ", z.tzname.c_str(), 1);
    tzset();
    std::tm tmv{};
    localtime_r(&now, &tmv);
    if (old) setenv("TZ", saved.c_str(), 1);
    else     unsetenv("TZ");
    tzset();

    r.hour   = tmv.tm_hour;
    r.minute = tmv.tm_min;
    r.day    = tmv.tm_mday;
    r.month  = tmv.tm_mon + 1;
    static const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
    r.weekday    = wd[tmv.tm_wday % 7];
    r.month_abbr = mo[tmv.tm_mon % 12];
    r.is_day     = (tmv.tm_hour >= 6 && tmv.tm_hour < 19);
    return r;
}

// Local UTC offset in fractional hours (handles DST + half-hour zones).
inline double local_utc_offset_hours(std::time_t now) {
    std::tm lt{}, gt{};
    localtime_r(&now, &lt);
    gmtime_r(&now, &gt);
    double l = lt.tm_hour * 3600.0 + lt.tm_min * 60.0 + lt.tm_sec;
    double g = gt.tm_hour * 3600.0 + gt.tm_min * 60.0 + gt.tm_sec;
    double diff = l - g;
    // Correct for day wrap.
    if (lt.tm_yday != gt.tm_yday) {
        if (lt.tm_yday > gt.tm_yday || (lt.tm_yday < gt.tm_yday && lt.tm_year >= gt.tm_year))
            diff += 86400.0;
        else
            diff -= 86400.0;
    }
    return diff / 3600.0;
}

struct Event {
    std::string name;
    std::string glyph;
    int days = 0;       // days until
    std::tm date{};
};

inline std::time_t make_midnight(int y, int m, int d) {
    std::tm t{};
    t.tm_year = y - 1900;
    t.tm_mon  = m - 1;
    t.tm_mday = d;
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
    t.tm_isdst = -1;
    return mktime(&t);
}

// Upcoming fixed-date events; rolls over to next year automatically.
inline std::vector<Event> upcoming_events(std::time_t now) {
    std::tm lt{};
    localtime_r(&now, &lt);
    int y = lt.tm_year + 1900;

    struct Def { int m, d; const char* name; const char* glyph; };
    static const Def defs[] = {
        {1,  1,  "New Year",      "\xF0\x9F\x8E\x86"},  // 🎆
        {2,  14, "Valentine's",   "\xE2\x9D\xA4"},      // ❤
        {3,  20, "Spring Equinox","\xF0\x9F\x8C\xB1"},  // 🌱
        {6,  21, "Summer Solstice","\xE2\x98\x80"},     // ☀
        {10, 31, "Halloween",     "\xF0\x9F\x8E\x83"},  // 🎃
        {12, 25, "Christmas",     "\xF0\x9F\x8E\x84"},  // 🎄
        {12, 31, "New Year's Eve","\xF0\x9F\x8E\x87"},  // 🎇
    };

    std::time_t today = make_midnight(y, lt.tm_mon + 1, lt.tm_mday);

    std::vector<Event> out;
    for (const auto& d : defs) {
        int ey = y;
        std::time_t when = make_midnight(ey, d.m, d.d);
        if (when < today) when = make_midnight(++ey, d.m, d.d);
        Event e;
        e.name  = d.name;
        e.glyph = d.glyph;
        e.days  = static_cast<int>((when - today) / 86400);
        localtime_r(&when, &e.date);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const Event& a, const Event& b) { return a.days < b.days; });
    if (out.size() > 4) out.resize(4);
    return out;
}

} // namespace chronos::timeutil
