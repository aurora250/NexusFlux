#include <NeFlux/advanced.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/config.hpp>
#include <NeFlux/collect.hpp>
#include <NeFlux/connections.hpp>
#include <NeFlux/diagnostics.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/filter.hpp>
#include <NeFlux/history.hpp>
#include <NeFlux/json.hpp>
#include <NeFlux/model.hpp>
#include <NeFlux/plot.hpp>
#include <NeFlux/recording.hpp>
#include <NeFlux/ros.hpp>
#include <NeFlux/tags.hpp>
#include <NeFlux/ui/app.hpp>
#include <NeFlux/util.hpp>
#include <cstdio> // for CHECK macro
#include <NeForce/core/file/filesystem.hpp>
#include <NeForce/core/system/environment.hpp>
#if NEFLUX_PLATFORM_LINUX
#    include <unistd.h>
#endif

namespace {

    int g_failures = 0;

#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            ++g_failures;                                               \
        }                                                               \
    } while (false)

    using namespace neforce;
    using namespace neforce::neflux;

    snapshot sample_snapshot();

    void test_json() {
        json value = json::object();
        value.set("a", json(static_cast<int64_t>(42)));
        value.set("b", json(0.5F));
        value.set("c", json("text"));
        json array = json::array();
        array.push(json(true));
        value.set("d", array);
        const string text = value.dump();
        const json parsed = json::parse(text.view());
        CHECK(parsed.integer_or("a") == 42);
        CHECK(parsed.get("c") != nullptr && parsed.string_or("c") == "text");
        CHECK(parsed.get("d") != nullptr && parsed.get("d")->size() == 1);
        CHECK(format_number(0.1, 15) == "0.1");
        CHECK(format_number(12.0, 15) == "12");
        string invalid("bad");
        invalid.push_back(static_cast<char>(0xFF));
        const json escaped = json(invalid);
        const string escaped_text = escaped.dump();
        CHECK(escaped_text.find("\\ufffd") != string::npos);
        CHECK(escaped_text.find(static_cast<char>(0xFF)) == string::npos);
        const string unicode = "进程 · 世界";
        CHECK(json(unicode).dump().find(unicode) != string::npos);
    }

    void test_config() {
        config cfg;
        cfg.theme_ = "nord";
        cfg.interval_ms_ = 500;
        cfg.sort_index_ = 2;
        cfg.reverse_sort_ = true;
        cfg.tree_ = true;
        cfg.display_.network_bits_ = true;
        cfg.display_.network_logarithmic_ = true;
        cfg.display_.show_battery_ = false;
        const string text = cfg.to_toml();
        const config parsed = config::from_toml(text);
        CHECK(parsed.theme_ == "nord");
        CHECK(parsed.interval_ms_ == 500);
        CHECK(parsed.sort_index_ == 2);
        CHECK(parsed.reverse_sort_);
        CHECK(parsed.tree_);
        CHECK(parsed.display_.network_bits_);
        CHECK(parsed.display_.network_logarithmic_);
        CHECK(!parsed.display_.show_battery_);
        bool rejected = false;
        try {
            config::from_toml("unknown_key = 1");
        } catch (const exception&) {
            rejected = true;
        }
        CHECK(rejected);
    }

    void test_plot() {
        deque<double> values;
        for (int index = 0; index < 100; ++index) {
            values.push_back(static_cast<double>(index));
        }
        plot_window window;
        window.samples = 100;
        window.end = 100;
        vector<plot_range> projected;
        window.project(values, 25, projected);
        CHECK(projected.size() == 25);
        CHECK(projected.back().last == 99.0);
        CHECK(nice_max(110.0) == 200.0);
        CHECK(nice_max(-1.0) == 1.0);
    }

    void test_filter() {
        process value;
        value.pid_ = 42;
        value.name_ = "python";
        value.user_ = "alice";
        value.command_ = "python train.py";
        value.memory_ = 2048ULL * 1024 * 1024;
        CHECK(query::parse("pid:42").matches(value));
        CHECK(query::parse("cpu:>10").matches(value) == false);
        CHECK(query::parse("!name:rust").matches(value));
        CHECK(query::parse("name:/python|ruby/").matches(value));
        CHECK(query::parse("user:alice mem:>1024").matches(value));
        CHECK(query::parse("user:bob | name:python").matches(value));
    }

    void test_tags() {
        snapshot value;
        process item;
        item.pid_ = 7;
        item.name_ = "postgres";
        item.command_ = "postgres -D /var/lib/postgresql/data";
        item.memory_ = 2ULL * 1024ULL * 1024ULL * 1024ULL;
        item.cpu_ = 120.0F;
        value.processes_.push_back(item);
        process resolver;
        resolver.pid_ = 8;
        resolver.name_ = "systemd-resolve";
        value.processes_.push_back(resolver);
        process shell;
        shell.pid_ = 9;
        shell.name_ = "zsh";
        value.processes_.push_back(shell);
        process kernel;
        kernel.pid_ = 10;
        kernel.name_ = "kworker/0:1";
        kernel.kernel_thread_ = true;
        value.processes_.push_back(kernel);
        annotate(value);
        const auto& tags = value.processes_[0].tags_;
        bool database = false;
        bool memory = false;
        bool cpu = false;
        for (const auto& tag: tags) {
            database = database || tag.key_ == "database";
            memory = memory || tag.key_ == "memory-heavy";
            cpu = cpu || tag.key_ == "cpu-cores";
        }
        CHECK(database);
        CHECK(memory);
        CHECK(cpu);
        CHECK(tag_matches(tags, "database"));
        CHECK(query_matches(tags, "tag:database", "postgres"));
        CHECK(!value.processes_[1].tags_.empty());
        CHECK(value.processes_[1].tags_[0].key_ == "dns");
        CHECK(value.processes_[2].tags_.empty());
        CHECK(value.processes_[3].tags_.empty());
    }

    void test_discovery() {
        const auto size = parse_size("1.5 GiB");
        CHECK(size.has_value() && *size == 1610612736ULL);
        CHECK(!parse_size("nope").has_value());
        const string table =
                "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
                "   0: 0100007F:0035 00000000:0000 0A 00000000:00000000 00:00000000 00000000   102        0 12345 1\n";
        const auto listeners = parse_listeners(table, "TCP");
        CHECK(listeners.size() == 1);
        if (!listeners.empty()) {
            CHECK(listeners[0].port_ == 53);
            CHECK(listeners[0].address_ == "127.0.0.1");
            CHECK(listeners[0].uid_ == 102);
        }
        CHECK(decode_address("0100007F:0035") == "127.0.0.1:53");
    }

    void test_network_attribution() {
        snapshot value = sample_snapshot();
        inventory data;
        data.extra_ = json::object();
        json payload = json::object();
        payload.set("output", json("{\"type\":\"map\",\"data\":{\"@tx\":{\"42\":2048}}}\n"
                                   "{\"type\":\"map\",\"data\":{\"@rx\":{\"42\":1024}}}"));
        data.extra_.set("Process TCP bytes / 1s (bpftrace)", payload);
        value.apply_inventory(data);
        CHECK(!value.processes_.empty());
        if (!value.processes_.empty()) {
            CHECK(value.processes_[0].rx_per_sec_.has_value() && *value.processes_[0].rx_per_sec_ == 1024.0);
            CHECK(value.processes_[0].tx_per_sec_.has_value() && *value.processes_[0].tx_per_sec_ == 2048.0);
        }
    }

    void test_history() {
        config cfg;
        cfg.history_ = 30;
        history value;
        snapshot sample;
        sample.elapsed_ = 1.0;
        sample.cpu_percent_ = 12.5F;
        sample.memory_.total_ = 100;
        sample.memory_.used_ = 50;
        value.ingest(sample, cfg);
        CHECK(value.sequence_ == 1);
        CHECK(value.cpu_.size() == 1);
        CHECK(value.cpu_.back() == 12.5);
        CHECK(value.memory_.back() == 50.0);
        const auto window = value.window(30);
        CHECK(window.end == 1);
    }

    void test_battery_and_gpu() {
        battery battery_value;
        battery_value.status_ = "Charging";
        battery_value.percent_ = 87.0F;
        battery_value.watts_ = 31.24F;
        battery_value.extra_ = json::object();
        battery_value.extra_.set("time_to_full_seconds", json(static_cast<uint64_t>(2460)));
        CHECK(battery_value.summary().find("87%") != string::npos);
        CHECK(battery_value.remaining().has_value());
        gpu device;
        device.unified_memory_ = true;
        device.memory_used_ = static_cast<uint64_t>(10) << 30;
        device.memory_total_ = static_cast<uint64_t>(32) << 30;
        device.memory_limit_ = static_cast<uint64_t>(20) << 30;
        CHECK(device.memory_label() == "UMEM");
        CHECK(device.memory_percent().has_value() && *device.memory_percent() == 50.0);
    }

    snapshot sample_snapshot() {
        snapshot value;
        value.timestamp_ = 1700000000;
        value.elapsed_ = 10.0;
        value.collection_ms_ = 3.5;
        value.hostname_ = "test-host";
        value.os_ = "Linux (Test)";
        value.kernel_ = "6.0.0";
        value.uptime_ = 3600;
        value.cpu_name_ = "Test CPU";
        value.cpu_percent_ = 25.0F;
        value.cpu_mhz_ = 3000;
        value.cores_ = {10.0F, 20.0F, 30.0F, 40.0F};
        value.core_temperatures_ = {optional<float>(45.0F), none, optional<float>(50.0F), none};
        value.load_[0] = 1.0F;
        value.load_[1] = 2.0F;
        value.load_[2] = 3.0F;
        value.memory_.total_ = 16ULL * 1024 * 1024 * 1024;
        value.memory_.used_ = 8ULL * 1024 * 1024 * 1024;
        value.memory_.available_ = 8ULL * 1024 * 1024 * 1024;
        value.memory_.free_ = 4ULL * 1024 * 1024 * 1024;
        value.memory_.cached_ = 2ULL * 1024 * 1024 * 1024;
        value.memory_.swap_total_ = 2ULL * 1024 * 1024 * 1024;
        value.memory_.swap_used_ = 0;
        process item;
        item.pid_ = 42;
        item.name_ = "python3";
        item.command_ = "python3 train.py";
        item.user_ = "alice";
        item.state_ = "Sleeping";
        item.cpu_ = 12.5F;
        item.memory_ = 512ULL * 1024 * 1024;
        item.thread_ = false;
        item.extra_ = json::object();
        value.processes_.push_back(item);
        disk root;
        root.name_ = "sda1";
        root.mount_ = "/";
        root.filesystem_ = "ext4";
        root.kind_ = "SSD";
        root.total_ = 1000ULL * 1024 * 1024 * 1024;
        root.used_ = 500ULL * 1024 * 1024 * 1024;
        root.available_ = 500ULL * 1024 * 1024 * 1024;
        root.extra_ = json::object();
        value.disks_.push_back(root);
        disk_io io;
        io.name_ = "sda";
        io.aggregate_ = true;
        io.read_per_sec_ = 1024.0;
        io.write_per_sec_ = 2048.0;
        io.busy_percent_ = 5.0;
        value.disk_io_.push_back(io);
        network net;
        net.name_ = "eth0";
        net.address_ = "192.0.2.10";
        net.state_ = "up";
        net.rx_per_sec_ = 1024.0;
        net.tx_per_sec_ = 512.0;
        net.rx_total_ = 100000;
        net.tx_total_ = 50000;
        value.networks_.push_back(net);
        gpu device;
        device.id_ = 0;
        device.name_ = "Test GPU";
        device.vendor_ = "AMD";
        device.backend_ = "DRM sysfs";
        device.uuid_ = "0000:00:01.0";
        device.memory_used_ = 1024ULL * 1024 * 1024;
        device.memory_total_ = 4ULL * 1024 * 1024 * 1024;
        device.utilization_ = 10;
        device.temperature_ = 55;
        device.extra_ = json::object();
        value.gpus_.push_back(device);
        sensor temperature;
        temperature.name_ = "Composite";
        temperature.temperature_ = 40.0F;
        value.sensors_.push_back(temperature);
        listener port;
        port.protocol_ = "TCP";
        port.address_ = "127.0.0.1";
        port.port_ = 8080;
        value.inventory_.listeners_.push_back(port);
        value.inventory_.extra_ = json::object();
        service unit;
        unit.name_ = "demo.service";
        unit.scope_ = "system";
        unit.active_ = "active";
        unit.sub_ = "running";
        unit.description_ = "Demo";
        value.inventory_.services_.push_back(unit);
        annotate(value);
        return value;
    }

    void test_custom_layout() {
        config cfg;
        cfg.columns_ = {"pid", "cpu", "memory", "command"};
        layout_node layout;
        layout.kind_ = layout_node::kind::split;
        layout.direction_ = "horizontal";
        layout.weights_ = {60, 40};
        layout_node cpu;
        cpu.kind_ = layout_node::kind::panel;
        cpu.panel_ = "cpu";
        layout_node memory;
        memory.kind_ = layout_node::kind::panel;
        memory.panel_ = "memory";
        layout.children_.push_back(cpu);
        layout.children_.push_back(memory);
        cfg.custom_layout_ = layout;
        const string text = cfg.to_toml();
        const config parsed = config::from_toml(text);
        CHECK(parsed.columns_.size() == 4);
        CHECK(parsed.custom_layout_.has_value());
        if (parsed.custom_layout_.has_value()) {
            CHECK(parsed.custom_layout_->children_.size() == 2);
            CHECK(parsed.custom_layout_->contains_panel("cpu"));
        }
        const snapshot value = sample_snapshot();
        const string rendered = ui::render_plaintext(value, value.inventory_, parsed, "overview", 120, 40);
        CHECK(rendered.find("CPU") != string::npos);
        const string processes = ui::render_plaintext(value, value.inventory_, parsed, "processes", 120, 40);
        CHECK(processes.find("COMMAND") != string::npos);
    }

    void test_advanced() {
        config cfg;
        cfg.columns_ = {"priority", "threads", "fd_count", "comm"};
        snapshot value = sample_snapshot();
        value.processes_.clear();
        process item;
        item.pid_ = 1;
        item.name_ = "test";
        item.command_ = "test";
        item.start_time_ = 0;
        item.extra_ = json::object();
        value.processes_.push_back(item);
        advanced telemetry;
        telemetry.collect(value, cfg);
        CHECK(value.extra_.get("CPU scheduler / swap IO") != nullptr);
        CHECK(!value.processes_.empty());
        CHECK(value.processes_[0].extra_.get("stat") != nullptr);
        cfg.columns_ = {"cpu_delay", "io_delay", "swap_delay", "priority"};
        value.processes_[0].extra_ = json::object();
        advanced delay_telemetry;
        delay_telemetry.collect(value, cfg);
        CHECK(value.processes_[0].extra_.get("delay_accounting_status") != nullptr ||
              value.processes_[0].extra_.get("cpu_delay") != nullptr);
        const json extras = discover_platform_extras(cfg);
        CHECK(extras.is_object());
#if NEFLUX_PLATFORM_LINUX
        CHECK(extras.get("Memory details") != nullptr);
        CHECK(extras.get("VM counters") != nullptr);
#endif
    }

    void test_ros_session() {
#if NEFLUX_PLATFORM_LINUX
        environment::set("ROS_VERSION", "2");
        process item;
        item.pid_ = static_cast<uint32_t>(::getpid());
        item.name_ = "ros-test";
        item.command_ = "ros-test --ros-args";
        string error;
        const auto session = open_ros_session(item, error);
        CHECK(session.has_value());
        if (session.has_value()) {
            CHECK(session->version_ == 2);
            CHECK(session->pid_ == item.pid_);
            CHECK(ros_summary(*session).find("ROS 2") != string::npos);
        } else {
            CHECK(!error.empty());
        }
        environment::unset("ROS_VERSION");
#endif
    }

    void test_recording() {
        const string path = "/tmp/neflux-test-recording.jsonl";
        ::neforce::filesystem::remove(::neforce::path(path));
        {
            recorder writer(path, 64);
            snapshot first = sample_snapshot();
            snapshot second = first;
            second.timestamp_ = 1700000001;
            second.cpu_percent_ = 50.0F;
            writer.write(first);
            writer.write(second);
            CHECK(writer.size() > 0);
        }
        const auto values = replay(path);
        CHECK(values.size() == 2);
        if (values.size() == 2) {
            CHECK(values[1].timestamp_ == 1700000001);
            CHECK(values[1].cpu_percent_ == 50.0F);
            CHECK(values[1].processes_.size() == values[0].processes_.size());
            CHECK(values[1].inventory_.services_.size() == 1);
            CHECK(values[1].inventory_.listeners_.size() == 1);
            CHECK(values[1].gpus_.size() == 1);
            const snapshot round_trip = snapshot_from_json(to_json(values[1]));
            CHECK(round_trip.inventory_.services_.size() == 1);
            CHECK(round_trip.inventory_.listeners_.size() == 1);
            CHECK(round_trip.inventory_.extra_.is_object());
            CHECK(round_trip.processes_.size() == 1);
            CHECK(round_trip.gpus_.size() == 1);
            CHECK(round_trip.sensors_.size() == 1);
            CHECK(round_trip.disks_.size() == 1);
        }
        const string csv = snapshot_csv(values[0], true);
        CHECK(csv.find("timestamp,path,value") == 0);
        CHECK(csv.find("system.cpu_percent") != string::npos);
        ::neforce::filesystem::remove(::neforce::path(path));
    }

    void test_parsers() {
        const string containers =
                "{\"ID\":\"abcdef123456\",\"Names\":\"web\",\"Image\":\"nginx:latest\",\"State\":\"running\","
                "\"Status\":\"Up 2 hours\",\"Ports\":\"0.0.0.0:80->80/tcp\",\"Command\":\"nginx\","
                "\"CreatedAt\":\"2026-01-01 00:00:00 +0000 UTC\",\"RunningFor\":\"2 hours ago\"}\n";
        auto parsed = parse_containers("docker", containers);
        CHECK(parsed.size() == 1);
        if (!parsed.empty()) {
            CHECK(parsed[0].name_ == "web");
            CHECK(parsed[0].state_ == "running");
            CHECK(parsed[0].runtime_ == "docker");
        }
        const string stats =
                "{\"ID\":\"abcdef123456\",\"Name\":\"web\",\"CPUPerc\":\"1.50%\",\"MemUsage\":\"100MiB / 1GiB\","
                "\"NetIO\":\"1KiB / 2KiB\",\"BlockIO\":\"3KiB / 4KiB\",\"MemPerc\":\"10.00%\",\"PIDs\":\"5\"}\n";
        apply_container_stats(parsed, stats);
        if (!parsed.empty()) {
            CHECK(parsed[0].cpu_.has_value() && parsed[0].cpu_.value() == 1.5F);
            CHECK(parsed[0].memory_.has_value() && *parsed[0].memory_ == 100U * 1024U * 1024U);
            CHECK(parsed[0].pids_.has_value() && *parsed[0].pids_ == 5);
        }
        const auto disk =
                parse_diskstats(string_view("   8       0 sda 100 0 2048 10 200 0 4096 20 0 30 40 0 0 0 0 0 0\n"));
        CHECK(disk.count("sda") == 1);
        if (disk.count("sda") != 0) {
            CHECK(get<0>(disk.at("sda")) == 2048U * 512U);
            CHECK(get<1>(disk.at("sda")) == 4096U * 512U);
        }
        const auto connections = parse_proc_connections(
                "tcp",
                "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
                "   0: 0100007F:0035 00000000:0000 0A 00000010:00000020 00:00000000 00000000   102        0 12345 1\n");
        CHECK(connections.size() == 1);
        if (!connections.empty()) {
            CHECK(connections[0].local_ == "127.0.0.1:53");
            CHECK(connections[0].state_ == "LISTEN");
            CHECK(connections[0].send_queue_.has_value() && *connections[0].send_queue_ == 16);
        }
    }

    void test_sampling_sources() {
        config cfg;
        cfg.collect_gpu_ = false;
        cfg.show_threads_ = false;
        cfg.show_kernel_threads_ = true;
        collector instance(false);
        const snapshot minimal = instance.collect_with_processes(cfg, false, false, false);
        CHECK(minimal.processes_.empty());
        CHECK(minimal.connections_.connections_.empty());
        const snapshot connected = instance.collect_with_processes(cfg, false, false, true);
#if NEFLUX_PLATFORM_LINUX
        CHECK(!connected.connections_.connections_.empty());
#else
        CHECK(connected.connections_.connections_.empty());
#endif
    }

    void test_render() {
        config cfg;
        const snapshot value = sample_snapshot();
        const inventory data = value.inventory_;
        for (const char* view_name:
             {"overview", "processes", "gpu", "storage", "network", "system", "containers", "services", "explorer"}) {
            const string text = ui::render_plaintext(value, data, cfg, view_name, 120, 36);
            CHECK(!text.empty());
        }
        const string storage = ui::render_plaintext(value, data, cfg, "storage", 120, 36);
        CHECK(storage.find("BLOCK DEVICES") != string::npos);
        const string system = ui::render_plaintext(value, data, cfg, "system", 120, 36);
        CHECK(system.find("TEMPERATURES") != string::npos);
        CHECK(system.find("PRESSURE") != string::npos);
        const string network = ui::render_plaintext(value, data, cfg, "network", 120, 36);
        CHECK(network.find("CONNECTIONS") != string::npos);
        const string explorer = ui::render_plaintext(value, data, cfg, "explorer", 120, 36);
        CHECK(explorer.find("SYSTEM EXPLORER") != string::npos);
        const string history = ui::render_plaintext(value, data, cfg, "history", 120, 36);
        CHECK(history.find("SERIES") != string::npos);
        const string events = ui::render_plaintext(value, data, cfg, "events", 120, 36);
        CHECK(events.find("EVENTS") != string::npos);
        const string svg = ui::render_svg(value, data, cfg, "processes", "summary", 100, 30);
        CHECK(svg.find("<svg") != string::npos);
        CHECK(svg.find("</svg>") != string::npos);
    }

} // namespace

int main() {
    test_json();
    test_config();
    test_plot();
    test_filter();
    test_tags();
    test_network_attribution();
    test_discovery();
    test_history();
    test_battery_and_gpu();
    test_custom_layout();
    test_advanced();
    test_recording();
    test_parsers();
    test_ros_session();
    test_sampling_sources();
    test_render();
    if (g_failures != 0) {
        std::printf("%d test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All NeFlux tests passed\n");
    return 0;
}
