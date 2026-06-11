#pragma once
// chronos::weather — REAL weather data from the Open-Meteo public API.
//
// Open-Meteo (https://open-meteo.com) is a free, no-API-key public weather
// service. We fetch the current conditions + today's high/low for the app's
// location over HTTPS (libcurl) on a background thread, parse the small JSON
// payload by hand (no extra deps), and publish a thread-safe snapshot the UI
// reads each frame. The service re-fetches every ~10 minutes and on demand.
//
//   WeatherService svc;
//   svc.configure(lat, lon);     // kicks off the first fetch
//   svc.tick();                  // call each frame: refreshes when due
//   Weather w = svc.snapshot();  // thread-safe copy of latest data

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#include <curl/curl.h>

namespace chronos::weather {

// ── a single weather snapshot ────────────────────────────────────────────────
struct Weather {
    bool   valid     = false;   // have we ever received real data?
    bool   stale     = false;   // last refresh failed; showing old data
    double temp_c    = 0;       // current air temperature (°C)
    double feels_c   = 0;       // apparent temperature (°C)
    double hi_c      = 0;       // today's forecast high
    double lo_c      = 0;       // today's forecast low
    int    humidity  = 0;       // relative humidity %
    double wind_kmh  = 0;       // wind speed (km/h)
    int    wind_dir  = 0;       // wind direction (° from)
    int    code      = 0;       // WMO weather code
    bool   is_day    = true;    // daylight flag from the API
    std::time_t fetched = 0;    // when this data was fetched (wall clock)
};

// WMO weather interpretation codes → short label.
inline const char* code_label(int c) {
    switch (c) {
        case 0:  return "Clear";
        case 1:  return "Mainly clear";
        case 2:  return "Partly cloudy";
        case 3:  return "Overcast";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: case 63: case 65: return "Rain";
        case 66: case 67: return "Freezing rain";
        case 71: case 73: case 75: return "Snow";
        case 77: return "Snow grains";
        case 80: case 81: case 82: return "Rain showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Thunderstorm + hail";
        default: return "Unknown";
    }
}

// A representative glyph for the condition. is_day picks sun vs. moon for the
// clear/partly cases. These are all single-column geometric/weather glyphs.
inline const char* code_glyph(int c, bool day) {
    switch (c) {
        case 0:  return day ? "\u2600" : "\u263d";          // ☀ / ☽
        case 1:  return day ? "\u2600" : "\u263d";
        case 2:  return "\u26c5";                            // ⛅
        case 3:  return "\u2601";                            // ☁
        case 45: case 48: return "\u2592";                   // ▒ fog
        case 51: case 53: case 55:
        case 56: case 57: return "\u2614";                   // ☔ drizzle
        case 61: case 63: case 65:
        case 66: case 67:
        case 80: case 81: case 82: return "\u2614";          // ☔ rain
        case 71: case 73: case 75:
        case 77: case 85: case 86: return "\u2744";          // ❄ snow
        case 95: case 96: case 99: return "\u26a1";          // ⚡ storm
        default: return "\u2026";
    }
}

// Compass label for a wind bearing.
inline const char* wind_compass(int deg) {
    static const char* C[8] = {"N","NE","E","SE","S","SW","W","NW"};
    int i = ((deg + 22) / 45) & 7;
    return C[i];
}

// ── tiny JSON scalar extractor ───────────────────────────────────────────────
// Open-Meteo's payload is flat and predictable, so rather than pull in a JSON
// library we find "key": and read the next number/scalar. Good enough for the
// handful of fields we need; robust against whitespace and key reordering.
namespace detail {
inline bool find_number(const std::string& js, const char* key, double& out) {
    // The key can appear multiple times (e.g. once in "current_units" with a
    // string value like "°C", once in "current" with the real number). Scan
    // every occurrence and return the first that yields an actual number.
    std::string needle = std::string("\"") + key + "\"";
    size_t k = 0;
    while ((k = js.find(needle, k)) != std::string::npos) {
        size_t colon = js.find(':', k + needle.size());
        k += needle.size();
        if (colon == std::string::npos) break;
        size_t i = colon + 1;
        // skip whitespace and an opening array bracket, but NOT a string quote
        // — a quoted value here means this is the units entry, not a number.
        while (i < js.size() && (js[i] == ' ' || js[i] == '\t' || js[i] == '[')) i++;
        if (i < js.size() && js[i] == '"') continue;     // string value → skip
        char* end = nullptr;
        double v = std::strtod(js.c_str() + i, &end);
        if (end != js.c_str() + i) { out = v; return true; }
    }
    return false;
}
} // namespace detail

// ── libcurl GET into a std::string ───────────────────────────────────────────
namespace detail {
inline size_t write_cb(char* ptr, size_t sz, size_t nm, void* ud) {
    auto* s = static_cast<std::string*>(ud);
    s->append(ptr, sz * nm);
    return sz * nm;
}
inline bool http_get(const std::string& url, std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    body.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "chronos-tui/1.0");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK && http == 200 && !body.empty();
}
} // namespace detail

