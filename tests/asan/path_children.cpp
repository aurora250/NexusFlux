#include <NeFlux/collect.hpp>
#include <NeFlux/config.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/memory/memory_pool.hpp>
#include <cstdio>

int main() {
    const neforce::neflux::config cfg;
    for (int round = 0; round < 3; ++round) {
        for (int pid = 1; pid <= 1023; ++pid) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "/proc/%d/fd", pid);
            try {
                const auto entries = neforce::path(buffer).children();
                static_cast<void>(entries);
            } catch (const neforce::exception&) {
            }
        }
        neforce::neflux::collector instance(false);
        instance.collect_with_processes(cfg, true, false, false);
    }
    return neforce::system_memory_pool().verify() ? 0 : 1;
}
