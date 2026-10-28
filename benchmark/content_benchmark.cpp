/**
 * @file content_benchmark.cpp
 * @brief NexusFlux 内容层渲染基准
 *
 * 覆盖 11 个页面在 120×40 / 160×48 下的完整路径：
 * build_content() → cell_grid → DOM panel/rich_text → headless render → plaintext。
 */

#include <NeFlux/ui/app.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/system/console.hpp>
#include <NeForce/core/time/clocks.hpp>

using namespace neforce;
using namespace neforce::neflux;

namespace {

    snapshot make_snapshot() {
        snapshot value;
        value.timestamp_ = 1'700'000'000;
        value.elapsed_ = 3600.0;
        value.hostname_ = "benchmark-host";
        value.os_ = "Linux";
        value.kernel_ = "6.8.0";
        value.uptime_ = 864000;
        value.cpu_name_ = "Benchmark CPU";
        value.cpu_percent_ = 37.5F;
        value.cpu_mhz_ = 3600;
        value.load_[0] = 1.25F;
        value.load_[1] = 1.10F;
        value.load_[2] = 0.95F;
        value.memory_.total_ = 32ULL * 1024 * 1024 * 1024;
        value.memory_.used_ = 12ULL * 1024 * 1024 * 1024;
        value.memory_.available_ = 20ULL * 1024 * 1024 * 1024;
        value.memory_.swap_total_ = 8ULL * 1024 * 1024 * 1024;
        value.memory_.swap_used_ = 1ULL * 1024 * 1024 * 1024;
        for (int core = 0; core < 16; ++core) {
            value.cores_.push_back(static_cast<float>((core * 17) % 100));
        }

        for (int index = 0; index < 300; ++index) {
            process item;
            item.pid_ = static_cast<uint32_t>(1000 + index);
            if (index != 0) {
                item.parent_ = static_cast<uint32_t>(1000 + index / 4);
            }
            item.start_time_ = 1'699'000'000 + static_cast<uint64_t>(index);
            item.name_ = format("worker-{}", static_cast<uint64_t>(index));
            item.command_ = format("/usr/bin/worker --index {} --serve --threads 4", static_cast<uint64_t>(index));
            item.user_ = index % 5 == 0 ? "root" : "benchmark";
            item.state_ = index % 7 == 0 ? "Sleeping" : "Running";
            item.cpu_ = static_cast<float>((index * 13) % 400) / 4.0F;
            item.memory_ = static_cast<uint64_t>(index + 1) * 3ULL * 1024 * 1024;
            item.virtual_memory_ = item.memory_ * 4;
            item.cpu_time_ms_ = static_cast<uint64_t>(index) * 1234;
            item.read_per_sec_ = static_cast<double>(index) * 1024.0;
            item.write_per_sec_ = static_cast<double>(index) * 512.0;
            item.read_total_ = static_cast<uint64_t>(index) * 1024 * 1024;
            item.write_total_ = static_cast<uint64_t>(index) * 512 * 1024;
            if (index % 9 == 0) {
                item.gpu_memory_ = static_cast<uint64_t>(index + 1) * 8ULL * 1024 * 1024;
                item.gpu_percent_ = static_cast<uint32_t>((index * 7) % 100);
            }
            item.extra_ = json::object();
            item.extra_.set("status", json::object());
            item.extra_.set("io", json::object());
            item.extra_.set("stat", json::object());
            value.processes_.push_back(move(item));
        }

        for (int index = 0; index < 4; ++index) {
            disk item;
            item.name_ = format("nvme{}n1", static_cast<uint64_t>(index));
            item.mount_ = index == 0 ? "/" : format("/mnt/disk{}", static_cast<uint64_t>(index));
            item.filesystem_ = "ext4";
            item.kind_ = "SSD";
            item.total_ = 1024ULL * 1024 * 1024 * 1024;
            item.used_ = item.total_ / 3;
            item.available_ = item.total_ - item.used_;
            value.disks_.push_back(move(item));
        }
        for (int index = 0; index < 4; ++index) {
            disk_io item;
            item.name_ = format("nvme{}n1", static_cast<uint64_t>(index));
            item.read_per_sec_ = static_cast<double>(index + 1) * 1024 * 1024;
            item.write_per_sec_ = static_cast<double>(index + 1) * 512 * 1024;
            item.busy_percent_ = 23.0 + index;
            item.read_iops_ = 1200.0 + index;
            item.write_iops_ = 800.0 + index;
            item.await_ms_ = 0.4 + index * 0.1;
            item.queue_depth_ = 2.0 + index;
            value.disk_io_.push_back(move(item));
        }
        for (int index = 0; index < 2; ++index) {
            network item;
            item.name_ = index == 0 ? "eth0" : "lo";
            item.mtu_ = 1500;
            item.speed_mbps_ = 10000;
            item.rx_per_sec_ = static_cast<double>(index + 1) * 1024 * 1024;
            item.tx_per_sec_ = static_cast<double>(index + 1) * 256 * 1024;
            item.rx_total_ = 1024ULL * 1024 * 1024;
            item.tx_total_ = 512ULL * 1024 * 1024;
            item.state_ = "up";
            item.address_ = index == 0 ? "192.0.2.10/24" : "127.0.0.1/8";
            value.networks_.push_back(move(item));
        }
        {
            gpu item;
            item.id_ = 0;
            item.name_ = "Benchmark GPU";
            item.vendor_ = "NVIDIA";
            item.backend_ = "NVML";
            item.uuid_ = "GPU-bench";
            value.gpus_.push_back(move(item));
        }
        for (int index = 0; index < 12; ++index) {
            sensor item;
            item.name_ = format("sensor{}", static_cast<uint64_t>(index));
            item.temperature_ = 40.0F + static_cast<float>(index);
            value.sensors_.push_back(move(item));
        }
        value.inventory_ = inventory{};
        for (int index = 0; index < 6; ++index) {
            container item;
            item.id_ = format("container{}", static_cast<uint64_t>(index));
            item.name_ = item.id_;
            item.runtime_ = "docker";
            item.image_ = "benchmark/image:latest";
            item.state_ = "running";
            item.status_ = "Up 2 hours";
            item.cpu_ = 1.5 + index;
            item.memory_ = 128ULL * 1024 * 1024;
            item.memory_limit_ = 1024ULL * 1024 * 1024;
            value.inventory_.containers_.push_back(move(item));
        }
        for (int index = 0; index < 10; ++index) {
            service item;
            item.name_ = format("benchmark.service{}", static_cast<uint64_t>(index));
            item.scope_ = "system";
            item.load_ = "loaded";
            item.active_ = "active";
            item.sub_ = "running";
            item.description_ = "benchmark service";
            item.pid_ = static_cast<uint32_t>(2000 + index);
            item.cpu_percent_ = 0.5F + static_cast<float>(index);
            item.memory_ = 64ULL * 1024 * 1024;
            value.inventory_.services_.push_back(move(item));
        }
        value.apply_inventory(value.inventory_);
        return value;
    }

