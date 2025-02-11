#include "bmc_epoch.hpp"

#include "utils.hpp"

#include <sys/timerfd.h>
#include <unistd.h>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Time/error.hpp>

#include <chrono>

// Need to do this since its not exported outside of the kernel.
// Refer : https://gist.github.com/lethean/446cea944b7441228298
#ifndef TFD_TIMER_CANCEL_ON_SET
#define TFD_TIMER_CANCEL_ON_SET (1 << 1)
#endif

namespace phosphor
{
namespace time
{
namespace // anonymous
{
constexpr auto systemdTimeService = "org.freedesktop.timedate1";
constexpr auto systemdTimePath = "/org/freedesktop/timedate1";
constexpr auto systemdTimeInterface = "org.freedesktop.timedate1";
constexpr auto methodSetTime = "SetTime";
} // namespace

PHOSPHOR_LOG2_USING;

namespace server = sdbusplus::xyz::openbmc_project::Time::server;
using namespace phosphor::logging;
using FailedError = sdbusplus::xyz::openbmc_project::Time::Error::Failed;
using namespace std::chrono;

void BmcEpoch::initialize()
{
    using InternalFailure =
        sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

    // Subscribe time change event
    // Choose the MAX time that is possible to avoid mis fires.
    constexpr itimerspec maxTime = {
        {0, 0},                                     // it_interval
        {system_clock::duration::max().count(), 0}, // it_value
    };

    timeFd = timerfd_create(CLOCK_REALTIME, 0);
    if (timeFd == -1)
    {
        error("Failed to create timerfd: {ERRNO}", "ERRNO", errno);
        elog<InternalFailure>();
    }

    auto r = timerfd_settime(
        timeFd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &maxTime, nullptr);
    if (r != 0)
    {
        error("Failed to set timerfd: {ERRNO}", "ERRNO", errno);
        elog<InternalFailure>();
    }

    sd_event_source* es = nullptr;
    r = sd_event_add_io(bus.get_event(), &es, timeFd, EPOLLIN, onTimeChange,
                        this);
    if (r < 0)
    {
        error("Failed to add event: {ERRNO}", "ERRNO", errno);
        elog<InternalFailure>();
    }
    timeChangeEventSource.reset(es);
}

BmcEpoch::~BmcEpoch()
{
    close(timeFd);
}

uint64_t BmcEpoch::elapsed() const
{
    return getTime().count();
}

uint64_t BmcEpoch::elapsed(uint64_t value)
{
    /*
        Mode  | Set BMC Time
        ----- | -------------
        NTP   | Fail to set
        MANUAL| OK
    */
    auto time = microseconds(value);
    setTime(time);

    server::EpochTime::elapsed(value);
    return value;
}

int BmcEpoch::onTimeChange(sd_event_source* /* es */, int fd,
                           uint32_t /* revents */, void* /* userdata */)
{
    std::array<char, 64> time{};

    // We are not interested in the data here.
    // So read until there is no new data here in the FD
    while (read(fd, time.data(), time.max_size()) > 0)
    {
        ;
    }

    return 0;
}

void BmcEpoch::onModeChanged(Mode mode)
{
    manager.setTimeMode(mode);
}

bool BmcEpoch::setTime(const microseconds& usec)
{
    auto method = bus.new_method_call(systemdTimeService, systemdTimePath,
                                      systemdTimeInterface, methodSetTime);
    method.append(static_cast<int64_t>(usec.count()),
                  false,  // relative
                  false); // user_interaction

    try
    {
        bus.call_noreply(method);
    }
    catch (const sdbusplus::exception_t& ex)
    {
        error("Error in setting system time: {ERROR}", "ERROR", ex);
        using namespace xyz::openbmc_project::Time;
        elog<FailedError>(Failed::REASON(ex.what()));
    }

    // Get string of the timestamp in ISO 8601 format
    std::chrono::system_clock::time_point timePoint =
        std::chrono::system_clock::time_point(
            std::chrono::microseconds(static_cast<int64_t>(usec.count())));
    std::time_t time = std::chrono::system_clock::to_time_t(timePoint);
    std::tm* gmTime = std::gmtime(&time);
    std::ostringstream timeStamp;
    timeStamp << std::put_time(gmTime, "%Y-%m-%dT%H:%M:%S") << "+00:00";

    // Add BMC time sync log
    std::string messageId = "OpenBMC.0.4.BMCSystemTimeUpdated";
    std::map<std::string, std::string> addData = {};
    addData["REDFISH_MESSAGE_ID"] = messageId;
    addData["REDFISH_MESSAGE_ARGS"] = timeStamp.str();
    addData["xyz.openbmc_project.Logging.Entry.Resolution"] = "None";
    utils::addEventLog(bus, messageId,
                       "xyz.openbmc_project.Logging.Entry.Level.Informational",
                       addData);
    return true;
}

microseconds BmcEpoch::getTime()
{
    auto now = system_clock::now();
    return duration_cast<microseconds>(now.time_since_epoch());
}

} // namespace time
} // namespace phosphor
