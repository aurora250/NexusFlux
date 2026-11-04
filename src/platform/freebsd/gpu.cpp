#include "../platform_backend.hpp"

namespace neforce::neflux::platform {

    string nvml_library_name() { return "libnvidia-ml.so.1"; }

    vector<gpu> read_platform_gpus(uint32_t first_id) {
        static_cast<void>(first_id);
        return {};
    }

} // namespace neforce::neflux::platform