    template <typename Fn>
    double measure_ns(const int iterations, Fn&& fn) {
        for (int warmup = 0; warmup < 2; ++warmup) {
            fn();
        }
        double best = 1e30;
        for (int sample = 0; sample < 3; ++sample) {
            const auto begin = steady_clock::now();
            for (int iteration = 0; iteration < iterations; ++iteration) {
                fn();
            }
            const auto end = steady_clock::now();
            const double ns =
                    static_cast<double>(time_cast<nanoseconds>(end - begin).count()) / static_cast<double>(iterations);
            best = ns < best ? ns : best;
        }
        return best;
    }

} // namespace

int main() {
    const snapshot value = make_snapshot();
    const inventory data = value.inventory_;
    config cfg;
    cfg.interval_ms_ = 1000;
    cfg.time_window_seconds_ = 180;
    const char* views[] = {"overview",   "processes", "gpu",     "storage", "network", "system",
                           "containers", "services",  "history", "events",  "explorer"};
    const int sizes[][2] = {{120, 40}, {160, 48}};
    println("view,width,height,ms_per_frame,bytes");
    for (const auto& size: sizes) {
        for (const char* view: views) {
            size_t bytes = 0;
            const double ns = measure_ns(20, [&] {
                const string text = ui::render_plaintext(value, data, cfg, view, size[0], size[1]);
                bytes = text.size();
            });
            printfln("{},{},{},{:.3f},{}", view, size[0], size[1], ns / 1e6, bytes);
        }
    }
    return 0;
}
