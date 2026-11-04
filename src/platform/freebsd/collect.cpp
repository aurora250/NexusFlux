#include "../platform_backend.hpp"
#include <NeFlux/util.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/system/sysinfo.hpp>

namespace neforce::neflux::platform {

    uint64_t cpu_frequency_mhz() { return 0; }

    vector<uint64_t> core_frequencies_mhz(size_t count) {
        static_cast<void>(count);
        return {};
    }

    vector<topology_entry> read_topology(size_t count) {
        static_cast<void>(count);
        return {};
    }

    vector<optional<float>> core_temperatures(const vector<topology_entry>& topology) {
        static_cast<void>(topology);
        return {};
    }

    proc_meminfo read_meminfo() { return {}; }

    vector<disk> read_disks() {
        vector<disk> out;
        for (const auto& mount: sysinfo::mount_points()) {
            disk item;
            item.name_ = mount.source.empty() ? mount.target : mount.source;
            item.mount_ = mount.target;
            item.filesystem_ = mount.filesystem;
            item.available_ = mount.available_bytes;
            item.total_ = mount.total_bytes;
            item.used_ = item.total_ > item.available_ ? item.total_ - item.available_ : 0;
            item.extra_ = json::object();
            out.push_back(move(item));
        }
        neforce::tim_sort(out.begin(), out.end(), [](const disk& a, const disk& b) { return a.mount_ < b.mount_; });
        return out;
    }

    vector<sensor> read_sensors() { return {}; }

    vector<battery> read_batteries() {
        vector<battery> out;
        const auto info = sysinfo::battery_status();
        if (!info.present) {
            return out;
        }
        battery item;
        item.name_ = "Battery";
        item.status_ = info.status;
        if (info.charge_percent >= 0.0) {
            item.percent_ = static_cast<float>(info.charge_percent);
        }
        if (info.rate_mw >= 0.0) {
            item.watts_ = static_cast<float>(info.rate_mw / 1000.0);
        }
        item.extra_ = json::object();
        if (info.capacity_mwh >= 0.0) {
            item.extra_.set("energy_wh", json(info.capacity_mwh / 1000.0));
        }
        if (info.full_capacity_mwh >= 0.0) {
            item.extra_.set("full_capacity_wh", json(info.full_capacity_mwh / 1000.0));
        }
        if (info.remaining_seconds >= 0) {
            if (info.charging) {
                item.extra_.set("time_to_full_seconds", json(static_cast<uint64_t>(info.remaining_seconds)));
            } else {
                item.extra_.set("time_to_empty_seconds", json(static_cast<uint64_t>(info.remaining_seconds)));
            }
        }
        out.push_back(move(item));
        return out;
    }

    vector<pressure> read_pressure() { return {}; }

    string process_state_text(const ::neforce::process::snapshot_entry& entry) {
        switch (entry.status) {
            case ::neforce::process::state::running:
                return "Running";
            case ::neforce::process::state::suspended:
                return "Suspended";
            case ::neforce::process::state::stopped:
                return "Stopped";
            case ::neforce::process::state::exited:
                return "Zombie";
            case ::neforce::process::state::unknown:
            default:
                return "Unknown";
        }
    }

} // namespace neforce::neflux::platform
