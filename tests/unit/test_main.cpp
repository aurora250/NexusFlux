#include <NeFlux/advanced.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/cli.hpp>
#include <NeFlux/config.hpp>
#include <NeFlux/collect.hpp>
#include <NeFlux/crash.hpp>
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
#include <NeFlux/ui/testing.hpp>
#include <NeForce/tui/headless.hpp>
#include <NeForce/tui/application.hpp>
#include <NeForce/tui/chart/canvas_chart.hpp>
#include <NeForce/tui/dom/element.hpp>
#include <NeForce/core/file/filesystem.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/core/system/sysinfo.hpp>
#include <NeForce/network/socket_base.hpp>
#include <NeFlux/platform.hpp>
#if NEFLUX_PLATFORM_LINUX
#    include <dirent.h>
#    include <elf.h>
#    include <sys/wait.h>
#    include <unistd.h>
#    include <csignal>
#    include <cstdlib>
#    include <cstring>
#endif
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

    bool cpu_has_detail(const json& cpu, const char* label) {
        const json* details = cpu.get("Details");
        if (details == nullptr || !details->is_array()) {
            return false;
        }
        for (const auto& item: details->as_array()) {
            const json* value = item.get("Label");
            if (value != nullptr && value->is_string() && value->as_string() == label) {
                return true;
            }
        }
        return false;
    }

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
        cfg.display_.transparent_background_ = false;
        cfg.display_.graph_symbol_ = "block";
        cfg.display_.cpu_lazy_sort_ = false;
        cfg.display_.rounded_corners_ = false;
        cfg.display_.process_colors_ = false;
        cfg.display_.process_gradient_ = false;
        cfg.display_.graph_symbol_cpu_ = "block";
        cfg.display_.graph_symbol_net_ = "tty";
        cfg.alert_commands_ = {{"echo", "alert"}};
        cfg.probes_.push_back(probe_config{"probe", {"true"}, 2});
        layout_node layout;
        layout.kind_ = layout_node::kind::split;
        layout.direction_ = "horizontal";
        layout.weights_ = {1, 1};
        layout_node first;
        first.panel_ = "cpu";
        layout_node second;
        second.panel_ = "processes";
        layout.children_.push_back(first);
        layout.children_.push_back(second);
        cfg.custom_layout_ = layout;
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
        CHECK(!parsed.display_.transparent_background_);
        CHECK(parsed.display_.graph_symbol_ == "block");
        CHECK(!parsed.display_.cpu_lazy_sort_);
        CHECK(!parsed.display_.rounded_corners_);
        CHECK(!parsed.display_.process_colors_);
        CHECK(!parsed.display_.process_gradient_);
        CHECK(parsed.display_.graph_symbol_cpu_ == "block");
        CHECK(parsed.display_.graph_symbol_net_ == "tty");
        CHECK(parsed.alert_commands_.size() == 1);
        CHECK(parsed.probes_.size() == 1);
        CHECK(parsed.probes_[0].timeout_seconds_ == 2);
        CHECK(parsed.custom_layout_.has_value());
        if (parsed.custom_layout_.has_value()) {
            CHECK(parsed.custom_layout_->children_.size() == 2);
            CHECK(parsed.custom_layout_->children_[1].panel_ == "processes");
        }
        bool rejected = false;
        try {
            ignore = config::from_toml("unknown_key = 1");
        } catch (const exception&) {
            rejected = true;
        }
        CHECK(rejected);
    }

    void test_cli() {
        char program[] = "nexus-flux";
        char interval[] = "--interval=500";
        char snapshot[] = "snapshot";
        char* args[] = {program, interval, snapshot};
        const cli_args parsed = parse_cli(3, args);
        CHECK(parsed.interval_.has_value() && *parsed.interval_ == 500);
        CHECK(parsed.snapshot_);
        CHECK(!help_requested());

        char short_interval[] = "-i";
        char short_value[] = "250";
        char record[] = "record";
        char file[] = "session.jsonl";
        char* record_args[] = {program, short_interval, short_value, record, file};
        const cli_args recorded = parse_cli(5, record_args);
        CHECK(recorded.interval_.has_value() && *recorded.interval_ == 250);
        CHECK(recorded.command_ == cli_command::record);
        CHECK(recorded.record_.has_value() && *recorded.record_ == "session.jsonl");

        char network[] = "network";
        char check[] = "check";
        char target[] = "127.0.0.1:80";
        char* network_args[] = {program, network, check, target};
        const cli_args checked = parse_cli(4, network_args);
        CHECK(checked.command_ == cli_command::network_check);
        CHECK(checked.target_ == "127.0.0.1:80");

        char help[] = "--help";
        char* help_args[] = {program, help};
        static_cast<void>(parse_cli(2, help_args));
        CHECK(help_requested());

        char unknown[] = "--unknown";
        char* unknown_args[] = {program, unknown};
        bool rejected = false;
        try {
            static_cast<void>(parse_cli(2, unknown_args));
        } catch (const exception&) {
            rejected = true;
        }
        CHECK(rejected);
    }

    void test_crash_core_dump() {
#if NEFLUX_PLATFORM_LINUX && (defined(__x86_64__) || defined(__aarch64__)) && !defined(__SANITIZE_ADDRESS__)
        char directory[] = "/tmp/neflux-crash-test-XXXXXX";
        char* created = ::mkdtemp(directory);
        CHECK(created != nullptr);
        if (created == nullptr) {
            return;
        }
        const pid_t child = ::fork();
        CHECK(child >= 0);
        if (child < 0) {
            static_cast<void>(filesystem::remove_all(path(directory)));
            return;
        }
        if (child == 0) {
            static_cast<void>(::setenv("XDG_CONFIG_HOME", directory, 1));
            char argument[] = "nexus-flux-test";
            char* arguments[] = {argument, nullptr};
            initialize_crash_logging(1, arguments);
            ::raise(SIGSEGV);
            ::_exit(99);
        }
        int status = 0;
        CHECK(::waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status));
        CHECK(WEXITSTATUS(status) == 128 + SIGSEGV);

        char log_directory[512]{};
        std::snprintf(log_directory, sizeof(log_directory), "%s/neflux/logs", directory);
        bool found_core = false;
        DIR* handle = ::opendir(log_directory);
        CHECK(handle != nullptr);
        if (handle != nullptr) {
            while (const dirent* entry = ::readdir(handle)) {
                const std::size_t length = std::strlen(entry->d_name);
                if (length < 5 || std::strcmp(entry->d_name + length - 5, ".core") != 0) {
                    continue;
                }
                char core_path[1024]{};
                std::snprintf(core_path, sizeof(core_path), "%s/%s", log_directory, entry->d_name);
                FILE* file = std::fopen(core_path, "rb");
                CHECK(file != nullptr);
                if (file != nullptr) {
                    Elf64_Ehdr header{};
                    const std::size_t read = std::fread(&header, 1, sizeof(header), file);
                    CHECK(read == sizeof(header));
                    CHECK(std::memcmp(header.e_ident, ELFMAG, SELFMAG) == 0);
                    CHECK(header.e_ident[EI_CLASS] == ELFCLASS64 && header.e_ident[EI_DATA] == ELFDATA2LSB);
                    CHECK(header.e_type == ET_CORE);
                    CHECK(header.e_machine == EM_X86_64 || header.e_machine == EM_AARCH64);
                    CHECK(header.e_phnum >= 2 && header.e_phentsize == sizeof(Elf64_Phdr));
                    Elf64_Phdr note_phdr{};
                    CHECK(std::fseek(file, static_cast<long>(header.e_phoff), SEEK_SET) == 0);
                    CHECK(std::fread(&note_phdr, 1, sizeof(note_phdr), file) == sizeof(note_phdr));
                    CHECK(note_phdr.p_type == PT_NOTE);
                    Elf64_Nhdr note_header{};
                    CHECK(std::fseek(file, static_cast<long>(note_phdr.p_offset), SEEK_SET) == 0);
                    CHECK(std::fread(&note_header, 1, sizeof(note_header), file) == sizeof(note_header));
                    CHECK(note_header.n_type == NT_PRSTATUS);
                    std::fclose(file);
                    found_core = true;
                }
                break;
            }
            ::closedir(handle);
        }
        CHECK(found_core);

        string crash_log_text;
        DIR* log_handle = ::opendir(log_directory);
        CHECK(log_handle != nullptr);
        if (log_handle != nullptr) {
            while (const dirent* entry = ::readdir(log_handle)) {
                if (std::strncmp(entry->d_name, "nexus-flux.log", 14) != 0) {
                    continue;
                }
                string candidate(log_directory);
                candidate.append("/");
                candidate.append(entry->d_name);
                const auto text = read_text_file(candidate);
                if (text.has_value()) {
                    crash_log_text = *text;
                }
            }
            ::closedir(log_handle);
        }
        CHECK(!crash_log_text.empty());
        CHECK(crash_log_text.find("fatal signal") != string::npos);
        CHECK(crash_log_text.find("SIGSEGV") != string::npos);
        CHECK(crash_log_text.find("dump=") != string::npos);
        static_cast<void>(filesystem::remove_all(path(directory)));
