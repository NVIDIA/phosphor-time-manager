#include "utils.hpp"

namespace phosphor
{
namespace time
{

namespace // anonymous
{
constexpr auto mapperBusname = "xyz.openbmc_project.ObjectMapper";
constexpr auto mapperPath = "/xyz/openbmc_project/object_mapper";
constexpr auto mapperInterface = "xyz.openbmc_project.ObjectMapper";
} // namespace

namespace utils
{

PHOSPHOR_LOG2_USING;

std::string getService(sdbusplus::bus_t& bus, const char* path,
                       const char* interface)
{
    auto mapper = bus.new_method_call(mapperBusname, mapperPath,
                                      mapperInterface, "GetObject");

    mapper.append(path, std::vector<std::string>({interface}));
    try
    {
        auto mapperResponseMsg = bus.call(mapper);

        std::vector<std::pair<std::string, std::vector<std::string>>>
            mapperResponse;
        mapperResponseMsg.read(mapperResponse);
        if (mapperResponse.empty())
        {
            error("Error reading mapper response");
            throw std::runtime_error("Error reading mapper response");
        }

        return mapperResponse[0].first;
    }
    catch (const sdbusplus::exception_t& ex)
    {
        error(
            "Mapper call failed: path:{PATH}, interface:{INTF}, error:{ERROR}",
            "PATH", path, "INTF", interface, "ERROR", ex);
        throw std::runtime_error("Mapper call failed");
    }
}

MapperResponse getSubTree(sdbusplus::bus_t& bus, const std::string& root,
                          const Interfaces& interfaces, int32_t depth)
{
    auto mapperCall = bus.new_method_call(mapperBusname, mapperPath,
                                          mapperInterface, "GetSubTree");
    mapperCall.append(root);
    mapperCall.append(depth);
    mapperCall.append(interfaces);

    auto response = bus.call(mapperCall);

    MapperResponse result;
    response.read(result);
    return result;
}

Mode strToMode(const std::string& mode)
{
    return ModeSetting::convertMethodFromString(mode);
}

std::string modeToStr(Mode mode)
{
    return sdbusplus::xyz::openbmc_project::Time::server::convertForMessage(
        mode);
}

void addEventLog(sdbusplus::bus_t& bus, const std::string& messageId,
                 const std::string& severity,
                 std::map<std::string, std::string>& addData)
{
    auto method = bus.new_method_call(
        "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Create", "Create");
    method.append(messageId);
    method.append(severity);
    method.append(addData);
    try
    {
        bus.call_noreply(method);
    }
    catch (const sdbusplus::exception_t& ex)
    {
        error("Failed to add event log: {ERROR}", "ERROR", ex);
    }
}
} // namespace utils
} // namespace time
} // namespace phosphor
