#include <NeFlux/taskstats.hpp>
namespace neforce::neflux {
    struct taskstats_collector::impl {};
    taskstats_collector::taskstats_collector() : impl_(make_unique<impl>()) {}
    taskstats_collector::~taskstats_collector() = default;
    json taskstats_collector::collect(uint32_t pid, uint64_t start_time, bool thread) {
        static_cast<void>(pid);
        static_cast<void>(start_time);
        static_cast<void>(thread);
        return json::object();
    }
    void taskstats_collector::retain(const set<pair<uint32_t, uint64_t>>& identities) {
        static_cast<void>(identities);
    }
} // namespace neforce::neflux
