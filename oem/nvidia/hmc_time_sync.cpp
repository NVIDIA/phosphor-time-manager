/**
 * hmc-time-sync: Push BMC wall-clock time to the HMC via a Redfish PATCH.
 *
 * Compiled in when -Dhmc-time-sync=enabled.  The HMC IP address is
 * supplied at build time via -Dhmc-time-sync-host=<ip> (default 172.31.13.251).
 *
 * Exits 0 on success, 1 after all retries are exhausted.
 * Intended to run as a systemd oneshot service with Restart=on-failure,
 * re-triggered hourly by set-hmc-time.timer.
 */

#include <curl/curl.h>

#include <phosphor-logging/lg2.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

namespace
{

constexpr auto hmcDateTimeUrl =
    "http://" HMC_TIME_SYNC_HOST "/redfish/v1/Managers/HGX_BMC_0";
constexpr int maxAttempts = 10;
constexpr int retryDelaySec = 5;
constexpr long connectTimeoutSec = 2;
constexpr long transferTimeoutSec = 5;

/** Format current UTC time as ISO-8601: "YYYY-MM-DDTHH:MM:SS+00:00" */
std::string currentUtcDateTime()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S+00:00");
    return oss.str();
}

/** Discard response body — curl write callback. */
std::size_t discardWrite(char* /*buf*/, std::size_t size, std::size_t nmemb,
                         void* /*userdata*/)
{
    return size * nmemb;
}

/** PATCH the current time to the HMC Redfish DateTime endpoint.
 *  Returns true on HTTP 2xx, false on any error. */
bool patchHmcTime()
{
    const std::string body =
        R"({"DateTime": ")" + currentUtcDateTime() + R"("})";

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        lg2::error("hmc-time-sync: curl_easy_init failed");
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, hmcDateTimeUrl);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connectTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, transferTimeoutSec);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardWrite);

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        lg2::error("hmc-time-sync: curl error: {ERR}", "ERR",
                   curl_easy_strerror(res));
        return false;
    }

    if (httpCode < 200 || httpCode >= 300)
    {
        lg2::error("hmc-time-sync: HTTP {CODE}", "CODE", httpCode);
        return false;
    }

    return true;
}

} // namespace

int main()
{
    for (int attempt = 1; attempt <= maxAttempts; ++attempt)
    {
        lg2::info("hmc-time-sync: attempt {COUNT}/{MAX}", "COUNT", attempt,
                  "MAX", maxAttempts);

        if (patchHmcTime())
        {
            lg2::info("hmc-time-sync: HMC time set successfully");
            return 0;
        }

        if (attempt < maxAttempts)
        {
            std::this_thread::sleep_for(std::chrono::seconds(retryDelaySec));
        }
    }

    lg2::error("hmc-time-sync: cannot sync HMC RTC after {MAX} attempts", "MAX",
               maxAttempts);
    return 1;
}
