#include "erot_time_manager.hpp"

#include "mctp_vdm_completion_codes.hpp"
#include "types.hpp"
#include "utils.hpp"

#include <xyz/openbmc_project/Logging/Entry/server.hpp>

using namespace mctp_vdm;

ErotTimeManager::ErotTimeManager(
    sdbusplus::bus::bus& bus,
    mctp_vdm::requester::Handler<mctp_vdm::requester::Request>& reqHandler,
    mctp_socket::Handler& sockHandler, mctp_vdm::InstanceIdMgr& instanceIdMgr) :
    bus(bus),
    reqHandler(reqHandler), sockHandler(sockHandler),
    instanceIdMgr(instanceIdMgr),
    bmcTimeChangedSignal(
        bus,
        sdbusplus::bus::match::rules::propertiesChanged(
            "/xyz/openbmc_project/time/bmc",
            "xyz.openbmc_project.Time.EpochTime"),
        std::bind_front(&ErotTimeManager::bmcTimeChangeHandler, this))
{}

void ErotTimeManager::bmcTimeChangeHandler(sdbusplus::message::message& msg)
{
    if (setErotTimeHandle)
    {
        if (!setErotTimeHandle.done())
        {
            lg2::info(" Setting ERoT time already in progress..");
            return;
        }
        else
        {
            setErotTimeHandle.destroy();
        }
    }

    dbus::Interface interface;
    dbus::PropertyMap propertyMap;
    msg.read(interface, propertyMap);

    if (propertyMap.contains("Elapsed"))
    {
        auto elapsedTime = std::get<uint64_t>(propertyMap.at("Elapsed"));
        std::vector<uint8_t> eids{};
        for (const auto& [uuid, mctpUUIDInfo] : mctpInfoMap)
        {
            eids.emplace_back(mctpUUIDInfo.top().eid);
        }
        auto co = setTimeOnErots(elapsedTime, eids);
        setErotTimeHandle = co.handle;
        if (setErotTimeHandle.done())
        {
            setErotTimeHandle = nullptr;
        }
    }
}

mctp_vdm::requester::Coroutine
    ErotTimeManager::setTimeOnErots(uint64_t epochElapsedTime,
                                    std::vector<uint8_t> eids)
{
    if (eids.empty())
    {
        co_return static_cast<int>(mctp_vdm::CompletionCodes::Success);
    }

    // Initialize MCTP sockets for the list of endpoints
    auto rc = sockHandler.activateSockets(eids);
    if (rc < 0)
    {
        lg2::error("Activating MCTP demux daemon sockets failed. RC={RC}", "RC",
                   unsigned(rc));
        co_return rc;
    }

    // Iterate through all the endpoints and set external timestamp
    for (const auto& eid : eids)
    {
        auto rc = co_await setTimeOnErot(eid, epochElapsedTime);
        if (rc != 0)
        {
            lg2::error(
                "Setting external timestamp on ERoT failed, EID={EID}, RC={RC}",
                "EID", eid, "RC", rc);
            createErrorLog(eid, rc);
        }
    }

    // Close MCTP demux daemon sockets
    sockHandler.deactivateSockets();
}

mctp_vdm::requester::Coroutine
    ErotTimeManager::setTimeOnErot(uint8_t eid, uint64_t epochElapsedTime)
{
    mctp::Request request(sizeof(mctp_vdm::MsgHeader) +
                          sizeof(epochElapsedTime));
    auto requestMsg = reinterpret_cast<mctp_vdm::MsgHeader*>(request.data());
    requestMsg->iana = htobe32(nvidiaIANA);
    requestMsg->request = 1;
    requestMsg->instanceId = instanceIdMgr.getInstanceId(eid);
    requestMsg->msgType = nvidiaMsgType;
    requestMsg->commandCode = addExtTimestamp;
    requestMsg->msgVersion = nvidiaMsgVersion;
    auto iter = request.begin() + sizeof(mctp_vdm::MsgHeader);
    auto beEpochElapsedTime = htobe64(epochElapsedTime);
    std::copy_n(reinterpret_cast<uint8_t*>(&beEpochElapsedTime),
                sizeof(beEpochElapsedTime), iter);

    const mctp_vdm::Message* responseMsg = nullptr;
    size_t responseLen = 0;

    auto rc = co_await mctp_vdm::requester::SendRecvMctpVdmMsg<
        mctp_vdm::requester::Handler<mctp_vdm::requester::Request>>(
        reqHandler, eid, request, &responseMsg, &responseLen);
    if (rc)
    {
        co_return rc;
    }

    if (responseLen != addExtTimestampRespBytes)
    {
        lg2::error("No response received for the request to set external "
                   "timestamp, responseLen={RESPONSELENGTH}",
                   "RESPONSELENGTH", responseLen);
        co_return static_cast<uint8_t>(
            mctp_vdm::CompletionCodes::ErrInvalidLength);
    }

    co_return responseMsg->payload[0];
}

mctp_vdm::requester::Coroutine ErotTimeManager::handleMctpEndpointsTask()
{
    uint64_t elapsedTime = 0;
    try
    {
        auto method = bus.new_method_call(
            "xyz.openbmc_project.Time.Manager", "/xyz/openbmc_project/time/bmc",
            "org.freedesktop.DBus.Properties", "Get");
        method.append("xyz.openbmc_project.Time.EpochTime", "Elapsed");
        dbus::Value value{};
        auto reply = bus.call(method);
        reply.read(value);
        elapsedTime = std::get<uint64_t>(value);
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("Error getting time from xyz.openbmc_project.Time.Manager.",
                   "ERROR", e);
        co_return static_cast<int>(mctp_vdm::CompletionCodes::ErrGeneral);
    }

    while (!queuedMctpInfos.empty())
    {
        const auto& mctpInfos = queuedMctpInfos.front();
        std::vector<uint8_t> eids{};

        for (auto& mctpInfo : mctpInfos)
        {
            auto eid = std::get<0>(mctpInfo);
            eids.emplace_back(eid);
        }

        co_await setTimeOnErots(elapsedTime, eids);

        queuedMctpInfos.pop();
    }

    co_return static_cast<int>(mctp_vdm::CompletionCodes::Success);
}

