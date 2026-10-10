#include "dali/Transport.hpp"

#include "dali/devices/DeviceAddressing.hpp"

namespace gateway::dali {

CommissionResult Transport::commission_devices(CommissionMode mode, ShortAddress start,
                                               unsigned max_devices) {
    DeviceAddressing addressing{*this};
    return addressing.commission(mode, start, max_devices);
}

}
