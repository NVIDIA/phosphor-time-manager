#include "erot_time_manager.hpp"
#include "handler.hpp"
#include "instance_id.hpp"
#include "request.hpp"
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

    // MCTP VDM requester handler
    requester::Handler<requester::Request> reqHandler(event, instanceIdMgr,
                                                      sockManager);

    mctp_socket::Handler sockHandler(event, reqHandler, sockManager);

    // ERoT time manager
    auto erotTimeManager = std::make_unique<ErotTimeManager>(
        bus, event, reqHandler, sockHandler, instanceIdMgr);

    std::unique_ptr<MctpDiscovery> mctpDiscoveryHandler =
        std::make_unique<MctpDiscovery>(
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