void ErotTimeManager::handleMctpEndpoints(const mctp::Infos& mctpInfos)
{
    // Populate MCTP info to update EROT's when BMC time changes
    std::vector<uint8_t> eids;
    for (auto& mctpInfo : mctpInfos)
    {
        auto eid = std::get<0>(mctpInfo);
        auto uuid = std::get<1>(mctpInfo);
        auto mediumType = std::get<2>(mctpInfo);
        if (mctpInfoMap.contains(uuid))
        {
            auto search = mctpInfoMap.find(uuid);
            for (const auto& info : search->second)
            {
                if (info.eid == eid)
                {
                    continue;
                }
            }
            search->second.push({eid, mediumType});
        }
        else
        {
            std::priority_queue<mctp::MctpEidInfo> mctpEidInfo;
            mctpEidInfo.push({eid, mediumType});
            mctpInfoMap.emplace(uuid, std::move(mctpEidInfo));
        }
    }
    queuedMctpInfos.emplace(mctpInfos);

    if (setErotTimeHandle)
    {
        if (setErotTimeHandle.done())
        {
            setErotTimeHandle.destroy();

            auto co = handleMctpEndpointsTask();
            setErotTimeHandle = co.handle;
            if (setErotTimeHandle.done())
            {
                setErotTimeHandle = nullptr;
            }
        }
    }
    else
    {
        auto co = handleMctpEndpointsTask();
        setErotTimeHandle = co.handle;
        if (setErotTimeHandle.done())
        {
            setErotTimeHandle = nullptr;
        }
    }
}

void ErotTimeManager::createErrorLog(uint8_t eid, uint8_t rc)
{
    mctp::UUID mctpUUID{};

    // Find the UUID corresponding to the EID
    for (const auto& [uuid, mctpInfo] : mctpInfoMap)
    {
        for (const auto& eidInfo : mctpInfo)
        {
            if (eidInfo.eid == eid)
            {
                mctpUUID = uuid;
                break;
            }
        }
        if (!mctpUUID.empty())
        {
            break;
        }
    }

    std::string erotName{};
    dbus::ObjectValueTree objects{};
    mctp::UUID uuid{};

    // Lookup ERoT inventory object with the UUID and fetch ERoT name
    try
    {
        auto method = bus.new_method_call("xyz.openbmc_project.PLDM", "/",
                                          "org.freedesktop.DBus.ObjectManager",
                                          "GetManagedObjects");
        auto reply = bus.call(method);
        reply.read(objects);
        for (const auto& [objectPath, interfaces] : objects)
        {
            if (interfaces.contains(mctp::UUIDInterface))
            {
                const auto& properties = interfaces.at(mctp::UUIDInterface);
                if (properties.contains("UUID"))
                {
                    uuid = std::get<std::string>(properties.at("UUID"));
                    if (uuid == mctpUUID)
                    {
                        erotName = objectPath.filename();
                        break;
                    }
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to fetch ERoT name to create error log.", "ERROR",
                   e);
    }

    if (erotName.empty())
    {
        erotName = std::to_string(rc);
    }

    // Assign message & resolution based on the error code
    std::string message{};
    std::string resolution{};

    if (rc == static_cast<uint8_t>(mctp_vdm::CompletionCodes::ErrGeneral))
    {
        message = "Failed to add external timestamp";
        resolution = "Retry the operation, if problem perists contact NVIDIA";
    }
    else if (rc ==
             static_cast<uint8_t>(mctp_vdm::CompletionCodes::ErrInvalidData))
    {
        message = "The message version for add external timestamp operation is "
                  "not supported";
        resolution = "Contact NVIDIA.";
    }
    else if (rc == static_cast<uint8_t>(mctp_vdm::CompletionCodes::ErrNotReady))
    {
        message = "Add external timestamp failed due to rate limit threshold "
                  "exceeded";
        resolution = "Wait for rate limit threshold to be cleared and retry "
                     "the operation.";
    }

    using namespace sdbusplus::xyz::openbmc_project::Logging::server;
    // using Level = ;
    std::map<std::string, std::string> addData;
    addData["REDFISH_MESSAGE_ID"] = "ResourceEvent.1.0.ResourceErrorsDetected";
    addData["REDFISH_MESSAGE_ARGS"] = (erotName + "," + message);
    // Level level = Level::Critical;
    addData["xyz.openbmc_project.Logging.Entry.Resolution"] = resolution;

    auto& asioConnection = utils::DBusHandler::getAsioConnection();
    auto severity =
        sdbusplus::xyz::openbmc_project::Logging::server::convertForMessage(
            sdbusplus::xyz::openbmc_project::Logging::server::Entry::Level::
                Critical);
    asioConnection->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                lg2::error("Error while logging message registry: ",
                           "ERROR_MESSAGE", ec.message());
                return;
            }
        },
        "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Create", "Create",
        "ResourceEvent.1.0.ResourceErrorsDetected", severity, addData);
    return;
}