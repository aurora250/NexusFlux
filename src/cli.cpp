#include <NeFlux/common.hpp>
#include <NeFlux/cli.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/exception/exception.hpp>
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

        optional<string> after_equals(const string& token) {
            const auto equals = token.find('=');
            if (equals == string::npos) {
                return none;
            }
            return token.substr(equals + 1);
        }

        string option_name(const string& token) {
            const auto equals = token.find('=');
            return equals == string::npos ? token : token.substr(0, equals);
        }

        void need_value(int argc, char** argv, int& index, const string& token, optional<string>& inline_value,
                        string& out) {
            if (inline_value.has_value()) {
                out = *inline_value;
                return;
            }
            if (index + 1 >= argc) {
                invalid(format("Missing value for {}", token));
            }
            out = argv[++index];
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
        vector<string> positionals;
        for (int index = 1; index < argc; ++index) {
            string token = argv[index];
            if (token == "-h" || token == "--help") {
                g_help = true;
                continue;
            }
            if (token == "-V" || token == "--version") {
                g_version = true;
                continue;
            }
            if (!starts_with(token.view(), "-")) {
                positionals.push_back(move(token));
                continue;
            }
            const string name = option_name(token);
            optional<string> inline_value = after_equals(token);
            string value;
            if (name == "-i" || name == "--interval") {
                need_value(argc, argv, index, token, inline_value, value);
                result.interval_ = to_u64(value, name);
                if (*result.interval_ < 250 || *result.interval_ > 10000) {
                    invalid("interval must be 250..=10000");
                }
            } else if (name == "--theme") {
                need_value(argc, argv, index, token, inline_value, value);
                result.theme_ = value;
            } else if (name == "--view") {
                need_value(argc, argv, index, token, inline_value, value);
                result.view_ = value;
            } else if (name == "--gpu-panel") {
                need_value(argc, argv, index, token, inline_value, value);
                result.gpu_panel_ = value;
            } else if (name == "--config") {
                need_value(argc, argv, index, token, inline_value, value);
                result.config_path_ = value;
            } else if (name == "--no-gpu") {
                result.no_gpu_ = true;
            } else if (name == "--snapshot") {
                result.snapshot_ = true;
            } else if (name == "--samples") {
                need_value(argc, argv, index, token, inline_value, value);
                result.samples_ = static_cast<uint32_t>(to_u64(value, name));
                if (*result.samples_ < 1 || *result.samples_ > 86400) {
                    invalid("samples must be 1..=86400");
                }
            } else if (name == "--export-svg") {
                need_value(argc, argv, index, token, inline_value, value);
                result.export_svg_ = value;
            } else if (name == "--width") {
                need_value(argc, argv, index, token, inline_value, value);
                result.width_ = static_cast<uint16_t>(to_u64(value, name));
                if (result.width_ < 60 || result.width_ > 300) {
                    invalid("width must be 60..=300");
                }
            } else if (name == "--height") {
                need_value(argc, argv, index, token, inline_value, value);
                result.height_ = static_cast<uint16_t>(to_u64(value, name));
                if (result.height_ < 18 || result.height_ > 120) {
                    invalid("height must be 18..=120");
                }
            } else if (name == "--write-config") {
                result.write_config_ = true;
            } else if (name == "--record") {
                need_value(argc, argv, index, token, inline_value, value);
                result.record_ = value;
            } else if (name == "--csv") {
                need_value(argc, argv, index, token, inline_value, value);
                result.csv_ = value;
            } else if (name == "--replay") {
                need_value(argc, argv, index, token, inline_value, value);
                result.replay_ = value;
            } else if (name == "--ascii") {
                result.ascii_ = true;
            } else if (name == "--no-color") {
                result.no_color_ = true;
            } else if (name == "--threads") {
                result.threads_ = true;
            } else if (name == "--process-network") {
                result.process_network_ = true;
            } else if (name == "--read-only") {
                result.read_only_ = true;
            } else if (name == "--full-sampling") {
                result.full_sampling_ = true;
            } else if (name == "--layout") {
                need_value(argc, argv, index, token, inline_value, value);
                result.layout_ = value;
            } else if (name == "--select-gpu") {
                need_value(argc, argv, index, token, inline_value, value);
                result.select_gpu_ = static_cast<uint32_t>(to_u64(value, name));
            } else if (name == "--min-free-mib") {
                need_value(argc, argv, index, token, inline_value, value);
                result.min_free_mib_ = to_u64(value, name);
            } else if (name == "--min-total-mib") {
                need_value(argc, argv, index, token, inline_value, value);
                result.min_total_mib_ = to_u64(value, name);
            } else if (name == "--max-gpu-utilization") {
                need_value(argc, argv, index, token, inline_value, value);
                result.max_gpu_utilization_ = static_cast<uint32_t>(to_u64(value, name));
            } else if (name == "--max-memory-utilization") {
                need_value(argc, argv, index, token, inline_value, value);
                result.max_memory_utilization_ = static_cast<uint32_t>(to_u64(value, name));
            } else if (name == "--no-config-exec") {
                result.no_config_exec_ = true;
            } else if (name == "--json") {
                result.json_ = true;
            } else if (name == "--reverse") {
                result.reverse_ = true;
            } else if (name == "--sort") {
                need_value(argc, argv, index, token, inline_value, value);
                parse_connection_sort(value, result);
            } else if (name == "--filter") {
                need_value(argc, argv, index, token, inline_value, value);
                result.filter_ = value;
            } else if (name == "--interface") {
                need_value(argc, argv, index, token, inline_value, value);
                result.interface_ = value;
            } else if (name == "--packets") {
                need_value(argc, argv, index, token, inline_value, value);
                result.packets_ = static_cast<uint32_t>(to_u64(value, name));
            } else if (name == "--seconds") {
                need_value(argc, argv, index, token, inline_value, value);
                result.seconds_ = to_u64(value, name);
            } else if (name == "--count") {
                need_value(argc, argv, index, token, inline_value, value);
                result.select_gpu_ = static_cast<uint32_t>(to_u64(value, name));
            } else if (name == "--check") {
                result.update_check_ = true;
            } else if (name == "--input") {
                need_value(argc, argv, index, token, inline_value, value);
                result.replay_ = value;
            } else {
                invalid(format("Unknown option: {}", name));
            }
        }

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
