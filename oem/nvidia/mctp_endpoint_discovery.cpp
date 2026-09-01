#include "mctp_endpoint_discovery.hpp"

#include "../config.h"

#include "constants.hpp"
#include "types.hpp"

#include <phosphor-logging/lg2.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <vector>

namespace mctp_vdm
{

using namespace dbus;

template <typename T>
MctpDiscovery<T>::MctpDiscovery(
    // NOLINTBEGIN
    sdbusplus::bus_t& bus, mctp_socket::Handler<T>& handler,
    std::initializer_list<MctpDiscoveryHandlerIntf*> list) :
    bus(bus),
    mctpEndpointAddedSignal(
        bus,
        sdbusplus::bus::match::rules::interfacesAdded(
            "/xyz/openbmc_project/mctp"),
        std::bind(std::mem_fn(&MctpDiscovery::discoverEndpoints), this,
                  std::placeholders::_1)),
    handler(handler), handlers(list)
// NOLINTEND
{
    mctp::Infos mctpInfos;
    try
    {
        const dbus::Interfaces ifaceList{"xyz.openbmc_project.MCTP.Endpoint"};
        auto method = bus.new_method_call(mapper::service, mapper::path,
                                          mapper::interface, "GetSubTree");

        std::map<std::string, std::map<std::string, std::vector<std::string>>>
            subtree;
        bus.call(method).read(subtree);

        if (subtree.empty())
        {
            lg2::info("No MCTP endpoints found");
            return;
        }

        for (const auto& [object, serviceMap] : subtree)
        {
            for (const auto& [service, interfaces] : serviceMap)
            {
                lg2::info("MCTP discovery: object = {OBJ}, service = {SERVICE}",
                          "OBJ", object, "SERVICE", service);
                try
                {
                    InterfaceMap ifaceMap;

                    // Always fetch UUID interface
                    {
                        PropertyMap uuidMap;
                        auto uuidProps = bus.new_method_call(
                            service.c_str(), object.c_str(),
                            "org.freedesktop.DBus.Properties", "GetAll");
                        uuidProps.append("xyz.openbmc_project.Common.UUID");
                        auto uuidResult = bus.call(uuidProps);
                        uuidResult.read(uuidMap);
                        ifaceMap["xyz.openbmc_project.Common.UUID"] = uuidMap;
                    }

                    // Always fetch UnixSocket interface
                    {
                        PropertyMap sockMap;
                        auto sockProps = bus.new_method_call(
                            service.c_str(), object.c_str(),
                            "org.freedesktop.DBus.Properties", "GetAll");
                        sockProps.append(
                            "xyz.openbmc_project.Common.UnixSocket");
                        auto sockResult = bus.call(sockProps);
                        sockResult.read(sockMap);
                        ifaceMap["xyz.openbmc_project.Common.UnixSocket"] =
                            sockMap;
                    }

                    // Always fetch Endpoint interface
                    {
                        PropertyMap epMap;
                        auto epProps = bus.new_method_call(
                            service.c_str(), object.c_str(),
                            "org.freedesktop.DBus.Properties", "GetAll");
                        epProps.append("xyz.openbmc_project.MCTP.Endpoint");
                        auto epResult = bus.call(epProps);
                        epResult.read(epMap);
                        ifaceMap["xyz.openbmc_project.MCTP.Endpoint"] = epMap;
                    }

                    populateMctpInfo(ifaceMap, mctpInfos);
                }
                catch (const std::exception& e)
                {
                    lg2::error(
                        "GetAll properties failed, PATH={PATH}, SERVICE={SERVICE}, ERROR={ERROR}",
                        "PATH", object.c_str(), "SERVICE", service.c_str(),
                        "ERROR", e);
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to get list of mctp endpoints: {ERROR}", "ERROR", e);
    }

    lg2::info("MCTP discovery: total endpoints found = {COUNT}", "COUNT",
              mctpInfos.size());
    handleMctpEndpoints(mctpInfos);
}

template <typename T>
void MctpDiscovery<T>::populateMctpInfo(const dbus::InterfaceMap& interfaces,
                                        mctp::Infos& mctpInfos)
{
    mctp::UUID uuid{};
    int type = 0;
    int protocol = 0;
    std::vector<uint8_t> address{};

    try
    {
        for (const auto& [intfName, properties] : interfaces)
        {
            if (intfName == mctp::uuidInterface)
            {
                uuid = std::get<std::string>(properties.at("UUID"));
            }

            if (intfName == unixSocketIntfName)
            {
                // NOLINTBEGIN
                type = std::get<size_t>(properties.at("Type"));
                protocol = std::get<size_t>(properties.at("Protocol"));
                address =
                    std::get<std::vector<uint8_t>>(properties.at("Address"));
                // NOLINTEND
            }
        }

        if (uuid.empty() || address.empty() || (type == 0))
        {
            return;
        }

        if (interfaces.contains(mctpEndpointIntfName))
        {
            const auto& properties = interfaces.at(mctpEndpointIntfName);
            if (properties.contains("EID") &&
                properties.contains("SupportedMessageTypes") &&
                properties.contains("MediumType"))
            {
                auto eid = std::get<size_t>(properties.at("EID"));
                auto mctpTypes = std::get<std::vector<uint8_t>>(
                    properties.at("SupportedMessageTypes"));
                auto mediumType =
                    std::get<std::string>(properties.at("MediumType"));
                auto networkId = std::get<size_t>(properties.at("NetworkId"));
                if (std::find(mctpTypes.begin(), mctpTypes.end(),
                              mctp_vdm::messageType) != mctpTypes.end())
                {
                    handler.registerMctpEndpoint(eid, type, protocol, address);
                    mctpInfos.emplace_back(
                        std::make_tuple(eid, uuid, mediumType, networkId));
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Error while getting properties.", "ERROR", e);
    }
}

template <typename T>
void MctpDiscovery<T>::discoverEndpoints(sdbusplus::message::message& msg)
{
    mctp::Infos mctpInfos;

    sdbusplus::object_path objPath;
    dbus::InterfaceMap interfaces;
    msg.read(objPath, interfaces);
    std::string obPath = objPath;
    populateMctpInfo(interfaces, mctpInfos);

    handleMctpEndpoints(mctpInfos);
}

template <typename T>
void MctpDiscovery<T>::handleMctpEndpoints(const mctp::Infos& mctpInfos)
{
    for (MctpDiscoveryHandlerIntf* handler : handlers)
    {
        if (handler)
        {
            handler->handleMctpEndpoints(mctpInfos);
        }
    }
}

#ifdef MCTP_IN_KERNEL
using TRequest = mctp_vdm::requester::InKernelRequest;
#else
using TRequest = mctp_vdm::requester::DaemonRequest;
#endif

// Explicit template instantiations
template class MctpDiscovery<TRequest>;

} // namespace mctp_vdm
