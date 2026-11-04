#include "../platform_backend.hpp"
#include <NeFlux/util.hpp>
#if defined(__APPLE__) || defined(__FreeBSD__)
#    include <cstdlib>
#endif

namespace neforce::neflux::platform {

    source_capabilities capabilities() {
        source_capabilities value;
        value.platform_ = platform_kind::freebsd;
        value.processes_ = true;
        value.threads_ = false;
        value.connections_ = true;
        value.gpu_ = true;
        value.containers_ = true;
        value.services_ = true;
        value.sensors_ = false;
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

    void discover_services(inventory& data) {
        try {
            const string listing = run_command("service", {"-l"}, 3000);
            for (const auto& line: split_lines(listing)) {
                string name = trim(line.view());
                if (name.empty() || name.find(' ') != string::npos) {
                    continue;
                }
                service item;
                item.scope_ = "system";
                item.name_ = move(name);
                item.load_ = "loaded";
                item.active_ = "unknown";
                item.sub_ = "unknown";
                item.description_ = "rc.d service";
                data.services_.push_back(move(item));
            }
            if (!data.services_.empty()) {
                data.service_managers_.push_back("rc.d");
            }
        } catch (const exception& error) {
            data.notes_.push_back(format("rc.d: {}", trim(error.what())));
        }
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
