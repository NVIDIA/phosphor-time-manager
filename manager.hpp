#pragma once

#include "config.h"

#include "property_change_listener.hpp"
#include "settings.hpp"
#include "types.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>

#include <string>

namespace phosphor
{
namespace time
{

/** @class Manager
 *  @brief The manager to handle OpenBMC time.
 *  @details It registers various time related settings and properties signals
 *  on DBus and handle the changes.
 *  For certain properties it also notifies the changed events to listeners.
 */
class Manager
{
  public:
    friend class TestManager;

    explicit Manager(sdbusplus::bus_t& bus);
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager(Manager&&) = delete;
    Manager& operator=(Manager&&) = delete;
    ~Manager() = default;

    void setTimeMode(Mode mode)
    {
        this->timeMode = mode;
    }

    Mode getTimeMode()
    {
        return this->timeMode;
    }

  private:
    /** @brief Persistent sdbusplus DBus connection */
    sdbusplus::bus_t& bus;

    /** @brief The match of systemd timedate property change */
    std::vector<sdbusplus::bus::match_t> timedateMatches;

    /** @brief The match of settings property change */
    std::vector<sdbusplus::bus::match_t> settingsMatches;

    /** @brief Settings objects of intereset */
    settings::Objects settings;

    /** @brief The current time mode */
    Mode timeMode = DEFAULT_TIME_MODE;

    /** @brief The timeout for NTP setting */
    std::chrono::seconds ntpTimeout = std::chrono::seconds(60);

    /** @brief Get setting from settingsd service
     *
     * @param[in] path - The dbus object path
     * @param[in] interface - The dbus interface
     * @param[in] setting - The string of the setting
     *
     * @return The setting value in string
     */
    std::string getSetting(const char* path, const char* interface,
                           const char* setting) const;

    /** @brief Set current time mode from the time mode string
     *
     * @param[in] mode - The string of time mode
     *
     * @return - true if the mode is updated
     *           false if it's the same as before
     */
    bool setCurrentTimeMode(const std::string& mode);

    /** @brief Called on time mode is changed
     *
     * Notify listeners that time mode is changed and update ntp setting
     *
     * @param[in] mode - The string of time mode
     */
    void onTimeModeChanged(const std::string& mode);

    /** @brief Callback to handle change in NTP
     *
     *  @param[in] msg - sdbusplus dbusmessage
     *
     *  @return 0 on success, < 0 on failure.
     */
    int onTimedateChanged(sdbusplus::message_t& msg);

    /** @brief Callback to handle change in a setting
     *
     *  @param[in] msg - sdbusplus dbusmessage
     *
     *  @return 0 on success, < 0 on failure.
     */
    int onSettingsChanged(sdbusplus::message_t& msg);

    /** @brief Notified on settings property changed
     *
     * @param[in] key - The name of property that is changed
     * @param[in] value - The value of the property
     * @param[in] forceSet - true: Force sync NTP Settings to systemd time
     *                             service, only be used during initialization
     *                       false: This is default value
     */
    void onPropertyChanged(const std::string& key, const std::string& value,
                           bool forceSet = false);

    /** @brief Update the NTP setting to systemd time service
     *
     * @param[in] value - The time mode value, e.g. "NTP" or "MANUAL"
     */
    void updateNtpSetting(const std::string& value);

    /** @brief The static function called on settings property changed
     *
     * @param[in] msg - Data associated with subscribed signal
     * @param[in] userData - Pointer to this object instance
     * @param[out] retError  - Not used but required with signal API
     */
    static int onPropertyChanged(sd_bus_message* msg, void* userData,
                                 sd_bus_error* retError);

    /** @brief The string of time mode property */
    static constexpr auto propertyTimeMode = "TimeSyncMethod";
};

} // namespace time
} // namespace phosphor