#endif
    }

    void test_cpu_info() {
        sysinfo::cpu_info x86_cpu;
        x86_cpu.vendor = "GenuineIntel";
        x86_cpu.brand = "Test x86 CPU";
        x86_cpu.max_mhz = 5400;
        x86_cpu.current_mhz = 4200;
        x86_cpu.cores = 8;
        x86_cpu.logical_processors = 16;
        x86_cpu.features = "sse sse2 avx2";
        x86_cpu.x86.family = "6";
        x86_cpu.x86.model = "183";
        x86_cpu.x86.stepping = "1";
        x86_cpu.x86.ext_family = "6";
        x86_cpu.x86.ext_model = "B7";
        x86_cpu.x86.revision = "1";
        x86_cpu.x86.core_vid = "1.20 V";
        x86_cpu.x86.technology = "10 nm";
        x86_cpu.x86.socket = "LGA1700";
        x86_cpu.x86.code_name = "Raptor Lake";
        x86_cpu.x86.bus_speed_mhz = 100;
        x86_cpu.x86.max_tdp_watts = 55;
        const json x86 = cpu_info_json(x86_cpu, sysinfo::architecture::x64);
        CHECK(x86.get("Architecture") != nullptr && x86.get("Architecture")->as_string() == "x86_64");
        CHECK(x86.get("Vendor") != nullptr && x86.get("Vendor")->as_string() == "GenuineIntel");
        CHECK(x86.get("Cores / Threads") != nullptr && x86.get("Cores / Threads")->as_string() == "8 / 16");
        CHECK(x86.get("Max clock") != nullptr && x86.get("Max clock")->as_string() == "5400 MHz");
        CHECK(x86.get("Family") != nullptr && x86.get("Family")->as_string() == "6");
        CHECK(x86.get("Socket") != nullptr && x86.get("Socket")->as_string() == "LGA1700");
        CHECK(x86.get("Multiplier") != nullptr && x86.get("Multiplier")->as_string() == "42.00");
        CHECK(x86.get("Package") == nullptr);
        CHECK(cpu_has_detail(x86, "Stepping"));
        CHECK(cpu_has_detail(x86, "Max TDP"));
        CHECK(!cpu_has_detail(x86, "MMU"));

        sysinfo::cpu_info arm_cpu;
        arm_cpu.vendor = "ARM";
        arm_cpu.brand = "Cortex-A76";
        arm_cpu.cores = 8;
        arm_cpu.logical_processors = 8;
        arm_cpu.arm.architecture = "8";
        arm_cpu.arm.implementer = "0x41";
        arm_cpu.arm.part = "0xd0b";
        arm_cpu.arm.variant = "0x1";
        arm_cpu.arm.revision = "1";
        arm_cpu.arm.core_name = "Cortex-A76";
        const json arm = cpu_info_json(arm_cpu, sysinfo::architecture::arm64);
        CHECK(arm.get("Architecture") != nullptr && arm.get("Architecture")->as_string() == "ARM64");
        CHECK(arm.get("Implementer") != nullptr && arm.get("Implementer")->as_string() == "0x41");
        CHECK(arm.get("Family") == nullptr);
        CHECK(cpu_has_detail(arm, "ARM arch"));
        CHECK(cpu_has_detail(arm, "Core name"));
        CHECK(!cpu_has_detail(arm, "Stepping"));

        sysinfo::cpu_info riscv_cpu;
        riscv_cpu.riscv.isa = "rv64imafdc";
        riscv_cpu.riscv.mmu = "svpbmt";
        riscv_cpu.riscv.uarch = "sifive,u74";
        riscv_cpu.riscv.mvendorid = "0x489";
        riscv_cpu.riscv.marchid = "0x8000000000000007";
        riscv_cpu.riscv.mimpid = "0x0";
        const json riscv = cpu_info_json(riscv_cpu, sysinfo::architecture::riscv64);
        CHECK(riscv.get("Architecture") != nullptr && riscv.get("Architecture")->as_string() == "RISC-V 64");
        CHECK(riscv.get("ISA") != nullptr && riscv.get("ISA")->as_string() == "rv64imafdc");
        CHECK(riscv.get("MMU") != nullptr && riscv.get("MMU")->as_string() == "svpbmt");
        CHECK(riscv.get("MVendor ID") != nullptr && riscv.get("MVendor ID")->as_string() == "0x489");
        CHECK(riscv.get("Family") == nullptr);

        sysinfo::cpu_info loong_cpu;
        loong_cpu.loongarch.family = "Loongson-3A6000";
        loong_cpu.loongarch.revision = "0x01";
        loong_cpu.loongarch.fpu_revision = "0x01";
        loong_cpu.loongarch.isa = "loongarch64";
        loong_cpu.loongarch.address_sizes = "48/48";
        loong_cpu.loongarch.machine = "Loongson-3A6000";
        loong_cpu.loongarch.system_type = "desktop";
        loong_cpu.loongarch.watchpoint = "8";
        loong_cpu.loongarch.tlb_entries = 64;
        const json loong = cpu_info_json(loong_cpu, sysinfo::architecture::loongarch64);
        CHECK(loong.get("Architecture") != nullptr && loong.get("Architecture")->as_string() == "LoongArch 64");
        CHECK(loong.get("Family") != nullptr && loong.get("Family")->as_string() == "Loongson-3A6000");
        CHECK(loong.get("TLB entries") != nullptr && loong.get("TLB entries")->as_string() == "64");
        CHECK(loong.get("ISA") != nullptr && loong.get("ISA")->as_string() == "loongarch64");

        const json unknown = cpu_info_json(sysinfo::cpu_info{}, sysinfo::architecture::unknown);
        CHECK(unknown.get("Architecture") != nullptr && unknown.get("Architecture")->as_string() == "Unknown");
        CHECK(unknown.get("Details") != nullptr && unknown.get("Details")->is_array() &&
              unknown.get("Details")->as_array().empty());
    }

    void test_tui_headless() {
#if NEFLUX_PLATFORM_LINUX
        int fds[2] = {-1, -1};
        CHECK(::pipe(fds) == 0);
        const pid_t child = ::fork();
        CHECK(child >= 0);
        if (child == 0) {
            ::close(fds[0]);
            const string frames = ui::headless_probe("\x1b|\x1b[<0;40;22M|\x1b|\x1b[<0;40;18M|\x1b");
            size_t offset = 0;
            while (offset < frames.size()) {
                const ssize_t written = ::write(fds[1], frames.data() + offset, frames.size() - offset);
                if (written <= 0) {
                    break;
                }
                offset += static_cast<size_t>(written);
            }
            ::close(fds[1]);
            ::_exit(0);
        }
        ::close(fds[1]);
        string frames;
        char buffer[4096];
        for (;;) {
            const ssize_t count = ::read(fds[0], buffer, sizeof(buffer));
            if (count <= 0) {
                break;
            }
            frames.append(buffer, static_cast<size_t>(count));
        }
        ::close(fds[0]);
        int status = 0;
        CHECK(::waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status));
        CHECK(WEXITSTATUS(status) == 0);
        CHECK(frames.find("Quit") != string::npos);
        CHECK(frames.find("Theme · preview") != string::npos);
        CHECK(frames.find("black") != string::npos);
        CHECK(frames.find("●") != string::npos);
        CHECK(frames.find("Settings") != string::npos);
        CHECK(ui::headless_probe_background("#wait|#esc|\\x1b[B|\\x1b[B|\\x1b[B|\\r|#wait|#esc", 40, 22) ==
              ui::headless_probe_background("#esc|\x1b[<0;40;16M|#wait|#esc", 40, 22));
        const string choice = ui::headless_probe("#esc|\x1b[<0;40;18M|\x1b[<0;40;14M");
        CHECK(choice.find("Setting · Theme") != string::npos);
        CHECK(choice.find("midnight") != string::npos);
        const string reopened = ui::headless_probe("#esc|\x1b[<0;40;16M|#wait|#esc");
        const auto reopened_marker = string("<<<FRAME>>>");
        const auto reopened_end = reopened.rfind(reopened_marker);
        const auto reopened_start = reopened.rfind(reopened_marker, reopened_end - 1);
        CHECK(reopened_start != string::npos && reopened_end != string::npos);
        if (reopened_start != string::npos && reopened_end != string::npos) {
            const string last = reopened.substr(reopened_start + reopened_marker.size(),
                                                reopened_end - reopened_start - reopened_marker.size());
            CHECK(last.find("Save config") != string::npos);
        }
        const uint32_t border_bg = ui::headless_probe_background("\x1b", 28, 14);
        CHECK((border_bg >> 24) == 0xFFU);
        CHECK(ui::headless_probe_background("\x1b", 91, 14) == border_bg);
        CHECK(ui::headless_probe_background("\x1b", 28, 25) == border_bg);
        CHECK(ui::headless_probe_background("\x1b", 91, 25) == border_bg);
        CHECK(ui::headless_probe_background("\x1b", 40, 14) == border_bg);
        CHECK(ui::headless_probe_background("\x1b", 28, 18) == border_bg);
        CHECK(ui::headless_probe_background("\x1b", 40, 16) == border_bg);
        CHECK(ui::headless_probe_background("\x1b", 80, 22) == border_bg);
        const auto menu_backgrounds = ui::headless_probe_backgrounds("\x1b", 28, 14, 64, 12);
        bool opaque = true;
        for (const uint32_t background: menu_backgrounds) {
            opaque = opaque && (background >> 24) == 0xFFU;
        }
        CHECK(opaque);
        const auto marker = string("<<<FRAME>>>");
        const auto last_end = frames.rfind(marker);
        const auto last_start = frames.rfind(marker, last_end - 1);
        CHECK(last_start != string::npos && last_end != string::npos);
        if (last_start != string::npos && last_end != string::npos) {
            const string last = frames.substr(last_start + marker.size(), last_end - last_start - marker.size());
            CHECK(last.find("Resume") != string::npos);
        }
