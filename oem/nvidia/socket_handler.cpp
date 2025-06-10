#include "socket_handler.hpp"

#include "constants.hpp"
#include "utils.hpp"

#include <libmctp-externals.h>
#include <linux/if_arp.h>
#include <linux/mctp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <phosphor-logging/lg2.hpp>

#include <functional>

namespace mctp_socket
{

template <typename T>
void Handler<T>::processRxMsg(uint8_t eid,
                              const std::vector<uint8_t>& requestMsg)
{
    // NOLINTBEGIN
    auto msg = reinterpret_cast<const mctp_vdm::Message*>(requestMsg.data());
    if (msg->hdr.request == 0)
    {
        auto response = reinterpret_cast<const mctp_vdm::Message*>(msg);
        size_t responseLen = requestMsg.size() -
                             sizeof(struct mctp_vdm::MsgHeader);
        handler.handleResponse(eid, msg->hdr.instanceId, msg->hdr.msgType,
                               msg->hdr.commandCode, response, responseLen);
    }
    // NOLINTEND
}

// DaemonHandler implementation
int DaemonHandler::initSocket(int type, int protocol,
                              const std::vector<uint8_t>& pathName)
{
    int rc = 0;
    int sockFd = socket(AF_UNIX, type, protocol);
    if (sockFd == -1)
    {
        rc = -errno;
        lg2::error("Failed to create the socket, RC={RC}", "RC", strerror(-rc));
        return rc;
    }

    auto fd = std::make_unique<utils::CustomFD>(sockFd);

    /* Initiate a connection to the socket */
    struct sockaddr_un addr
    {};
    addr.sun_family = AF_UNIX;
    // NOLINTBEGIN
    memcpy(addr.sun_path, pathName.data(), pathName.size());
    rc = connect(sockFd, reinterpret_cast<struct sockaddr*>(&addr),
                 pathName.size() + sizeof(addr.sun_family));
    if (rc == -1)
    {
        rc = -errno;
        lg2::error("Failed to connect to the socket, RC={RC}", "RC",
                   strerror(-rc));
        return rc;
    }
    
    /* Register for MCTP VDM message type */
    ssize_t result =
        write(sockFd, &mctp_vdm::messageType, sizeof(mctp_vdm::messageType));
    if (result == -1)
    {
        rc = -errno;
        lg2::error(
            "Failed to send message type as MCTP VDM to demux daemon, RC={RC}",
            "RC", strerror(-rc));
        return rc;
    }

    auto io = std::make_unique<IO>(event, sockFd, EPOLLIN,
                                   std::bind(&DaemonHandler::handleReceivedMsg,
                                             this, std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));

    socketInfoMap[pathName] = std::tuple(std::move(fd), std::move(io));

    return sockFd;
    // NOLINTEND
}

void DaemonHandler::handleReceivedMsg(IO& io, int fd, uint32_t revents)
{
    if ((revents & EPOLLIN) == 0U)
    {
        return;
    }
    // NOLINTBEGIN
    int returnCode = 0;
    ssize_t peekedLength = recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC);
    if (peekedLength == 0)
    {
        // MCTP daemon has closed the socket this daemon is connected to.
        // This may or may not be an error scenario, in either case the
        // recovery mechanism for this daemon is to restart, and hence
        // exit the event loop, that will cause this daemon to exit with a
        // failure code.
        lg2::error("Socket connection closed. Terminating.");
        io.get_event().exit(0);
    }
    else if (peekedLength <= -1)
    {
        returnCode = -errno;
        lg2::error("recv system call failed, RC={RC}", "RC", returnCode);
    }
    else
    {
        std::vector<uint8_t> requestMsg(peekedLength);
        auto recvDataLength = recv(fd, static_cast<void*>(requestMsg.data()),
                                   peekedLength, 0);
        if (recvDataLength == peekedLength)
        {
            utils::printBuffer(utils::rx, requestMsg);

            if (mctp_vdm::messageType != requestMsg[2])
            {
                // Skip this message and continue.
                lg2::info("Skipping non-VDM message type: {TYPE}", "TYPE",
                          requestMsg[2]);
            }
            else
            {
                using tag_owner_and_tag = uint8_t;
                using type = uint8_t;
                uint8_t eid = requestMsg[1];
                // Extract payload from the MCTP message
                std::vector<uint8_t> payload(requestMsg.begin() +
                                                 sizeof(tag_owner_and_tag) +
                                                 sizeof(eid) + sizeof(type),
                                             requestMsg.end());
                processRxMsg(eid, payload);
            }
        }
        else
        {
            lg2::error("Failure to read peeked length packet. peekedLength="
                       "{PEEKEDLENGTH} recvDataLength={RECVDATALENGTH}",
                       "PEEKEDLENGTH", peekedLength, "RECVDATALENGTH",
                       recvDataLength);
        }
    }
    // NOLINTEND
}

