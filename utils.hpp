#pragma once

#include <phosphor-logging/log.hpp>

#include <fstream>

namespace phosphor
{
namespace time
{
namespace utils
{

using namespace phosphor::logging;

/** @brief Read data with type T from file
 *
 * @param[in] fileName - The name of file to read from
 *
 * @return The data with type T
 */
template <typename T>
T readData(const char* fileName)
{
    T data{};
    std::ifstream fs(fileName);
    if (fs.is_open())
    {
        fs >> data;
    }
    return data;
}

/** @brief Write data with type T to file
 *
 * @param[in] fileName - The name of file to write to
 * @param[in] data - The data with type T to write to file
 */
template <typename T>
void writeData(const char* fileName, T&& data)
{
    std::ofstream fs(fileName, std::ios::out);
    if (fs.is_open())
    {
        fs << std::forward<T>(data);
    }
}

/** @brief The template function to get property from the requested dbus path
 *
 * @param[in] bus          - The Dbus bus object
 * @param[in] service      - The Dbus service name
 * @param[in] path         - The Dbus object path
 * @param[in] interface    - The Dbus interface
 * @param[in] propertyName - The property name to get
 *
 * @return The value of the property
 */
template <typename T>
T getProperty(sdbusplus::bus::bus& bus,
              const char* service,
              const char* path,
              const char* interface,
              const char* propertyName)
{
    sdbusplus::message::variant<T> value{};
    auto method = bus.new_method_call(service,
                                      path,
                                      "org.freedesktop.DBus.Properties",
                                      "Get");
    method.append(interface, propertyName);
    auto reply = bus.call(method);
    if (reply)
    {
        reply.read(value);
    }
    else
    {
        // TODO: use elog to throw exception
        log<level::ERR>("Failed to get property",
                        entry("SERVICE=%s", service),
                        entry("PATH=%s", path),
                        entry("INTERFACE=%s", interface),
                        entry("PROPERTY=%s", propertyName));
    }
    return value.template get<T>();
}

}
}
}