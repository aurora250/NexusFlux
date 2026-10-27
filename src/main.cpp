#include <NeFlux/common.hpp>
#include <NeFlux/cli.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/filter.hpp>
#include <NeFlux/collect.hpp>
#include <NeFlux/config.hpp>
#include <NeFlux/connections.hpp>
#include <NeFlux/crash.hpp>
#include <NeFlux/diagnostics.hpp>
#include <NeFlux/gpu.hpp>
#include <NeFlux/recording.hpp>
#include <NeFlux/tags.hpp>
#include <NeFlux/ui/painter.hpp>
#include <NeFlux/ui/app.hpp>

#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>

#include <NeForce/core/exception/exception.hpp>

#if NEFLUX_PLATFORM_WINDOWS
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    ifndef gai_strerror
#        define gai_strerror gai_strerrorA
#    endif
#else
#    include <arpa/inet.h>
#    include <csignal>
#    include <netdb.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif
#include <cerrno>
#include <csignal>

#include <NeForce/core/system/console.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/network/http/http_client.hpp>

namespace {

    using namespace neforce;

    void out(const char* text) { sys_console::instance().print_string(text); }
    void out(string_view text) { sys_console::instance().print_string(text); }
    void out(const string& text) { sys_console::instance().print_string(text); }
    void outln(const char* text) {
        sys_console::instance().print_string(text);
        sys_console::instance().print_string("\n");
    }
    void outln(string_view text) {
        sys_console::instance().print_string(text);
        sys_console::instance().print_string("\n");
    }
    void outln(const string& text) {
        sys_console::instance().print_string(text);
        sys_console::instance().print_string("\n");
    }
    void err(const char* text) { sys_console::instance().print_error(text); }
    void err(string_view text) { sys_console::instance().print_error(text); }
    void err(const string& text) { sys_console::instance().print_error(text); }
    void errln(const char* text) {
        sys_console::instance().print_error(text);
        sys_console::instance().print_error("\n");
    }
    void errln(string_view text) {
        sys_console::instance().print_error(text);
        sys_console::instance().print_error("\n");
    }
    void errln(const string& text) {
        sys_console::instance().print_error(text);
        sys_console::instance().print_error("\n");
    }

    volatile sig_atomic_t g_stop = 0;

    void on_signal(int) { g_stop = 1; }

    void install_signals() {
        ::signal(SIGINT, on_signal);
        ::signal(SIGTERM, on_signal);
    }

    bool stopped() { return g_stop != 0; }

    void wait_until(int64_t deadline) {
        while (!stopped() && neforce::neflux::monotonic_ms() < deadline) {
            const int64_t remaining = deadline - neforce::neflux::monotonic_ms();
            neforce::neflux::sleep_ms(static_cast<uint64_t>(remaining < 50 ? (remaining < 1 ? 1 : remaining) : 50));
        }
    }

    using namespace neforce;
    using namespace neforce::neflux;

