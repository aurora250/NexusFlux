#include <NeFlux/common.hpp>
#include <NeFlux/cli.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/exception/exception.hpp>
#include <NeForce/core/system/cmdline.hpp>
namespace neforce::neflux {

    namespace {

        bool g_help = false;
        bool g_version = false;

        NEFORCE_NORETURN void invalid(const string& message) {
            char buffer[512] = {};
            const size_t count = message.size() < sizeof(buffer) - 1 ? message.size() : sizeof(buffer) - 1;
            memory_copy(buffer, message.data(), count);
            throw value_exception(buffer);
        }

        uint64_t to_u64(const string& text, const string& option) {
            const auto value = parse_u64(text.view());
            if (!value.has_value()) {
                invalid(format("Invalid value for {}: {}", option, text));
            }
            return *value;
        }

        void parse_connection_sort(const string& value, cli_args& result) {
            if (value == "traffic" || value == "process" || value == "pid" || value == "proto" || value == "local" ||
                value == "remote" || value == "state" || value == "download" || value == "upload") {
                result.sort_ = value;
                return;
            }
            invalid(format("Invalid connection sort: {}", value));
        }

    } // namespace


    bool help_requested() noexcept { return g_help; }

    bool version_requested() noexcept { return g_version; }

    cli_args parse_cli(int argc, char** argv) {
        g_help = false;
        g_version = false;
        cli_args result;

        cmdline parser;
        const auto add = [&parser](const char* name, const char short_name, const bool value = false) {
            parser.add_option(name, short_name, "", value);
        };
        add("interval", 'i', true);
        add("theme", 0, true);
        add("view", 0, true);
        add("gpu-panel", 0, true);
        add("config", 0, true);
        add("no-gpu", 0);
        add("snapshot", 0);
        add("samples", 0, true);
        add("export-svg", 0, true);
        add("width", 0, true);
        add("height", 0, true);
        add("write-config", 0);
        add("record", 0, true);
        add("csv", 0, true);
        add("replay", 0, true);
        add("ascii", 0);
        add("no-color", 0);
        add("threads", 0);
        add("process-network", 0);
        add("read-only", 0);
        add("full-sampling", 0);
        add("layout", 0, true);
        add("select-gpu", 0, true);
        add("min-free-mib", 0, true);
        add("min-total-mib", 0, true);
        add("max-gpu-utilization", 0, true);
        add("max-memory-utilization", 0, true);
        add("no-config-exec", 0);
        add("json", 0);
        add("reverse", 0);
        add("sort", 0, true);
        add("filter", 0, true);
        add("interface", 0, true);
        add("packets", 0, true);
        add("seconds", 0, true);
        add("count", 0, true);
        add("check", 0);
        add("input", 0, true);
        add("help", 'h');
        add("version", 'V');
        vector<string> arguments;
        arguments.reserve(static_cast<size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            arguments.push_back(argv[index]);
        }
        parser.parse(arguments);

        g_help = parser.has("help");
        g_version = parser.has("version");

        if (parser.has("interval")) {
            result.interval_ = to_u64(parser.get("interval"), "--interval");
            if (*result.interval_ < 250 || *result.interval_ > 10000) {
                invalid("interval must be 250..=10000");
            }
        }
        if (parser.has("theme")) {
            result.theme_ = parser.get("theme");
        }
        if (parser.has("view")) {
            result.view_ = parser.get("view");
        }
        if (parser.has("gpu-panel")) {
            result.gpu_panel_ = parser.get("gpu-panel");
        }
        if (parser.has("config")) {
            result.config_path_ = parser.get("config");
        }
        result.no_gpu_ = parser.has("no-gpu");
        result.snapshot_ = parser.has("snapshot");
        if (parser.has("samples")) {
            const uint64_t value = to_u64(parser.get("samples"), "--samples");
            if (value < 1 || value > 86400) {
                invalid("samples must be 1..=86400");
            }
            result.samples_ = static_cast<uint32_t>(value);
        }
        if (parser.has("export-svg")) {
            result.export_svg_ = parser.get("export-svg");
        }
        if (parser.has("width")) {
            const uint64_t value = to_u64(parser.get("width"), "--width");
            if (value < 60 || value > 300) {
                invalid("width must be 60..=300");
            }
            result.width_ = static_cast<uint16_t>(value);
        }
        if (parser.has("height")) {
            const uint64_t value = to_u64(parser.get("height"), "--height");
            if (value < 18 || value > 120) {
                invalid("height must be 18..=120");
            }
            result.height_ = static_cast<uint16_t>(value);
        }
        result.write_config_ = parser.has("write-config");
        if (parser.has("record")) {
            result.record_ = parser.get("record");
        }
        if (parser.has("csv")) {
            result.csv_ = parser.get("csv");
        }
        if (parser.has("replay")) {
            result.replay_ = parser.get("replay");
        }
        if (parser.has("input")) {
            result.replay_ = parser.get("input");
        }
        result.ascii_ = parser.has("ascii");
        result.no_color_ = parser.has("no-color");
        result.threads_ = parser.has("threads");
        result.process_network_ = parser.has("process-network");
        result.read_only_ = parser.has("read-only");
        result.full_sampling_ = parser.has("full-sampling");
        if (parser.has("layout")) {
            result.layout_ = parser.get("layout");
        }
        if (parser.has("select-gpu")) {
            result.select_gpu_ = static_cast<uint32_t>(to_u64(parser.get("select-gpu"), "--select-gpu"));
        }
        if (parser.has("count")) {
            result.select_gpu_ = static_cast<uint32_t>(to_u64(parser.get("count"), "--count"));
        }
        if (parser.has("min-free-mib")) {
            result.min_free_mib_ = to_u64(parser.get("min-free-mib"), "--min-free-mib");
        }
        if (parser.has("min-total-mib")) {
            result.min_total_mib_ = to_u64(parser.get("min-total-mib"), "--min-total-mib");
        }
        if (parser.has("max-gpu-utilization")) {
            result.max_gpu_utilization_ =
                    static_cast<uint32_t>(to_u64(parser.get("max-gpu-utilization"), "--max-gpu-utilization"));
        }
        if (parser.has("max-memory-utilization")) {
            result.max_memory_utilization_ =
                    static_cast<uint32_t>(to_u64(parser.get("max-memory-utilization"), "--max-memory-utilization"));
        }
        result.no_config_exec_ = parser.has("no-config-exec");
        result.json_ = parser.has("json");
        result.reverse_ = parser.has("reverse");
        if (parser.has("sort")) {
            parse_connection_sort(parser.get("sort"), result);
        }
        if (parser.has("filter")) {
            result.filter_ = parser.get("filter");
        }
        if (parser.has("interface")) {
            result.interface_ = parser.get("interface");
        }
        if (parser.has("packets")) {
            result.packets_ = static_cast<uint32_t>(to_u64(parser.get("packets"), "--packets"));
        }
        if (parser.has("seconds")) {
            result.seconds_ = to_u64(parser.get("seconds"), "--seconds");
        }
        result.update_check_ = parser.has("check");

        const auto& positionals = parser.positional_args();
        if (positionals.empty()) {
            return result;
        }
        const string command = positionals[0];
        if (command == "monitor") {
            result.command_ = cli_command::monitor;
        } else if (command == "snapshot") {
            result.command_ = cli_command::monitor;
            result.snapshot_ = true;
        } else if (command == "sample") {
            result.command_ = cli_command::monitor;
            result.samples_ = result.samples_.has_value() ? result.samples_ : optional<uint32_t>(10);
        } else if (command == "record") {
            if (positionals.size() < 2) {
                invalid("record requires a file");
            }
            result.command_ = cli_command::record;
            result.record_ = positionals[1];
        } else if (command == "replay") {
            if (positionals.size() < 2) {
                invalid("replay requires a file");
            }
            result.command_ = cli_command::replay;
            result.replay_ = positionals[1];
        } else if (command == "export") {
            if (positionals.size() < 3) {
                invalid("export requires a format and an output path");
            }
            if (positionals[1] == "csv") {
                result.command_ = cli_command::export_csv;
                result.csv_ = positionals[2];
            } else if (positionals[1] == "svg") {
                result.command_ = cli_command::export_svg;
                result.export_svg_ = positionals[2];
            } else {
                invalid(format("Unknown export format: {}", positionals[1]));
            }
        } else if (command == "gpu") {
            if (positionals.size() >= 2 && positionals[1] == "select") {
                result.command_ = cli_command::gpu_select;
                result.select_gpu_ = result.select_gpu_.has_value() ? result.select_gpu_ : optional<uint32_t>(1);
            } else {
                result.command_ = cli_command::monitor;
                result.view_ = "gpu";
            }
        } else if (command == "network") {
            if (positionals.size() == 1) {
                result.command_ = cli_command::monitor;
                result.view_ = "network";
            } else if (positionals[1] == "connections") {
                result.command_ = cli_command::network_connections;
            } else if (positionals[1] == "check") {
                if (positionals.size() < 3) {
                    invalid("network check requires a target");
                }
                result.command_ = cli_command::network_check;
                result.target_ = positionals[2];
            } else if (positionals[1] == "capture") {
                if (positionals.size() < 3) {
                    invalid("network capture requires an output path");
                }
                result.command_ = cli_command::network_capture;
                result.file_ = positionals[2];
                if (result.capture_filter_.empty()) {
                    result.capture_filter_ = "tcp or udp or icmp or icmp6";
                }
            } else if (positionals[1] == "read") {
                if (positionals.size() < 3) {
                    invalid("network read requires a PCAP file");
                }
                result.command_ = cli_command::network_read;
                result.file_ = positionals[2];
            } else {
                invalid(format("Unknown network command: {}", positionals[1]));
            }
        } else if (command == "doctor") {
            result.command_ = cli_command::doctor;
        } else if (command == "config") {
            if (positionals.size() >= 2 && positionals[1] == "write") {
                result.command_ = cli_command::config_write;
            } else if (positionals.size() >= 2 && positionals[1] == "path") {
                result.command_ = cli_command::config_path;
            } else {
                result.command_ = cli_command::config_path;
            }
        } else if (command == "completions") {
            if (positionals.size() < 2) {
                invalid("completions requires a shell");
            }
            result.command_ = cli_command::completions;
            result.shell_ = positionals[1];
        } else if (command == "man") {
            result.command_ = cli_command::man;
        } else if (command == "update") {
            result.command_ = cli_command::update;
        } else {
            invalid(format("Unknown command: {}", command));
        }
        return result;
    }