#endif
    }

    void test_util() {
        CHECK(fit_display("abc", 5) == "abc  ");
        CHECK(fit_display("abc", 5, true) == "  abc");
        CHECK(fit_display("中", 4) == "中  ");
        CHECK(display_width("中") == 2);
        CHECK(display_width("🚀") == 2);
        CHECK(display_width("abc") == 3);
        CHECK(trim_view("  value \t") == "value");
        CHECK(parse_u64("42").value() == 42);
        CHECK(!parse_u64("42x").has_value());
        CHECK(parse_i64("-7").value() == -7);
        CHECK(parse_f64("1.5%").value() == 1.5);
    }

    void test_chart_zero_and_missing() {
        struct chart_root final : tui::component<> {
            deque<double> values;
            tui::chart_series series;
            chart_root() {
                series.name = "series";
                series.stream = &values;
            }
            tui::element render() override {
                auto* stream = &series;
                const size_t samples = values.size();
                return tui::element::canvas([stream, samples](tui::canvas_painter& canvas) {
                    tui::canvas_chart_window window;
                    window.samples = samples;
                    window.end = samples;
                    tui::canvas_chart(canvas, tui::canvas_chart_style::from_theme(tui::dark_theme, "braille", false))
                            .raster(0, 0, canvas.width(), canvas.height(), {*stream}, 100.0, window, false, 0);
                });
            }
        };
        const auto glyph_count = [](const string& text) {
            size_t count = 0;
            for (const char character: text) {
                if (character != ' ' && character != '\n' && character != '\r') {
                    ++count;
                }
            }
            return count;
        };
        chart_root zero;
        zero.values = {0.0, 0.0, 0.0, 0.0};
        tui::headless zero_app(&zero, 20, 6);
        zero_app.render();
        CHECK(glyph_count(zero_app.plaintext()) > 0);

        chart_root missing;
        missing.values = {numeric_traits<double>::quiet_nan(), numeric_traits<double>::quiet_nan(),
                          numeric_traits<double>::quiet_nan(), numeric_traits<double>::quiet_nan()};
        tui::headless missing_app(&missing, 20, 6);
        missing_app.render();
        CHECK(glyph_count(missing_app.plaintext()) == 0);
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
        CHECK(tui::canvas_chart_nice_max(110.0) == 200.0);
        CHECK(tui::canvas_chart_nice_max(-1.0) == 1.0);
    }

    void test_filter() {
        neforce::neflux::process value;
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
        neforce::neflux::process item;
        item.pid_ = 7;
        item.name_ = "postgres";
        item.command_ = "postgres -D /var/lib/postgresql/data";
        item.memory_ = 2ULL * 1024ULL * 1024ULL * 1024ULL;
        item.cpu_ = 120.0F;
        value.processes_.push_back(item);
        neforce::neflux::process resolver;
        resolver.pid_ = 8;
        resolver.name_ = "systemd-resolve";
        value.processes_.push_back(resolver);
        neforce::neflux::process shell;
        shell.pid_ = 9;
        shell.name_ = "zsh";
        value.processes_.push_back(shell);
        neforce::neflux::process kernel;
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
        disk_io device;
        device.name_ = "sda";
        device.aggregate_ = true;
        device.read_per_sec_ = 100.0;
        device.write_per_sec_ = 50.0;
        sample.disk_io_.push_back(device);
        value.ingest(sample, cfg);
        const auto* first = value.disk("sda");
        CHECK(first != nullptr && first->read_.back() == 100.0 && first->write_.back() == 50.0);
        sample.disk_io_[0].read_per_sec_ = 200.0;
        value.ingest(sample, cfg);
        const auto* second = value.disk("sda");
        CHECK(second != nullptr && second->read_.back() == 200.0 && second->read_.size() == 2);
        CHECK(value.disk_read_.back() == 200.0);
        const auto window = value.window(30);
        CHECK(window.end == value.sequence_);
    }

    void test_neforce_system_components() {
#if NEFLUX_PLATFORM_LINUX
        const auto processes = neforce::process::snapshot();
        CHECK(!processes.empty());
        CHECK(processes.front().pid > 0);
        const auto mounts = neforce::sysinfo::mount_points();
        CHECK(!mounts.empty());
        CHECK(!mounts.front().target.empty());
#endif
        CHECK(neforce::socket_base::state_name(neforce::socket_base::state::listen) == "LISTEN");
        CHECK(neforce::socket_base::state_name(neforce::socket_base::state::established) == "ESTABLISHED");
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
        neforce::neflux::process item;
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
        virtual_machine vm;
        vm.name_ = "test-vm";
        vm.backend_ = "libvirt";
        vm.state_ = "running";
        vm.vcpus_ = 4;
        vm.memory_ = 2ULL * 1024 * 1024 * 1024;
        vm.max_memory_ = 4ULL * 1024 * 1024 * 1024;
        vm.pid_ = 4242;
        value.inventory_.virtual_machines_.push_back(vm);
        value.inventory_.vm_managers_.push_back("libvirt");
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
        neforce::neflux::process item;
        item.pid_ = 1;
        item.name_ = "test";
        item.command_ = "test";
        item.start_time_ = 0;
        item.extra_ = json::object();
        value.processes_.push_back(item);
        advanced telemetry;
        telemetry.collect(value, cfg);
#if NEFLUX_PLATFORM_LINUX
        CHECK(value.extra_.get("CPU scheduler / swap IO") != nullptr);
        CHECK(!value.processes_.empty());
        CHECK(value.processes_[0].extra_.get("stat") != nullptr);
#endif
        cfg.columns_ = {"cpu_delay", "io_delay", "swap_delay", "priority"};
        value.processes_[0].extra_ = json::object();
        advanced delay_telemetry;
        delay_telemetry.collect(value, cfg);
#if NEFLUX_PLATFORM_LINUX
        CHECK(value.processes_[0].extra_.get("delay_accounting_status") != nullptr ||
              value.processes_[0].extra_.get("cpu_delay") != nullptr);
#endif
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
        neforce::neflux::process item;
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
#if NEFLUX_PLATFORM_LINUX
            CHECK(connections[0].state_ == "LISTEN");
#endif
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
        const json* hardware = minimal.extra_.get("Hardware");
        CHECK(hardware != nullptr);
        const json* cpu = hardware != nullptr ? hardware->get("CPU") : nullptr;
        CHECK(cpu != nullptr);
        CHECK(cpu != nullptr && cpu->get("Architecture") != nullptr);
        CHECK(cpu != nullptr && cpu->get("Details") != nullptr && cpu->get("Details")->is_array());
        if (cpu != nullptr && cpu->get("Architecture") != nullptr) {
            const string system_page = ui::render_plaintext(minimal, minimal.inventory_, cfg, "system", 160, 48);
            CHECK(system_page.find("Architecture") != string::npos);
            CHECK(system_page.find(cpu->get("Architecture")->as_string()) != string::npos);
            const json* details = cpu->get("Details");
            if (details != nullptr) {
                for (const auto& item: details->as_array()) {
                    const json* label = item.get("Label");
                    const json* value = item.get("Value");
                    if (label == nullptr || value == nullptr || !label->is_string() || !value->is_string() ||
                        value->as_string() == "—") {
                        continue;
                    }
                    CHECK(system_page.find(label->as_string()) != string::npos);
                    break;
                }
            }
        }
        const snapshot connected = instance.collect_with_processes(cfg, false, false, true);
        CHECK(connected.connections_.connections_.size() >= minimal.connections_.connections_.size());
    }


    void test_selection_clipboard_state() {
        struct dummy_root final : tui::component<> {
            tui::element render() override { return tui::element::text("selectable text"); }
        };
        dummy_root root;
        tui::headless app(&root, 120, 40);
        app.render();
        app.feed("\x1b[<0;1;1M\x1b[<32;10;1M");
        CHECK(tui::application::has_selection());
        CHECK(!tui::application::selection_text().empty());
        tui::application::copy_selection();
        CHECK(!tui::application::has_selection());
    }
    void test_tools_and_column_window() {
        // 主菜单进入 Sort 子菜单后 Esc 应回到主菜单
        const string submenu = ui::headless_probe("#wait|#esc|\x1b[B|\x1b[B|\x1b[B|\r|#wait|#esc");
        CHECK(submenu.find("Save config") != string::npos);
        // F6 打开上下文工具菜单（进程页）
        const string tools = ui::headless_probe_sized(120, 40, "2|\x1b[17~");
        CHECK(tools.find("Tools") != string::npos);
        CHECK(tools.find("Signal") != string::npos);
        // F7 打开日志弹窗
        const string logs = ui::headless_probe_sized(120, 40, "2|\x1b[18~");
        CHECK(logs.find("Logs") != string::npos);
        // Shift+Right 横向滚动进程列窗口
        const string columns = ui::headless_probe_sized(120, 40, "2|\x1b[1;2C");
        CHECK(columns.find("Columns start at") != string::npos);
        // Ctrl+C 复制由 reconciler 在 dispatch_key 中拦截（copy-on-select 同样可用）
    }
    void test_tab_layout_hits() {
        // 无头终端固定 120 × 40：标签栏高亮与 mouse hit 必须来自同一份布局
        const string initial = ui::headless_probe("");
        CHECK(initial.find("120 × 40 · Processes") != string::npos);

        // 点击 [1] Overview（第 2 行、第 6 列，1 基）后切换视图
        const string overview = ui::headless_probe("\x1b[<0;6;2M");
        CHECK(overview.rfind("120 × 40 · Overview") != string::npos);
        // 选中背景覆盖 [1] Overview 的 0..13 列，不越界到 [2]
        const uint32_t overview_bg = ui::headless_probe_background("\x1b[<0;6;2M", 0, 1);
        CHECK(overview_bg == ui::headless_probe_background("\x1b[<0;6;2M", 13, 1));
        CHECK(overview_bg != ui::headless_probe_background("\x1b[<0;6;2M", 14, 1));

        // 点击 [2] Processes 后高亮正好覆盖 14..28 列
        const string processes = ui::headless_probe("\x1b[<0;16;2M");
        CHECK(processes.rfind("120 × 40 · Processes") != string::npos);
        const uint32_t processes_bg = ui::headless_probe_background("\x1b[<0;16;2M", 14, 1);
        CHECK(processes_bg == ui::headless_probe_background("\x1b[<0;16;2M", 28, 1));
        CHECK(processes_bg != ui::headless_probe_background("\x1b[<0;16;2M", 29, 1));

        // [+] 溢出指示（107 列）打开视图菜单
        const string menu = ui::headless_probe("\x1b[<0;108;2M");
        CHECK(menu.rfind("Explorer") != string::npos);
    }

    void test_popup_overlay() {
        // 弹窗是叠加层：菜单/帮助/设置都以 DOM 行渲染，且只覆盖自身区域
        const string menu = ui::headless_probe("#wait|#esc|\\x1b[B|\\x1b[B|\\x1b[B|\\r|#wait|#esc");
        CHECK(menu.rfind("Quit") != string::npos);
        CHECK(menu.rfind("Settings") != string::npos);
        CHECK(menu.rfind("PROCESSES") != string::npos); // 弹窗外的内容仍然可见
        const string help = ui::headless_probe("\x1bOP");
        CHECK(help.rfind("Help") != string::npos);
        CHECK(help.rfind("Change page") != string::npos);
        const string settings = ui::headless_probe("\x1bOQ");
        CHECK(settings.rfind("Config file") != string::npos);
        CHECK(settings.rfind("Sampling interval") != string::npos);
        const string sort = ui::headless_probe("\x1bOS");
        CHECK(sort.rfind("Sort") != string::npos);
        // 弹窗区域外（120 × 40）的背景必须与未打开弹窗时一致
        CHECK(ui::headless_probe_background("#wait|#esc|\\x1b[B|\\x1b[B|\\x1b[B|\\r|#wait|#esc", 2, 10) ==
              ui::headless_probe_background("", 2, 10));
        CHECK(ui::headless_probe_background("\x1bOQ", 2, 10) == ui::headless_probe_background("", 2, 10));
    }

    void test_process_table_hits() {
        snapshot value = sample_snapshot();
        neforce::neflux::process second = value.processes_.front();
        second.pid_ = 43;
        second.start_time_ = value.processes_.front().start_time_ + 100;
        second.name_ = "nginx";
        value.processes_.push_back(second);
        const inventory data = value.inventory_;
        // 120 × 40 进程页：表格行从第 8 行开始，第一行默认选中
        const uint32_t selected = ui::headless_probe_seeded_background(value, data, "processes", "", 5, 8);
        const uint32_t plain = ui::headless_probe_seeded_background(value, data, "processes", "", 5, 9);
        CHECK(selected != plain);
        // 点击第二行（1 基列 6、行 10）按 PID + start time 命中，选中背景互换
        const string click = "\x1b[<0;6;10M";
        CHECK(ui::headless_probe_seeded_background(value, data, "processes", click.view(), 5, 9) == selected);
        CHECK(ui::headless_probe_seeded_background(value, data, "processes", click.view(), 5, 8) == plain);
    }

    void test_chrome_background() {
        // 默认 transparent_background = true：chrome 行不得出现不透明底色
        CHECK(ui::headless_probe_background("", 40, 1) == 0); // 未选中 tab
        CHECK(ui::headless_probe_background("", 2, 2) == 0);  // context 行
        // 选中 tab 仍保留选中底色
        CHECK(ui::headless_probe_background("", 20, 1) != 0);
    }

    void test_small_terminal_chrome() {
        // 小终端只显示 resize 提示，不绘制 header/tabs/context/footer
        const string small = ui::headless_probe_sized(40, 10, "");
        CHECK(small.find("Resize to at least 60 × 18") != string::npos);
        CHECK(small.find("Recommended: 140 × 44") != string::npos);
        CHECK(small.find("Overview") == string::npos);
        CHECK(small.find("Discovering host") == string::npos);
        CHECK(small.find("Esc Menu") == string::npos);
        const string normal = ui::headless_probe_sized(120, 40, "");
        CHECK(normal.find("Overview") != string::npos);
        CHECK(normal.find("Discovering host") != string::npos);
        CHECK(normal.find("Esc Menu") != string::npos);
    }

    void test_resize_layout() {
        // 缩放当帧就使用新尺寸：footer 与内容一起重建
        const string shrunk = ui::headless_probe_resized(120, 40, 60, 18);
        CHECK(shrunk.find("60 × 18") != string::npos);
        CHECK(shrunk.find("120 × 40") == string::npos);
        CHECK(shrunk.find("PROCESSES") != string::npos);
        const string grown = ui::headless_probe_resized(60, 18, 120, 40);
        CHECK(grown.find("120 × 40") != string::npos);
        CHECK(grown.find("PROCESSES") != string::npos);
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
        const string vms_page = ui::headless_probe("9");
        CHECK(vms_page.find("VIRTUAL MACHINES") != string::npos);
        const string vms = ui::render_plaintext(value, data, cfg, "vms", 120, 36);
        CHECK(vms.find("VIRTUAL MACHINES") != string::npos);
        CHECK(vms.find("test-vm") != string::npos);
        CHECK(vms.find("libvirt") != string::npos);
        const string vm_menu = ui::headless_probe_seeded(value, data, "vms", "\x1b[<2;10;9M", 120, 40);
        CHECK(vm_menu.find("VM · test-vm") != string::npos);
        CHECK(vm_menu.find("Shutdown") != string::npos);
        const string service_panel = ui::headless_probe_seeded(value, data, "services", "D", 120, 40);
        CHECK(service_panel.find("SERVICE · demo.service") != string::npos);
        const string service_menu = ui::headless_probe_seeded(value, data, "services", "\x1b[<2;10;9M", 120, 40);
        CHECK(service_menu.find("SERVICE · demo.service") != string::npos);
        CHECK(service_menu.find("Restart service") != string::npos);
        const string service_menu_click =
                ui::headless_probe_seeded(value, data, "services", "\x1b[<2;10;9M|\x1b[<0;60;16M", 120, 40);
        CHECK(service_menu_click.find("Main PID") != string::npos);
        inventory reordered = data;
        service other = data.services_.front();
        other.name_ = "other.service";
        reordered.services_.clear();
        reordered.services_.push_back(other);
        reordered.services_.push_back(data.services_.front());
        const string tracked_service =
                ui::headless_probe_service_refresh(value, data, reordered, "\x1b[<0;10;9M", "\r", 120, 40);
        CHECK(tracked_service.find("Name          demo.service") != string::npos);
        inventory many_listeners = data;
        many_listeners.listeners_.clear();
        for (uint32_t index = 0; index < 30; ++index) {
            listener item;
            item.protocol_ = "TCP";
            item.address_ = format("10.0.0.{}", index);
            item.port_ = static_cast<uint16_t>(8000 + index);
            many_listeners.listeners_.push_back(item);
        }
        const string listener_page = ui::headless_probe_seeded(value, many_listeners, "system", "", 120, 40);
        CHECK(listener_page.find("LISTENERS · 30") != string::npos);
        const string listener_scrolled =
                ui::headless_probe_seeded(value, many_listeners, "system", "\x1b[6~|\x1b[6~|\x1b[6~", 120, 40);
        CHECK(listener_scrolled.find("LISTENERS · 30") != string::npos);
        CHECK(listener_scrolled.find("10.0.0.25") != string::npos);
        snapshot many_cores = value;
        many_cores.cores_.assign(32, 12.5F);
        many_cores.pcore_count_ = 8;
        many_cores.ecore_count_ = 16;
        const string overview_many = ui::render_plaintext(many_cores, data, cfg, "overview", 140, 44);
        CHECK(overview_many.find("8P + 16E / 32T") != string::npos);
        CHECK(overview_many.find("Per-core history · 8P + 16E / 32T") != string::npos);
        CHECK(overview_many.find("C00") != string::npos);
        CHECK(overview_many.find("C15") != string::npos);
        CHECK(overview_many.find("C16") != string::npos);
        CHECK(overview_many.find("C31") != string::npos);
        snapshot missing_gpu = value;
        missing_gpu.gpus_[0].utilization_.reset();
        missing_gpu.gpus_[0].memory_used_.reset();
        missing_gpu.gpus_[0].memory_total_.reset();
        missing_gpu.gpus_[0].temperature_.reset();
        missing_gpu.gpus_[0].process_status_ = "Driver not loaded; telemetry unavailable";
        const string gpu_page = ui::render_plaintext(missing_gpu, data, cfg, "gpu", 140, 40);
        CHECK(gpu_page.find("Driver not loaded") != string::npos);
        const string theme_file = "/tmp/neflux-theme-unit.theme";
        CHECK(::neforce::filesystem::create_and_write(::neforce::path(theme_file),
                                                        "theme[main_bg]=\"#102030\"\ntheme[main_fg]=\"#ffffff\"\n"));
        config theme_cfg = cfg;
        theme_cfg.theme_file_ = theme_file;
        const string themed_svg = ui::render_svg(value, data, theme_cfg, "processes", "summary", 100, 30);
        CHECK(themed_svg.find("#102030") != string::npos);
        const string svg = ui::render_svg(value, data, cfg, "processes", "summary", 100, 30);
        CHECK(svg.find("<svg") != string::npos);
        CHECK(svg.find("</svg>") != string::npos);
    }

} // namespace

int main() {
    test_json();
    test_config();
    test_cli();
    test_cpu_info();
    test_crash_core_dump();
    test_tui_headless();
    test_util();
    test_plot();
    test_chart_zero_and_missing();
    test_filter();
    test_tags();
    test_network_attribution();
    test_discovery();
    test_history();
    test_neforce_system_components();
    test_battery_and_gpu();
    test_custom_layout();
    test_advanced();
    test_recording();
    test_parsers();
    test_ros_session();
    test_sampling_sources();
    test_selection_clipboard_state();
    test_tools_and_column_window();
    test_tab_layout_hits();
    test_popup_overlay();
    test_process_table_hits();
    test_chrome_background();
    test_small_terminal_chrome();
    test_resize_layout();
    test_render();
    if (g_failures != 0) {
        std::printf("%d test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All NeFlux tests passed\n");
    return 0;
}