// InKernelHandler implementation
int InKernelHandler::initSocket(
    [[maybe_unused]] int type, [[maybe_unused]] int protocol,
    [[maybe_unused]] const std::vector<uint8_t>& pathName)
{
    // NOLINTBEGIN
    if (isFdValid)
    {
        return fd;
    }

    fd = socket(AF_MCTP, SOCK_DGRAM, 0);
    if (fd == -1)
    {
        int rc = -errno;
        lg2::error("Failed to create MCTP socket, RC={RC}", "RC",
                   strerror(-rc));
        return rc;
    }

    socklen_t optlen = sizeof(sendBufferSize);
    int rc = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendBufferSize, &optlen);
    if (rc == -1)
    {
        rc = -errno;
        lg2::error("Error getting socket send buffer size, RC={RC}", "RC",
                   strerror(-rc));
        close(fd);
        fd = -1;
        return rc;
    }

    struct sockaddr_mctp addr;
    memset(&addr, 0, sizeof(addr));

    addr.smctp_family = AF_MCTP;
    addr.smctp_network = MCTP_NET_ANY;
    addr.smctp_addr.s_addr = MCTP_ADDR_ANY;
    addr.smctp_tag = MCTP_TAG_OWNER;
    addr.smctp_type = mctp_vdm::messageType;

    rc = bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (rc == -1)
    {
        rc = -errno;
        lg2::error("Error binding socket to MCTP VDM type, RC={RC}", "RC",
                   strerror(-rc));
        close(fd);
        fd = -1;
        return rc;
    }

    io = std::make_unique<IO>(event, fd, EPOLLIN,
                              std::bind(&InKernelHandler::handleReceivedMsg,
                                        this, std::placeholders::_1,
                                        std::placeholders::_2,
                                        std::placeholders::_3));

    isFdValid = true;
    return fd;
    // NOLINTEND
}

void InKernelHandler::handleReceivedMsg(IO& io, int fd, uint32_t revents)
{
    if ((revents & EPOLLIN) == 0U)
    {
        return;
    }
    // NOLINTBEGIN
    int returnCode = 0;
    ssize_t peekedLength = recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC);
    if (peekedLength == 0)
    {
        lg2::error("Socket connection closed. Terminating.");
        io.get_event().exit(0);
        // lg2::info("InKernelHandler::handleReceivedMsg exit");
        return;
    }
    else if (peekedLength < 0)
    {
        returnCode = -errno;
        // lg2::error("recv system call failed, RC={RC}", "RC", returnCode);
        // lg2::info("InKernelHandler::handleReceivedMsg exit");
        return;
    }

    std::vector<uint8_t> requestMsg(peekedLength);
    struct sockaddr_mctp addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t addrlen = sizeof(addr);

    ssize_t recvDataLength =
        recvfrom(fd, static_cast<void*>(requestMsg.data()), peekedLength, 0,
                 reinterpret_cast<struct sockaddr*>(&addr), &addrlen);

    if (recvDataLength != peekedLength)
    {
        returnCode = -errno;
        lg2::error(
            "Failed to read complete packet. peekedLength={PEEKEDLENGTH} recvDataLength={RECVDATALENGTH} ErrorNo={ERROR}",
            "PEEKEDLENGTH", peekedLength, "RECVDATALENGTH", recvDataLength,
            "ERROR", returnCode);
        return;
    }

    utils::printBuffer(utils::rx, requestMsg);
    processRxMsg(addr.smctp_addr.s_addr, requestMsg);
    // NOLINTEND
}

// Explicit template instantiations
template class Handler<mctp_vdm::requester::DaemonRequest>;
template class Handler<mctp_vdm::requester::InKernelRequest>;

} // namespace mctp_socket