    int run_headless(const cli_args& args, config cfg) {
        const uint32_t sample_count = args.samples_.has_value() ? *args.samples_ : (args.snapshot_ ? 1 : 1);
        unique_ptr<recorder> writer;
        if (args.record_.has_value()) {
            writer = make_unique<recorder>(*args.record_, cfg.record_max_mib_);
        }
        string csv_text;
        bool csv_header = true;

        const string wanted = args.view_.has_value() ? *args.view_ : "overview";
        const auto export_svg_file = [&](const snapshot& value, const inventory& data) {
            if (!args.export_svg_.has_value()) {
                return true;
            }
            string svg;
            try {
                svg = ui::render_svg(value, data, cfg, wanted, args.gpu_panel_, args.width_, args.height_);
            } catch (const exception& error) {
                errln(format("SVG render failed: {}", error.what()));
                return false;
            } catch (...) {
                errln("SVG render failed: unknown exception");
                return false;
            }
            if (!write_text_file(*args.export_svg_, svg)) {
                errln(format("Cannot write SVG: {}", *args.export_svg_));
                return false;
            }
            errln(format("Saved {}", *args.export_svg_));
            return true;
        };
        if (args.replay_.has_value()) {
            auto snapshots = replay(*args.replay_);
            size_t limit = args.samples_.has_value() ? *args.samples_ : (args.snapshot_ ? 1 : snapshots.size());
            if (limit > snapshots.size()) {
                limit = snapshots.size();
            }
            snapshot last;
            for (size_t index = 0; index < limit; ++index) {
                const auto& item = snapshots[index];
                last = item;
                if (writer != nullptr) {
                    writer->write(item);
                }
                if (args.csv_.has_value()) {
                    csv_text.append(snapshot_csv(item, csv_header));
                    csv_header = false;
                } else if (!args.export_svg_.has_value()) {
                    const string text = to_json(item).dump();
                    outln(text);
                }
            }
            if (args.csv_.has_value() && !write_text_file(*args.csv_, csv_text)) {
                errln(format("Cannot write CSV: {}", *args.csv_));
                return 1;
            }
            if (args.export_svg_.has_value() && last.timestamp_ != 0) {
                return export_svg_file(last, last.inventory_) ? 0 : 1;
            }
            return 0;
        }

        collector collector_instance(cfg.collect_gpu_);
        inventory discovered;
        inventory_rates rates;
        try {
            discovered = discover_with_config(cfg);
            rates.apply(discovered);
        } catch (const exception& error) {
            errln(format("Discovery: {}", error.what()));
        }
        const uint32_t total = args.samples_.has_value() ? *args.samples_ : 1;
        snapshot last;
        int64_t deadline = monotonic_ms() + static_cast<int64_t>(cfg.interval_ms_);
        for (uint32_t index = 0; index < total && !stopped(); ++index) {
            wait_until(deadline);
            if (stopped()) {
                break;
            }
            auto sample = collector_instance.collect(cfg);
            sample.apply_inventory(discovered);
            annotate(sample);
            last = sample;
            if (writer != nullptr) {
                writer->write(sample);
            }
            if (args.csv_.has_value()) {
                csv_text.append(snapshot_csv(sample, csv_header));
                csv_header = false;
            } else if (!args.export_svg_.has_value()) {
                const string text =
                        (args.samples_.has_value() || total > 1) ? to_json(sample).dump() : to_json(sample).dump(true);
                outln(text);
            }
            deadline = monotonic_ms() + static_cast<int64_t>(cfg.interval_ms_);
        }
        if (args.csv_.has_value() && !write_text_file(*args.csv_, csv_text)) {
            errln(format("Cannot write CSV: {}", *args.csv_));
            return 1;
        }
        if (args.export_svg_.has_value()) {
            return export_svg_file(last, discovered) ? 0 : 1;
        }
        return 0;
    }

