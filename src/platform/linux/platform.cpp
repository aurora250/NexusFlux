#include "../platform_backend.hpp"
#include <NeFlux/util.hpp>

namespace neforce::neflux::platform {

    source_capabilities capabilities() {
        source_capabilities value;
        value.platform_ = platform_kind::linux;
        value.processes_ = true;
        value.threads_ = true;
        value.connections_ = true;
        value.gpu_ = true;
        value.containers_ = true;
        value.services_ = true;
        value.sensors_ = true;
        value.pressure_ = true;
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
        const auto text = read_text_file("/proc/loadavg");
        if (!text.has_value()) {
            return;
        }
        const auto parts = split_whitespace(text->view());
        for (size_t i = 0; i < 3 && i < parts.size(); ++i) {
            const auto value = parse_f64(parts[i].view());
            load[i] = value.has_value() ? static_cast<float>(*value) : 0.0F;
        }
    }

} // namespace neforce::neflux::platform
