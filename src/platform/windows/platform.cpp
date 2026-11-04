#include "../platform_backend.hpp"
#include <NeFlux/util.hpp>
#if defined(__APPLE__) || defined(__FreeBSD__)
#    include <cstdlib>
#endif

namespace neforce::neflux::platform {

    source_capabilities capabilities() {
        source_capabilities value;
        value.platform_ = platform_kind::windows;
        value.processes_ = true;
        value.threads_ = true;
        value.connections_ = true;
        value.gpu_ = true;
        value.containers_ = true;
        value.services_ = true;
        value.sensors_ = true;
        value.actions_ = true;
        value.signals_ = true;
        value.nice_ = true;
        value.affinity_ = true;
        value.process_logs_ = true;
        value.process_files_ = true;
        value.environment_ = true;
        value.workload_actions_ = true;
        value.vm_actions_ = true;
        return value;
    }

    void read_load(float (&load)[3]) {
        load[0] = 0.0F;
        load[1] = 0.0F;
        load[2] = 0.0F;
#if defined(__APPLE__) || defined(__FreeBSD__)
        double values[3] = {0.0, 0.0, 0.0};
        if (::getloadavg(values, 3) == 3) {
            load[0] = static_cast<float>(values[0]);
            load[1] = static_cast<float>(values[1]);
            load[2] = static_cast<float>(values[2]);
        }
#endif
    }

} // namespace neforce::neflux::platform