// ── the service ──────────────────────────────────────────────────────────────
class WeatherService {
public:
    WeatherService() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~WeatherService() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        curl_global_cleanup();
    }

    // Set/lock-in the location. Triggers an immediate first fetch.
    void configure(double lat, double lon) {
        lat_ = lat; lon_ = lon;
        request_refresh();
    }

    // Call once per frame. Spawns a background fetch when one is due and no
    // fetch is already in flight.
    void tick() {
        if (fetching_.load()) return;
        auto now = std::chrono::steady_clock::now();
        bool periodic = have_fetched_ &&
                        now - last_attempt_ >= std::chrono::minutes(10);
        bool due = want_refresh_.exchange(false) || !have_fetched_ || periodic;
        if (!due) return;
        last_attempt_ = now;
        have_fetched_ = true;
        start_fetch();
    }

    // Force a refresh on the next tick (e.g. user changed location).
    void request_refresh() { want_refresh_ = true; }

    // Thread-safe copy of the latest snapshot.
    Weather snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        return cur_;
    }

private:
    void start_fetch() {
        if (worker_.joinable()) worker_.join();   // previous one finished
        fetching_ = true;
        double lat = lat_, lon = lon_;
        worker_ = std::thread([this, lat, lon] {
            Weather w = fetch_blocking(lat, lon);
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (w.valid) {
                    cur_ = w;
                } else {
                    cur_.stale = cur_.valid;       // keep old data, mark stale
                }
            }
            fetching_ = false;
        });
    }

    static Weather fetch_blocking(double lat, double lon) {
        Weather w;
        char url[512];
        std::snprintf(url, sizeof url,
            "https://api.open-meteo.com/v1/forecast"
            "?latitude=%.4f&longitude=%.4f"
            "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
            "weather_code,wind_speed_10m,wind_direction_10m,is_day"
            "&daily=temperature_2m_max,temperature_2m_min"
            "&timezone=auto&forecast_days=1",
            lat, lon);

        std::string body;
        if (!detail::http_get(url, body)) return w;   // valid stays false

        double v;
        if (detail::find_number(body, "temperature_2m", v))       w.temp_c   = v;
        if (detail::find_number(body, "apparent_temperature", v)) w.feels_c  = v;
        if (detail::find_number(body, "relative_humidity_2m", v)) w.humidity = (int)std::lround(v);
        if (detail::find_number(body, "weather_code", v))         w.code     = (int)std::lround(v);
        if (detail::find_number(body, "wind_speed_10m", v))       w.wind_kmh = v;
        if (detail::find_number(body, "wind_direction_10m", v))   w.wind_dir = (int)std::lround(v);
        if (detail::find_number(body, "is_day", v))               w.is_day   = v >= 0.5;
        if (detail::find_number(body, "temperature_2m_max", v))   w.hi_c     = v;
        if (detail::find_number(body, "temperature_2m_min", v))   w.lo_c     = v;

        // require the headline temperature to have parsed for the data to count
        if (detail::find_number(body, "temperature_2m", v)) {
            w.valid = true;
            w.stale = false;
            w.fetched = std::time(nullptr);
        }
        return w;
    }

    double lat_ = 0, lon_ = 0;
    bool   have_fetched_ = false;
    std::chrono::steady_clock::time_point last_attempt_{};

    std::atomic<bool> running_{true};
    std::atomic<bool> fetching_{false};
    std::atomic<bool> want_refresh_{false};

    std::thread worker_;
    mutable std::mutex mu_;
    Weather cur_;
};

} // namespace chronos::weather
