#pragma once
// chronos::geo — automatic location from the public IP address.
//
// On startup (when the user hasn't pinned CHRONOS_LAT/LON) we ask a free
// IP-geolocation service for the approximate latitude/longitude and city of
// this machine's public IP, over HTTP on a background thread. The result is
// published as a thread-safe snapshot the app polls each frame and, on the
// first success, uses to point the sky + weather at the user's real location.
//
//   GeoService geo;
//   geo.start();                 // kicks off the lookup off-thread
//   if (auto loc = geo.take())   // non-null exactly once, when it lands
//       use(loc->lat, loc->lon, loc->place);
//
// Uses ip-api.com (no API key, no signup). If the lookup fails (offline,
// blocked), the app simply keeps its default/configured location.

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <curl/curl.h>

namespace chronos::geo {

struct Location {
    double      lat = 0, lon = 0;
    std::string place;        // "City, Country" (best-effort)
};

namespace detail {
inline size_t geo_write_cb(char* ptr, size_t sz, size_t nm, void* ud) {
    static_cast<std::string*>(ud)->append(ptr, sz * nm);
    return sz * nm;
}

// pull a JSON number for "key": value
inline bool json_number(const std::string& js, const char* key, double& out) {
    std::string needle = std::string("\"") + key + "\":";
    size_t k = js.find(needle);
    if (k == std::string::npos) return false;
    size_t i = k + needle.size();
    while (i < js.size() && (js[i] == ' ' || js[i] == '\t')) i++;
    char* end = nullptr;
    double v = std::strtod(js.c_str() + i, &end);
    if (end == js.c_str() + i) return false;
    out = v;
    return true;
}

// pull a JSON string for "key":"value"
inline bool json_string(const std::string& js, const char* key, std::string& out) {
    std::string needle = std::string("\"") + key + "\":\"";
    size_t k = js.find(needle);
    if (k == std::string::npos) return false;
    size_t i = k + needle.size();
    size_t e = js.find('"', i);
    if (e == std::string::npos) return false;
    out = js.substr(i, e - i);
    return true;
}
} // namespace detail

class GeoService {
public:
    // Begin the lookup on a background thread. Safe to call once.
    void start() {
        if (started_.exchange(true)) return;
        worker_ = std::thread([this] {
            Location loc;
            if (lookup(loc)) {
                std::lock_guard<std::mutex> lk(mu_);
                result_ = loc;
                ready_ = true;
            }
            done_ = true;
        });
        worker_.detach();
    }

    // Returns the location exactly once, after a successful lookup; nullopt
    // otherwise. The app calls this each frame and applies it the moment it
    // becomes available.
    std::optional<Location> take() {
        if (!ready_.load()) return std::nullopt;
        std::lock_guard<std::mutex> lk(mu_);
        if (taken_ || !result_) return std::nullopt;
        taken_ = true;
        return result_;
    }

    ~GeoService() = default;

private:
    static bool lookup(Location& out) {
        CURL* curl = curl_easy_init();
        if (!curl) return false;
        std::string body;
        curl_easy_setopt(curl, CURLOPT_URL,
            "http://ip-api.com/json/?fields=status,city,regionName,country,lat,lon");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, detail::geo_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "chronos-tui/1.0");
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        CURLcode rc = curl_easy_perform(curl);
        long http = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
        curl_easy_cleanup(curl);
        if (rc != CURLE_OK || http != 200 || body.empty()) return false;

        std::string status;
        if (detail::json_string(body, "status", status) && status != "success")
            return false;
        double lat, lon;
        if (!detail::json_number(body, "lat", lat)) return false;
        if (!detail::json_number(body, "lon", lon)) return false;
        out.lat = lat;
        out.lon = lon;
        std::string city, country;
        detail::json_string(body, "city", city);
        detail::json_string(body, "country", country);
        if (!city.empty() && !country.empty()) out.place = city + ", " + country;
        else if (!city.empty())                out.place = city;
        else if (!country.empty())             out.place = country;
        return true;
    }

    std::atomic<bool> started_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> done_{false};
    std::mutex mu_;
    std::optional<Location> result_;
    bool taken_ = false;
    std::thread worker_;
};

} // namespace chronos::geo