    string version_text() { return "nexus-flux 0.1.0"; }

    string help_text() {
        return R"(One terminal. Your whole system. CPU, GPU, containers and services.

Usage: nexus-flux [OPTIONS] [COMMAND]

Commands:
  monitor              Open the interactive system monitor (default)
  snapshot             Print one JSON snapshot, including process command lines
  sample               Print newline-delimited JSON samples (ten by default)
  record <FILE>        Record telemetry while monitoring
  replay <FILE>        Replay a local recording
  export <FORMAT> <OUTPUT>  Export to CSV or SVG (--input FILE)
  gpu [select]         Open the GPU view or select CUDA devices
  network [COMMAND]    Open connection monitoring, check a target, capture packets
  doctor               Explain resource problems and next steps
  config [path|write]  Print the configuration path or write preferences
  completions <SHELL>  Generate shell completions
  man                  Print the manual page
  update               Update this copy of nexus-flux

Options:
  -i, --interval <MS>        Sampling interval in milliseconds (250..=10000)
      --theme <THEME>        Color theme
      --view <VIEW>          Initial page
      --gpu-panel <PANEL>    summary/history/device
      --config <PATH>        Alternate configuration file
      --no-gpu               Disable GPU driver queries
      --ascii                Use ASCII glyphs
      --no-color             Disable colors
      --threads              Show threads
      --process-network      Sample per-process network I/O
      --read-only            Disable state changes
      --full-sampling        Preload all background sources
      --layout <LAYOUT>      balanced/compute/storage/minimal
      --no-config-exec       Disable configured probes and alert commands
  -h, --help                 Print help
  -V, --version              Print version
)";
    }

} // namespace neforce::neflux
