#include "../platform_backend.hpp"
#include <NeFlux/util.hpp>
#if defined(__APPLE__) || defined(__FreeBSD__)
#    include <cstdlib>
#endif

namespace neforce::neflux::platform {

    source_capabilities capabilities() {
        source_capabilities value;
        value.platform_ = platform_kind::macos;
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
            const string listing = run_command("launchctl", {"list"}, 3000);
            bool first = true;
            for (const auto& line: split_lines(listing)) {
                if (first) {
                    first = false;
                    continue;
                }
                const auto parts = split_whitespace(line.view());
                if (parts.size() < 3) {
                    continue;
                }
                service item;
                item.scope_ = "user";
                item.name_ = parts.back();
                item.load_ = "loaded";
                item.active_ = parts[0] == "-" ? "loaded" : "active";
                item.sub_ = parts[0] == "-" ? "waiting" : "running";
                item.description_ = "launchd job";
                if (parts[0] != "-") {
                    const auto pid = parse_u64(parts[0].view());
                    if (pid.has_value()) {
                        item.pid_ = static_cast<uint32_t>(*pid);
                    }
                }
                data.services_.push_back(move(item));
            }
            if (!data.services_.empty()) {
                data.service_managers_.push_back("launchd");
            }
        } catch (const exception& error) {
            data.notes_.push_back(format("launchd: {}", trim(error.what())));
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