    string check_target(const string& target) {
        if (starts_with(target.view(), "http://") || starts_with(target.view(), "https://")) {
            const int64_t began = monotonic_ms();
            try {
                neforce::io_context context;
                neforce::http::http_client client(context);
                client.set_timeout(neforce::milliseconds(5000));
                client.set_follow_redirects(true);
                const auto response = client.get(target);
                const double elapsed = static_cast<double>(monotonic_ms() - began) / 1000.0;
                return format("HTTP {} · {} in {:.2f}s", static_cast<int>(response.status),
                              response.is_success() ? "success" : "failure", elapsed);
            } catch (const exception& error) {
                return format("HTTP check failed: {}", error.what());
            }
        }
        string host;
        string port = "443";
        bool https = false;
        if (starts_with(target.view(), "http://")) {
            host = target.substr(7);
        } else if (starts_with(target.view(), "https://")) {
            host = target.substr(8);
            https = true;
        } else {
            host = target;
        }
        const auto slash = host.find('/');
        if (slash != string::npos) {
            host = host.substr(0, slash);
        }
        const auto colon = host.rfind(':');
        if (colon != string::npos) {
            port = host.substr(colon + 1);
            host = host.substr(0, colon);
        } else if (!https) {
            port = "80";
        }
        if (host.empty()) {
            return "Target must be a URL or host:port";
        }
#if NEFLUX_PLATFORM_WINDOWS
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* results = nullptr;
        const int resolved = getaddrinfo(host.view().data(), port.view().data(), &hints, &results);
        if (resolved != 0 || results == nullptr) {
            return format("DNS failed for {}: {}", host, gai_strerror(resolved));
        }
        int descriptor = -1;
        int code = -1;
        const int64_t began = monotonic_ms();
        for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
#if NEFLUX_PLATFORM_WINDOWS
            descriptor = static_cast<int>(socket(item->ai_family, item->ai_socktype, item->ai_protocol));
#else
            descriptor = socket(item->ai_family, item->ai_socktype | SOCK_CLOEXEC, item->ai_protocol);
#endif
            if (descriptor < 0) {
                continue;
            }
            timeval timeout{};
            timeout.tv_sec = 4;
            setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#if NEFLUX_PLATFORM_WINDOWS
            code = connect(static_cast<SOCKET>(descriptor), item->ai_addr, static_cast<int>(item->ai_addrlen));
#else
            code = connect(descriptor, item->ai_addr, item->ai_addrlen);
#endif
            if (code == 0) {
                break;
            }
#if NEFLUX_PLATFORM_WINDOWS
            closesocket(static_cast<SOCKET>(descriptor));
#else
            close(descriptor);
#endif
            descriptor = -1;
        }
        freeaddrinfo(results);
        const double elapsed = static_cast<double>(monotonic_ms() - began) / 1000.0;
        if (descriptor >= 0) {
#if NEFLUX_PLATFORM_WINDOWS
            closesocket(static_cast<SOCKET>(descriptor));
            WSACleanup();
#else
            close(descriptor);
#endif
        }
        if (code != 0) {
            return format("TCP {}:{} failed: {} ({:.2f}s)", host, port, format("errno {}", errno), elapsed);
        }
        return format("TCP {}:{} connected in {:.2f}s{}", host, port, elapsed,
                      https ? " (TLS not verified by this TCP check)" : "");
    }

    string read_capture(const string& file_path) {
        const auto text = read_text_file(file_path);
        if (!text.has_value() || text->size() < 24) {
            return format("Cannot read PCAP: {}", file_path);
        }
        const auto byte_at = [&text](size_t offset) -> uint8_t { return static_cast<uint8_t>((*text)[offset]); };
        const uint32_t magic = static_cast<uint32_t>(byte_at(0)) | (static_cast<uint32_t>(byte_at(1)) << 8) |
                               (static_cast<uint32_t>(byte_at(2)) << 16) | (static_cast<uint32_t>(byte_at(3)) << 24);
        const bool little = magic == 0xA1B2C3D4;
        const bool nano = magic == 0xA1B23C4D;
        if (!little && !nano) {
            return "Not a little-endian PCAP file";
        }
        const auto read32 = [&text](size_t offset) {
            return static_cast<uint32_t>(static_cast<uint8_t>((*text)[offset])) |
                   (static_cast<uint32_t>(static_cast<uint8_t>((*text)[offset + 1])) << 8) |
                   (static_cast<uint32_t>(static_cast<uint8_t>((*text)[offset + 2])) << 16) |
                   (static_cast<uint32_t>(static_cast<uint8_t>((*text)[offset + 3])) << 24);
        };
        string out = format("PCAP {} · linktype {} · {} bytes\n", file_path, read32(20), text->size());
        size_t offset = 24;
        size_t index = 0;
        while (offset + 16 <= text->size() && index < 60) {
            const uint32_t seconds = read32(offset);
            const uint32_t captured = read32(offset + 8);
            const uint32_t original = read32(offset + 12);
            offset += 16;
            if (captured > text->size() - offset) {
                out.append("truncated packet record\n");
                break;
            }
            out.append(format("#{} t={} len={}/{} cap={}\n", index + 1, seconds, original, captured, captured));
            offset += captured;
            ++index;
        }
        return out;
    }

    vector<string> capture_packets(const cli_args& args) {
#if NEFLUX_PLATFORM_WINDOWS
        static_cast<void>(args);
        throw value_exception("Packet capture requires tcpdump on Linux or macOS");
#else
        vector<string> command = {"-i", args.interface_, "-w", args.file_, "-c", format("{}", args.packets_), "-s", "0",
                                  "-q"};
        if (!args.capture_filter_.empty()) {
            command.push_back(args.capture_filter_);
        }
        const auto output = run_command("tcpdump", command, static_cast<int>(args.seconds_ * 1000 + 5000), true);
        return {output};
#endif
    }

    void filter_network_sample(connection_sample& sample, const cli_args& args) {
        if (args.filter_.empty()) {
            return;
        }
        vector<connection> rows;
        for (auto& item: sample.connections_) {
            if (connection_matches(item, args.filter_)) {
                rows.push_back(move(item));
            }
        }
        sample.connections_ = move(rows);
    }

    vector<string> network_connection_rows(const cli_args& args) {
        connection_collector collector;
        static_cast<void>(collector.collect());
        sleep_ms(1000);
        auto sample = collector.collect();
        filter_network_sample(sample, args);
        auto rows = move(sample.connections_);
        const auto metric = [](const connection& item) {
            return (item.rx_per_sec_.has_value() ? *item.rx_per_sec_ : 0.0) +
                   (item.tx_per_sec_.has_value() ? *item.tx_per_sec_ : 0.0);
        };
        const auto before = [&](const connection& left, const connection& right) {
            bool result = false;
            if (args.sort_ == "process") {
                result = !left.process_names_.empty() &&
                         (right.process_names_.empty() || left.process_names_[0] < right.process_names_[0]);
            } else if (args.sort_ == "pid") {
                result = !left.pids_.empty() && (right.pids_.empty() || left.pids_[0] < right.pids_[0]);
            } else if (args.sort_ == "proto") {
                result = left.protocol_ < right.protocol_;
            } else if (args.sort_ == "local") {
                result = left.local_ < right.local_;
            } else if (args.sort_ == "remote") {
                result = left.remote_ < right.remote_;
            } else if (args.sort_ == "state") {
                result = left.state_ < right.state_;
            } else if (args.sort_ == "download") {
                result = (left.rx_per_sec_.has_value() ? *left.rx_per_sec_ : 0.0) >
                         (right.rx_per_sec_.has_value() ? *right.rx_per_sec_ : 0.0);
            } else if (args.sort_ == "upload") {
                result = (left.tx_per_sec_.has_value() ? *left.tx_per_sec_ : 0.0) >
                         (right.tx_per_sec_.has_value() ? *right.tx_per_sec_ : 0.0);
            } else {
                result = metric(left) > metric(right);
            }
            return result;
        };
        stable_sort_by_index(rows, [&](const connection& left, const connection& right) {
            return args.reverse_ ? before(right, left) : before(left, right);
        });
        vector<string> lines;
        for (const auto& item: rows) {
            const string process_name = item.process_names_.empty() ? string("—") : item.process_names_[0];
            const string pid = item.pids_.empty() ? string("—") : format("{}", item.pids_[0]);
            lines.push_back(
                    format("{:<24} {:<12} {}  {} -> {}  {}  {} / {}", truncate_display(process_name.view(), 23), pid,
                           item.protocol_, item.local_, item.remote_, item.state_,
                           item.rx_per_sec_.has_value() ? format("{}/s", bytes(*item.rx_per_sec_)) : string("—"),
                           item.tx_per_sec_.has_value() ? format("{}/s", bytes(*item.tx_per_sec_)) : string("—")));
        }
        string header = format("{} · sampled at {} · owners at {}", sample.status_, sample.sampled_at_,
                               sample.owners_sampled_at_);
        lines.insert(lines.begin(), header);
        return lines;
    }

    string doctor_trends(const vector<snapshot>& samples) {
        if (samples.size() < 2) {
            return {};
        }
        const size_t begin = samples.size() > 60 ? samples.size() - 60 : 0;
        auto line = [&](const string& label, const vector<double>& values, const string& unit) {
            if (values.empty()) {
                return string();
            }
            double minimum = values[0];
            double peak = values[0];
            double total = 0.0;
            for (const double value: values) {
                minimum = neforce::min(minimum, value);
                peak = neforce::max(peak, value);
                total += value;
            }
            return format("{}: {:.1f} / {:.1f} / {:.1f} / {:.1f}{} ({} samples)\n", label, values.back(), minimum, peak,
                          total / static_cast<double>(values.size()), unit, static_cast<uint64_t>(values.size()));
        };
        vector<double> cpu;
        vector<double> memory;
        vector<double> gpu;
        vector<double> disk;
        vector<double> network;
        for (size_t index = begin; index < samples.size(); ++index) {
            const auto& item = samples[index];
            cpu.push_back(item.cpu_percent_);
            memory.push_back(percent(item.memory_.used_, item.memory_.total_));
            if (!item.gpus_.empty() && item.gpus_[0].utilization_.has_value()) {
                gpu.push_back(*item.gpus_[0].utilization_);
            }
            double read = 0.0;
            double write = 0.0;
            for (const auto& io: item.disk_io_) {
                if (io.aggregate_) {
                    read += io.read_per_sec_;
                    write += io.write_per_sec_;
                }
            }
            disk.push_back(read + write);
            double rx = 0.0;
            double tx = 0.0;
            for (const auto& net: item.networks_) {
                rx += net.rx_per_sec_;
                tx += net.tx_per_sec_;
            }
            network.push_back(rx + tx);
        }
        string out = "\nRECENT TRENDS · current / minimum / peak / average\n";
        out.append(line("CPU total (%)", cpu, ""));
        out.append(line("RAM used (%)", memory, ""));
        out.append(line("GPU 0 util (%)", gpu, ""));
        out.append(line("Disk read+write", disk, " B/s"));
        out.append(line("Network rx+tx", network, " B/s"));
        return out;
    }

    int run_doctor(const cli_args& args, config cfg) {
        snapshot value;
        inventory data;
        vector<snapshot> samples;
        bool recorded = false;
        if (args.replay_.has_value()) {
            auto recorded_samples = replay(*args.replay_);
            samples = recorded_samples;
            value = recorded_samples.back();
            data = value.inventory_;
            recorded = true;
        } else {
            cfg.alert_commands_.clear();
            collector instance(cfg.collect_gpu_);
            data = discover_with_config(cfg);
            value = instance.collect(cfg);
            samples.push_back(value);
            sleep_ms(cfg.interval_ms_);
            value = instance.collect(cfg);
            samples.push_back(value);
            value.apply_inventory(data);
        }
        value.alerts_.clear();
        value.alerts_ = resource_alerts(value, cfg);
        const string view = args.view_.has_value() ? *args.view_ : "overview";
        auto items = findings(value, cfg);
        vector<finding> filtered;
        for (const auto& item: items) {
            if (relevant(view, item.domain_)) {
                filtered.push_back(item);
            }
        }
        const string investigation =
                "\nHOW TO INVESTIGATE\n"
                "CPU or RAM pressure: Processes identifies owners and logs.\n"
                "GPU failures: GPU identifies memory owners, driver status and hardware signals.\n"
                "Disk full/slow: Storage checks capacity, latency, inodes and large files.\n"
                "Can't connect/port conflict: Network identifies connections and tests a target.\n"
                "Container/service failures: open logs and exit/health details before restarting.\n"
                "[F6] Tools lists available actions and additional details for this resource.\n";
        const string body = report(value, cfg) + investigation + doctor_trends(samples);
        if (args.json_) {
            json out = json::object();
            out.set("sampled_at", json(value.timestamp_));
            out.set("recorded", json(recorded));
            json list = json::array();
            for (const auto& item: filtered) {
                list.push(to_json(item));
            }
            out.set("findings", move(list));
            out.set("report", json(body));
            outln(out.dump(true));
        } else {
            out(body);
        }
        return 0;
    }

    string completions(const string& shell) {
        if (shell == "bash") {
            return R"BASH(_nexus_flux_complete() {
    local cur="${COMP_WORDS[COMP_CWORD]}" prev="${COMP_WORDS[COMP_CWORD-1]}"
    local commands="monitor snapshot sample record replay export gpu network doctor config completions man update"
    local options="--interval --theme --view --gpu-panel --config --no-gpu --samples --width --height --ascii --no-color --threads --process-network --read-only --full-sampling --layout --min-free-mib --min-total-mib --max-gpu-utilization --max-memory-utilization --no-config-exec --help --version"
    case "$prev" in
        --theme) COMPREPLY=($(compgen -W "midnight nord ember black white graphite ocean forest sand" -- "$cur")); return 0 ;;
        --view) COMPREPLY=($(compgen -W "overview processes gpu storage network system containers services explorer history events" -- "$cur")); return 0 ;;
        --gpu-panel) COMPREPLY=($(compgen -W "summary history device" -- "$cur")); return 0 ;;
        --layout) COMPREPLY=($(compgen -W "balanced compute storage minimal" -- "$cur")); return 0 ;;
        completions) COMPREPLY=($(compgen -W "bash zsh fish elvish powershell" -- "$cur")); return 0 ;;
        --config|--input|--record|--export-svg|--csv) COMPREPLY=($(compgen -f -- "$cur")); return 0 ;;
        network) COMPREPLY=($(compgen -W "connections check capture read" -- "$cur")); return 0 ;;
        export) COMPREPLY=($(compgen -W "csv svg" -- "$cur")); return 0 ;;
        gpu) COMPREPLY=($(compgen -W "select" -- "$cur")); return 0 ;;
        config) COMPREPLY=($(compgen -W "path write" -- "$cur")); return 0 ;;
    esac
    if [[ "$cur" == -* ]]; then
        COMPREPLY=($(compgen -W "$options" -- "$cur"))
    else
        COMPREPLY=($(compgen -W "$commands" -- "$cur"))
    fi
}
complete -F _nexus_flux_complete nexus-flux neflux
)BASH";
        }
        if (shell == "zsh") {
            return R"ZSH(#compdef nexus-flux neflux
_nexus_flux() {
    _arguments \
        '(-i --interval)'{-i,--interval}'[sampling interval in milliseconds]:milliseconds:' \
        '--theme[color theme]:theme:(midnight nord ember black white graphite ocean forest sand)' \
        '--view[initial page]:view:(overview processes gpu storage network system containers services explorer history events)' \
        '--gpu-panel[GPU display]:panel:(summary history device)' \
        '--config[alternate configuration file]:file:_files' \
        '--no-gpu[disable GPU driver queries]' \
        '--samples[sample count]:count:' \
        '--width[SVG width]:columns:' \
        '--height[SVG height]:rows:' \
        '--ascii[use ASCII glyphs]' \
        '--no-color[monochrome palette]' \
        '--threads[show threads]' \
        '--process-network[sample per-process TCP]' \
        '--read-only[disable state changes]' \
        '--full-sampling[activate all sources]' \
        '--layout[overview layout]:layout:(balanced compute storage minimal)' \
        '--min-free-mib[minimum free VRAM]:MiB:' \
        '--min-total-mib[minimum total VRAM]:MiB:' \
        '--max-gpu-utilization[maximum GPU utilization]:percent:' \
        '--max-memory-utilization[maximum memory utilization]:percent:' \
        '--no-config-exec[disable configured commands]' \
        '(-h --help)'{-h,--help}'[print help]' \
        '(-V --version)'{-V,--version}'[print version]' \
        '1:command:(monitor snapshot sample record replay export gpu network doctor config completions man update)' \
        '*::argument:->args'
    case $state in
        args)
            case $words[2] in
                completions) _values 'shell' bash zsh fish elvish powershell ;;
                export) _values 'format' csv svg ;;
                gpu) _values 'action' select ;;
                network) _values 'action' connections check capture read ;;
                config) _values 'action' path write ;;
            esac
            ;;
    esac
}
_nexus_flux "$@"
)ZSH";
        }
        if (shell == "fish") {
            return R"FISH(complete -c nexus-flux -f -a "monitor snapshot sample record replay export gpu network doctor config completions man update"
complete -c nexus-flux -s i -l interval -d "Sampling interval in milliseconds" -x -a "250 500 1000 2000 5000"
complete -c nexus-flux -l theme -d "Color theme" -x -a "midnight nord ember black white graphite ocean forest sand"
complete -c nexus-flux -l view -d "Initial page" -x -a "overview processes gpu storage network system containers services explorer history events"
complete -c nexus-flux -l gpu-panel -d "GPU display" -x -a "summary history device"
complete -c nexus-flux -l config -d "Alternate configuration file" -r
complete -c nexus-flux -l no-gpu -d "Disable GPU driver queries"
complete -c nexus-flux -l samples -d "Sample count" -x
complete -c nexus-flux -l width -d "SVG width" -x
complete -c nexus-flux -l height -d "SVG height" -x
complete -c nexus-flux -l ascii -d "Use ASCII glyphs"
complete -c nexus-flux -l no-color -d "Monochrome palette"
complete -c nexus-flux -l threads -d "Show threads"
complete -c nexus-flux -l process-network -d "Sample per-process TCP"
complete -c nexus-flux -l read-only -d "Disable state changes"
complete -c nexus-flux -l full-sampling -d "Activate all sources"
complete -c nexus-flux -l layout -d "Overview layout" -x -a "balanced compute storage minimal"
complete -c nexus-flux -l no-config-exec -d "Disable configured commands"
complete -c nexus-flux -s h -l help -d "Print help"
complete -c nexus-flux -s V -l version -d "Print version"
complete -c nexus-flux -n "__fish_seen_subcommand_from completions" -a "bash zsh fish elvish powershell"
complete -c nexus-flux -n "__fish_seen_subcommand_from export" -a "csv svg"
complete -c nexus-flux -n "__fish_seen_subcommand_from network" -a "connections check capture read"
complete -c nexus-flux -n "__fish_seen_subcommand_from gpu" -a "select"
complete -c nexus-flux -n "__fish_seen_subcommand_from config" -a "path write"
)FISH";
        }
        if (shell == "elvish") {
            return "set edit:completion:arg-completer[nexus-flux] = { |@words| put monitor snapshot sample record "
                   "replay export gpu network doctor config completions man update }\n";
        }
        if (shell == "powershell") {
            return R"PS(Register-ArgumentCompleter -Native -CommandName nexus-flux,neflux -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)
    $commands = 'monitor','snapshot','sample','record','replay','export','gpu','network','doctor','config','completions','man','update'
    $options = '--interval','--theme','--view','--gpu-panel','--config','--no-gpu','--samples','--width','--height','--ascii','--no-color','--threads','--process-network','--read-only','--full-sampling','--layout','--min-free-mib','--min-total-mib','--max-gpu-utilization','--max-memory-utilization','--no-config-exec','--help','--version'
    if ($wordToComplete -like '-*') { $options } else { $commands } |
        Where-Object { $_ -like "$wordToComplete*" } |
        ForEach-Object { [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_) }
})PS";
        }
        return help_text();
    }

    string man_text() {
        return R"(.ie \n(.g .ds Aq \(aq
.el .ds Aq '
.TH NEXUS-FLUX 1 "" "nexus-flux 0.1.0"
.SH NAME
nexus-flux \- one terminal for CPU, GPU, processes, storage, network, containers and services
.SH SYNOPSIS
.B nexus-flux
[\fIOPTIONS\fR] [\fICOMMAND\fR]
.br
.B neflux
[\fIOPTIONS\fR] [\fICOMMAND\fR]
.SH DESCRIPTION
.B nexus-flux
is a NexusForce-based system monitor.
It samples local telemetry, renders an eight-page TUI, records and replays snapshots, and
exports JSON, NDJSON, CSV and SVG.
.SH OPTIONS
.TP
.B \-i, \-\-interval \fIMILLISECONDS\fR
Sampling interval, 250 to 10000 ms.
.TP
.B \-\-theme \fINAME\fR
midnight, nord, ember, black, white, graphite, ocean, forest or sand.
.TP
.B \-\-view \fINAME\fR
overview, processes, gpu, storage, network, system, containers, services, history or events.
.TP
.B \-\-gpu-panel \fIPANEL\fR
summary, history or device.
.TP
.B \-\-config \fIPATH\fR
Alternate configuration file.
.TP
.B \-\-no-gpu
Disable GPU driver and DRM queries.
.TP
.B \-\-samples \fICOUNT\fR
Emit COUNT NDJSON samples (default 10).
.TP
.B \-\-width \fICOLUMNS\fR, \-\-height \fIROWS\fR
Headless SVG dimensions (defaults 160x48).
.TP
.B \-\-ascii
Use pure ASCII glyphs.
.TP
.B \-\-no-color
Monochrome palette.
.TP
.B \-\-threads
Include threads in the process list.
.TP
.B \-\-process-network
Sample per-process TCP bytes through bpftrace when available.
.TP
.B \-\-read-only
Disable process actions, probes, alert commands and packet capture.
.TP
.B \-\-full-sampling
Preload all pages and keep every background source active.
.TP
.B \-\-layout \fINAME\fR
balanced, compute, storage or minimal.
.TP
.B \-\-min-free-mib, \-\-min-total-mib, \-\-max-gpu-utilization, \-\-max-memory-utilization
GPU selection filters.
.TP
.B \-\-no-config-exec
Disable configured probes and alert commands.
.TP
.B \-h, \-\-help
Print help.
.TP
.B \-V, \-\-version
Print version.
.SH COMMANDS
.TP
.B monitor
Open the interactive monitor (default).
.TP
.B snapshot
Print one JSON snapshot, including process command lines.
.TP
.B sample
Print newline-delimited JSON samples.
.TP
.B record \fIFILE\fR
Record telemetry while monitoring, or headless with --samples.
.TP
.B replay \fIFILE\fR
Replay a recording in the TUI, or print JSON with --samples.
.TP
.B export \fIcsv|svg\fR \fIOUTPUT\fR
Export live telemetry or --input FILE.
.TP
.B gpu [select]
Open the GPU view or select visible CUDA devices.
.TP
.B network [connections|check|capture|read]
Open connection monitoring, check a target or capture packets.
.TP
.B doctor
Explain resource problems, recent trends and next steps.
.TP
.B config [path|write]
Print the configuration path or write current preferences.
.TP
.B completions \fISHELL\fR
Generate bash, zsh, fish, elvish or PowerShell completions.
.TP
.B man
Print this manual page.
.TP
.B update [--check]
Report update status. This build is not installer-managed.
.SH KEYS
1-8 switch pages; 0 history; E events; F1 help; F2 settings; F3 filter; F4 sort;
F5 view; F6 details; F7 logs; C columns; R ROS graph; q quits.
.SH FILES
\fI$XDG_CONFIG_HOME/neflux/config.toml\fR
.br
\fI$HOME/.config/neflux/config.toml\fR
.SH ENVIRONMENT
.B NO_COLOR
disables color when supported by the terminal.
.SH SEE ALSO
.BR top (1),
.BR htop (1),
.BR nvtop (1)
)";
    }

    int run_gpu_select(const cli_args& args, const config& cfg) {
        const auto result = collect_gpus(cfg.collect_gpu_, false);
        gpu_selection selection;
        selection.min_free_bytes_ = args.min_free_mib_ * 1024ULL * 1024ULL;
        selection.min_total_bytes_ = args.min_total_mib_ * 1024ULL * 1024ULL;
        selection.max_gpu_utilization_ = args.max_gpu_utilization_;
        selection.max_memory_utilization_ = args.max_memory_utilization_;
        const auto devices = select_gpus(result.devices_, selection);
        const uint32_t count = args.select_gpu_.has_value() ? *args.select_gpu_ : 1;
        if (devices.size() < count) {
            errln("Not enough readable CUDA devices with requested free memory");
            return 1;
        }
        string visible = "CUDA_VISIBLE_DEVICES=";
        for (uint32_t index = 0; index < count; ++index) {
            if (index != 0) {
                visible.push_back(',');
            }
            visible.append(devices[index].uuid_);
        }
        outln(visible);
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    using namespace neforce;
    using namespace neforce::neflux;
    initialize_crash_logging(argc, argv);
    cli_args args;
    try {
        args = parse_cli(argc, argv);
    } catch (const exception& error) {
        errln(format("{}\n\n{}", error.what(), help_text()));
        return 2;
    }
    if (help_requested()) {
        out(help_text());
        return 0;
    }
    if (version_requested()) {
        outln(version_text());
        return 0;
    }
    install_signals();
    const string config_path = args.config_path_.has_value() ? *args.config_path_ : config::path();
    if (args.command_ == cli_command::config_path) {
        outln(config_path);
        return 0;
    }
    config cfg;
    try {
        cfg = config::load(config_path);
    } catch (const exception& error) {
        errln(format("Invalid config: {}", error.what()));
        return 1;
    }
    if (args.interval_.has_value()) {
        cfg.interval_ms_ = *args.interval_;
    }
    if (args.theme_.has_value()) {
        cfg.theme_ = *args.theme_;
    }
    cfg.collect_gpu_ = cfg.collect_gpu_ && !args.no_gpu_;
    cfg.ascii_ = cfg.ascii_ || args.ascii_;
    cfg.no_color_ = cfg.no_color_ || args.no_color_;
    cfg.show_threads_ = cfg.show_threads_ || args.threads_;
    cfg.process_network_ = cfg.process_network_ || args.process_network_;
    if (args.read_only_ || args.no_config_exec_) {
        cfg.config_exec_ = false;
    }
    if (args.layout_.has_value()) {
        cfg.layout_ = *args.layout_;
        cfg.custom_layout_.reset();
    }
    try {
        cfg.validate();
    } catch (const exception& error) {
        errln(error.what());
        return 1;
    }
    if (args.command_ == cli_command::config_write || args.write_config_) {
        try {
            cfg.save(config_path);
        } catch (const exception& error) {
            errln(error.what());
            return 1;
        }
        outln(format("Saved {}", config_path));
        return 0;
    }
    if (args.command_ == cli_command::gpu_select) {
        return run_gpu_select(args, cfg);
    }
    if (args.command_ == cli_command::completions) {
        const string text = completions(args.shell_);
        out(text);
        return 0;
    }
    if (args.command_ == cli_command::man) {
        out(man_text());
        return 0;
    }
    if (args.command_ == cli_command::update) {
        outln("nexus-flux is not installer-managed; rebuild or reinstall to update.");
        return args.update_check_ ? 0 : 1;
    }
    if (args.command_ == cli_command::network_check) {
        outln(check_target(args.target_));
        return 0;
    }
    if (args.command_ == cli_command::network_read) {
        out(read_capture(args.file_));
        return 0;
    }
    if (args.command_ == cli_command::network_capture) {
        if (args.read_only_) {
            errln("Packet capture disabled in read-only mode");
            return 1;
        }
        try {
            const auto output = capture_packets(args);
            for (const auto& line: output) {
                outln(line);
            }
        } catch (const exception& error) {
            errln(error.what());
            return 1;
        }
        return 0;
    }
    if (args.command_ == cli_command::network_connections) {
        if (args.json_) {
            connection_collector collector;
            static_cast<void>(collector.collect());
            sleep_ms(1000);
            auto sample = collector.collect();
            filter_network_sample(sample, args);
            outln(to_json(sample).dump(true));
            return 0;
        }
        for (const auto& line: network_connection_rows(args)) {
            outln(line);
        }
        return 0;
    }
    if (args.command_ == cli_command::doctor) {
        try {
            return run_doctor(args, cfg);
        } catch (const exception& error) {
            errln(format("Doctor failed: {}", error.what()));
            return 1;
        }
    }
    if (args.snapshot_ || args.samples_.has_value() || args.replay_.has_value() || args.csv_.has_value() ||
        args.export_svg_.has_value()) {
        return run_headless(args, move(cfg));
    }
    return ui::run_interactive(args, move(cfg), config_path);
}
