#pragma once

#include "handler.hpp"
#include "socket_manager.hpp"
#include "utils.hpp"

#include <sdeventplus/event.hpp>
#include <sdeventplus/source/io.hpp>

#include <map>
#include <optional>
#include <unordered_map>

namespace mctp_socket
{

using PathName = std::string;

using namespace sdeventplus;
using namespace sdeventplus::source;

/** @class Handler
 *
 *  Base class for MCTP socket handlers that defines the interface for
 *  communication with MCTP endpoints.
 */
template <typename T = mctp_vdm::requester::RequestRetryTimer>
class Handler
{
  public:
    Handler() = delete;
    Handler(const Handler&) = delete;
    Handler(Handler&&) = default;
    Handler& operator=(const Handler&) = delete;
    Handler& operator=(Handler&&) = default;
    virtual ~Handler() = default;

    /** @brief Constructor
     *
     *  @param[in] event - daemon's main event loop
     *  @param[in] handler - MCTP VDM request handler
     *  @param[in/out] manager - MCTP socket manager
     */
    explicit Handler(sdeventplus::Event& event,
                     mctp_vdm::requester::Handler<T>& handler,
                     mctp_socket::Manager& manager) :
        event(event),
        handler(handler), manager(manager)
    {}

    /** @brief Register MCTP endpoint with socket information
     *
     *  @param[in] eid - MCTP endpoint ID
     *  @param[in] type - socket type
     *  @param[in] protocol - socket protocol
     *  @param[in] pathName - socket path name
     */
    void registerMctpEndpoint(uint8_t eid, int type, int protocol,
                              const std::vector<uint8_t>& pathName)
    {
        if (eidToSockMap.find(eid) == eidToSockMap.end())
        {
            eidToSockMap[eid] = std::make_tuple(type, protocol, pathName);
        }
    }

    /** @brief Activates sockets for the given EIDs
     *
     *  @param[in] eids - vector of MCTP endpoint IDs
     *  @return 0 on success, negative value on failure
     */
    int activateSockets(const std::vector<uint8_t>& eids)
    {
        for (const auto& eid : eids)
        {
            auto type = std::get<0>(eidToSockMap[eid]);
            auto protocol = std::get<1>(eidToSockMap[eid]);
            auto pathName = std::get<2>(eidToSockMap[eid]);

            auto entry = socketInfoMap.find(pathName);
            if (entry == socketInfoMap.end())
            {
                auto fd = initSocket(type, protocol, pathName);
                if (fd < 0)
                {
                    lg2::error("Error initialising socket for EID={EID}", "EID",
                               eid);
                    continue;
                }
                manager.registerEndpoint(eid, fd);
            }
            else
            {
                manager.registerEndpoint(
                    eid, (*(std::get<0>(entry->second)).get())());
            }
        }
        return 0;
    }

    /** @brief Deactivates all sockets and clears endpoint registrations */
    void deactivateSockets()
    {
        socketInfoMap.clear();
        manager.clearMctpEndpoints();
    }

  protected:
    sdeventplus::Event& event;
    mctp_vdm::requester::Handler<T>& handler;
    mctp_socket::Manager& manager;

    /** @brief Socket information for MCTP Tx/Rx daemons */
    std::map<std::vector<uint8_t>,
             std::tuple<std::unique_ptr<utils::CustomFD>, std::unique_ptr<IO>>>
        socketInfoMap;

    /** @brief Socket information for MCTP Tx/Rx daemons */
    std::map<uint8_t, std::tuple<int, int, std::vector<uint8_t>>> eidToSockMap;

    /** @brief Initialize socket with given parameters
     *
     *  @param[in] type - socket type
     *  @param[in] protocol - socket protocol
     *  @param[in] pathName - socket path name
     *  @return socket file descriptor on success, negative value on failure
     */
    virtual int initSocket(int type, int protocol,
                           const std::vector<uint8_t>& pathName) = 0;

    /** @brief Process received MCTP message
     *
     *  @param[in] requestMsg - received message data
     */
    void processRxMsg(uint8_t eid, const std::vector<uint8_t>& requestMsg);
};

/** @class DaemonHandler
 *
 *  Handler implementation for MCTP communication via daemon
 */
class DaemonHandler : public Handler<mctp_vdm::requester::DaemonRequest>
{
  public:
    using Handler<mctp_vdm::requester::DaemonRequest>::Handler;

  private:
    int initSocket(int type, int protocol,
                   const std::vector<uint8_t>& pathName) override;
    void handleReceivedMsg(IO& io, int fd, uint32_t revents);
};

/** @class InKernelHandler
 *
 *  Handler implementation for in-kernel MCTP communication
 */
class InKernelHandler : public Handler<mctp_vdm::requester::InKernelRequest>
{
  public:
    using Handler<mctp_vdm::requester::InKernelRequest>::Handler;

  private:
    int initSocket(int type, int protocol,
                   const std::vector<uint8_t>& pathName) override;
    void handleReceivedMsg(IO& io, int fd, uint32_t revents);

    std::unique_ptr<IO> io;
    int fd{-1};
    int sendBufferSize{0};
    bool isFdValid{false};
};

} // namespace mctp_socket
