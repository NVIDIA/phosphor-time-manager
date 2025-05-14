#include "../config.h"

#include "erot_time_manager.hpp"
#include "handler.hpp"
#include "instance_id.hpp"
#include "request.hpp"
#include "socket_handler.hpp"
#include "types.hpp"

#include <sdbusplus/bus.hpp>
#include <sdeventplus/event.hpp>

int main(int /*argc*/, char** /*argv*/)
{
    auto event = sdeventplus::Event::get_default();
    auto bus = sdbusplus::bus::new_default();
    mctp_socket::Manager sockManager;
    mctp_vdm::InstanceIdMgr instanceIdMgr;

    using namespace mctp_vdm;

#ifdef MCTP_IN_KERNEL
    using TRequest = mctp_vdm::requester::InKernelRequest;
    using TSocketHandler = mctp_socket::InKernelHandler;
#else
    using TRequest = mctp_vdm::requester::DaemonRequest;
    using TSocketHandler = mctp_socket::DaemonHandler;
#endif

    requester::Handler<TRequest> reqHandler(event, instanceIdMgr, sockManager);

    TSocketHandler sockHandler(event, reqHandler, sockManager);

    auto erotTimeManager = std::make_unique<ErotTimeManager<TRequest>>(
        bus, event, reqHandler, sockHandler, instanceIdMgr);

    std::unique_ptr<MctpDiscovery<TRequest>> mctpDiscoveryHandler =
        std::make_unique<MctpDiscovery<TRequest>>(
            bus, sockHandler,
            std::initializer_list<mctp_vdm::MctpDiscoveryHandlerIntf*>{
                erotTimeManager.get()});

    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);

    auto returnCode = event.loop();

    if (returnCode)
    {
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);

    return 0;
}
