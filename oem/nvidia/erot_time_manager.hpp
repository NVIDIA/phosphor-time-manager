#pragma once

#include "instance_id.hpp"
#include "mctp_endpoint_discovery.hpp"
#include "types.hpp"

#include <coroutine>
#include <queue>
#include <tuple>
#include <unordered_map>

#include <sdeventplus/source/io.hpp>

namespace mctp
{

using Priority = int;
static std::unordered_map<mctp::Medium, Priority> mediumPriority{
    {"xyz.openbmc_project.MCTP.Endpoint.MediaTypes.PCIe", 0},
    {"xyz.openbmc_project.MCTP.Endpoint.MediaTypes.SPI", 1},
    {"xyz.openbmc_project.MCTP.Endpoint.MediaTypes.SMBus", 2},
};

struct MctpEidInfo
{
    uint8_t eid;
    mctp::Medium medium;

    friend bool operator<(MctpEidInfo const& lhs, MctpEidInfo const& rhs)
    {
        return mediumPriority.at(lhs.medium) > mediumPriority.at(rhs.medium);
    }
};

struct MCTPEidInfoPriorityQueue : std::priority_queue<MctpEidInfo>
{
    auto begin() const
    {
        return c.begin();
    }
    auto end() const
    {
        return c.end();
    }
};

using MctpInfoMap = std::unordered_map<UUID, MCTPEidInfoPriorityQueue>;

} // namespace mctp

class MctpDiscoveryHandlerIntf;
using namespace sdeventplus::source;

/** @class ErotTimeManager
 *
 *  ERoT time manager
 *  communicate with the endpoint. The lookup APIs are used when processing MCTP
 *  VDM Rx messages and when sending MCTP VDM Tx messages.
 */
class ErotTimeManager : public mctp_vdm::MctpDiscoveryHandlerIntf
{
  public:
    ErotTimeManager() = delete;
    ErotTimeManager(const ErotTimeManager&) = delete;
    ErotTimeManager(ErotTimeManager&&) = delete;
    ErotTimeManager& operator=(const ErotTimeManager&) = delete;
    ErotTimeManager& operator=(ErotTimeManager&&) = delete;
    ~ErotTimeManager();

    /** @brief
     *
     *  @param[in] bus - reference to systemd bus
     *  @param[in] reqHandler - MCTP VDM requester handler
     *  @param[in] sockHandler - MCTP demux daemon socket handler
     *  @param[in] instanceIdMgr - Instance ID Manager
     */
    explicit ErotTimeManager(
        sdbusplus::bus::bus& bus,
        sdeventplus::Event& event,
        mctp_vdm::requester::Handler<mctp_vdm::requester::Request>& reqHandler,
        mctp_socket::Handler& sockHandler,
        mctp_vdm::InstanceIdMgr& instanceIdMgr);

    mctp_vdm::requester::Coroutine setTimeOnErots(uint64_t epochElapsedTime,
                                                  std::vector<uint8_t> eids);
    mctp_vdm::requester::Coroutine setTimeOnErot(uint8_t eid,
                                                 uint64_t epochElapsedTime);

    mctp_vdm::requester::Coroutine handleMctpEndpointsTask();

    void handleMctpEndpoints(const mctp::Infos& mctpInfos);

  private:
    void createErrorLog(uint8_t eid, uint8_t rc);

    /** @brief reference to the systemd bus */
    sdbusplus::bus::bus& bus;

    /** @brief reference to the event loop */
    sdeventplus::Event& event;

    mctp_vdm::requester::Handler<mctp_vdm::requester::Request>& reqHandler;

    mctp_socket::Handler& sockHandler;

    mctp::MctpInfoMap mctpInfoMap;

    mctp_vdm::InstanceIdMgr& instanceIdMgr;

    /** @brief A queue of MctpInfos to be discovered **/
    std::queue<mctp::Infos> queuedMctpInfos{};

    /** @brief Coroutine handle for setting ERoT time */
    std::coroutine_handle<> setErotTimeHandle;

    /** @brief Timer object to watch for system time changes */
    int timerFd = -1;

    /** @brief I/O event source to watch for system time changes */
    std::unique_ptr<IO> mcTimeChangeIO = nullptr;

    void handleTimeChange(IO& io, int fd, uint32_t revents);
};
