#include <NeFlux/common.hpp>
#include <NeFlux/ui/app.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/collect.hpp>
#include <NeFlux/diagnostics.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/filter.hpp>
#include <NeFlux/history.hpp>
#include <NeFlux/recording.hpp>
#include <NeFlux/ros.hpp>
#include <NeFlux/tags.hpp>
#include <NeFlux/ui/painter.hpp>
#include <NeFlux/ui/testing.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/async/atomic.hpp>
#include <NeForce/core/async/io_context.hpp>
#include <NeForce/core/async/mutex.hpp>
#include <NeForce/core/async/thread.hpp>
#include <NeForce/core/async/this_thread.hpp>
#include <NeForce/core/system/console.hpp>
#include <NeForce/core/system/environment.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/tui/application.hpp>
#include <NeForce/tui/component/component.hpp>
#include <NeForce/tui/dom/element.hpp>
#include <NeForce/tui/headless.hpp>
#include <NeForce/tui/screen.hpp>
#if NEFLUX_PLATFORM_WINDOWS
#    include <windows.h>
#    include <csignal>
#else
#    include <csignal>
#    include <sys/resource.h>
#    include <sys/syscall.h>
#    include <sys/types.h>
#    include <unistd.h>
#endif
namespace neforce::neflux::ui {

    namespace {

        NEFORCE_NODISCARD string align_cell(const char* text, int width, bool right = false);

        NEFORCE_NODISCARD string align_cell(const string& text, int width, bool right = false);

        NEFORCE_NODISCARD string align_cell(const char* text, int width, bool right) {
            return fit_display(string_view(text), width, right) + " ";
        }

        NEFORCE_NODISCARD string align_cell(const string& text, int width, bool right) {
            return fit_display(text.view(), width, right) + " ";
        }

        NEFORCE_NODISCARD vector<string> wrap_display(string_view text, int width) {
            vector<string> lines;
            if (width <= 0 || text.empty()) {
                return lines;
            }
            string remaining(text);
            while (!remaining.empty()) {
                size_t end = 0;
                while (end < remaining.size()) {
                    size_t next = end + 1;
                    while (next < remaining.size() && (static_cast<unsigned char>(remaining[next]) & 0xC0U) == 0x80U) {
                        ++next;
                    }
                    if (display_width(remaining.substr(0, next).view()) > width) {
                        break;
                    }
                    end = next;
                }
                if (end == 0) {
                    end = remaining.size() > 1 ? 2 : 1;
                }
                if (end < remaining.size()) {
                    const size_t space = remaining.rfind(' ', end - 1);
                    if (space != string::npos && space > 0 && space >= static_cast<size_t>(width / 2)) {
                        end = space;
                    }
                }
                string line = remaining.substr(0, end);
                line.trim();
                if (!line.empty()) {
                    lines.push_back(move(line));
                }
                remaining = remaining.substr(end);
                while (!remaining.empty() && remaining[0] == ' ') {
                    remaining = remaining.substr(1);
                }
            }
            if (lines.empty()) {
                lines.push_back(string());
            }
            return lines;
        }

        enum class view : byte_t {
            overview,
            processes,
            gpu,
            storage,
            network,
            system,
            containers,
            services,
            vms,
            history,
            events,
            explorer,
        };

        struct view_info {
            view value;
            string_view key;
            string_view name;
        };

        constexpr view_info views[] = {
                {view::overview, "1", "Overview"},
                {view::processes, "2", "Processes"},
                {view::gpu, "3", "GPU"},
                {view::storage, "4", "Storage"},
                {view::network, "5", "Network"},
                {view::system, "6", "System"},
                {view::containers, "7", "Containers"},
                {view::services, "8", "Services"},
                {view::vms, "9", "VMs"},
        };

        constexpr size_t view_count = sizeof(views) / sizeof(views[0]);

        string normalize_theme_color(string_view raw) {
            string text = trim(raw);
            if (text.size() >= 2 &&
                ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\''))) {
                text = text.substr(1, text.size() - 2);
            }
            const auto hex_digit = [](const char c) -> int {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f') {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F') {
                    return c - 'A' + 10;
                }
                return -1;
            };
            if (!text.empty() && text.front() == '#') {
                string digits = text.substr(1);
                if (digits.size() == 2) {
                    digits = string(6, digits[0]);
                } else if (digits.size() == 3) {
                    digits = format("{}{}{}{}{}{}", digits[0], digits[0], digits[1], digits[1], digits[2], digits[2]);
                } else if (digits.size() == 8) {
                    digits = digits.substr(0, 6);
                }
                if (digits.size() == 6) {
                    for (const char c: digits) {
                        if (hex_digit(c) < 0) {
                            return {};
                        }
                    }
                    return format("#{}", digits);
                }
            }
            const auto parts = split_whitespace(text);
            if (parts.size() == 3) {
                int channels[3] = {0, 0, 0};
                for (size_t i = 0; i < 3; ++i) {
                    const auto value = parse_u64(parts[i].view());
                    if (!value.has_value() || *value > 255) {
                        return {};
                    }
                    channels[i] = static_cast<int>(*value);
                }
                return format("#{:02x}{:02x}{:02x}", channels[0], channels[1], channels[2]);
            }
            return {};
        }

        bool merge_theme_file(const string& file_path, json& custom) {
            const auto text = read_text_file(file_path);
            if (!text.has_value()) {
                return false;
            }
            if (!custom.is_object()) {
                custom = json::object();
            }
            bool any = false;
            for (const auto& line: split_lines(*text)) {
                const auto key_begin = line.find("theme[");
                const auto key_end = line.find(']', key_begin + 6);
                const auto equals = line.find('=', key_end + 1);
                if (key_begin == string::npos || key_end == string::npos || equals == string::npos) {
                    continue;
                }
                const string key = line.substr(key_begin + 6, key_end - key_begin - 6);
                const string value = normalize_theme_color(line.view().substr(equals + 1));
                if (value.empty()) {
                    continue;
                }
                string target;
                if (key == "main_bg") {
                    target = "background";
                } else if (key == "main_fg") {
                    target = "text";
                } else if (key == "title") {
                    target = "blue";
                } else if (key == "hi_fg") {
                    target = "accent";
                } else if (key == "selected_bg") {
                    target = "selection";
                } else if (key == "inactive_fg") {
                    target = "muted";
                } else if (key == "proc_misc") {
                    target = "amber";
                } else if (key == "div_line" || key == "cpu_box" || key == "mem_box" || key == "net_box" ||
                           key == "proc_box") {
                    target = "border";
                } else if (key == "selected_fg") {
                    target = "text";
                } else if (key == "graph_text" || key == "meter_bg" || key == "followed_bg" || key == "followed_fg" ||
                           key == "proc_pause_bg" || key == "proc_follow_bg" || key == "proc_banner_bg" ||
                           key == "proc_banner_fg") {
                    target = key;
                } else if (key == "cpu_start" || key == "cpu_mid" || key == "cpu_end" || key == "temp_start" ||
                           key == "temp_mid" || key == "temp_end") {
                    target = key;
                } else if (key == "process_start") {
                    target = "proc_start";
                } else if (key == "process_mid") {
                    target = "proc_mid";
                } else if (key == "process_end") {
                    target = "proc_end";
                } else if (key == "download_start") {
                    target = "net_start";
                } else if (key == "download_mid") {
                    target = "net_mid";
                } else if (key == "download_end") {
                    target = "net_end";
                }
                if (!target.empty()) {
                    custom.set(target, json(value));
                    any = true;
                }
            }
            return any;
        }

        vector<string> theme_files() {
            vector<string> dirs;
            if (const auto xdg = environment::get("XDG_CONFIG_HOME"); !xdg.empty()) {
                dirs.push_back(xdg + "/neflux/themes");
            }
            if (const auto home = environment::get("HOME"); !home.empty()) {
                dirs.push_back(home + "/.config/neflux/themes");
            }
            if (const auto appdata = environment::get("APPDATA"); !appdata.empty()) {
                dirs.push_back(appdata + "/neflux/themes");
            }
            if (const auto local = environment::get("LOCALAPPDATA"); !local.empty()) {
                dirs.push_back(local + "/neflux/themes");
            }
            if (const auto config_dir = environment::config_directory(); !config_dir.empty()) {
                dirs.push_back(config_dir + "/themes");
            }
            dirs.push_back("/usr/local/share/btop/themes");
            dirs.push_back("/usr/share/btop/themes");
            if (const auto custom = environment::get("NEFLUX_THEME_DIR"); !custom.empty()) {
                dirs.push_back(custom);
            }
            vector<string> out;
            for (const auto& dir: dirs) {
                const auto folder = ::neforce::path(dir);
                if (!folder.exists() || !folder.is_directory()) {
                    continue;
                }
                for (const auto& child: folder.children()) {
                    const string file = child.str();
                    if (ends_with(file, ".theme")) {
                        out.push_back(file);
                    }
                }
            }
            neforce::tim_sort(out.begin(), out.end(), [](const string& a, const string& b) { return a < b; });
            return out;
        }

        palette palette_from_config(const config& cfg) {
            json custom = cfg.custom_colors_;
            if (!cfg.theme_file_.empty()) {
                static_cast<void>(merge_theme_file(cfg.theme_file_, custom));
            }
            palette colors = make_palette(cfg.theme_, custom);
            if (cfg.no_color_) {
                colors = monochrome_palette(colors);
            }
            return colors;
        }

        string_view view_name(view value) noexcept {
            for (const auto& item: views) {
                if (item.value == value) {
                    return item.name;
                }
            }
            if (value == view::history) {
                return "History";
            }
            if (value == view::events) {
                return "Events";
            }
            if (value == view::explorer) {
                return "Explorer";
            }
            return "Overview";
        }

        size_t view_index(view value) noexcept {
            for (size_t i = 0; i < view_count; ++i) {
                if (views[i].value == value) {
                    return i;
                }
            }
            return 0;
        }

        struct tab_layout_entry {
            string label;
            int x = 0;
            int width = 0;
            size_t index = 0;
            bool overflow = false;
        };

        enum class sort_mode : byte_t {
            cpu,
            memory,
            gpu,
            vram,
            read,
            write,
            io,
            time,
            pid,
            name,
            user,
            nice,
            vms,
            rx,
            tx,
            read_total,
            write_total,
            state,
        };

        struct sort_info {
            string_view name;
            sort_mode mode;
            bool descending;
        };

        constexpr sort_info sorts[] = {
                {"CPU", sort_mode::cpu, true},
                {"RAM", sort_mode::memory, true},
                {"GPU", sort_mode::gpu, true},
                {"VRAM", sort_mode::vram, true},
                {"READ/s", sort_mode::read, true},
                {"WRITE/s", sort_mode::write, true},
                {"I/O", sort_mode::io, true},
                {"CPU TIME", sort_mode::time, true},
                {"PID", sort_mode::pid, false},
                {"NAME", sort_mode::name, false},
                {"USER", sort_mode::user, false},
                {"NICE", sort_mode::nice, false},
                {"VIRTUAL MEMORY", sort_mode::vms, true},
                {"NETWORK RX", sort_mode::rx, true},
                {"NETWORK TX", sort_mode::tx, true},
                {"TOTAL READ", sort_mode::read_total, true},
                {"TOTAL WRITE", sort_mode::write_total, true},
                {"STATE", sort_mode::state, false},
        };

        struct menu_entry {
            string label_;
            string key_;
            string action_;
        };

        enum class hit_action : byte_t {
            none,
            tab,
            process_sort,
            gpu_device,
            gpu_process_row,
            gpu_prev,
            gpu_next,
            interface_sort,
            connection_sort,
            process_row,
            network_interface_row,
            connection_row,
            storage_fs_row,
            storage_io_row,
            workload_row,
            explorer_row,
            history_row,
            event_row,
            footer_action,
            process_scrollbar,
            process_tree_toggle,
            view_overflow,
            history_scroll,
            events_scroll,
            explorer_scroll,
            connections_scroll,
            workload_scroll,
            settings_scroll,
        };

        struct hit_target {
            rect area_;
            hit_action action_ = hit_action::none;
            size_t index_ = 0;
            uint32_t pid_ = 0;
            uint64_t start_time_ = 0;
        };

        enum class popup_kind : byte_t {
            none,
            main_menu,
            help,
            settings,
            sort,
            view_menu,
            time_menu,
            commands,
            detail,
            info,
            prompt,
            confirm,
            signal_menu,
            tools,
            columns,
            theme_menu,
        };

        enum class prompt_action : byte_t {
            none,
            nice,
            affinity,
        };

        struct process_row {
            size_t index_ = 0;            ///< 快照内索引
            int depth_ = 0;               ///< 树深度
            uint32_t group_count_ = 0;    ///< 聚合进程数
            double group_cpu_ = 0.0;      ///< 聚合 CPU
            uint64_t group_memory_ = 0;   ///< 聚合内存
            bool tree_aggregate_ = false; ///< 树模式累计子进程
        };

        struct sampler {
            config cfg_;
            bool replay_ = false;
            bool read_only_ = false;
            atomic<bool> stop_{false};
            atomic<bool> need_gpu_processes_{false};
            atomic<bool> need_connections_{false};
            atomic<bool> need_containers_{false};
            atomic<bool> need_services_{false};
            atomic<bool> need_extras_{false};
            atomic<bool> discovery_wake_{false};
            thread worker_;
            thread discovery_worker_;
            mutex mutex_;
            snapshot pending_;
            bool has_pending_ = false;
            inventory inventory_;
            bool has_inventory_ = false;
            optional<string> error_;
            vector<snapshot> replay_data_;
            size_t replay_index_ = 0;
            size_t replay_limit_ = 0;
            int64_t next_replay_ms_ = 0;
            bool replay_finished_ = false;

            sampler(config cfg, optional<string> replay, bool read_only, bool full_sampling) :
            cfg_(move(cfg)),
            replay_(replay.has_value()),
            read_only_(read_only) {
                if (full_sampling) {
                    need_gpu_processes_ = true;
                    need_connections_ = true;
                    need_containers_ = true;
                    need_services_ = true;
                    need_extras_ = true;
                }
                if (replay.has_value()) {
                    replay_data_ = replay_file(*replay);
                    replay_limit_ = replay_data_.size();
                    if (!replay_data_.empty()) {
                        pending_ = replay_data_[0];
                        has_pending_ = true;
                        inventory_ = replay_data_[0].inventory_;
                        has_inventory_ = true;
                    }
                    next_replay_ms_ = monotonic_ms() + static_cast<int64_t>(cfg_.interval_ms_);
                } else {
                    worker_ = thread([this] { run_collector(); });
                    discovery_worker_ = thread([this] { run_discovery(); });
                }
            }

            ~sampler() {
                stop_ = true;
                if (worker_.joinable()) {
                    worker_.join();
                }
                if (discovery_worker_.joinable()) {
                    discovery_worker_.join();
                }
            }

            static vector<snapshot> replay_file(const string& file_path) {
                vector<snapshot> data;
                const auto samples = replay(file_path);
                for (const auto& item: samples) {
                    data.push_back(item);
                }
                return data;
            }

            void run_collector() {
                try {
                    collector instance(cfg_.collect_gpu_);
                    int64_t deadline = monotonic_ms();
                    while (!stop_) {
                        auto sample =
                                instance.collect_with_processes(cfg_, true, need_gpu_processes_, need_connections_);
                        {
                            unique_lock<mutex> guard(mutex_);
                            pending_ = move(sample);
                            has_pending_ = true;
                        }
                        deadline += static_cast<int64_t>(cfg_.interval_ms_);
                        while (!stop_ && monotonic_ms() < deadline) {
                            neforce::this_thread::sleep_for_ms(20);
                        }
                    }
                } catch (const neforce::exception& failure) {
                    unique_lock<mutex> guard(mutex_);
                    error_ = failure.what();
                }
            }

            void run_discovery() {
                inventory_rates rates;
                while (!stop_) {
                    const discovery_sources sources{need_containers_, need_services_, need_extras_};
                    if (sources.containers_ || sources.services_ || sources.extras_) {
                        try {
                            auto found = discover_with_sources(cfg_, sources);
                            rates.apply(found);
                            unique_lock<mutex> guard(mutex_);
                            inventory_ = move(found);
                            has_inventory_ = true;
                        } catch (const neforce::exception& failure) {
                            unique_lock<mutex> guard(mutex_);
                            error_ = failure.what();
                        }
                    }
                    discovery_wake_ = false;
                    for (int step = 0; step < 50 && !stop_; ++step) {
                        if (discovery_wake_) {
                            break;
                        }
                        sleep_ms(100);
                    }
                }
            }

            void activate(view target) {
                if (target == view::processes || target == view::gpu || target == view::containers ||
                    target == view::services || target == view::history || target == view::events) {
                    need_gpu_processes_ = true;
                }
                if (target == view::network) {
                    need_connections_ = true;
                    need_extras_ = true;
                }
                if (target == view::containers || target == view::vms) {
                    need_containers_ = true;
                }
                if (target == view::services) {
                    need_services_ = true;
                }
                if (target == view::storage || target == view::system) {
                    need_extras_ = true;
                }
                discovery_wake_ = true;
            }

            bool take_snapshot(snapshot& out) {
                if (replay_) {
                    if (replay_finished_) {
                        return false;
                    }
                    if (monotonic_ms() < next_replay_ms_) {
                        return false;
                    }
                    next_replay_ms_ = monotonic_ms() + static_cast<int64_t>(cfg_.interval_ms_);
                    if (replay_index_ + 1 < replay_data_.size()) {
                        ++replay_index_;
                    } else {
                        replay_finished_ = true;
                    }
                    out = replay_data_[replay_index_];
                    return true;
                }
                unique_lock<mutex> guard(mutex_);
                if (!has_pending_) {
                    return false;
                }
                out = move(pending_);
                has_pending_ = false;
                return true;
            }

            bool take_inventory(inventory& out) {
                if (replay_) {
                    return false;
                }
                unique_lock<mutex> guard(mutex_);
                if (!has_inventory_) {
                    return false;
                }
                out = inventory_;
                has_inventory_ = false;
                return true;
            }

            optional<string> take_error() {
                unique_lock<mutex> guard(mutex_);
                auto value = error_;
                error_.reset();
                return value;
            }

            void seek(int delta) {
                if (!replay_ || replay_data_.empty()) {
                    return;
                }
                int64_t target = static_cast<int64_t>(replay_index_) + delta;
                target = neforce::max(static_cast<int64_t>(0),
                                      neforce::min(static_cast<int64_t>(replay_data_.size()) - 1, target));
                replay_index_ = static_cast<size_t>(target);
                replay_finished_ = false;
                unique_lock<mutex> guard(mutex_);
                pending_ = replay_data_[replay_index_];
                has_pending_ = true;
                inventory_ = pending_.inventory_;
                has_inventory_ = true;
                next_replay_ms_ = monotonic_ms() + static_cast<int64_t>(cfg_.interval_ms_);
            }

            NEFORCE_NODISCARD size_t replay_index() const noexcept { return replay_index_; }
            NEFORCE_NODISCARD size_t replay_size() const noexcept { return replay_data_.size(); }
        };

        view parse_view(const string& name);

        class flux_component final : public tui::component<> {
        public:
            flux_component(sampler* source, config cfg, string config_path, cli_args args) :
            sampler_(source),
            cfg_(move(cfg)),
            config_path_(move(config_path)),
            args_(move(args)) {
                replay_ = source != nullptr && source->replay_;
                tree_ = cfg_.tree_;
                auto_collapse_done_ = false;
            }

            void setup() override {
                tick_ = &create_state<int>(0);
                sort_index_ = neforce::min(cfg_.sort_index_, sizeof(sorts) / sizeof(sorts[0]) - 1);
                reverse_ = cfg_.reverse_sort_;
                tree_ = cfg_.tree_;
                configure_theme();
                if (!seeded_) {
                    if (args_.view_.has_value()) {
                        current_ = parse_view(*args_.view_);
                    } else if (cfg_.view_ < static_cast<int>(view_count)) {
                        current_ = views[cfg_.view_].value;
                    } else if (cfg_.view_ == 9) {
                        current_ = view::explorer;
                    } else if (cfg_.view_ == 10) {
                        current_ = view::history;
                    } else if (cfg_.view_ == 11) {
                        current_ = view::events;
                    }
                }
                if (args_.gpu_panel_ == "device") {
                    gpu_section_ = 2;
                } else if (args_.gpu_panel_ == "history") {
                    gpu_section_ = 1;
                }
                if (sampler_ != nullptr && !replay_) {
                    sampler_->activate(current_);
                }
                schedule_poll();
            }

            void on_terminal_resize(const int width, const int height) override {
                terminal_width_ = width;
                terminal_height_ = height;
            }

            tui::element render() override {
                auto content = build_content();
                auto popup = build_popup();
                auto base = tui::element::canvas([this](tui::canvas_painter& canvas) { draw_background(canvas); },
                                                 make_canvas_style());
                vector<tui::element> layers;
                layers.push_back(move(base));
                layers.push_back(move(content));
                layers.push_back(make_header_element());
                layers.push_back(make_tabs_element());
                layers.push_back(make_context_element());
                layers.push_back(make_footer_element());
                if (popup_ != popup_kind::none) {
                    layers.push_back(move(popup));
                }
                return tui::element::zstack(move(layers));
            }

            void seed_terminal_size(const int width, const int height) {
                terminal_width_ = width;
                terminal_height_ = height;
            }

            void seed_history(const vector<snapshot>& values, inventory data, view target) {
                if (values.empty()) {
                    seed(snapshot{}, move(data), target);
                    return;
                }
                for (const auto& value: values) {
                    snap_ = value;
                    if (!value.inventory_.containers_.empty() || !value.inventory_.services_.empty()) {
                        snap_.apply_inventory(value.inventory_);
                    }
                    history_.ingest(snap_, cfg_);
                }
                current_ = target;
                seeded_ = true;
                inventory_ = move(data);
                status_.clear();
                apply_tree_auto_collapse();
            }

            void seed(snapshot value, inventory data, view target) {
                snap_ = move(value);
                inventory_ = move(data);
                current_ = target;
                seeded_ = true;
                status_.clear();
                apply_tree_auto_collapse();
                if (replay_) {
                    inventory_ = snap_.inventory_;
                }
                history_.ingest(snap_, cfg_);
                if (!snap_.gpus_.empty() && selected_gpu_ >= snap_.gpus_.size()) {
                    selected_gpu_ = snap_.gpus_.size() - 1;
                }
            }

            bool on_key(const tui::key_event& event) override {
                if (popup_ != popup_kind::none) {
                    return handle_popup_key(event);
                }
                if (editing_filter_) {
                    return handle_filter_key(event);
                }
                return handle_global_key(event);
            }

            bool on_mouse(const tui::mouse_event& event) override { return handle_mouse(event); }

        private:
            sampler* sampler_ = nullptr;
            config cfg_;
            string config_path_;
            cli_args args_;
            palette colors_{};
            snapshot snap_;
            inventory inventory_;
            history history_;
            process_history process_history_;                        ///< 当前选中进程的历史
            optional<pair<uint32_t, uint64_t>> process_history_key_; ///< 历史对应的 PID/启动时间
            view current_ = view::overview;
            size_t selected_ = 0;
            optional<pair<uint32_t, uint64_t>> selected_process_;
            optional<pair<string, string>> selected_service_;
            size_t table_offset_ = 0;
            size_t sort_index_ = 0;
            bool reverse_ = false;
            bool tree_ = false;
            bool auto_collapse_done_ = false;
            int terminal_width_ = 0;
            int terminal_height_ = 0;
            bool process_paused_ = false;
            bool follow_process_ = false;
            bool process_side_panel_ = false;
            bool workload_side_panel_ = false;
            bool dragging_process_scrollbar_ = false;
            bool free_scroll_ = false;
            bool storage_device_auto_ = true;
            hit_action dragging_scroll_action_ = hit_action::none;
            rect dragging_scroll_rect_{};
            set<uint32_t> collapsed_;
            set<pair<uint32_t, uint64_t>> marked_;
            string filter_;
            bool editing_filter_ = false;
            string filter_before_;
            size_t filter_cursor_ = 0;
            string status_ = "Discovering host…";
            size_t selected_gpu_ = 0;
            size_t selected_io_ = 0;
            size_t storage_section_ = 0;
            size_t gpu_section_ = 0;
            size_t network_section_ = 0;
            size_t selected_connection_ = 0;
            size_t connection_offset_ = 0;
            size_t network_sort_ = 0;
            size_t interface_sort_ = 0;
            rect network_connections_rect_{};
            rect network_interfaces_rect_{};
            rect workload_rect_{};
            rect listeners_rect_{};
            size_t listener_offset_ = 0;
            rect process_table_rect_{};
            rect explorer_rect_{};
            rect storage_filesystems_rect_{};
            rect storage_io_rect_{};
            popup_kind popup_ = popup_kind::none;
            popup_kind popup_parent_ = popup_kind::none;
            string popup_title_;
            vector<menu_entry> popup_entries_;
            size_t popup_selected_ = 0;
            size_t popup_scroll_ = 0;
            vector<hit_target> hits_;
            rect popup_rect_{};
            int popup_first_row_ = 0;
            size_t popup_visible_rows_ = 0;
            vector<string> detail_lines_;
            size_t detail_scroll_ = 0;
            size_t settings_index_ = 0;
            size_t setting_choice_row_ = 0;
            size_t commands_index_ = 0;
            string command_text_;
            bool command_editing_ = false;
            size_t command_cursor_ = 0;
            int64_t last_escape_ms_ = 0;
            size_t column_offset_ = 0;
            bool replay_ = false;
            tui::state<int>* tick_ = nullptr;
            rect body_{};
            cell_grid content_grid_;
            cell_grid popup_grid_;
            vector<painter::pane> content_panes_;
            rect footer_{};
            rect tabs_{};
            int tab_start_ = 0;
            int64_t last_click_ms_ = 0;
            int last_click_row_ = -1;
            uint32_t last_click_pid_ = 0;
            uint64_t last_click_start_time_ = 0;
            bool quit_ = false;
            bool detail_open_ = false;
            bool main_quit_ = false;
            set<pair<uint32_t, uint64_t>> marked_processes_;
            prompt_action prompt_action_ = prompt_action::none;
            string prompt_text_;
            size_t prompt_cursor_ = 0;
            string confirm_message_;
            int pending_signal_ = 0;
            string pending_title_;
            string pending_workload_action_;
            map<string, uint64_t> pending_alerts_;
            set<string> active_alerts_;
            vector<event> alert_events_;
            optional<ros_session> ros_session_;
            bool seeded_ = false;
            bool gpu_only_ = false;
            bool own_only_ = false;
            string ros_kind_;
            vector<string> ros_entries_;

            tui::element make_header_element() const {
                if (terminal_width_ < 60 || terminal_height_ < 18) {
                    return tui::element::empty();
                }
                vector<tui::text_span> spans;
                tui::style title_style;
                title_style.fg = colors_.mint_;
                title_style.bold = true;
                spans.push_back({" nexus-flux", title_style, {}});

                tui::style host_style;
                host_style.fg = colors_.text_;
                spans.push_back({format("· {}", snap_.hostname_), host_style, {}});

                tui::style muted_style;
                muted_style.fg = colors_.muted_;
                if (snap_.os_.empty()) {
                    const int column = 22;
                    int used = 1 + 11 + 2 + static_cast<int>(display_width(snap_.hostname_.view()));
                    if (used < column) {
                        spans.push_back({string(static_cast<size_t>(column - used), ' '), {}, {}});
                    }
                    spans.push_back({"· discovering", muted_style, {}});
                }

                string right = format("[{}] · [-]{:.2f}s[+] · up {}", time_label(cfg_.time_window_seconds_),
                                      static_cast<double>(cfg_.interval_ms_) / 1000.0, duration(snap_.uptime_));
                if (replay_) {
                    right = format("REPLAY {}/{} · {}", sampler_->replay_index() + 1, sampler_->replay_size(), right);
                }

                tui::box_props props;
                props.dir = tui::style::direction::row;
                props.width = tui::style::size_hint{tui::style::size_hint::fill, 1};
                props.height = tui::style::size_hint{tui::style::size_hint::fixed, 1};
                return tui::element::hbox({tui::element::rich_text(move(spans)), tui::element::spacer(1),
                                           tui::element::text(move(right), muted_style)},
                                          props);
            }

            NEFORCE_NODISCARD vector<tab_layout_entry> tab_layout() const {
                vector<tab_layout_entry> layout;
                if (terminal_width_ < 60 || terminal_height_ < 18) {
                    return layout;
                }
                int x = 0;
                for (size_t index = 0; index < sizeof(views) / sizeof(views[0]); ++index) {
                    string label = format("[{}] {}", views[index].key, views[index].name);
                    int width = display_width(label.view()) + 2;
                    if (x + width > terminal_width_ && index >= 8) {
                        label = format("[{}]", views[index].key);
                        width = display_width(label.view()) + 2;
                    }
                    if (x + width > terminal_width_ - 8) {
                        break;
                    }
                    tab_layout_entry entry;
                    entry.label = move(label);
                    entry.x = x;
                    entry.width = width;
                    entry.index = index;
                    layout.push_back(move(entry));
                    x += width;
                }
                if (x + 4 <= terminal_width_) {
                    tab_layout_entry entry;
                    entry.label = "[+]";
                    entry.x = x;
                    entry.width = 4;
                    entry.overflow = true;
                    layout.push_back(move(entry));
                }
                return layout;
            }

            tui::element make_tabs_element() const {
                const vector<tab_layout_entry> layout = tab_layout();
                if (layout.empty()) {
                    return tui::element::empty();
                }
                vector<tui::text_span> spans;
                const size_t active = view_index(current_);
                for (const auto& entry: layout) {
                    if (entry.overflow) {
                        tui::style style;
                        style.fg = colors_.muted_;
                        style.bold = true;
                        spans.push_back({entry.label, style, {}});
                        continue;
                    }
                    const bool selected = entry.index == active;
                    tui::style style;
                    style.fg = selected ? colors_.mint_ : colors_.muted_;
                    if (selected) {
                        style.bg = colors_.selection_;
                    }
                    style.bold = selected;
                    spans.push_back({format(" {} ", entry.label), style, {}});
                }
                tui::style base;
                base.width = tui::style::size_hint{tui::style::size_hint::fill, 1};
                base.height = tui::style::size_hint{tui::style::size_hint::fixed, 1};
                struct tui::style::margin pad;
                pad.top = 1;
                base.margin = pad;
                return tui::element::rich_text(move(spans), base);
            }

            void record_tab_hits() {
                tabs_ = rect{0, 1, terminal_width_, 1};
                for (const auto& entry: tab_layout()) {
                    hits_.push_back({{entry.x, 1, entry.width, 1},
                                     entry.overflow ? hit_action::view_overflow : hit_action::tab,
                                     entry.index});
                }
            }

            tui::element make_footer_element() const {
                if (terminal_width_ < 60 || terminal_height_ < 18) {
                    return tui::element::empty();
                }
                struct action {
                    string_view key;
                    string_view label;
                };
                vector<action> actions{{"Esc", "Menu"},   {"F3", "Filter"}, {"F4", "Sort"}, {"F5", "View"},
                                       {"F6", "Tools"},   {"F7", "Logs"},   {"F1", "Help"}, {"F2", "Settings"},
                                       {":", "Commands"}, {"q", "Quit"}};
                if (terminal_width_ < 100) {
                    actions = {{"Esc", "Menu"}, {"F3", "Filter"}, {"F4", "Sort"}, {"F6", "Tools"},
                               {"F7", "Logs"},  {"F1", "Help"},   {"q", "Quit"}};
                }
                if (replay_) {
                    actions.push_back({", / .", "Seek"});
                }
                if (current_ == view::processes) {
                    actions.push_back({"Shift+←/→", "Columns"});
                }
                vector<tui::text_span> spans;
                tui::style key_style;
                key_style.fg = colors_.blue_;
                key_style.bold = true;
                tui::style label_style;
                label_style.fg = colors_.muted_;
                int x = 1;
                for (const auto& item: actions) {
                    const int width = static_cast<int>(item.key.size() + item.label.size() + 3);
                    if (x + width >= terminal_width_) {
                        break;
                    }
                    spans.push_back({string(item.key), key_style, {}});
                    spans.push_back({" ", label_style, {}});
                    spans.push_back({string(item.label), label_style, {}});
                    spans.push_back({"  ", {}, {}});
                    x += width;
                }
                tui::style right_style;
                right_style.fg = colors_.border_;
                const string right = format("{} × {} · {}{}", terminal_width_, terminal_height_, view_name(current_),
                                            replay_ ? " · REPLAY" : (args_.read_only_ ? " · READ-ONLY" : ""));
                tui::box_props props;
                props.dir = tui::style::direction::row;
                props.width = tui::style::size_hint{tui::style::size_hint::fill, 1};
                props.height = tui::style::size_hint{tui::style::size_hint::fixed, 1};
                auto row = tui::element::hbox({tui::element::rich_text(move(spans)), tui::element::spacer(1),
                                               tui::element::text(move(right), right_style)},
                                              props);
                tui::box_props outer;
                outer.dir = tui::style::direction::column;
                outer.width = tui::style::size_hint{tui::style::size_hint::fill, 1};
                outer.height = tui::style::size_hint{tui::style::size_hint::fill, 1};
                return tui::element::vbox({tui::element::spacer(1), move(row)}, outer);
            }

            string context_line() const {
                if (editing_filter_) {
                    return format(" Filter: {}█", filter_);
                }
                if (!status_.empty()) {
                    return " " + status_;
                }
                if (!filter_.empty()) {
                    const auto parsed = query::parse(filter_);
                    return parsed.error().has_value() ? format(" Invalid filter: {}", *parsed.error())
                                                      : format(" Filter: {}", filter_);
                }
                return format(" {}", summary(snap_, cfg_));
            }

            tui::style make_context_style() const {
                tui::style value;
                value.width = tui::style::size_hint{tui::style::size_hint::fill, 1};
                value.height = tui::style::size_hint{tui::style::size_hint::fixed, 1};
                struct tui::style::margin pad;
                pad.top = 2;
                value.margin = pad;
                value.fg = colors_.muted_;
                return value;
            }

            tui::element make_context_element() const {
                if (terminal_width_ < 60 || terminal_height_ < 18) {
                    return tui::element::empty();
                }
                return tui::element::text(context_line(), make_context_style());
            }

            static tui::style make_canvas_style() {
                tui::style style;
                style.width = tui::style::size_hint{tui::style::size_hint::fill, 1};
                style.height = tui::style::size_hint{tui::style::size_hint::fill, 1};
                return style;
            }

            void configure_theme() {
                colors_ = palette_from_config(cfg_);
                tui::theme theme = tui::dark_theme;
                theme.bg = colors_.bg_;
                theme.fg = colors_.text_;
                theme.primary = colors_.mint_;
                theme.secondary = colors_.blue_;
                theme.border = colors_.border_;
                theme.muted = colors_.muted_;
            }

            void schedule_poll() {
                if (sampler_ == nullptr) {
                    return;
                }
                ctx_->schedule_timer(40, [this] {
                    poll();
                    if (!quit_) {
                        schedule_poll();
                    }
                });
            }

            void poll() {
                if (quit_ || sampler_ == nullptr) {
                    return;
                }
                snapshot incoming;
                bool changed = false;
                while (sampler_->take_snapshot(incoming)) {
                    apply_snapshot(move(incoming));
                    changed = true;
                }
                inventory found;
                if (sampler_->take_inventory(found)) {
                    inventory_ = move(found);
                    snap_.apply_inventory(inventory_);
                    annotate(snap_);
                    changed = true;
                }
                if (const auto error = sampler_->take_error(); error.has_value()) {
                    status_ = *error;
                    changed = true;
                }
                if (changed) {
                    if (selected_process_.has_value()) {
                        bool present = false;
                        for (const auto& item: snap_.processes_) {
                            if (item.pid_ == selected_process_->first &&
                                item.start_time_ == selected_process_->second) {
                                present = true;
                                break;
                            }
                        }
                        if (!present) {
                            selected_process_.reset();
                            process_history_key_.reset();
                            detail_open_ = false;
                            process_history_ = process_history{};
                        }
                    }
                    *tick_ = tick_->value() + 1;
                }
            }

            void refresh_alerts() {
                vector<string> conditions;
                if (snap_.cpu_percent_ >= cfg_.cpu_warning_) {
                    conditions.push_back(format("CPU {:.1f}% >= {:.0f}%", snap_.cpu_percent_, cfg_.cpu_warning_));
                }
                if (snap_.memory_.total_ > 0) {
                    const double used = percent(snap_.memory_.used_, snap_.memory_.total_);
                    if (used >= cfg_.memory_warning_) {
                        conditions.push_back(format("Memory {:.1f}% >= {:.0f}%", used, cfg_.memory_warning_));
                    }
                }
                for (const auto& device: snap_.gpus_) {
                    if (!device.temperature_.has_value() || *device.temperature_ < cfg_.gpu_temperature_warning_) {
                        continue;
                    }
                    conditions.push_back(format("GPU {} {}°C >= {}°C", device.id_, *device.temperature_,
                                                cfg_.gpu_temperature_warning_));
                }
                const auto now = static_cast<uint64_t>(monotonic_ms());
                const uint64_t hold_ms = cfg_.alert_hold_seconds_ * 1000;
                map<string, uint64_t> pending;
                set<string> active;
                for (const auto& condition: conditions) {
                    const auto existing = pending_alerts_.find(condition);
                    if (active_alerts_.count(condition) != 0) {
                        active.insert(condition);
                        pending[condition] = existing == pending_alerts_.end() ? now : existing->second;
                        continue;
                    }
                    uint64_t since = now;
                    if (existing == pending_alerts_.end()) {
                        pending_alerts_[condition] = now;
                    } else {
                        since = existing->second;
                    }
                    pending[condition] = since;
                    if (hold_ms == 0 || now - since >= hold_ms) {
                        active.insert(condition);
                        alert_events_.push_back(event{unix_time(), condition, true});
                    }
                }
                for (const auto& condition: active_alerts_) {
                    if (active.count(condition) == 0) {
                        alert_events_.push_back(event{unix_time(), condition, false});
                    }
                }
                pending_alerts_ = move(pending);
                active_alerts_ = move(active);
                snap_.alerts_.clear();
                for (const auto& condition: active_alerts_) {
                    snap_.alerts_.push_back(condition);
                }
                if (alert_events_.size() > 2000) {
                    alert_events_.erase(alert_events_.begin(),
                                        alert_events_.begin() + static_cast<ptrdiff_t>(alert_events_.size() - 2000));
                }
            }

            void apply_snapshot(snapshot incoming) {
                incoming.alerts_.clear();
                if (process_paused_ && !snap_.processes_.empty()) {
                    incoming.processes_ = snap_.processes_;
                } else if (cfg_.display_.cpu_lazy_sort_) {
                    for (auto& item: incoming.processes_) {
                        for (const auto& previous: snap_.processes_) {
                            if (item.pid_ == previous.pid_ && item.start_time_ == previous.start_time_) {
                                item.cpu_ = static_cast<float>(previous.cpu_ * 0.7 + item.cpu_ * 0.3);
                                break;
                            }
                        }
                    }
                }
                snap_ = move(incoming);
                if (tree_ && !auto_collapse_done_) {
                    apply_tree_auto_collapse();
                }
                if (!inventory_.containers_.empty() || !inventory_.services_.empty() ||
                    !inventory_.listeners_.empty() || !inventory_.extra_.empty()) {
                    snap_.apply_inventory(inventory_);
                }
                annotate(snap_);
                refresh_alerts();
                history_.ingest(snap_, cfg_);
                if (selected_process_.has_value()) {
                    for (const auto& item: snap_.processes_) {
                        if (item.pid_ == selected_process_->first && item.start_time_ == selected_process_->second) {
                            if (!process_history_key_.has_value() ||
                                process_history_key_->first != selected_process_->first ||
                                process_history_key_->second != selected_process_->second) {
                                process_history_key_ = selected_process_;
                                process_history_ = process_history{};
                            }
                            process_history_.push(item, item.gpu_memory_, item.gpu_percent_, cfg_.history_);
                            break;
                        }
                    }
                }
                if (follow_process_ && selected_process_.has_value()) {
                    const auto rows = process_rows();
                    for (size_t index = 0; index < rows.size(); ++index) {
                        const auto& item = snap_.processes_[rows[index].index_];
                        if (item.pid_ == selected_process_->first && item.start_time_ == selected_process_->second) {
                            selected_ = index;
                            break;
                        }
                    }
                }
                if (snap_.timestamp_ != 0) {
                    status_.clear();
                }
                if (snap_.gpus_.size() <= selected_gpu_) {
                    selected_gpu_ = snap_.gpus_.empty() ? 0 : snap_.gpus_.size() - 1;
                }
            }

            NEFORCE_NODISCARD bool visible_process(const process& value) const {
                if (value.thread_ && !cfg_.show_threads_) {
                    return false;
                }
                if (value.kernel_thread_ && !cfg_.show_kernel_threads_) {
                    return false;
                }
                if (gpu_only_ && !value.gpu_percent_.has_value() && !value.gpu_memory_.has_value() &&
                    value.gpu_ids_.empty()) {
                    return false;
                }
                if (own_only_) {
                    string current_user = environment::get("USER");
                    if (current_user.empty()) {
                        current_user = environment::get("USERNAME");
                    }
                    if (!current_user.empty() && value.user_ != current_user) {
                        return false;
                    }
                }
                if (!filter_.empty()) {
                    const auto parsed = query::parse(filter_);
                    if (!parsed.matches(value)) {
                        return false;
                    }
                }
                return true;
            }

            NEFORCE_NODISCARD bool compare_process(const process& left, const process& right) const {
                const auto mode = sorts[neforce::min(sort_index_, sizeof(sorts) / sizeof(sorts[0]) - 1)].mode;
                bool result = false;
                switch (mode) {
                    case sort_mode::cpu:
                        result = left.cpu_ > right.cpu_;
                        break;
                    case sort_mode::memory:
                        result = left.memory_ > right.memory_;
                        break;
                    case sort_mode::gpu:
                        result = left.gpu_percent_.has_value() &&
                                 (!right.gpu_percent_.has_value() || *left.gpu_percent_ > *right.gpu_percent_);
                        break;
                    case sort_mode::vram:
                        result = left.gpu_memory_.has_value() &&
                                 (!right.gpu_memory_.has_value() || *left.gpu_memory_ > *right.gpu_memory_);
                        break;
                    case sort_mode::read:
                        result = left.read_per_sec_ > right.read_per_sec_;
                        break;
                    case sort_mode::write:
                        result = left.write_per_sec_ > right.write_per_sec_;
                        break;
                    case sort_mode::io:
                        result = left.read_per_sec_ + left.write_per_sec_ > right.read_per_sec_ + right.write_per_sec_;
                        break;
                    case sort_mode::time:
                        result = left.cpu_time_ms_ > right.cpu_time_ms_;
                        break;
                    case sort_mode::pid:
                        result = left.pid_ < right.pid_;
                        break;
                    case sort_mode::name:
                        result = left.name_ < right.name_;
                        break;
                    case sort_mode::user:
                        result = left.user_ < right.user_;
                        break;
                    case sort_mode::nice:
                        result = left.nice_.has_value() && (!right.nice_.has_value() || *left.nice_ < *right.nice_);
                        break;
                    case sort_mode::vms:
                        result = left.virtual_memory_ > right.virtual_memory_;
                        break;
                    case sort_mode::rx:
                        result = left.rx_per_sec_.has_value() &&
                                 (!right.rx_per_sec_.has_value() || *left.rx_per_sec_ > *right.rx_per_sec_);
                        break;
                    case sort_mode::tx:
                        result = left.tx_per_sec_.has_value() &&
                                 (!right.tx_per_sec_.has_value() || *left.tx_per_sec_ > *right.tx_per_sec_);
                        break;
                    case sort_mode::read_total:
                        result = left.read_total_ > right.read_total_;
                        break;
                    case sort_mode::write_total:
                        result = left.write_total_ > right.write_total_;
                        break;
                    case sort_mode::state:
                        result = left.state_ < right.state_;
                        break;
                }
                if (left.pid_ == right.pid_) {
                    return false;
                }
                if (left.cpu_ == right.cpu_ && mode == sort_mode::cpu) {
                    return left.pid_ < right.pid_;
                }
                return reverse_ ? !result : result;
            }

            NEFORCE_NODISCARD vector<process_row> process_rows() const {
                vector<size_t> indices;
                for (size_t index = 0; index < snap_.processes_.size(); ++index) {
                    if (visible_process(snap_.processes_[index])) {
                        indices.push_back(index);
                    }
                }
                vector<size_t> order(indices.size());
                for (size_t i = 0; i < order.size(); ++i) {
                    order[i] = i;
                }
                neforce::tim_sort(order.begin(), order.end(), [&](size_t left, size_t right) {
                    const auto& a = snap_.processes_[indices[left]];
                    const auto& b = snap_.processes_[indices[right]];
                    const bool result = compare_process(a, b);
                    if (result) {
                        return true;
                    }
                    if (compare_process(b, a)) {
                        return false;
                    }
                    return a.pid_ < b.pid_;
                });
                vector<process_row> rows;
                if (!tree_) {
                    if (cfg_.display_.group_processes_) {
                        map<string, vector<size_t>> groups;
                        for (const size_t index: order) {
                            groups[snap_.processes_[indices[index]].name_].push_back(indices[index]);
                        }
                        struct grouped {
                            process_row row_;
                            process aggregate_;
                        };
                        vector<grouped> items;
                        for (const auto& entry: groups) {
                            size_t representative = entry.second[0];
                            double cpu = 0.0;
                            uint64_t memory = 0;
                            for (const size_t index: entry.second) {
                                const auto& item = snap_.processes_[index];
                                cpu += item.cpu_;
                                memory += item.memory_;
                                if (item.cpu_ > snap_.processes_[representative].cpu_) {
                                    representative = index;
                                }
                            }
                            process aggregate = snap_.processes_[representative];
                            aggregate.cpu_ = static_cast<float>(cpu);
                            aggregate.memory_ = memory;
                            grouped value;
                            value.row_ = process_row{representative, 0, static_cast<uint32_t>(entry.second.size()), cpu,
                                                     memory};
                            value.aggregate_ = move(aggregate);
                            items.push_back(move(value));
                        }
                        neforce::tim_sort(items.begin(), items.end(), [&](const grouped& left, const grouped& right) {
                            const bool result = compare_process(left.aggregate_, right.aggregate_);
                            if (result) {
                                return true;
                            }
                            if (compare_process(right.aggregate_, left.aggregate_)) {
                                return false;
                            }
                            return left.aggregate_.pid_ < right.aggregate_.pid_;
                        });
                        for (const auto& item: items) {
                            rows.push_back(item.row_);
                        }
                        return rows;
                    }
                    for (const size_t index: order) {
                        rows.push_back(process_row{indices[index], 0});
                    }
                    return rows;
                }
                map<uint32_t, vector<size_t>> children;
                set<uint32_t> present;
                for (const size_t index: indices) {
                    present.insert(snap_.processes_[index].pid_);
                }
                for (const size_t index: indices) {
                    const auto& item = snap_.processes_[index];
                    const uint32_t parent = item.parent_.has_value() ? *item.parent_ : 0;
                    children[present.count(parent) != 0 ? parent : 0].push_back(index);
                }
                for (auto& entry: children) {
                    neforce::tim_sort(entry.second.begin(), entry.second.end(), [&](size_t left, size_t right) {
                        return compare_process(snap_.processes_[left], snap_.processes_[right]);
                    });
                }
                set<uint32_t> visited;
                const auto walk = [&](auto&& self, uint32_t parent, int depth) -> void {
                    const auto found = children.find(parent);
                    if (found == children.end()) {
                        return;
                    }
                    for (const size_t index: found->second) {
                        const auto& item = snap_.processes_[index];
                        if (!visited.insert(item.pid_).second) {
                            continue;
                        }
                        rows.push_back(process_row{index, depth});
                        if (collapsed_.count(item.pid_) == 0) {
                            self(self, item.pid_, depth + 1);
                        }
                    }
                };
                walk(walk, 0, 0);
                for (const size_t index: indices) {
                    if (visited.count(snap_.processes_[index].pid_) == 0) {
                        rows.push_back(process_row{index, 0});
                    }
                }
                if (cfg_.tree_aggregate_) {
                    struct subtree_total {
                        double cpu = 0.0;
                        uint64_t memory = 0;
                        uint32_t count = 0;
                    };
                    const auto subtotal = [&](auto&& self, uint32_t pid) -> subtree_total {
                        subtree_total total;
                        const auto found = children.find(pid);
                        if (found == children.end()) {
                            return total;
                        }
                        for (const size_t child: found->second) {
                            const auto& item = snap_.processes_[child];
                            ++total.count;
                            total.cpu += item.cpu_;
                            total.memory += item.memory_;
                            const auto child_total = self(self, item.pid_);
                            total.cpu += child_total.cpu;
                            total.memory += child_total.memory;
                            total.count += child_total.count;
                        }
                        return total;
                    };
                    for (auto& row: rows) {
                        const auto& item = snap_.processes_[row.index_];
                        const auto found = children.find(item.pid_);
                        if (found == children.end() || found->second.empty()) {
                            continue;
                        }
                        const auto total = subtotal(subtotal, item.pid_);
                        row.group_count_ = total.count + 1;
                        row.group_cpu_ = item.cpu_ + total.cpu;
                        row.group_memory_ = item.memory_ + total.memory;
                        row.tree_aggregate_ = true;
                    }
                }
                return rows;
            }

            void apply_tree_auto_collapse() {
                auto_collapse_done_ = true;
                if (!tree_ || cfg_.tree_auto_collapse_ == 0) {
                    return;
                }
                set<uint32_t> present;
                for (const auto& item: snap_.processes_) {
                    present.insert(item.pid_);
                }
                map<uint32_t, uint32_t> children;
                for (const auto& item: snap_.processes_) {
                    if (!item.parent_.has_value() || *item.parent_ == item.pid_) {
                        continue;
                    }
                    if (present.count(*item.parent_) != 0) {
                        ++children[*item.parent_];
                    }
                }
                for (const auto& entry: children) {
                    if (entry.first != 0 && entry.second >= cfg_.tree_auto_collapse_) {
                        collapsed_.insert(entry.first);
                    }
                }
            }

            NEFORCE_NODISCARD optional<process> selected_process_value() const {
                if (!selected_process_.has_value()) {
                    return none;
                }
                for (const auto& item: snap_.processes_) {
                    if (item.pid_ == selected_process_->first && item.start_time_ == selected_process_->second) {
                        return item;
                    }
                }
                return none;
            }

            void select_process(const process& item) {
                const pair<uint32_t, uint64_t> key{item.pid_, item.start_time_};
                selected_process_ = key;
                if (process_history_key_.has_value() && process_history_key_->first == key.first &&
                    process_history_key_->second == key.second) {
                    return;
                }
                process_history_key_ = key;
                process_history_ = process_history{};
                process_history_.push(item, item.gpu_memory_, item.gpu_percent_, cfg_.history_);
            }

            void remember_selection(const vector<process_row>& rows) {
                if (selected_ < rows.size()) {
                    select_process(snap_.processes_[rows[selected_].index_]);
                    follow_process_ = true;
                }
            }

            void move_selection(int delta, size_t count) {
                if (count == 0) {
                    selected_ = 0;
                    return;
                }
                const int64_t next = static_cast<int64_t>(selected_) + delta;
                selected_ = static_cast<size_t>(
                        neforce::max(static_cast<int64_t>(0), neforce::min(static_cast<int64_t>(count) - 1, next)));
            }

            void draw_background(tui::canvas_painter& canvas) {
                const int width = canvas.width();
                const int height = canvas.height();
                painter brush(canvas, colors_, cfg_.ascii_, cfg_.display_.transparent_background_,
                              cfg_.display_.graph_symbol_, cfg_.display_.rounded_corners_,
                              cfg_.display_.graph_gradient_);
                brush.clear(rect{0, 0, width, height}, colors_.bg_);
                if (width < 60 || height < 18) {
                    brush.text_center(0, height / 2 - 1, width, "nexus-flux", colors_.mint_);
                    brush.text_center(0, height / 2, width, "Resize to at least 60 × 18", colors_.text_);
                    brush.text_center(0, height / 2 + 1, width, "Recommended: 140 × 44", colors_.muted_);
                }
            }

            tui::element build_content() {
                hits_.clear();
                const int width = terminal_width_;
                const int height = terminal_height_;
                if (width < 60 || height < 18) {
                    return tui::element::empty();
                }
                record_tab_hits();
                footer_ = rect{0, height - 1, width, 1};
                body_ = rect{0, 3, width, height - 4};
                content_grid_.reset(rect{0, 0, width, height});
                painter brush(content_grid_, colors_, cfg_.ascii_, cfg_.display_.transparent_background_,
                              cfg_.display_.graph_symbol_, cfg_.display_.rounded_corners_,
                              cfg_.display_.graph_gradient_);
                brush.clear(rect{0, 0, width, height}, colors_.bg_);
                switch (current_) {
                    case view::overview:
                        draw_overview(brush, body_);
                        break;
                    case view::processes:
                        draw_processes(brush, body_);
                        break;
                    case view::gpu:
                        draw_gpu_page(brush, body_);
                        break;
                    case view::storage:
                        draw_storage_page(brush, body_);
                        break;
                    case view::network:
                        draw_network_page(brush, body_);
                        break;
                    case view::system:
                        draw_system_page(brush, body_);
                        break;
                    case view::containers:
                        draw_workloads(brush, body_, true);
                        break;
                    case view::services:
                        draw_workloads(brush, body_, false);
                        break;
                    case view::vms:
                        draw_virtual_machines(brush, body_);
                        break;
                    case view::history:
                        draw_history_page(brush, body_);
                        break;
                    case view::events:
                        draw_events_page(brush, body_);
                        break;
                    case view::explorer:
                        draw_explorer_page(brush, body_);
                        break;
                }
                content_panes_ = brush.panes();
                return make_content_element();
            }

            NEFORCE_NODISCARD vector<tui::text_span> make_row_spans(const cell_grid& grid, const int row,
                                                                    const int begin, const int end) const {
                vector<tui::text_span> spans;
                string run;
                grid_cell run_cell{};
                const auto push_run = [&spans, &run, &run_cell]() {
                    if (run.empty()) {
                        return;
                    }
                    tui::text_span span;
                    span.content = move(run);
                    span.style = make_cell_style(run_cell);
                    spans.push_back(move(span));
                    run = string{};
                };
                for (int column = begin; column <= end; ++column) {
                    const auto& cell = grid.at(column, row);
                    if (cell.continuation) {
                        continue;
                    }
                    if (run.empty() || !same_cell_style(run_cell, cell)) {
                        push_run();
                        run_cell = cell;
                    }
                    append_codepoint(run, cell.value);
                }
                push_run();
                return spans;
            }

            static bool same_cell_style(const grid_cell& left, const grid_cell& right) noexcept {
                return left.bold == right.bold && same_color(left.fg, right.fg) && same_color(left.bg, right.bg);
            }

            static bool same_color(const color& left, const color& right) noexcept {
                return left.A() == right.A() && left.R() == right.R() && left.G() == right.G() && left.B() == right.B();
            }

            static tui::style make_cell_style(const grid_cell& cell) {
                tui::style style;
                style.fg = cell.fg;
                if (cell.bg.A() != 0) {
                    style.bg = cell.bg;
                }
                if (cell.bold) {
                    style.bold = true;
                }
                return style;
            }

            static void append_codepoint(string& target, const codepoint& value) {
                const char32_t raw = static_cast<char32_t>(value.value());
                codepoint::encode_utf32(&raw, 1, target);
            }

            tui::element make_island_element(plot_island island, const rect& grid_area) const {
                const rect local{island.area.x_ - grid_area.x_, island.area.y_ - grid_area.y_, island.area.w_,
                                 island.area.h_};
                function<void(painter&, const rect&)> paint = move(island.paint);
                return tui::element::canvas(
                        [this, local, paint](tui::canvas_painter& canvas) {
                            painter brush(canvas, colors_, cfg_.ascii_, cfg_.display_.transparent_background_,
                                          cfg_.display_.graph_symbol_, cfg_.display_.rounded_corners_,
                                          cfg_.display_.graph_gradient_);
                            paint(brush, local);
                        },
                        make_canvas_style());
            }

            bool pane_matches_grid(const painter::pane& pane) const {
                const rect& area = pane.area_;
                if (area.w_ < 2 || area.h_ < 2) {
                    return false;
                }
                const int width = content_grid_.width();
                const int height = content_grid_.height();
                const bool ascii = pane.ascii_;
                const bool rounded = pane.rounded_;
                const char32_t tl = ascii ? U'+' : (rounded ? U'╭' : U'┌');
                const char32_t tr = ascii ? U'+' : (rounded ? U'╮' : U'┐');
                const char32_t bl = ascii ? U'+' : (rounded ? U'╰' : U'└');
                const char32_t br = ascii ? U'+' : (rounded ? U'╯' : U'┘');
                const char32_t hz = ascii ? U'-' : U'─';
                const char32_t vt = ascii ? U'|' : U'│';
                const auto matches = [&](int x, int y, char32_t cp, color fg, bool bold, bool check_fg) {
                    if (x < 0 || y < 0 || x >= width || y >= height) {
                        return true;
                    }
                    const auto& cell = content_grid_.at(x, y);
                    if (cell.value.value() != cp || cell.bold != bold) {
                        return false;
                    }
                    if (check_fg && !same_color(cell.fg, fg)) {
                        return false;
                    }
                    return true;
                };
                for (int x = area.x_; x < area.right(); ++x) {
                    const char32_t top = x == area.x_ ? tl : (x == area.right() - 1 ? tr : hz);
                    const char32_t bottom = x == area.x_ ? bl : (x == area.right() - 1 ? br : hz);
                    if (!matches(x, area.y_, top, colors_.border_, false, true) ||
                        !matches(x, area.bottom() - 1, bottom, colors_.border_, false, true)) {
                        return false;
                    }
                }
                for (int y = area.y_ + 1; y < area.bottom() - 1; ++y) {
                    if (!matches(area.x_, y, vt, colors_.border_, false, true) ||
                        !matches(area.right() - 1, y, vt, colors_.border_, false, true)) {
                        return false;
                    }
                }
                if (!pane.title_.empty()) {
                    size_t index = 0;
                    int x = area.x_ + 3;
                    while (index < pane.title_.size()) {
                        const codepoint cp = codepoint::decode_utf8(reinterpret_cast<const byte_t*>(pane.title_.data()),
                                                                    index, pane.title_.size());
                        if (!matches(x, area.y_, cp.value(), pane.accent_, true, true)) {
                            return false;
                        }
                        x += cp.display_width();
                    }
                    const int title_width = x - (area.x_ + 3);
                    const auto& left_pad = content_grid_.at(area.x_ + 2, area.y_);
                    if (left_pad.value.value() != U' ' || !same_color(left_pad.bg, pane.background_)) {
                        return false;
                    }
                    const auto& right_pad = content_grid_.at(area.x_ + 2 + title_width + 1, area.y_);
                    if (right_pad.value.value() != U' ' || !same_color(right_pad.bg, pane.background_)) {
                        return false;
                    }
                }
                return true;
            }

            tui::element make_pane_element(const painter::pane& pane) const {
                using size_hint = tui::style::size_hint;
                const int first_col = pane.area_.x_ + 1;
                const int last_col = pane.area_.right() - 2;
                const int first_row = pane.area_.y_ + 1;
                const int last_row = pane.area_.bottom() - 2;
                vector<tui::element> rows;
                if (last_col >= first_col && last_row >= first_row) {
                    rows.reserve(static_cast<size_t>(last_row - first_row + 1));
                    for (int row = first_row; row <= last_row; ++row) {
                        tui::style row_style;
                        row_style.width = size_hint{size_hint::fill, 1};
                        row_style.height = size_hint{size_hint::fixed, 1};
                        rows.push_back(tui::element::rich_text(make_row_spans(content_grid_, row, first_col, last_col),
                                                               row_style, tui::style::wrap_mode::none));
                    }
                }
                tui::box_props rows_props;
                rows_props.dir = tui::style::direction::column;
                rows_props.width = size_hint{size_hint::fill, 1};
                rows_props.height = size_hint{size_hint::fill, 1};

                tui::style style;
                if (pane.ascii_) {
                    style.border = tui::style::border::ascii;
                } else if (pane.rounded_) {
                    style.border = tui::style::border::rounded;
                } else {
                    style.border = tui::style::border::single;
                }
                style.borderColor = colors_.border_;
                style.bg = pane.background_;
                style.fg = pane.accent_;
                struct tui::style::margin margin;
                margin.top = pane.area_.y_;
                margin.left = pane.area_.x_;
                margin.right = content_grid_.width() - pane.area_.x_ - pane.area_.w_;
                margin.bottom = content_grid_.height() - pane.area_.y_ - pane.area_.h_;
                style.margin = margin;
                return tui::element::panel(tui::element::vbox(move(rows), rows_props), pane.title_, style);
            }

            tui::element make_base_row(const int row, const int begin, const int end) const {
                tui::style row_style;
                row_style.width = tui::style::size_hint{tui::style::size_hint::fill, 1};
                row_style.height = tui::style::size_hint{tui::style::size_hint::fixed, 1};
                struct tui::style::margin margin;
                margin.top = row;
                margin.bottom = content_grid_.height() - row - 1;
                margin.left = begin;
                margin.right = content_grid_.width() - end - 1;
                row_style.margin = margin;
                return tui::element::rich_text(make_row_spans(content_grid_, row, begin, end), row_style,
                                               tui::style::wrap_mode::none);
            }

            tui::element make_content_element() const {
                using size_hint = tui::style::size_hint;
                vector<const painter::pane*> panes;
                panes.reserve(content_panes_.size());
                for (const auto& pane: content_panes_) {
                    panes.push_back(&pane);
                }

                const int width = content_grid_.width();
                const int height = content_grid_.height();
                vector<tui::element> layers;
                vector<tui::element> base_rows;
                vector<bool> covered;
                for (int row = 0; row < height; ++row) {
                    covered.assign(static_cast<size_t>(width), false);
                    for (const auto* pane: panes) {
                        if (row < pane->area_.y_ || row >= pane->area_.bottom()) {
                            continue;
                        }
                        for (int x = pane->area_.x_; x < pane->area_.right() && x < width; ++x) {
                            if (x >= 0) {
                                covered[static_cast<size_t>(x)] = true;
                            }
                        }
                    }
                    int column = 0;
                    while (column < width) {
                        if (covered[static_cast<size_t>(column)]) {
                            ++column;
                            continue;
                        }
                        const int begin = column;
                        while (column < width && !covered[static_cast<size_t>(column)]) {
                            ++column;
                        }
                        base_rows.push_back(make_base_row(row, begin, column - 1));
                    }
                }
                if (!base_rows.empty()) {
                    tui::style base_style;
                    base_style.width = size_hint{size_hint::fill, 1};
                    base_style.height = size_hint{size_hint::fill, 1};
                    layers.push_back(tui::element::zstack(move(base_rows)).with_style(base_style));
                }
                for (const auto* pane: panes) {
                    layers.push_back(make_pane_element(*pane));
                }
                for (const auto* pane: panes) {
                    if (pane_matches_grid(*pane)) {
                        continue;
                    }
                    const int left = pane->area_.x_;
                    const int right = pane->area_.right() - 1;
                    const int top = pane->area_.y_;
                    const int bottom = pane->area_.bottom() - 1;
                    layers.push_back(make_base_row(top, left, right));
                    if (bottom != top) {
                        layers.push_back(make_base_row(bottom, left, right));
                    }
                    for (int row = top + 1; row < bottom; ++row) {
                        layers.push_back(make_base_row(row, left, left));
                        if (right != left) {
                            layers.push_back(make_base_row(row, right, right));
                        }
                    }
                }
                for (const auto& island: content_grid_.islands()) {
                    layers.push_back(make_island_element(island, content_grid_.area()));
                }
                tui::style style;
                style.width = size_hint{size_hint::fill, 1};
                style.height = size_hint{size_hint::fill, 1};
                return tui::element::zstack(move(layers)).with_style(style);
            }

            tui::element make_overlay_row(const int row) const {
                const int width = popup_grid_.width();
                int first = -1;
                int last = -1;
                for (int column = 0; column < width; ++column) {
                    const auto& cell = popup_grid_.at(column, row);
                    if (!cell.painted || cell.continuation) {
                        continue;
                    }
                    if (first < 0) {
                        first = column;
                    }
                    last = column;
                }
                tui::style row_style;
                row_style.width = tui::style::size_hint{tui::style::size_hint::fill, 1};
                row_style.height = tui::style::size_hint{tui::style::size_hint::fixed, 1};
                if (first < 0) {
                    return tui::element::rich_text({}, row_style, tui::style::wrap_mode::none);
                }
                struct tui::style::margin margin;
                margin.left = first;
                row_style.margin = margin;
                return tui::element::rich_text(make_row_spans(popup_grid_, row, first, last), row_style,
                                               tui::style::wrap_mode::none);
            }

            tui::element make_overlay_element() const {
                vector<tui::element> rows;
                const int height = popup_grid_.height();
                rows.reserve(static_cast<size_t>(neforce::max(0, height)));
                for (int row = 0; row < height; ++row) {
                    rows.push_back(make_overlay_row(row));
                }
                tui::box_props rows_props;
                rows_props.dir = tui::style::direction::column;
                rows_props.width = tui::style::size_hint{tui::style::size_hint::fill, 1};
                rows_props.height = tui::style::size_hint{tui::style::size_hint::fill, 1};
                vector<tui::element> layers;
                layers.push_back(tui::element::vbox(move(rows), rows_props));
                for (const auto& island: popup_grid_.islands()) {
                    layers.push_back(make_island_element(island, popup_grid_.area()));
                }
                tui::style style;
                style.width = tui::style::size_hint{tui::style::size_hint::fill, 1};
                style.height = tui::style::size_hint{tui::style::size_hint::fill, 1};
                return tui::element::zstack(move(layers)).with_style(style);
            }

            tui::element build_popup() {
                if (popup_ == popup_kind::none) {
                    return tui::element::empty();
                }
                const int width = terminal_width_;
                const int height = terminal_height_;
                if (width <= 0 || height <= 0) {
                    return tui::element::empty();
                }
                popup_grid_.reset(rect{0, 0, width, height});
                painter brush(popup_grid_, colors_, cfg_.ascii_, cfg_.display_.transparent_background_,
                              cfg_.display_.graph_symbol_, cfg_.display_.rounded_corners_,
                              cfg_.display_.graph_gradient_);
                draw_popup(brush);
                return make_overlay_element();
            }

            NEFORCE_NODISCARD vector<string> active_overview_boxes() const {
                vector<string> boxes;
                if (cfg_.overview_presets_.empty() || cfg_.overview_preset_ >= cfg_.overview_presets_.size()) {
                    for (const char* name: {"cpu", "memory", "gpu", "storage", "network", "processes"}) {
                        boxes.push_back(name);
                    }
                    return boxes;
                }
                const auto& preset = cfg_.overview_presets_[cfg_.overview_preset_];
                size_t begin = 0;
                while (begin <= preset.size()) {
                    size_t end = preset.find(',', begin);
                    if (end == string::npos) {
                        end = preset.size();
                    }
                    string token = preset.substr(begin, end - begin);
                    token.trim();
                    if (!token.empty()) {
                        boxes.push_back(move(token));
                    }
                    begin = end + 1;
                }
                return boxes;
            }

            NEFORCE_NODISCARD bool overview_has_box(const vector<string>& boxes, string_view name) const {
                for (const auto& item: boxes) {
                    if (item.view() == name) {
                        return true;
                    }
                }
                return false;
            }

            void cycle_overview_preset() {
                if (cfg_.overview_presets_.empty()) {
                    return;
                }
                cfg_.overview_preset_ = (cfg_.overview_preset_ + 1) % cfg_.overview_presets_.size();
                status_ = format("Overview preset {}/{}: {}", cfg_.overview_preset_ + 1, cfg_.overview_presets_.size(),
                                 cfg_.overview_presets_[cfg_.overview_preset_]);
            }

            NEFORCE_NODISCARD bool layout_visible(const layout_node& node) const {
                if (node.kind_ == layout_node::kind::panel) {
                    if (node.panel_ == "gpu") {
                        return cfg_.collect_gpu_ || !snap_.gpus_.empty();
                    }
                    return true;
                }
                for (const auto& child: node.children_) {
                    if (layout_visible(child)) {
                        return true;
                    }
                }
                return false;
            }

            void draw_custom_layout(painter& brush, const rect& area, const layout_node& node) {
                if (node.kind_ == layout_node::kind::panel) {
                    if (node.panel_ == "cpu") {
                        draw_cpu_panel(brush, area);
                    } else if (node.panel_ == "memory") {
                        draw_memory_panel(brush, area);
                    } else if (node.panel_ == "gpu") {
                        if (layout_visible(node)) {
                            draw_gpu_card(brush, area);
                        }
                    } else if (node.panel_ == "storage") {
                        draw_storage_card(brush, area);
                    } else if (node.panel_ == "network") {
                        draw_network_card(brush, area);
                    } else if (node.panel_ == "processes") {
                        paint_process_table(brush, layout_process_table(area, process_rows()));
                    }
                    return;
                }
                vector<const layout_node*> children;
                uint32_t total = 0;
                for (size_t index = 0; index < node.children_.size(); ++index) {
                    if (!layout_visible(node.children_[index])) {
                        continue;
                    }
                    children.push_back(&node.children_[index]);
                    total += index < node.weights_.size() ? node.weights_[index] : 1;
                }
                if (children.empty() || total == 0) {
                    return;
                }
                rect cursor = area;
                for (size_t index = 0; index < children.size(); ++index) {
                    const auto original = static_cast<size_t>(children[index] - node.children_.data());
                    const uint32_t weight = original < node.weights_.size() ? node.weights_[original] : 1;
                    const int span =
                            index + 1 == children.size()
                                    ? (node.direction_ == "horizontal" ? cursor.w_ : cursor.h_)
                                    : static_cast<int>(static_cast<int64_t>(node.direction_ == "horizontal" ? area.w_
                                                                                                            : area.h_) *
                                                       weight / total) -
                                              1;
                    if (node.direction_ == "horizontal") {
                        draw_custom_layout(brush, rect{cursor.x_, cursor.y_, neforce::max(1, span), cursor.h_},
                                           *children[index]);
                        cursor.x_ += neforce::max(1, span) + 1;
                        cursor.w_ -= neforce::max(1, span) + 1;
                    } else {
                        draw_custom_layout(brush, rect{cursor.x_, cursor.y_, cursor.w_, neforce::max(1, span)},
                                           *children[index]);
                        cursor.y_ += neforce::max(1, span) + 1;
                        cursor.h_ -= neforce::max(1, span) + 1;
                    }
                    total -= weight;
                }
            }

            void draw_overview(painter& brush, const rect& area) {
                const auto& display = cfg_.display_;
                if (cfg_.custom_layout_.has_value()) {
                    draw_custom_layout(brush, area, *cfg_.custom_layout_);
                    return;
                }
                if (display.overview_panel_ != "all") {
                    if (display.overview_panel_ == "cpu") {
                        draw_cpu_panel(brush, area);
                    } else if (display.overview_panel_ == "memory") {
                        draw_memory_panel(brush, area);
                    } else if (display.overview_panel_ == "gpu") {
                        draw_gpu_card(brush, area);
                    } else if (display.overview_panel_ == "storage") {
                        draw_storage_card(brush, area);
                    } else {
                        draw_network_card(brush, area);
                    }
                    return;
                }
                if (cfg_.layout_ == "compute") {
                    const int left = area.w_ * 60 / 100;
                    draw_cpu_panel(brush, rect{area.x_, area.y_, left - 1, area.h_});
                    if (snap_.gpus_.empty() && !cfg_.collect_gpu_) {
                        draw_memory_panel(brush, rect{area.x_ + left, area.y_, area.w_ - left, area.h_});
                    } else {
                        draw_gpu_card(brush, rect{area.x_ + left, area.y_, area.w_ - left, area.h_});
                    }
                    return;
                }
                if (cfg_.layout_ == "storage") {
                    draw_storage_page(brush, area);
                    return;
                }
                if (area.h_ < 30 || area.w_ < 100 || cfg_.layout_ == "minimal") {
                    const int left = area.w_ * 62 / 100;
                    draw_cpu_panel(brush, rect{area.x_, area.y_, left - 1, area.h_});
                    draw_memory_panel(brush, rect{area.x_ + left, area.y_, area.w_ - left, area.h_});
                    return;
                }
                const auto boxes = active_overview_boxes();
                const bool show_cpu = overview_has_box(boxes, "cpu");
                const bool show_processes = overview_has_box(boxes, "processes");
                if (boxes.empty()) {
                    brush.box(area, " OVERVIEW ", colors_.muted_);
                    brush.text(area.x_ + 2, area.y_ + 1, "No overview boxes in the active preset.", colors_.muted_);
                    return;
                }
                const size_t core_rows = (snap_.cores_.size() + 1) / 2;
                const int process_height = show_processes && area.h_ >= 36 ? neforce::min(8, area.h_ / 5) : 0;
                const int reserved = (process_height > 0 ? process_height + 1 : 0);
                int cpu_height = show_cpu ? neforce::max(10, neforce::min(static_cast<int>(core_rows) + 2,
                                                                          area.h_ - reserved - 8))
                                          : 0;
                cpu_height = neforce::min(cpu_height, area.h_ - 8);
                int cursor_y = area.y_;
                if (show_cpu) {
                    draw_cpu_panel(brush, rect{area.x_, cursor_y, area.w_, cpu_height});
                    cursor_y += cpu_height + 1;
                }
                vector<const char*> card_names;
                const bool gpu_visible = cfg_.collect_gpu_ || !snap_.gpus_.empty();
                if (overview_has_box(boxes, "memory")) {
                    card_names.push_back("memory");
                }
                if (overview_has_box(boxes, "gpu") && gpu_visible) {
                    card_names.push_back("gpu");
                }
                if (overview_has_box(boxes, "storage")) {
                    card_names.push_back("storage");
                }
                if (overview_has_box(boxes, "network")) {
                    card_names.push_back("network");
                }
                const int card_height = neforce::max(8, area.h_ - (cursor_y - area.y_) - reserved);
                const rect cards{area.x_, cursor_y, area.w_, card_height};
                if (!card_names.empty()) {
                    const int count = static_cast<int>(card_names.size());
                    const int card_width = (cards.w_ - (count - 1)) / count;
                    int x = cards.x_;
                    for (size_t index = 0; index < card_names.size(); ++index) {
                        const int width = index + 1 == card_names.size() ? cards.right() - x : card_width;
                        const string_view name = card_names[index];
                        if (name == "memory") {
                            draw_memory_panel(brush, rect{x, cards.y_, width, cards.h_});
                        } else if (name == "gpu") {
                            draw_gpu_card(brush, rect{x, cards.y_, width, cards.h_});
                        } else if (name == "storage") {
                            draw_storage_card(brush, rect{x, cards.y_, width, cards.h_});
                        } else {
                            draw_network_card(brush, rect{x, cards.y_, width, cards.h_});
                        }
                        x += width + 1;
                    }
                }
                if (process_height > 0) {
                    const int process_y = cards.bottom() + 1;
                    if (process_y < area.bottom()) {
                        paint_process_table(brush, layout_process_table(
                                                           rect{area.x_, process_y, area.w_, area.bottom() - process_y},
                                                           process_rows()));
                    }
                }
            }

            NEFORCE_NODISCARD optional<float> cpu_temperature() const {
                if (!cfg_.display_.cpu_sensor_.empty()) {
                    for (const auto& item: snap_.sensors_) {
                        if (item.name_ == cfg_.display_.cpu_sensor_) {
                            return item.temperature_;
                        }
                    }
                    return none;
                }
                optional<float> best;
                int best_rank = 99;
                for (const auto& item: snap_.sensors_) {
                    if (!neforce::is_finite(item.temperature_)) {
                        continue;
                    }
                    const string name = lower_ascii(item.name_.view());
                    int rank = 99;
                    if (name.contains("cpu package") || (name.contains("coretemp") && name.contains("package")) ||
                        (name.contains("k10temp") && name.contains("tdie"))) {
                        rank = 0;
                    } else if (name.contains("k10temp") && name.contains("tctl")) {
                        rank = 1;
                    } else if (name.contains("coretemp") || starts_with(name.view(), "cpu") ||
                               name.contains("cpu_thermal") || name.contains("cpu-thermal") ||
                               name.contains("soc_thermal")) {
                        rank = 2;
                    }
                    if (rank < best_rank) {
                        best_rank = rank;
                        best = item.temperature_;
                    }
                }
                return best;
            }

            NEFORCE_NODISCARD painter graph_painter(painter& brush, const string& setting) const {
                const string& symbol = setting == "default" ? cfg_.display_.graph_symbol_ : setting;
                return brush.with_graph_symbol(symbol);
            }

            NEFORCE_NODISCARD string cpu_core_summary() const {
                if (snap_.pcore_count_ != 0 && snap_.ecore_count_ != 0) {
                    return format("{}P + {}E / {}T", snap_.pcore_count_, snap_.ecore_count_, snap_.cores_.size());
                }
                return format("{} cores", snap_.cores_.size());
            }

            void draw_core_grid(painter& brush, const rect& area) {
                if (area.h_ < 2 || area.w_ < 8) {
                    return;
                }
                const int count = static_cast<int>(snap_.cores_.size());
                const string title =
                        count > 0 ? format("Per-core history · {}", cpu_core_summary()) : string("Per-core history");
                brush.text(area.x_, area.y_, truncate_display(title.view(), area.w_).view(), colors_.muted_);
                if (count == 0 || area.h_ < 3) {
                    return;
                }
                const int rows_available = area.h_ - 1;
                const int columns = neforce::max(1, (count + rows_available - 1) / rows_available);
                const int rows = (count + columns - 1) / columns;
                const int cell_width = neforce::max(1, area.w_ / columns);
                for (int index = 0; index < count; ++index) {
                    const int column = index / rows;
                    const int row = index % rows;
                    if (column >= columns) {
                        break;
                    }
                    const int x = area.x_ + column * cell_width;
                    const int y = area.y_ + 1 + row;
                    if (y >= area.bottom()) {
                        break;
                    }
                    const double value = snap_.cores_[static_cast<size_t>(index)];
                    const color tint = value >= 90 ? colors_.red_ : (value >= 70 ? colors_.amber_ : colors_.mint_);
                    brush.text(x, y, format("C{:02}", static_cast<uint64_t>(index)).view(), colors_.muted_);
                    brush.track(x + 4, y, neforce::max(1, cell_width - 14), value, 100.0, tint);
                    brush.text_right(x, y, cell_width - 1, format("{:5.1}%", value).view(), tint);
                }
            }

            void draw_cpu_panel(painter& brush, const rect& area) {
                if (area.h_ < 3 || area.w_ < 10) {
                    return;
                }
                const string title = format(" CPU  {:.1f}% · {} ", snap_.cpu_percent_, cpu_core_summary());
                brush.box(area, title.view(), colors_.mint_);
                const rect inner = area.inset(1);
                if (inner.h_ < 2) {
                    return;
                }
                const auto temperature = cpu_temperature();
                string info = snap_.cpu_name_;
                if (info.empty()) {
                    info = "CPU";
                }
                if (snap_.pcore_count_ != 0 || snap_.ecore_count_ != 0) {
                    info.append(format(" · {}P + {}E", snap_.pcore_count_, snap_.ecore_count_));
                }
                const string stats =
                        format(" · Clock {}", snap_.cpu_mhz_ > 0 ? format("{} MHz", snap_.cpu_mhz_) : string("—"));
                info.append(stats);
                if (temperature.has_value()) {
                    info.append(format(" · Temp {:.1f}{}", cfg_.display_.temperature_value(*temperature),
                                       cfg_.display_.temperature_symbol()));
                }
                info.append(format(" · Load {:.2f} {:.2f} {:.2f}", snap_.load_[0], snap_.load_[1], snap_.load_[2]));
                brush.text(inner.x_, inner.y_, truncate_display(info.view(), inner.w_).view(), colors_.text_);
                if (inner.h_ < 4) {
                    return;
                }
                const bool gpu_footer = cfg_.display_.gpu_in_cpu_box_ && !snap_.gpus_.empty();
                const int content_bottom = inner.bottom() - (gpu_footer ? 1 : 0);
                const bool wide = inner.w_ >= 70 && snap_.cores_.size() > 6;
                int core_width = wide ? neforce::max(24, inner.w_ * 38 / 100) : inner.w_;
                if (wide && snap_.cores_.size() > 16) {
                    core_width = neforce::max(core_width, inner.w_ * 46 / 100);
                }
                rect chart_area{inner.x_, inner.y_ + 1, wide ? inner.w_ - core_width - 2 : inner.w_,
                                content_bottom - inner.y_ - 1};
                if (!wide) {
                    chart_area.h_ = neforce::max(3, (content_bottom - inner.y_ - 1) * 2 / 3);
                }
                const string upper = cfg_.display_.cpu_graph_;
                const string lower = cfg_.display_.cpu_graph_lower_;
                const deque<double>* upper_values = &history_.cpu_;
                if (upper != "total") {
                    const auto found = history_.series_.find(format("CPU {} (%)", upper));
                    upper_values = found == history_.series_.end() ? nullptr : &found->second;
                }
                const deque<double>* lower_values = nullptr;
                if (lower != "none" && lower != upper) {
                    if (lower == "total") {
                        lower_values = &history_.cpu_;
                    } else {
                        const auto found = history_.series_.find(format("CPU {} (%)", lower));
                        lower_values = found == history_.series_.end() ? nullptr : &found->second;
                    }
                }
                graph_painter(brush, cfg_.display_.graph_symbol_cpu_)
                        .cpu_chart(chart_area, upper, upper_values, lower, lower_values, chart_window());
                if (!wide) {
                    const int grid_top = neforce::min(chart_area.bottom(), content_bottom - 1);
                    draw_core_grid(brush, rect{inner.x_, grid_top, inner.w_, content_bottom - grid_top});
                } else {
                    draw_core_grid(brush, rect{inner.x_ + inner.w_ - core_width, inner.y_ + 1, core_width,
                                               content_bottom - inner.y_ - 1});
                }
                if (gpu_footer) {
                    const auto index = neforce::min(selected_gpu_, snap_.gpus_.size() - 1);
                    const auto& gpu = snap_.gpus_[index];
                    const string gpu_text =
                            format("GPU{} {}/{} · {}% · {} · {}", gpu.id_, index + 1, snap_.gpus_.size(),
                                   gpu.utilization_.has_value() ? *gpu.utilization_ : 0,
                                   gpu.memory_used_.has_value() && gpu.memory_total_.has_value()
                                           ? format("{}/{}", bytes(static_cast<double>(*gpu.memory_used_)),
                                                    bytes(static_cast<double>(*gpu.memory_total_)))
                                           : string("—"),
                                   gpu.temperature_.has_value() ? cfg_.display_.temperature(*gpu.temperature_, 0)
                                                                : string("—"));
                    brush.text(inner.x_, content_bottom, truncate_display(gpu_text.view(), inner.w_).view(),
                               colors_.blue_);
                }
            }

            void draw_memory_panel(painter& brush, const rect& area) {
                if (area.h_ < 4 || area.w_ < 10) {
                    return;
                }
                brush.box(area, " MEMORY ", colors_.purple_);
                const rect inner = area.inset(1);
                const auto& memory = snap_.memory_;
                brush.text(inner.x_, inner.y_, format("Total {}", bytes(static_cast<double>(memory.total_))).view(),
                           colors_.text_, none, true);
                struct row_info {
                    string_view name;
                    optional<uint64_t> value;
                    string_view series;
                    color tint;
                };
                const row_info rows[] = {
                        {"Used", memory.total_ > 0 ? optional<uint64_t>(memory.used_) : none, "RAM (%)",
                         colors_.purple_},
                        {"Available", memory.total_ > 0 ? optional<uint64_t>(memory.available_) : none,
                         "RAM available (%)", colors_.mint_},
                        {"Cached", memory.cached_, "RAM cached (%)", colors_.amber_},
                        {"Free", memory.free_, "RAM free (%)", colors_.blue_},
                };
                const int body_height = inner.h_ - 1;
                const bool swap = cfg_.display_.show_swap_ && memory.swap_total_ > 0;
                const int entries = swap ? 5 : 4;
                const int per_row = cfg_.display_.memory_graphs_ && body_height > entries
                                            ? neforce::max(1, body_height / entries)
                                            : 1;
                int y = inner.y_ + 1;
                for (const auto& item: rows) {
                    if (y >= inner.bottom()) {
                        break;
                    }
                    const auto ratio = item.value.has_value() && memory.total_ > 0
                                               ? optional<double>(percent(*item.value, memory.total_))
                                               : none;
                    const string label = format("{} (%) {}", item.name,
                                                item.value.has_value() ? bytes(static_cast<double>(*item.value)) : "—");
                    brush.text(inner.x_, y, truncate_display(label.view(), inner.w_ - 7).view(),
                               item.value.has_value() ? item.tint : colors_.muted_);
                    if (ratio.has_value()) {
                        brush.text_right(inner.x_, y, inner.w_, format("{:5.1f}%", *ratio).view(), item.tint);
                    }
                    ++y;
                    if (per_row > 1 && y < inner.bottom()) {
                        const auto found = history_.series_.find(item.series);
                        if (found != history_.series_.end()) {
                            graph_painter(brush, cfg_.display_.graph_symbol_mem_)
                                    .trend(rect{inner.x_, y, inner.w_, per_row - 1},
                                           {chart_series{item.name, &found->second, item.tint}}, 100.0, chart_window(),
                                           "%", false);
                        }
                        y += per_row - 1;
                    }
                }
                if (swap && y < inner.bottom()) {
                    const int swap_y = y;
                    const color swap_tint = colors_.muted_;
                    const string swap_label = format("Swap {} / {}", bytes(static_cast<double>(memory.swap_used_)),
                                                     bytes(static_cast<double>(memory.swap_total_)));
                    brush.text(inner.x_, swap_y, truncate_display(swap_label.view(), inner.w_ - 7).view(), swap_tint);
                    brush.text_right(inner.x_, swap_y, inner.w_,
                                     format("{:5.1}%", percent(memory.swap_used_, memory.swap_total_)).view(),
                                     swap_tint);
                    const int graph_y = swap_y + 1;
                    const int graph_h = neforce::min(per_row - 1, inner.bottom() - graph_y);
                    if (graph_h > 1) {
                        const auto found = history_.series_.find("Swap (%)");
                        if (found != history_.series_.end()) {
                            graph_painter(brush, cfg_.display_.graph_symbol_mem_)
                                    .trend(rect{inner.x_, graph_y, inner.w_, graph_h},
                                           {chart_series{"Swap", &found->second, swap_tint}}, 100.0, chart_window(),
                                           "%", false);
                        }
                    } else if (graph_h == 1) {
                        brush.track(inner.x_, graph_y, inner.w_, percent(memory.swap_used_, memory.swap_total_), 100.0,
                                    swap_tint);
                    }
                }
            }

            void draw_gpu_card(painter& brush, const rect& area) {
                if (area.h_ < 4 || area.w_ < 10) {
                    return;
                }
                if (snap_.gpus_.empty()) {
                    brush.box(area,
                              snap_.timestamp_ == 0 && !replay_ ? " GPU · collecting telemetry "
                                                                : " GPU · no readable telemetry ",
                              colors_.blue_);
                    if (area.h_ > 5) {
                        brush.text_center(area.x_, area.y_ + area.h_ / 2, area.w_,
                                          snap_.gpu_status_.empty() ? string_view("NVML unavailable")
                                                                    : snap_.gpu_status_.view(),
                                          colors_.muted_);
                    }
                    return;
                }
                if (cfg_.display_.multi_gpu_overview_ && snap_.gpus_.size() > 1) {
                    brush.box(area, format(" GPU · {} devices ", snap_.gpus_.size()).view(), colors_.blue_);
                    const rect inner = area.inset(1);
                    const size_t rows_per_gpu = inner.h_ >= 10 ? 2 : 1;
                    const size_t visible = static_cast<size_t>(neforce::max<int>(1, (inner.h_ - 1) / rows_per_gpu));
                    const bool paged = snap_.gpus_.size() > visible;
                    const size_t page_start = (neforce::min(selected_gpu_, snap_.gpus_.size() - 1) / visible) * visible;
                    const size_t page_end = neforce::min(snap_.gpus_.size(), page_start + visible);
                    int y = inner.y_;
                    for (size_t index = page_start; index < page_end && y < inner.bottom(); ++index) {
                        const auto& item = snap_.gpus_[index];
                        const bool active = index == neforce::min(selected_gpu_, snap_.gpus_.size() - 1);
                        const color tint = device_tint(index);
                        if (active) {
                            brush.fill(rect{inner.x_, y, inner.w_, 1}, " ", colors_.text_, colors_.selection_);
                        }
                        const string name =
                                format("GPU{} {}", item.id_, item.name_.empty() ? item.vendor_ : item.name_);
                        brush.text(inner.x_, y, truncate_display(name.view(), inner.w_ - 11).view(),
                                   active ? tint : colors_.text_);
                        const string usage =
                                item.utilization_.has_value() ? format("{:>3}%", *item.utilization_) : string("  —");
                        brush.text_right(inner.x_, y, inner.w_, usage.view(), active ? colors_.text_ : tint);
                        hits_.push_back({{inner.x_, y, inner.w_, 1}, hit_action::gpu_device, index});
                        ++y;
                        if (rows_per_gpu > 1 && y < inner.bottom()) {
                            const string memory = format(
                                    "{} / {}", bytes(static_cast<double>(item.memory_used_.value_or(0))),
                                    item.memory_total_.has_value() ? bytes(static_cast<double>(*item.memory_total_))
                                                                   : string("—"));
                            const string detail =
                                    format("{}{}", memory,
                                           item.temperature_.has_value()
                                                   ? format(" · {}", cfg_.display_.temperature(*item.temperature_, 0))
                                                   : string());
                            brush.text(inner.x_ + 2, y, truncate_display(detail.view(), inner.w_ - 2).view(),
                                       colors_.muted_);
                            ++y;
                        }
                    }
                    const size_t chart_index = neforce::min(selected_gpu_, snap_.gpus_.size() - 1);
                    if (y + 2 < inner.bottom()) {
                        const auto& device = snap_.gpus_[chart_index];
                        const auto found = history_.gpu_details_.find(device.uuid_);
                        if (found != history_.gpu_details_.end()) {
                            graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                    .trend(rect{inner.x_, y, inner.w_, inner.bottom() - y},
                                           {chart_series{"GPU", &found->second.utilization_, colors_.blue_},
                                            chart_series{"VRAM", &found->second.vram_, colors_.purple_}},
                                           100.0, chart_window(), "%", false);
                        }
                    }
                    if (paged) {
                        brush.text_right(area.x_, area.bottom() - 1, area.w_,
                                         format("[ {}/{} ]", neforce::min(selected_gpu_, snap_.gpus_.size() - 1) + 1,
                                                snap_.gpus_.size())
                                                 .view(),
                                         colors_.muted_);
                    }
                    return;
                }
                const size_t index = neforce::min(selected_gpu_, snap_.gpus_.size() - 1);
                const auto& device = snap_.gpus_[index];
                const string title = format(" GPU {} · {} ", device.id_, device.vendor_);
                brush.box(area, title.view(), colors_.blue_);
                const rect inner = area.inset(1);
                if (inner.h_ < 2) {
                    return;
                }
                brush.text(inner.x_, inner.y_, truncate_display(device.name_.view(), inner.w_).view(), colors_.text_,
                           none, true);
                int y = inner.y_ + 1;
                const int bar_width = neforce::max(3, inner.w_ - 16);
                if (device.utilization_.has_value()) {
                    brush.text(inner.x_, y, "GPU", colors_.muted_);
                    brush.track(inner.x_ + 4, y, bar_width, *device.utilization_, 100.0, colors_.blue_);
                    brush.text_right(inner.x_, y, inner.w_, format("{:3}%", *device.utilization_).view(),
                                     colors_.blue_);
                } else {
                    brush.text(inner.x_, y, "GPU   —", colors_.muted_);
                }
                ++y;
                if (y < inner.bottom() && device.memory_percent().has_value()) {
                    brush.text(inner.x_, y, device.memory_label().view(), colors_.muted_);
                    brush.track(inner.x_ + 4, y, bar_width, *device.memory_percent(), 100.0, colors_.purple_);
                    brush.text_right(inner.x_, y, inner.w_, format("{:3.0f}%", *device.memory_percent()).view(),
                                     colors_.purple_);
                    ++y;
                }
                if (y < inner.bottom()) {
                    const string memory = format(
                            "{} / {}",
                            bytes(static_cast<double>(device.memory_used_.has_value() ? *device.memory_used_ : 0)),
                            device.memory_total_.has_value() ? bytes(static_cast<double>(*device.memory_total_))
                                                             : string("—"));
                    brush.text(inner.x_, y, truncate_display(memory.view(), inner.w_).view(), colors_.muted_);
                    ++y;
                }
                if (y < inner.bottom()) {
                    const string line = format(
                            "{} · {} · fan {}",
                            device.temperature_.has_value() ? cfg_.display_.temperature(*device.temperature_, 0)
                                                            : string("—"),
                            device.power_watts_.has_value() ? format("{:.0f} W", *device.power_watts_) : string("—"),
                            device.fan_.has_value() ? format("{}%", *device.fan_) : string("—"));
                    brush.text(inner.x_, y, truncate_display(line.view(), inner.w_).view(), colors_.muted_);
                    ++y;
                }
                if (y + 2 < inner.bottom()) {
                    const auto found = history_.gpu_details_.find(device.uuid_);
                    if (found != history_.gpu_details_.end()) {
                        graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                .trend(rect{inner.x_, y, inner.w_, inner.bottom() - y},
                                       {chart_series{"GPU", &found->second.utilization_, colors_.blue_},
                                        chart_series{device.memory_label(), &found->second.vram_, colors_.purple_}},
                                       100.0, chart_window(), "%", false);
                    }
                }
                if (snap_.gpus_.size() > 1) {
                    const string footer = format("[ Prev {}/{} Next ]", index + 1, snap_.gpus_.size());
                    brush.text_right(area.x_, area.bottom() - 1, area.w_, footer.view(), colors_.blue_);
                    const int footer_width = display_width(footer.view());
                    const int footer_x = area.right() - footer_width;
                    const int next_x = area.right() - display_width(" Next ]");
                    hits_.push_back({{footer_x, area.bottom() - 1, next_x - footer_x, 1}, hit_action::gpu_prev, 0});
                    hits_.push_back({{next_x, area.bottom() - 1, area.right() - next_x, 1}, hit_action::gpu_next, 0});
                }
            }

            NEFORCE_NODISCARD color device_tint(size_t index) const {
                switch (index % 5) {
                    case 0:
                        return colors_.mint_;
                    case 1:
                        return colors_.blue_;
                    case 2:
                        return colors_.purple_;
                    case 3:
                        return colors_.amber_;
                    default:
                        return colors_.red_;
                }
            }

            void draw_storage_card(painter& brush, const rect& area) {
                if (area.h_ < 4 || area.w_ < 10) {
                    return;
                }
                brush.box(area, " STORAGE ", colors_.amber_);
                const rect inner = area.inset(1);
                double read = 0.0;
                double write = 0.0;
                for (const auto& item: snap_.disk_io_) {
                    if (item.aggregate_) {
                        read += item.read_per_sec_;
                        write += item.write_per_sec_;
                    }
                }
                int y = inner.y_;
                const int shown = neforce::min(3, neforce::max(1, (inner.h_ - 5) / 2));
                for (int index = 0; index < shown && compare_less(index, snap_.disks_.size()); ++index) {
                    const auto& disk = snap_.disks_[static_cast<size_t>(index)];
                    const double usage = percent(disk.used_, disk.total_);
                    const color tint = usage >= 90 ? colors_.red_ : device_tint(static_cast<size_t>(index));
                    brush.text(inner.x_, y, truncate_display(disk.mount_.view(), inner.w_ - 8).view(), colors_.text_);
                    brush.text_right(inner.x_, y, inner.w_, format("{:5.1f}%", usage).view(), tint);
                    if (y + 1 < inner.bottom()) {
                        brush.track(inner.x_, y + 1, inner.w_, usage, 100.0, tint);
                    }
                    y += 2;
                }
                if (y < inner.bottom()) {
                    brush.text(inner.x_, y, format("R {}/s  W {}/s", bytes(read), bytes(write)).view(), colors_.muted_);
                }
                if (y + 2 < inner.bottom() && (!history_.disk_read_.empty() || !history_.disk_write_.empty())) {
                    vector<chart_series> series;
                    series.push_back({"Read", &history_.disk_read_, colors_.mint_});
                    series.push_back({"Write", &history_.disk_write_, colors_.blue_});
                    graph_painter(brush, cfg_.display_.graph_symbol_proc_)
                            .trend(
                                    rect{inner.x_, y + 1, inner.w_, inner.bottom() - y - 1}, series,
                                    [&] {
                                        double maximum = 1.0;
                                        for (const auto& item: series) {
                                            for (const double value: *item.values_) {
                                                if (neforce::is_finite(value)) {
                                                    maximum = neforce::max(maximum, value);
                                                }
                                            }
                                        }
                                        return tui::canvas_chart_nice_max(maximum);
                                    }(),
                                    storage_chart_window(), "B/s", false);
                }
            }

            void draw_network_card(painter& brush, const rect& area) {
                if (area.h_ < 4 || area.w_ < 10) {
                    return;
                }
                const auto* net = history_.network(cfg_);
                double rx = 0.0;
                double tx = 0.0;
                size_t visible = 0;
                for (const auto& item: snap_.networks_) {
                    if (!cfg_.display_.network_interface_.empty() && item.name_ != cfg_.display_.network_interface_) {
                        continue;
                    }
                    if (cfg_.display_.network_interface_.empty() && !cfg_.show_loopback_ && is_loopback(item.name_)) {
                        continue;
                    }
                    rx += item.rx_per_sec_;
                    tx += item.tx_per_sec_;
                    ++visible;
                }
                const string title = cfg_.display_.network_interface_.empty()
                                             ? format(" NETWORK · {} interfaces ", static_cast<uint64_t>(visible))
                                             : format(" NETWORK · {} ", cfg_.display_.network_interface_);
                brush.box(area, title.view(), colors_.mint_);
                const rect inner = area.inset(1);
                if (inner.h_ < 2) {
                    return;
                }
                brush.text(inner.x_, inner.y_,
                           format("▼ {}   ▲ {}", cfg_.display_.rate(rx), cfg_.display_.rate(tx)).view(), colors_.text_);
                const bool show_total = inner.h_ > 6;
                if (show_total) {
                    uint64_t total = 0;
                    if (cfg_.display_.network_start_zero_ && net != nullptr) {
                        total = net->rx_session_ + net->tx_session_;
                    } else {
                        for (const auto& item: snap_.networks_) {
                            if (!cfg_.display_.network_interface_.empty() &&
                                item.name_ != cfg_.display_.network_interface_) {
                                continue;
                            }
                            if (cfg_.display_.network_interface_.empty() && !cfg_.show_loopback_ &&
                                is_loopback(item.name_)) {
                                continue;
                            }
                            total += item.rx_total_ + item.tx_total_;
                        }
                    }
                    brush.text(inner.x_, inner.y_ + 1,
                               format("{} {}", cfg_.display_.network_start_zero_ ? "Session" : "Total",
                                      bytes(static_cast<double>(total)))
                                       .view(),
                               colors_.muted_);
                }
                if (net != nullptr && inner.h_ > 4) {
                    const bool sync_scale = cfg_.display_.network_sync_scale_;
                    const auto fixed = static_cast<double>(cfg_.display_.network_scale_);
                    const double rx_peak =
                            fixed > 0.0 ? fixed
                                        : (sync_scale ? neforce::max(net->rx_peak_, net->tx_peak_) : net->rx_peak_);
                    const double tx_peak =
                            fixed > 0.0 ? fixed
                                        : (sync_scale ? neforce::max(net->rx_peak_, net->tx_peak_) : net->tx_peak_);
                    const bool swap = cfg_.display_.network_swap_;
                    const int chart_top = inner.y_ + (show_total ? 3 : 2);
                    const int half = neforce::max(1, (inner.bottom() - chart_top) / 2);
                    if (!swap) {
                        graph_painter(brush, cfg_.display_.graph_symbol_net_)
                                .trend(rect{inner.x_, chart_top, inner.w_, half},
                                       {chart_series{"RX", &net->rx_, colors_.mint_}},
                                       tui::canvas_chart_nice_max(neforce::max(1.0, rx_peak)), chart_window(), "B/s",
                                       false, cfg_.display_.network_logarithmic_);
                        graph_painter(brush, cfg_.display_.graph_symbol_net_)
                                .trend(rect{inner.x_, chart_top + half, inner.w_, inner.bottom() - chart_top - half},
                                       {chart_series{"TX", &net->tx_, colors_.blue_}},
                                       tui::canvas_chart_nice_max(neforce::max(1.0, tx_peak)), chart_window(), "B/s",
                                       false, cfg_.display_.network_logarithmic_);
                    } else {
                        graph_painter(brush, cfg_.display_.graph_symbol_net_)
                                .trend(rect{inner.x_, chart_top, inner.w_, half},
                                       {chart_series{"TX", &net->tx_, colors_.blue_}},
                                       tui::canvas_chart_nice_max(neforce::max(1.0, tx_peak)), chart_window(), "B/s",
                                       false, cfg_.display_.network_logarithmic_);
                        graph_painter(brush, cfg_.display_.graph_symbol_net_)
                                .trend(rect{inner.x_, chart_top + half, inner.w_, inner.bottom() - chart_top - half},
                                       {chart_series{"RX", &net->rx_, colors_.mint_}},
                                       tui::canvas_chart_nice_max(neforce::max(1.0, rx_peak)), chart_window(), "B/s",
                                       false, cfg_.display_.network_logarithmic_);
                    }
                }
            }

            void draw_process_side_panel(painter& brush, const rect& area) {
                if (area.h_ < 5 || area.w_ < 24) {
                    return;
                }
                const auto value = selected_process_value();
                if (!value.has_value()) {
                    brush.box(area, " PROCESS ", colors_.mint_);
                    brush.empty(area.inset(1), "No process selected", "Use ↑/↓ or click a process row.");
                    return;
                }
                const auto& item = *value;
                brush.box(area, format(" PROCESS · {} ", item.name_).view(), colors_.mint_);
                int y = area.y_ + 1;
                const int bottom = area.bottom() - 1;
                const auto line = [&](const string& text, color tint) {
                    if (y < bottom) {
                        brush.text(area.x_ + 2, y, truncate_display(text.view(), area.w_ - 4).view(), move(tint));
                        ++y;
                    }
                };
                line(format("PID {} · {} · {}", static_cast<uint64_t>(item.pid_), item.user_, item.state_),
                     colors_.text_);
                line(format("CPU {:.1f}% · RAM {} · I/O {}/s", item.cpu_, bytes(static_cast<double>(item.memory_)),
                            bytes(item.read_per_sec_ + item.write_per_sec_)),
                     colors_.muted_);
                line(format("Start {} · runtime {}", format("{}", static_cast<uint64_t>(item.start_time_)),
                            duration(item.runtime_)),
                     colors_.muted_);
                const auto command_lines = wrap_display(item.command_.view(), neforce::max(8, area.w_ - 12));
                for (size_t index = 0; index < command_lines.size(); ++index) {
                    line(index == 0 ? format("Command {}", command_lines[index])
                                    : format("        {}", command_lines[index]),
                         colors_.text_);
                }
                if (y < bottom - 2 && !process_history_.cpu_.empty()) {
                    ++y;
                    const int chart_height = neforce::min(6, bottom - y);
                    graph_painter(brush, cfg_.display_.graph_symbol_proc_)
                            .trend(rect{area.x_ + 2, y, area.w_ - 4, chart_height},
                                   {chart_series{"process CPU %", &process_history_.cpu_, colors_.mint_}}, 100.0,
                                   chart_window(), "%", false);
                    y += chart_height + 1;
                }
                const auto detail = build_detail();
                for (const auto& text: detail) {
                    if (y >= bottom) {
                        break;
                    }
                    if (starts_with(text.view(), "Command ")) {
                        continue;
                    }
                    if (text.empty()) {
                        ++y;
                        continue;
                    }
                    line(text, starts_with(text.view(), "#") ? colors_.mint_ : colors_.muted_);
                }
            }

            struct process_cell_view {
                int x_ = 0;
                int width_ = 0;
                string text_;
                color fg_{};
                bool right_ = false;
            };

            struct process_table_row_view {
                size_t index_ = 0;
                size_t process_index_ = 0;
                int y_ = 0;
                color fg_{};
                color bg_{};
                bool active_ = false;
                bool followed_ = false;
                int indent_ = 0;
                int toggle_width_ = 0;
                vector<process_cell_view> cells_;
            };

            struct process_table_view {
                rect area_{};
                rect inner_{};
                rect scrollbar_{};
                string title_;
                int header_y_ = 0;
                int visible_ = 0;
                size_t offset_ = 0;
                size_t total_ = 0;
                bool tree_ = false;
                vector<process_cell_view> header_cells_;
                vector<process_table_row_view> rows_;
            };

            void draw_processes(painter& brush, const rect& area) {
                const auto rows = process_rows();
                const rect summary{area.x_, area.y_, area.w_, neforce::min(3, area.h_)};
                brush.box(summary,
                          format(" PROCESSES · {} visible / {} sampled ", static_cast<uint64_t>(rows.size()),
                                 static_cast<uint64_t>(snap_.processes_.size()))
                                  .view(),
                          colors_.mint_);
                if (summary.h_ > 1) {
                    const auto& s = snap_;
                    const string line = format(" Load {:.2f} {:.2f} {:.2f} · Threads {} · Avg {:.1f}% · {} rows",
                                               s.load_[0], s.load_[1], s.load_[2],
                                               s.extra_.get("Process counts") != nullptr &&
                                                               s.extra_.get("Process counts")->get("threads") != nullptr
                                                       ? s.extra_.get("Process counts")->get("threads")->as_integer()
                                                       : 0,
                                               s.cpu_percent_, static_cast<uint64_t>(rows.size()));
                    brush.text(summary.x_ + 2, summary.y_ + 1, truncate_display(line.view(), summary.w_ - 4).view(),
                               colors_.muted_);
                }
                const rect table{area.x_, area.y_ + summary.h_, area.w_, area.h_ - summary.h_};
                if (table.h_ < 4) {
                    return;
                }
                if (process_side_panel_ && area.w_ >= 80 && selected_process_.has_value()) {
                    const int left = neforce::max(40, area.w_ * 68 / 100);
                    const auto view = layout_process_table(rect{table.x_, table.y_, left - 1, table.h_}, rows);
                    paint_process_table(brush, view);
                    draw_process_side_panel(brush, rect{table.x_ + left, table.y_, table.w_ - left, table.h_});
                } else {
                    const auto view = layout_process_table(table, rows);
                    paint_process_table(brush, view);
                }
            }

            struct column_def {
                string key_;
                string title_;
                int width_;
                bool right_;
            };

            static const vector<column_def>& column_catalog() {
                static const column_def catalog[] = {
                        {"pid", "PID", 7, true},
                        {"ppid", "PPID", 7, true},
                        {"user", "USER", 12, false},
                        {"uid", "UID", 6, true},
                        {"cpu", "CPU%", 7, true},
                        {"cpu_normalized", "CPU/T%", 8, true},
                        {"memory", "RAM", 9, true},
                        {"memory_percent", "RAM%", 6, true},
                        {"vms", "VMS", 9, true},
                        {"shared", "SHARED", 9, true},
                        {"vram", "VRAM", 9, true},
                        {"gpu", "GPU%", 6, true},
                        {"state", "STATE", 16, false},
                        {"name", "NAME", 20, false},
                        {"command", "COMMAND", 0, false},
                        {"comm", "COMM", 16, false},
                        {"threads", "THR", 5, true},
                        {"priority", "PRI", 5, true},
                        {"nice", "NICE", 5, true},
                        {"processor", "CPU#", 5, true},
                        {"runtime", "ELAPSED", 10, false},
                        {"time", "CPU TIME", 10, false},
                        {"read", "READ/s", 10, true},
                        {"write", "WRITE/s", 10, true},
                        {"read_total", "TOTAL READ", 11, true},
                        {"write_total", "TOTAL WRITE", 11, true},
                        {"rx", "RX/s", 10, true},
                        {"tx", "TX/s", 10, true},
                        {"tag", "TAGS", 24, false},
                        {"thread", "THREAD", 7, false},
                        {"kernel_thread", "KERNEL", 7, false},
                        {"oom", "OOM", 5, true},
                        {"cgroup", "CGROUP", 24, false},
                        {"autogroup", "AUTOGROUP", 18, false},
                        {"comm_extra", "COMM", 16, false},
                        {"exe", "EXE", 30, false},
                        {"cwd", "CWD", 30, false},
                        {"wchan", "WCHAN", 18, false},
                        {"security", "SECURITY", 18, false},
                        {"fd_count", "FDS", 6, true},
                        {"scheduling", "SCHED", 10, false},
                        {"rt_priority", "RTPRI", 6, true},
                        {"pss", "PSS", 9, true},
                        {"uss", "USS", 9, true},
                        {"swap", "SWAP", 9, true},
                        {"minor_faults", "MINFLT", 9, true},
                        {"major_faults", "MAJFLT", 8, true},
                        {"utime", "UTIME", 9, true},
                        {"stime", "STIME", 9, true},
                        {"context_switches", "CTX", 9, true},
                        {"io_priority", "IO PRIO", 14, false},
                        {"tgid", "TGID", 7, true},
                        {"sid", "SID", 7, true},
                        {"pgrp", "PGRP", 7, true},
                        {"tty", "TTY", 10, false},
                        {"tpgid", "TPGID", 7, true},
                        {"oom_score", "OOM", 6, true},
                        {"oom_adj", "OOM ADJ", 8, true},
                        {"policy", "POLICY", 10, false},
                        {"cpus_allowed", "CPUS", 14, false},
                        {"last_cpu", "CPU#", 5, true},
                        {"start_time", "START", 12, true},
                        {"syscall", "SYSCALL", 8, true},
                        {"io_rchar", "RCHAR/s", 10, true},
                        {"io_wchar", "WCHAR/s", 10, true},
                        {"io_read_bytes", "READ B", 10, true},
                        {"io_write_bytes", "WRITE B", 10, true},
                        {"io_syscr", "SYSR", 8, true},
                        {"io_syscw", "SYSCW", 8, true},
                        {"io_cancelled", "CANCEL", 8, true},
                        {"delay_cpu", "CPU DLY", 9, true},
                        {"delay_io", "IO DLY", 9, true},
                        {"delay_swap", "SWAP DLY", 9, true},
                        {"vm_hwm", "VMHWM", 9, true},
                        {"vm_rss", "VMRSS", 9, true},
                        {"vm_data", "VMDATA", 9, true},
                        {"vm_stk", "VMSTK", 7, true},
                        {"vm_exe", "VMEXE", 8, true},
                        {"vm_lib", "VMLIB", 8, true},
                        {"vm_swap", "VMSWAP", 9, true},
                        {"rss_anon", "RSS ANON", 9, true},
                        {"rss_file", "RSS FILE", 9, true},
                        {"rss_shmem", "RSS SHM", 9, true},
                        {"pss_anon", "PSS ANON", 9, true},
                        {"pss_file", "PSS FILE", 9, true},
                        {"pss_shmem", "PSS SHM", 9, true},
                        {"uss_clean", "USS CLN", 9, true},
                        {"uss_dirty", "USS DRY", 9, true},
                        {"swap_pss", "SWAP PSS", 9, true},
                        {"locked", "LOCKED", 9, true},
                };
                static_assert(sizeof(catalog) / sizeof(catalog[0]) == 90,
                              "NexusFlux exposes exactly 90 process columns");
                static const vector<column_def> value(catalog, catalog + sizeof(catalog) / sizeof(catalog[0]));
                return value;
            }

            static const column_def& column_definition(string_view key) {
                for (const auto& item: column_catalog()) {
                    if (item.key_ == key) {
                        return item;
                    }
                }
                static const column_def fallback{"field", "FIELD", 12, false};
                return fallback;
            }

            NEFORCE_NODISCARD vector<column_def> active_columns() const {
                vector<column_def> all;
                if (cfg_.columns_.empty()) {
                    for (const char* key: {"pid", "user", "cpu", "memory", "vram", "gpu", "state", "command"}) {
                        all.push_back(column_definition(key));
                    }
                } else {
                    for (const auto& key: cfg_.columns_) {
                        all.push_back(column_definition(key.view()));
                    }
                }
                if (all.empty()) {
                    return all;
                }
                const size_t offset = neforce::min(column_offset_, all.size() - 1);
                return vector<column_def>(all.begin() + static_cast<ptrdiff_t>(offset), all.end());
            }

            NEFORCE_NODISCARD static optional<string> extra_text(const process& item, const char* section,
                                                                 const char* key) {
                const json* block = item.extra_.get(section);
                if (block == nullptr || !block->is_object()) {
                    return none;
                }
                const json* value = block->get(key);
                if (value == nullptr || value->is_null()) {
                    return none;
                }
                if (value->is_string()) {
                    return value->as_string();
                }
                return value->dump();
            }

            NEFORCE_NODISCARD color process_tint_value(double cpu) const {
                cpu = neforce::clamp(cpu, 0.0, 100.0);
                const color& start = cpu < 50.0 ? colors_.proc_start_ : colors_.proc_mid_;
                const color& finish = cpu < 50.0 ? colors_.proc_mid_ : colors_.proc_end_;
                const double fraction = cpu < 50.0 ? cpu / 50.0 : (cpu - 50.0) / 50.0;
                const auto blend_channel = [fraction](int from, int to) {
                    return static_cast<int>(
                            neforce::round(static_cast<double>(from) + (static_cast<double>(to) - from) * fraction));
                };
                const color accent{blend_channel(start.R(), finish.R()), blend_channel(start.G(), finish.G()),
                                   blend_channel(start.B(), finish.B())};
                if (!cfg_.display_.process_gradient_) {
                    return accent;
                }
                const auto mix = [](const color& base, const color& tint, int weight) {
                    return color{(base.R() * (10 - weight) + tint.R() * weight) / 10,
                                 (base.G() * (10 - weight) + tint.G() * weight) / 10,
                                 (base.B() * (10 - weight) + tint.B() * weight) / 10};
                };
                return mix(colors_.text_, accent, static_cast<int>(neforce::round(3.0 + cpu * 0.07)));
            }

            NEFORCE_NODISCARD color process_tint(const process& item) const {
                return process_tint_value(static_cast<double>(item.cpu_));
            }

            NEFORCE_NODISCARD pair<string, color> column_value(const process& item, const string& key,
                                                               const process_row* row = nullptr) const {
                if (key == "pid") {
                    const bool marked = marked_processes_.count({item.pid_, item.start_time_}) != 0;
                    return {format("{}{}", marked ? "*" : "", static_cast<uint64_t>(item.pid_)), colors_.text_};
                }
                if (key == "ppid") {
                    return {item.parent_.has_value() ? format("{}", *item.parent_) : string("—"), colors_.muted_};
                }
                if (key == "user") {
                    return {item.user_, colors_.muted_};
                }
                if (key == "uid") {
                    return {extra_text(item, "status", "Uid").has_value() ? *extra_text(item, "status", "Uid")
                                                                          : string("—"),
                            colors_.muted_};
                }
                if (key == "cpu") {
                    const double value = row != nullptr && row->group_count_ > 1 ? row->group_cpu_ : item.cpu_;
                    return {value < 0.05 ? string("0.0") : format("{:.1f}", value),
                            value >= 50 ? colors_.red_ : (value >= 20 ? colors_.amber_ : colors_.mint_)};
                }
                if (key == "cpu_normalized") {
                    const double cores = static_cast<double>(neforce::max<size_t>(snap_.cores_.size(), 1));
                    const double value = item.cpu_ / cores;
                    return {format("{:.1f}", value), colors_.mint_};
                }
                if (key == "memory") {
                    const double value = row != nullptr && row->group_count_ > 1
                                                 ? static_cast<double>(row->group_memory_)
                                                 : static_cast<double>(item.memory_);
                    return {bytes(value), colors_.purple_};
                }
                if (key == "memory_percent") {
                    return {format("{:.1f}", percent(item.memory_, snap_.memory_.total_)), colors_.purple_};
                }
                if (key == "vms") {
                    return {bytes(static_cast<double>(item.virtual_memory_)), colors_.purple_};
                }
                if (key == "shared") {
                    return {extra_text(item, "shared_bytes", "").has_value()
                                    ? bytes(static_cast<double>(item.extra_.get("shared_bytes")->as_integer()))
                                    : string("—"),
                            colors_.purple_};
                }
                if (key == "vram") {
                    return {item.gpu_memory_.has_value() ? bytes(static_cast<double>(*item.gpu_memory_)) : string("—"),
                            colors_.blue_};
                }
                if (key == "gpu") {
                    return {item.gpu_percent_.has_value() ? format("{}", *item.gpu_percent_) : string("—"),
                            colors_.blue_};
                }
                if (key == "state") {
                    return {item.state_, state_color(item.state_, colors_)};
                }
                if (key == "name") {
                    return {row != nullptr && row->group_count_ > 1 && !row->tree_aggregate_
                                    ? format("{} ({})", item.name_, static_cast<uint64_t>(row->group_count_))
                                    : item.name_,
                            colors_.text_};
                }
                if (key == "comm" || key == "comm_extra") {
                    const auto comm = extra_text(item, "comm", "");
                    return {comm.has_value() ? *comm : item.name_, colors_.muted_};
                }
                if (key == "command") {
                    string command = item.command_.empty() ? item.name_ : item.command_;
                    if (cfg_.display_.process_basename_) {
                        command = basename_of(command.view());
                    }
                    if (row != nullptr && row->group_count_ > 1 && !row->tree_aggregate_) {
                        command.append(format(" ({})", static_cast<uint64_t>(row->group_count_)));
                    }
                    return {command, colors_.muted_};
                }
                if (key == "threads") {
                    return {extra_text(item, "stat", "threads").has_value() ? *extra_text(item, "stat", "threads")
                                                                            : string("—"),
                            colors_.muted_};
                }
                if (key == "priority" || key == "processor" || key == "scheduling" || key == "minor_faults" ||
                    key == "major_faults" || key == "utime" || key == "stime") {
                    const auto value = extra_text(item, "stat", key.view().data());
                    return {value.has_value() ? *value : string("—"), colors_.muted_};
                }
                if (key == "rt_priority") {
                    const auto value = extra_text(item, "stat", "rt_priority");
                    return {value.has_value() ? *value : string("—"), colors_.muted_};
                }
                if (key == "pss" || key == "uss" || key == "swap") {
                    const char* field = key == "pss" ? "Pss" : (key == "uss" ? "Private_Clean" : "Swap");
                    const auto value = extra_text(item, "smaps_rollup", field);
                    return {value.has_value() ? *value : string("—"), colors_.purple_};
                }
                if (key == "nice") {
                    return {item.nice_.has_value() ? format("{}", *item.nice_) : string("—"), colors_.muted_};
                }
                if (key == "runtime") {
                    return {duration(item.runtime_), colors_.muted_};
                }
                if (key == "time") {
                    return {duration(item.cpu_time_ms_ / 1000), colors_.muted_};
                }
                if (key == "read") {
                    return {cfg_.display_.rate(item.read_per_sec_), colors_.amber_};
                }
                if (key == "write") {
                    return {cfg_.display_.rate(item.write_per_sec_), colors_.amber_};
                }
                if (key == "read_total") {
                    return {bytes(static_cast<double>(item.read_total_)), colors_.muted_};
                }
                if (key == "write_total") {
                    return {bytes(static_cast<double>(item.write_total_)), colors_.muted_};
                }
                if (key == "rx") {
                    return {item.rx_per_sec_.has_value() ? cfg_.display_.rate(*item.rx_per_sec_) : string("—"),
                            colors_.mint_};
                }
                if (key == "tx") {
                    return {item.tx_per_sec_.has_value() ? cfg_.display_.rate(*item.tx_per_sec_) : string("—"),
                            colors_.blue_};
                }
                if (key == "tag") {
                    return {tag_text(item.tags_), colors_.blue_};
                }
                if (key == "thread") {
                    return {item.thread_ ? "yes" : "no", colors_.muted_};
                }
                if (key == "kernel_thread") {
                    return {item.kernel_thread_ ? "yes" : "no", colors_.muted_};
                }
                if (key == "oom") {
                    return {extra_text(item, "oom", "").has_value() ? *extra_text(item, "oom", "") : string("—"),
                            colors_.muted_};
                }
                if (key == "cgroup" || key == "wchan" || key == "security" || key == "exe" || key == "cwd" ||
                    key == "io_priority") {
                    const char* section = key == "io_priority" ? "io_priority" : key.view().data();
                    const auto value = extra_text(item, section, "");
                    return {value.has_value() ? truncate_display(value->view(), 60) : string("—"), colors_.muted_};
                }
                if (key == "autogroup") {
                    const auto value = extra_text(item, "autogroup", "");
                    return {value.has_value() ? *value : string("—"), colors_.muted_};
                }
                if (key == "fd_count") {
                    return {extra_text(item, "fd_count", "").has_value() ? *extra_text(item, "fd_count", "")
                                                                         : string("—"),
                            colors_.muted_};
                }
                if (key == "context_switches") {
                    const auto value = extra_text(item, "status", "voluntary_ctxt_switches");
                    return {value.has_value() ? *value : string("—"), colors_.muted_};
                }
                if (key == "sid" || key == "pgrp" || key == "tty" || key == "tpgid" || key == "last_cpu" ||
                    key == "policy") {
                    const char* field = key == "sid"        ? "session"
                                        : key == "last_cpu" ? "processor"
                                        : key == "policy"   ? "policy"
                                                            : key.view().data();
                    const auto value = extra_text(item, "stat", field);
                    return {value.has_value() ? *value : string("—"), colors_.muted_};
                }
                if (key == "tgid" || key == "cpus_allowed") {
                    const char* field = key == "tgid" ? "Tgid" : "Cpus_allowed_list";
                    const auto value = extra_text(item, "status", field);
                    return {value.has_value() ? *value : string("—"), colors_.muted_};
                }
                if (key == "oom_score" || key == "oom_adj") {
                    const json* value = item.extra_.get("oom");
                    return {value != nullptr ? value->dump() : string("—"), colors_.muted_};
                }
                if (key == "start_time") {
                    return {item.start_time_ == 0 ? string("—") : format("{}", item.start_time_), colors_.muted_};
                }
                if (key == "syscall") {
                    const json* value = item.extra_.get("syscall");
                    return {value != nullptr ? value->dump() : string("—"), colors_.muted_};
                }
                if (starts_with(key.view(), "io_")) {
                    const char* field = key == "io_rchar"         ? "rchar"
                                        : key == "io_wchar"       ? "wchar"
                                        : key == "io_read_bytes"  ? "read_bytes"
                                        : key == "io_write_bytes" ? "write_bytes"
                                        : key == "io_syscr"       ? "syscr"
                                        : key == "io_syscw"       ? "syscw"
                                                                  : "cancelled_write_bytes";
                    const auto value = extra_text(item, "io", field);
                    if (!value.has_value()) {
                        return {"—", colors_.muted_};
                    }
                    if (key == "io_read_bytes" || key == "io_write_bytes") {
                        const auto number = parse_u64(value->view());
                        return {number.has_value() ? bytes(static_cast<double>(*number)) : *value, colors_.amber_};
                    }
                    return {*value, colors_.muted_};
                }
                if (starts_with(key.view(), "delay_")) {
                    const json* value = item.extra_.get(key);
                    return {value != nullptr ? value->dump() : string("—"), colors_.muted_};
                }
                if (starts_with(key.view(), "vm_")) {
                    struct vm_field {
                        const char* key;
                        const char* field;
                    };
                    static const vm_field vm_fields[] = {
                            {"vm_hwm", "VmHWM"}, {"vm_rss", "VmRSS"}, {"vm_data", "VmData"}, {"vm_stk", "VmStk"},
                            {"vm_exe", "VmExe"}, {"vm_lib", "VmLib"}, {"vm_swap", "VmSwap"}};
                    const char* field = nullptr;
                    for (const auto& item_field: vm_fields) {
                        if (key == item_field.key) {
                            field = item_field.field;
                        }
                    }
                    if (field == nullptr) {
                        return {"—", colors_.muted_};
                    }
                    const auto value = extra_text(item, "status", field);
                    return {value.has_value() ? *value : string("—"), colors_.purple_};
                }
                if (key == "rss_anon" || key == "rss_file" || key == "rss_shmem") {
                    const char* field = key == "rss_anon" ? "RssAnon" : (key == "rss_file" ? "RssFile" : "RssShmem");
                    const auto value = extra_text(item, "status", field);
                    return {value.has_value() ? *value : string("—"), colors_.purple_};
                }
                if (key == "pss_anon" || key == "pss_file" || key == "pss_shmem") {
                    const char* field = key == "pss_anon" ? "Pss_Anon" : (key == "pss_file" ? "Pss_File" : "Pss_Shmem");
                    const auto value = extra_text(item, "smaps_rollup", field);
                    return {value.has_value() ? *value : string("—"), colors_.purple_};
                }
                if (key == "uss_clean" || key == "uss_dirty") {
                    const char* field = key == "uss_clean" ? "Private_Clean" : "Private_Dirty";
                    const auto value = extra_text(item, "smaps_rollup", field);
                    return {value.has_value() ? *value : string("—"), colors_.purple_};
                }
                if (key == "swap_pss") {
                    const auto value = extra_text(item, "smaps_rollup", "SwapPss");
                    return {value.has_value() ? *value : string("—"), colors_.purple_};
                }
                if (key == "locked") {
                    const auto value = extra_text(item, "status", "VmLck");
                    return {value.has_value() ? *value : string("—"), colors_.purple_};
                }
                if (const json* value = item.extra_.get(key); value != nullptr) {
                    return {value->is_string() ? value->as_string() : value->dump(), colors_.muted_};
                }
                return {"—", colors_.muted_};
            }

            NEFORCE_NODISCARD process_table_view layout_process_table(const rect& area,
                                                                      const vector<process_row>& rows) {
                process_table_rect_ = area;
                process_table_view view;
                view.area_ = area;
                view.tree_ = tree_;
                view.total_ = rows.size();
                const auto columns = active_columns();
                int fixed = 0;
                bool has_fill = false;
                for (const auto& item: columns) {
                    fixed += item.width_;
                    has_fill = has_fill || item.width_ == 0;
                }
                const int fill_width = has_fill ? neforce::max(8, area.w_ - fixed - 2) : 0;
                view.inner_ = rect{area.x_ + 1, area.y_ + 1, area.w_ - 2, area.h_ - 2};
                view.header_y_ = area.y_ + 1;
                view.title_ = format(" PROCESSES · {} columns ", columns.size());
                int x = view.inner_.x_;
                size_t column_hit = 0;
                for (const auto& item: columns) {
                    const int width = item.width_ == 0 ? fill_width : item.width_;
                    if (x + width > view.inner_.right()) {
                        break;
                    }
                    process_cell_view header;
                    header.x_ = x;
                    header.width_ = width;
                    header.text_ = item.title_;
                    header.fg_ = colors_.muted_;
                    header.right_ = item.right_;
                    view.header_cells_.push_back(move(header));
                    hits_.push_back({{x, view.header_y_, width, 1}, hit_action::process_sort, column_hit});
                    ++column_hit;
                    x += width;
                }
                view.visible_ = neforce::max(0, view.inner_.h_ - 2);
                if (!rows.empty() && selected_ >= rows.size()) {
                    selected_ = rows.size() - 1;
                }
                if (!free_scroll_) {
                    if (selected_ >= table_offset_ + static_cast<size_t>(view.visible_)) {
                        table_offset_ = selected_ + 1 - static_cast<size_t>(view.visible_);
                    }
                    table_offset_ = neforce::min(selected_, table_offset_);
                } else {
                    table_offset_ =
                            rows.size() > static_cast<size_t>(view.visible_)
                                    ? neforce::min(table_offset_, rows.size() - static_cast<size_t>(view.visible_))
                                    : 0;
                }
                view.offset_ = table_offset_;
                for (int row = 0; row < view.visible_; ++row) {
                    const size_t index = table_offset_ + static_cast<size_t>(row);
                    if (index >= rows.size()) {
                        break;
                    }
                    const auto& item = snap_.processes_[rows[index].index_];
                    process_table_row_view row_view;
                    row_view.index_ = index;
                    row_view.process_index_ = rows[index].index_;
                    row_view.y_ = view.inner_.y_ + 1 + row;
                    row_view.active_ = index == selected_;
                    const bool marked = marked_processes_.count({item.pid_, item.start_time_}) != 0;
                    row_view.followed_ = follow_process_ && selected_process_.has_value() &&
                                         selected_process_->first == item.pid_ &&
                                         selected_process_->second == item.start_time_;
                    row_view.bg_ = row_view.active_ ? colors_.selection_ : colors_.panel_;
                    if (row_view.followed_ && !row_view.active_) {
                        row_view.bg_ = colors_.followed_bg_;
                    } else if (marked && !row_view.active_) {
                        row_view.bg_ = colors_.bg_;
                    }
                    row_view.fg_ = row_view.followed_ ? colors_.followed_fg_ : colors_.text_;
                    row_view.indent_ = neforce::min(rows[index].depth_, 4);
                    hits_.push_back({{view.inner_.x_, row_view.y_, view.inner_.w_, 1},
                                     hit_action::process_row,
                                     index,
                                     item.pid_,
                                     item.start_time_});
                    if (tree_ && row_view.indent_ > 0) {
                        row_view.toggle_width_ = neforce::min(view.inner_.w_, row_view.indent_ * 2);
                        hits_.push_back({{view.inner_.x_, row_view.y_, row_view.toggle_width_, 1},
                                         hit_action::process_tree_toggle,
                                         index,
                                         item.pid_,
                                         item.start_time_});
                    }
                    int cx = view.inner_.x_;
                    for (const auto& column: columns) {
                        const int width = column.width_ == 0 ? fill_width : column.width_;
                        if (cx + width > view.inner_.right() || width <= 0) {
                            break;
                        }
                        auto cell = column_value(item, column.key_, &rows[index]);
                        if (cfg_.display_.process_colors_ && !row_view.active_ &&
                            (column.key_ == "command" || column.key_ == "name" || column.key_ == "user")) {
                            const double cpu = rows[index].tree_aggregate_ ? rows[index].group_cpu_
                                                                           : static_cast<double>(item.cpu_);
                            cell.second = process_tint_value(cpu);
                        }
                        if ((column.key_ == "command" || column.key_ == "name") && tree_ && row_view.indent_ > 0) {
                            string prefix;
                            for (int level = 1; level < row_view.indent_; ++level) {
                                prefix.append("  ");
                            }
                            prefix.append("└ ");
                            cell.first = prefix + cell.first;
                        }
                        process_cell_view cell_view;
                        cell_view.x_ = cx;
                        cell_view.width_ = width;
                        cell_view.text_ = truncate_display(cell.first.view(), width - 1);
                        cell_view.fg_ = cell.second;
                        cell_view.right_ = column.right_;
                        row_view.cells_.push_back(move(cell_view));
                        cx += width;
                    }
                    view.rows_.push_back(move(row_view));
                }
                view.scrollbar_ =
                        rect{view.inner_.right() - 1, view.inner_.y_ + 1, 1, neforce::max(0, view.inner_.h_ - 2)};
                hits_.push_back({view.scrollbar_, hit_action::process_scrollbar, 0});
                return view;
            }

            void paint_process_table(painter& brush, const process_table_view& view) {
                brush.box(view.area_, view.title_.view(), colors_.mint_);
                brush.fill(rect{view.inner_.x_, view.header_y_, view.inner_.w_, 1}, " ", colors_.muted_,
                           colors_.panel_);
                for (const auto& cell: view.header_cells_) {
                    if (cell.right_) {
                        brush.text_right(cell.x_, view.header_y_, cell.width_ - 1, cell.text_.view(), cell.fg_,
                                         colors_.panel_, true);
                    } else {
                        brush.text(cell.x_ + 1, view.header_y_, cell.text_.view(), cell.fg_, colors_.panel_, true);
                    }
                }
                for (const auto& row: view.rows_) {
                    brush.fill(rect{view.inner_.x_, row.y_, view.inner_.w_, 1}, " ", row.fg_, row.bg_);
                    for (const auto& cell: row.cells_) {
                        if (cell.right_) {
                            brush.text_right(cell.x_, row.y_, cell.width_ - 1, cell.text_.view(), cell.fg_, row.bg_);
                        } else {
                            brush.text(cell.x_ + 1, row.y_, cell.text_.view(), cell.fg_, row.bg_);
                        }
                    }
                }
                brush.scrollbar(view.scrollbar_, view.total_, static_cast<size_t>(view.visible_), view.offset_);
            }

            NEFORCE_NODISCARD vector<string> history_keys() const {
                vector<string> keys;
                for (const auto& entry: history_.series_) {
                    if (!filter_.empty() && !contains_ignore_case(entry.first.view(), filter_.view())) {
                        continue;
                    }
                    keys.push_back(entry.first);
                }
                return keys;
            }

            void draw_history_page(painter& brush, const rect& area) {
                const int left = area.w_ * 35 / 100;
                const rect list{area.x_, area.y_, neforce::max(20, left - 1), area.h_};
                brush.box(list, format(" SERIES · {} ", history_keys().size()).view(), colors_.mint_);
                const auto keys = history_keys();
                const int visible = neforce::max(0, list.h_ - 2);
                if (!keys.empty() && selected_ >= keys.size()) {
                    selected_ = keys.size() - 1;
                }
                if (!free_scroll_) {
                    if (selected_ >= table_offset_ + static_cast<size_t>(visible)) {
                        table_offset_ = selected_ + 1 - static_cast<size_t>(visible);
                    }
                    table_offset_ = neforce::min(selected_, table_offset_);
                } else {
                    table_offset_ = keys.size() > static_cast<size_t>(visible)
                                            ? neforce::min(table_offset_, keys.size() - static_cast<size_t>(visible))
                                            : 0;
                }
                for (int row = 0; row < visible && table_offset_ + static_cast<size_t>(row) < keys.size(); ++row) {
                    const size_t index = table_offset_ + static_cast<size_t>(row);
                    const bool active = index == selected_;
                    const int y = list.y_ + 2 + row;
                    if (active) {
                        brush.fill(rect{list.x_ + 1, y, list.w_ - 2, 1}, " ", colors_.text_, colors_.selection_);
                    }
                    hits_.push_back({{list.x_ + 1, y, list.w_ - 2, 1}, hit_action::history_row, index});
                    brush.text(list.x_ + 2, y, truncate_display(keys[index].view(), list.w_ - 4).view(),
                               active ? colors_.mint_ : colors_.text_, active ? colors_.selection_ : colors_.panel_);
                }
                const rect history_scrollbar{list.right() - 2, list.y_ + 2, 2, neforce::max(0, list.h_ - 3)};
                hits_.push_back({history_scrollbar, hit_action::history_scroll, 0});
                brush.scrollbar(history_scrollbar, keys.size(), static_cast<size_t>(visible), table_offset_);
                const rect right{area.x_ + list.w_ + 1, area.y_, area.w_ - list.w_ - 1, area.h_};
                if (keys.empty()) {
                    brush.box(right, " HISTORY ", colors_.blue_);
                    brush.empty(right.inset(1), "No series", "Select another view or wait for samples.");
                    return;
                }
                const string& key = keys[selected_];
                const auto found = history_.series_.find(key);
                if (found == history_.series_.end()) {
                    return;
                }
                const auto& values = found->second;
                double maximum = 1.0;
                for (const double value: values) {
                    if (neforce::is_finite(value)) {
                        maximum = neforce::max(maximum, value);
                    }
                }
                const bool percentage = key.contains("(%)") && !key.contains("main CPU");
                const double axis = percentage ? 100.0 : tui::canvas_chart_nice_max(maximum * 1.1);
                string unit;
                const auto unit_begin = key.rfind(" (");
                if (unit_begin != string::npos && key[key.size() - 1] == ')') {
                    unit = key.substr(unit_begin + 2, key.size() - unit_begin - 3);
                }
                brush.box(right,
                          format(" {} · {} samples · latest {} ", key, static_cast<uint64_t>(values.size()),
                                 values.empty() ? string("—") : format("{:.2f}", values.back()))
                                  .view(),
                          colors_.blue_);
                brush.chart(rect{right.x_ + 1, right.y_ + 1, right.w_ - 2, right.h_ - 2}, "Sample history",
                            {chart_series{"value", &values, colors_.mint_}}, axis, unit.view(), chart_window());
            }

            void draw_events_page(painter& brush, const rect& area) {
                brush.box(area,
                          format(" EVENTS · {}s hold · {} recorded ", static_cast<uint64_t>(cfg_.alert_hold_seconds_),
                                 static_cast<uint64_t>(alert_events_.size()))
                                  .view(),
                          colors_.amber_);
                vector<const event*> rows;
                for (auto iterator = alert_events_.rbegin(); iterator != alert_events_.rend(); ++iterator) {
                    if (filter_.empty() || event_matches(*iterator, filter_)) {
                        rows.push_back(&*iterator);
                    }
                }
                if (!rows.empty() && selected_ >= rows.size()) {
                    selected_ = rows.size() - 1;
                }
                const int visible = neforce::max(0, area.h_ - 3);
                if (selected_ >= table_offset_ + static_cast<size_t>(visible)) {
                    table_offset_ = selected_ + 1 - static_cast<size_t>(visible);
                }
                table_offset_ = neforce::min(selected_, table_offset_);
                brush.text(area.x_ + 2, area.y_ + 1, "UNIX TIME      STATE        CONDITION", colors_.muted_, none,
                           true);
                for (int row = 0; row < visible && table_offset_ + static_cast<size_t>(row) < rows.size(); ++row) {
                    const size_t index = table_offset_ + static_cast<size_t>(row);
                    const auto* item = rows[index];
                    const int y = area.y_ + 2 + row;
                    const bool active = index == selected_;
                    if (active) {
                        brush.fill(rect{area.x_ + 1, y, area.w_ - 2, 1}, " ", colors_.text_, colors_.selection_);
                    }
                    hits_.push_back({{area.x_ + 1, y, area.w_ - 2, 1}, hit_action::event_row, index});
                    const string line = format("{:<14} {:<12} {}", static_cast<uint64_t>(item->timestamp_),
                                               item->active_ ? "ACTIVE" : "RECOVERED", item->condition_);
                    brush.text(area.x_ + 2, y, truncate_display(line.view(), area.w_ - 4).view(),
                               item->active_ ? colors_.amber_ : colors_.muted_,
                               active ? colors_.selection_ : colors_.panel_);
                }
                if (rows.empty()) {
                    brush.empty(area.inset(1), "No alert events", "Alerts appear after the configured hold time.");
                }
                const rect events_scrollbar{area.right() - 3, area.y_ + 2, 2, neforce::max(0, area.h_ - 3)};
                hits_.push_back({events_scrollbar, hit_action::events_scroll, 0});
                brush.scrollbar(events_scrollbar, rows.size(), static_cast<size_t>(visible), table_offset_);
            }

            NEFORCE_NODISCARD vector<pair<string, json>> extra_rows() const {
                vector<pair<string, json>> rows;
                const string needle = lower_ascii(filter_.view());
                for (const auto& entry: snap_.extra_.as_object()) {
                    if (needle.empty() || contains_ignore_case(entry.first.view(), needle.view())) {
                        rows.push_back(entry);
                    }
                }
                for (const auto& entry: inventory_.extra_.as_object()) {
                    if (needle.empty() || contains_ignore_case(entry.first.view(), needle.view())) {
                        rows.push_back(entry);
                    }
                }
                return rows;
            }

            void draw_explorer_page(painter& brush, const rect& area) {
                explorer_rect_ = area;
                const auto rows = extra_rows();
                brush.box(area, format(" SYSTEM EXPLORER · {} ", rows.size()).view(), colors_.blue_);
                brush.text(area.x_ + 2, area.y_ + 1, "SOURCE / CAPABILITY", colors_.muted_);
                brush.text(area.x_ + area.w_ * 35 / 100, area.y_ + 1, "DATA / STATUS", colors_.muted_);
                const int visible = neforce::max(0, area.h_ - 3);
                if (!rows.empty() && selected_ >= rows.size()) {
                    selected_ = rows.size() - 1;
                }
                if (selected_ >= table_offset_ + static_cast<size_t>(visible)) {
                    table_offset_ = selected_ + 1 - static_cast<size_t>(visible);
                }
                table_offset_ = neforce::min(selected_, table_offset_);
                const int name_width = area.w_ * 35 / 100 - 2;
                for (int offset = 0; offset < visible && table_offset_ + static_cast<size_t>(offset) < rows.size();
                     ++offset) {
                    const size_t index = table_offset_ + static_cast<size_t>(offset);
                    const int y = area.y_ + 2 + offset;
                    const bool active = index == selected_;
                    hits_.push_back({{area.x_ + 1, y, area.w_ - 2, 1}, hit_action::explorer_row, index});
                    if (active) {
                        brush.fill(rect{area.x_ + 1, y, area.w_ - 2, 1}, " ", colors_.text_, colors_.selection_);
                    }
                    const auto& entry = rows[index];
                    string summary;
                    if (entry.second.is_array()) {
                        summary = format("{} records · Enter inspect", static_cast<uint64_t>(entry.second.size()));
                    } else if (entry.second.is_object()) {
                        const json* error = entry.second.get("error");
                        if (error != nullptr) {
                            summary = error->is_string() ? error->as_string() : error->dump();
                        } else {
                            summary = format("{} fields · Enter inspect", entry.second.size());
                        }
                    } else if (entry.second.is_string()) {
                        const auto lines = split_lines(entry.second.as_string());
                        summary = lines.empty() ? string() : lines[0];
                    } else {
                        summary = entry.second.dump();
                    }
                    brush.text(area.x_ + 2, y, truncate_display(entry.first.view(), name_width).view(),
                               active ? colors_.text_ : colors_.mint_);
                    brush.text(area.x_ + area.w_ * 35 / 100, y,
                               truncate_display(summary.view(), area.w_ - area.w_ * 35 / 100 - 2).view(),
                               active ? colors_.text_ : colors_.muted_);
                }
                if (rows.empty()) {
                    brush.empty(area.inset(1), "No extra sources",
                                "Open Network, Storage or System to collect extras.");
                }
                const rect explorer_scrollbar{area.right() - 1, area.y_ + 2, 1, neforce::max(0, area.h_ - 3)};
                hits_.push_back({explorer_scrollbar, hit_action::explorer_scroll, 0});
                brush.scrollbar(explorer_scrollbar, rows.size(), static_cast<size_t>(visible), table_offset_);
            }

            void open_extra_detail() {
                const auto rows = extra_rows();
                if (rows.empty()) {
                    return;
                }
                const auto& entry = rows[neforce::min(selected_, rows.size() - 1)];
                const string dumped = entry.second.dump(true);
                detail_lines_ = split_lines(dumped);
                if (detail_lines_.empty()) {
                    detail_lines_.push_back(dumped.empty() ? string("(no data)") : dumped);
                }
                popup_title_ = entry.first;
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            void draw_gpu_page(painter& brush, const rect& area) {
                if (snap_.gpus_.empty()) {
                    brush.box(area, " GPU ", colors_.blue_);
                    brush.empty(area.inset(1), "No readable GPU telemetry", snap_.gpu_status_.view());
                    return;
                }
                if (selected_gpu_ >= snap_.gpus_.size()) {
                    selected_gpu_ = snap_.gpus_.size() - 1;
                }
                const size_t index = selected_gpu_;
                const auto& device = snap_.gpus_[index];
                const int cards_height = neforce::min(9, area.h_ / 3);
                const size_t page_start = index / 3 * 3;
                const size_t page_end = neforce::min(snap_.gpus_.size(), page_start + 3);
                const int card_width = neforce::max(28, area.w_ / static_cast<int>(page_end - page_start));
                int x = area.x_;
                for (size_t i = page_start; i < page_end; ++i) {
                    const auto& item = snap_.gpus_[i];
                    const rect card{x, area.y_, card_width - 1, cards_height};
                    const bool active = i == index;
                    hits_.push_back({card, hit_action::gpu_device, i});
                    brush.box(card, format(" GPU {} · {} ", item.id_, item.vendor_).view(),
                              active ? colors_.blue_ : colors_.muted_);
                    if (card.h_ > 3) {
                        brush.text(card.x_ + 2, card.y_ + 1, truncate_display(item.name_.view(), card.w_ - 4).view(),
                                   item.utilization_.has_value() ? colors_.text_ : colors_.muted_);
                        const bool has_telemetry = item.utilization_.has_value() || item.memory_total_.has_value() ||
                                                   item.temperature_.has_value() || item.clock_mhz_.has_value();
                        const string line =
                                has_telemetry
                                        ? format("GPU {} · VRAM {} · {}",
                                                 item.utilization_.has_value() ? format("{}%", *item.utilization_)
                                                                               : string("—"),
                                                 item.memory_percent().has_value()
                                                         ? format("{:.0f}%", *item.memory_percent())
                                                         : string("—"),
                                                 item.temperature_.has_value()
                                                         ? cfg_.display_.temperature(*item.temperature_, 0)
                                                         : string("—"))
                                        : (item.process_status_.empty() ? snap_.gpu_status_ : item.process_status_);
                        brush.text(card.x_ + 2, card.y_ + 2, truncate_display(line.view(), card.w_ - 4).view(),
                                   colors_.muted_);
                        brush.track(card.x_ + 2, card.y_ + 3, neforce::max(1, card.w_ - 4),
                                    item.utilization_.has_value() ? optional<double>(*item.utilization_) : none, 100.0,
                                    colors_.blue_);
                    }
                    x += card_width;
                }
                if (gpu_section_ == 2) {
                    const rect details{area.x_, area.y_ + cards_height, area.w_, area.h_ - cards_height};
                    const int left_width = neforce::max(30, details.w_ * 45 / 100);
                    const rect info{details.x_, details.y_, left_width - 1, details.h_};
                    const rect processes{details.x_ + left_width, details.y_, details.w_ - left_width, details.h_};
                    brush.box(info, " DEVICE ", colors_.blue_);
                    vector<pair<string, string>> rows;
                    rows.push_back({"Name", device.name_});
                    rows.push_back({"Vendor", device.vendor_});
                    rows.push_back({"Backend", device.backend_});
                    rows.push_back({"UUID", device.uuid_});
                    rows.push_back({"Driver", device.driver_.empty() ? "—" : device.driver_});
                    rows.push_back({"NVML", device.nvml_version_.has_value() ? *device.nvml_version_ : "—"});
                    rows.push_back(
                            {"CUDA", device.cuda_driver_version_.has_value() ? *device.cuda_driver_version_ : "—"});
                    rows.push_back(
                            {"GPU", device.utilization_.has_value() ? format("{}%", *device.utilization_) : "—"});
                    rows.push_back(
                            {"Memory", device.memory_used_.has_value() && device.memory_total_.has_value()
                                               ? format("{} / {}", bytes(static_cast<double>(*device.memory_used_)),
                                                        bytes(static_cast<double>(*device.memory_total_)))
                                               : "—"});
                    rows.push_back({"Memory bus", device.memory_utilization_.has_value()
                                                          ? format("{}%", *device.memory_utilization_)
                                                          : "—"});
                    rows.push_back({"Temperature", device.temperature_.has_value()
                                                           ? cfg_.display_.temperature(*device.temperature_, 0)
                                                           : "—"});
                    rows.push_back({"Fan", device.fan_.has_value() ? format("{}%", *device.fan_) : "—"});
                    rows.push_back({"Power", device.power_watts_.has_value()
                                                     ? format("{:.0f} / {:.0f} W", *device.power_watts_,
                                                              device.power_limit_watts_.value_or(*device.power_watts_))
                                                     : "—"});
                    rows.push_back(
                            {"Clock", device.clock_mhz_.has_value() ? format("{} MHz", *device.clock_mhz_) : "—"});
                    rows.push_back({"Memory clock", device.memory_clock_mhz_.has_value()
                                                            ? format("{} MHz", *device.memory_clock_mhz_)
                                                            : "—"});
                    rows.push_back(
                            {"PCIe", device.pcie_rx_kib_.has_value() && device.pcie_tx_kib_.has_value()
                                             ? format("{} / {} KiB/s", *device.pcie_rx_kib_, *device.pcie_tx_kib_)
                                             : "—"});
                    rows.push_back({"Encoder", device.encoder_.has_value() ? format("{}%", *device.encoder_) : "—"});
                    rows.push_back({"Decoder", device.decoder_.has_value() ? format("{}%", *device.decoder_) : "—"});
                    rows.push_back({"P-state", device.performance_state_.value_or("—")});
                    rows.push_back(
                            {"Status", device.process_status_.empty() ? snap_.gpu_status_ : device.process_status_});
                    rows.push_back({"Processes", format("{} sampled", device.processes_.size())});
                    int row = 0;
                    for (const auto& item: rows) {
                        const int y = info.y_ + 1 + row;
                        if (y >= info.bottom() - 1) {
                            break;
                        }
                        brush.text(info.x_ + 2, y, truncate_display(item.first.view(), 14).view(), colors_.muted_);
                        brush.text(info.x_ + 17, y, truncate_display(item.second.view(), info.w_ - 20).view(),
                                   colors_.text_);
                        ++row;
                    }
                    brush.box(processes, format(" GPU {} · {} processes ", device.id_, device.processes_.size()).view(),
                              colors_.blue_);
                    brush.text(processes.x_ + 2, processes.y_ + 1,
                               truncate_display((align_cell("PID", 8, true) + align_cell("PROCESS", 28) +
                                                 align_cell("GPU%", 6, true) + align_cell("VRAM", 10, true) + "KIND")
                                                        .view(),
                                                processes.w_ - 4)
                                       .view(),
                               colors_.muted_);
                    int process_row = 0;
                    for (const auto& item: device.processes_) {
                        const int y = processes.y_ + 2 + process_row;
                        if (y >= processes.bottom() - 1) {
                            break;
                        }
                        string name;
                        for (const auto& proc: snap_.processes_) {
                            if (proc.pid_ == item.pid_) {
                                name = proc.name_;
                                break;
                            }
                        }
                        const string line =
                                align_cell(format("{}", static_cast<uint64_t>(item.pid_)), 8, true) +
                                align_cell(name, 28) +
                                align_cell(item.utilization_.has_value() ? format("{}", *item.utilization_)
                                                                         : string("—"),
                                           6, true) +
                                align_cell(item.memory_.has_value() ? bytes(static_cast<double>(*item.memory_))
                                                                    : string("—"),
                                           10, true) +
                                item.kind_;
                        hits_.push_back(
                                {{processes.x_ + 1, y, processes.w_ - 2, 1}, hit_action::gpu_process_row, item.pid_});
                        brush.text(processes.x_ + 2, y, truncate_display(line.view(), processes.w_ - 4).view(),
                                   colors_.text_);
                        ++process_row;
                    }
                    if (device.processes_.empty()) {
                        brush.text(processes.x_ + 2, processes.y_ + 2,
                                   device.process_status_.empty() ? string_view("No processes")
                                                                  : device.process_status_.view(),
                                   colors_.muted_);
                    }
                    return;
                }
                const rect charts{area.x_, area.y_ + cards_height, area.w_, neforce::min(10, area.h_ / 3)};
                const auto found = history_.gpu_details_.find(device.uuid_);
                if (found != history_.gpu_details_.end()) {
                    graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                            .chart(rect{charts.x_, charts.y_, charts.w_ / 2 - 1, charts.h_}, "GPU utilization",
                                   {chart_series{"GPU", &found->second.utilization_, colors_.blue_}}, 100.0, "%",
                                   chart_window());
                    graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                            .chart(rect{charts.x_ + charts.w_ / 2, charts.y_, charts.w_ / 2, charts.h_},
                                   format("VRAM ({})", device.memory_label()).view(),
                                   {chart_series{"VRAM", &found->second.vram_, colors_.purple_}}, 100.0, "%",
                                   chart_window());
                }
                const rect table{area.x_, area.y_ + cards_height + charts.h_, area.w_,
                                 area.h_ - cards_height - charts.h_};
                brush.box(table, format(" GPU {} · {} processes ", device.id_, device.processes_.size()).view(),
                          colors_.blue_);
                if (table.h_ > 2) {
                    brush.text(table.x_ + 2, table.y_ + 1,
                               truncate_display((align_cell("PID", 8, true) + align_cell("PROCESS", 28) +
                                                 align_cell("GPU%", 6, true) + align_cell("VRAM", 10, true) + "KIND")
                                                        .view(),
                                                table.w_ - 4)
                                       .view(),
                               colors_.muted_);
                    int row = 0;
                    for (const auto& item: device.processes_) {
                        const int y = table.y_ + 2 + row;
                        if (y >= table.bottom() - 1) {
                            break;
                        }
                        string name;
                        for (const auto& proc: snap_.processes_) {
                            if (proc.pid_ == item.pid_) {
                                name = proc.name_;
                                break;
                            }
                        }
                        const string line =
                                align_cell(format("{}", static_cast<uint64_t>(item.pid_)), 8, true) +
                                align_cell(name, 28) +
                                align_cell(item.utilization_.has_value() ? format("{}", *item.utilization_)
                                                                         : string("—"),
                                           6, true) +
                                align_cell(item.memory_.has_value() ? bytes(static_cast<double>(*item.memory_))
                                                                    : string("—"),
                                           10, true) +
                                item.kind_;
                        brush.text(table.x_ + 2, y, truncate_display(line.view(), table.w_ - 4).view(), colors_.text_);
                        ++row;
                    }
                    if (device.processes_.empty()) {
                        brush.text(table.x_ + 2, table.y_ + 2,
                                   device.process_status_.empty() ? string_view("No processes")
                                                                  : device.process_status_.view(),
                                   colors_.muted_);
                    }
                }
            }

            void draw_storage_page(painter& brush, const rect& area) {
                if (!snap_.disk_io_.empty() && selected_io_ >= snap_.disk_io_.size()) {
                    selected_io_ = 0;
                }
                const disk_io* selected_device = !storage_device_auto_ && selected_io_ < snap_.disk_io_.size()
                                                         ? &snap_.disk_io_[selected_io_]
                                                         : nullptr;
                const disk_history* selected_history =
                        selected_device != nullptr ? history_.disk(selected_device->name_) : nullptr;
                const bool per_device = selected_device != nullptr && selected_history != nullptr;
                const deque<double>& chart_read = per_device ? selected_history->read_ : history_.disk_read_;
                const deque<double>& chart_write = per_device ? selected_history->write_ : history_.disk_write_;
                double read_total = 0.0;
                double write_total = 0.0;
                for (const auto& item: snap_.disk_io_) {
                    if (item.aggregate_) {
                        read_total += item.read_per_sec_;
                        write_total += item.write_per_sec_;
                    }
                }
                constexpr int stats_height = 3;
                const rect stats{area.x_, area.y_, area.w_, neforce::min(stats_height, area.h_)};
                brush.box(stats, " STORAGE ", colors_.amber_);
                const int stat_width = neforce::max(10, stats.w_ / 3);
                brush.text(stats.x_ + 2, stats.y_ + 1, format("FILESYSTEMS {:>3}", snap_.disks_.size()).view(),
                           colors_.amber_);
                brush.text(stats.x_ + stat_width, stats.y_ + 1, format("READ  {}/s", bytes(read_total)).view(),
                           colors_.mint_);
                brush.text(stats.x_ + stat_width * 2, stats.y_ + 1, format("WRITE {}/s", bytes(write_total)).view(),
                           colors_.blue_);
                int cursor = stats.bottom();
                const int chart_height = area.h_ >= 23 ? area.h_ / 3 : 0;
                if (chart_height > 0) {
                    const rect chart_area{area.x_, cursor, area.w_, chart_height};
                    double peak = 1024.0;
                    for (const double value: chart_read) {
                        peak = neforce::max(peak, value);
                    }
                    for (const double value: chart_write) {
                        peak = neforce::max(peak, value);
                    }
                    const string chart_title = per_device ? format(" DISK THROUGHPUT · {} ", selected_device->name_)
                                                          : string(" DISK THROUGHPUT · physical devices ");
                    brush.box(chart_area, chart_title.view(), colors_.amber_);
                    graph_painter(brush, cfg_.display_.graph_symbol_proc_)
                            .chart(chart_area.inset(1), per_device ? "selected device" : "read / write",
                                   {chart_series{"Read", &chart_read, colors_.mint_},
                                    chart_series{"Write", &chart_write, colors_.blue_}},
                                   tui::canvas_chart_nice_max(peak * 1.1), "B/s", storage_chart_window());
                    bool has_data = false;
                    const size_t samples = neforce::min(chart_read.size(), chart_write.size());
                    for (size_t index = 0; index < samples && !has_data; ++index) {
                        has_data = chart_read[index] > 0.0 || chart_write[index] > 0.0;
                    }
                    if (!has_data) {
                        brush.text(chart_area.x_ + 2, chart_area.y_ + 2, "No disk I/O in window", colors_.muted_);
                    }
                    cursor += chart_height;
                }
                const int remaining = area.h_ - (cursor - area.y_);
                const int filesystem_height = neforce::max(6, remaining * 42 / 100);
                const rect filesystems{area.x_, cursor, area.w_, filesystem_height};
                storage_filesystems_rect_ = filesystems;
                brush.box(filesystems, " FILESYSTEMS ", colors_.amber_);
                const bool compact = filesystems.w_ < 100;
                const int mount_width = neforce::max(10, filesystems.w_ * 24 / 100);
                const int device_width = compact ? 0 : neforce::max(10, filesystems.w_ * 15 / 100);
                const int fs_width = compact ? 0 : 8;
                constexpr int used_width = 9;
                constexpr int total_width = 9;
                const int inode_width = compact ? 0 : 12;
                constexpr int cap_width = 7;
                string header = align_cell("MOUNT", mount_width);
                if (!compact) {
                    header.append(align_cell("DEVICE", device_width));
                    header.append(align_cell("FS", fs_width));
                }
                header.append(align_cell("USED", used_width, true));
                header.append(align_cell("TOTAL", total_width, true));
                if (!compact) {
                    header.append(align_cell("INODES FREE", inode_width, true));
                }
                header.append(align_cell("CAP", cap_width, true));
                brush.text(filesystems.x_ + 2, filesystems.y_ + 1,
                           truncate_display(header.view(), filesystems.w_ - 4).view(), colors_.muted_);
                if (!snap_.disks_.empty() && selected_ >= snap_.disks_.size()) {
                    selected_ = snap_.disks_.size() - 1;
                }
                int y = filesystems.y_ + 2;
                for (size_t index = 0; index < snap_.disks_.size() && y < filesystems.bottom() - 1; ++index, ++y) {
                    const auto& disk = snap_.disks_[index];
                    const double usage = percent(disk.used_, disk.total_);
                    const color tint = usage >= 90 ? colors_.red_ : device_tint(index);
                    const bool active = storage_section_ == 0 && selected_ == index;
                    hits_.push_back(
                            {{filesystems.x_ + 1, y, filesystems.w_ - 2, 1}, hit_action::storage_fs_row, index});
                    if (active) {
                        brush.fill(rect{filesystems.x_ + 1, y, filesystems.w_ - 2, 1}, " ", colors_.text_,
                                   colors_.selection_);
                    }
                    string line = align_cell(disk.mount_, mount_width);
                    if (!compact) {
                        line.append(align_cell(disk.name_, device_width));
                        line.append(align_cell(disk.filesystem_, fs_width));
                    }
                    line.append(align_cell(bytes(static_cast<double>(disk.used_)), used_width, true));
                    line.append(align_cell(bytes(static_cast<double>(disk.total_)), total_width, true));
                    if (!compact) {
                        const string inodes = disk.extra_.get("inodes_free") != nullptr
                                                      ? format("{}", disk.extra_.get("inodes_free")->as_integer())
                                                      : string("—");
                        line.append(align_cell(inodes, inode_width, true));
                    }
                    line.append(align_cell(format("{:.1f}%", usage), cap_width, true));
                    brush.text(filesystems.x_ + 2, y, truncate_display(line.view(), filesystems.w_ - 4).view(),
                               active ? colors_.text_ : tint);
                }
                if (snap_.disks_.empty()) {
                    brush.text(filesystems.x_ + 2, filesystems.y_ + 2, "No readable mounts", colors_.muted_);
                }
                const rect io{area.x_, filesystems.bottom(), area.w_, area.h_ - (filesystems.bottom() - area.y_)};
                storage_io_rect_ = io;
                brush.box(io, format(" BLOCK DEVICES · {} ", static_cast<uint64_t>(snap_.disk_io_.size())).view(),
                          colors_.amber_);
                if (io.h_ <= 2) {
                    return;
                }
                const bool wide = io.w_ >= 100;
                const int io_device_width = neforce::max(12, io.w_ * 18 / 100);
                constexpr int read_width = 11;
                constexpr int write_width = 11;
                constexpr int busy_width = 7;
                constexpr int iops_width = 7;
                constexpr int await_width = 8;
                constexpr int queue_width = 6;
                string io_header = align_cell("DEVICE", io_device_width);
                io_header.append(align_cell("READ/s", read_width, true));
                io_header.append(align_cell("WRITE/s", write_width, true));
                io_header.append(align_cell("BUSY", busy_width, true));
                if (wide) {
                    io_header.append(align_cell("R IOPS", iops_width, true));
                    io_header.append(align_cell("W IOPS", iops_width, true));
                }
                io_header.append(align_cell("AWAIT", await_width, true));
                io_header.append(align_cell("QUEUE", queue_width, true));
                brush.text(io.x_ + 2, io.y_ + 1, truncate_display(io_header.view(), io.w_ - 4).view(), colors_.muted_);
                int row = io.y_ + 2;
                for (size_t index = 0; index < snap_.disk_io_.size() && row < io.bottom() - 1; ++index, ++row) {
                    const auto& item = snap_.disk_io_[index];
                    hits_.push_back({{io.x_ + 1, row, io.w_ - 2, 1}, hit_action::storage_io_row, index});
                    const bool active = !storage_device_auto_ && storage_section_ == 1 && index == selected_io_ &&
                                        current_ == view::storage;
                    if (active) {
                        brush.fill(rect{io.x_ + 1, row, io.w_ - 2, 1}, " ", colors_.text_, colors_.selection_);
                    }
                    string line = align_cell(item.name_, io_device_width);
                    line.append(align_cell(cfg_.display_.rate(item.read_per_sec_), read_width, true));
                    line.append(align_cell(cfg_.display_.rate(item.write_per_sec_), write_width, true));
                    line.append(align_cell(item.busy_percent_.has_value() ? format("{:.1f}%", *item.busy_percent_)
                                                                          : string("—"),
                                           busy_width, true));
                    if (wide) {
                        line.append(align_cell(item.read_iops_.has_value() ? format("{:.1f}", *item.read_iops_)
                                                                           : string("—"),
                                               iops_width, true));
                        line.append(align_cell(item.write_iops_.has_value() ? format("{:.1f}", *item.write_iops_)
                                                                            : string("—"),
                                               iops_width, true));
                    }
                    line.append(align_cell(item.await_ms_.has_value() ? format("{:.2f}", *item.await_ms_) : string("—"),
                                           await_width, true));
                    line.append(align_cell(item.queue_depth_.has_value() ? format("{:.2f}", *item.queue_depth_)
                                                                         : string("—"),
                                           queue_width, true));
                    brush.text(io.x_ + 2, row, truncate_display(line.view(), io.w_ - 4).view(),
                               active ? colors_.text_ : (item.aggregate_ ? colors_.mint_ : colors_.muted_));
                }
                if (snap_.disk_io_.empty()) {
                    brush.text(io.x_ + 2, io.y_ + 2, "No readable block devices", colors_.muted_);
                }
            }

            void open_storage_detail() {
                detail_lines_.clear();
                if (storage_section_ == 0 && !snap_.disks_.empty() && selected_ < snap_.disks_.size()) {
                    const auto& disk = snap_.disks_[selected_];
                    detail_lines_.push_back(format("# {} · {}", disk.mount_, disk.name_));
                    detail_lines_.push_back(format("Filesystem    {}", disk.filesystem_));
                    detail_lines_.push_back(format("Kind          {}", disk.kind_));
                    detail_lines_.push_back(format("Removable     {}", disk.removable_ ? "yes" : "no"));
                    detail_lines_.push_back(format("Total         {}", bytes(static_cast<double>(disk.total_))));
                    detail_lines_.push_back(format("Used          {}", bytes(static_cast<double>(disk.used_))));
                    detail_lines_.push_back(format("Available     {}", bytes(static_cast<double>(disk.available_))));
                    detail_lines_.push_back(format("Capacity      {:.1f}%", percent(disk.used_, disk.total_)));
                    for (const auto& field: disk.extra_.as_object()) {
                        detail_lines_.push_back(
                                format("{:<13} {}", field.first,
                                       field.second.is_string() ? field.second.as_string() : field.second.dump()));
                    }
                }
                if (storage_section_ == 1 && selected_io_ < snap_.disk_io_.size()) {
                    storage_device_auto_ = false;
                    const auto& io = snap_.disk_io_[selected_io_];
                    if (!detail_lines_.empty()) {
                        detail_lines_.push_back("");
                    }
                    detail_lines_.push_back(format("# block device · {}", io.name_));
                    detail_lines_.push_back(format("Read          {}/s", bytes(io.read_per_sec_)));
                    detail_lines_.push_back(format("Write         {}/s", bytes(io.write_per_sec_)));
                    detail_lines_.push_back(format("Read IOPS     {}", io.read_iops_.has_value()
                                                                               ? format("{:.1f}", *io.read_iops_)
                                                                               : string("—")));
                    detail_lines_.push_back(format("Write IOPS    {}", io.write_iops_.has_value()
                                                                               ? format("{:.1f}", *io.write_iops_)
                                                                               : string("—")));
                    detail_lines_.push_back(format("Await         {}", io.await_ms_.has_value()
                                                                               ? format("{:.2f} ms", *io.await_ms_)
                                                                               : string("—")));
                    detail_lines_.push_back(format("Queue depth   {}", io.queue_depth_.has_value()
                                                                               ? format("{:.2f}", *io.queue_depth_)
                                                                               : string("—")));
                    detail_lines_.push_back(format("Busy          {}", io.busy_percent_.has_value()
                                                                               ? format("{:.1f}%", *io.busy_percent_)
                                                                               : string("—")));
                }
                if (detail_lines_.empty()) {
                    status_ = "No storage device selected.";
                    return;
                }
                popup_title_ = "STORAGE";
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            NEFORCE_NODISCARD vector<size_t> filtered_networks() const {
                vector<size_t> rows;
                for (size_t index = 0; index < snap_.networks_.size(); ++index) {
                    const auto& item = snap_.networks_[index];
                    if (!cfg_.show_loopback_ && is_loopback(item.name_)) {
                        continue;
                    }
                    if (!filter_.empty()) {
                        const string haystack = format("{} {} {}", item.name_, item.address_, item.state_);
                        if (!contains_ignore_case(haystack.view(), lower_ascii(filter_).view())) {
                            continue;
                        }
                    }
                    rows.push_back(index);
                }
                const auto before = [&](size_t left, size_t right) {
                    const auto& a = snap_.networks_[left];
                    const auto& b = snap_.networks_[right];
                    switch (interface_sort_ % 5) {
                        case 1:
                            return a.tx_per_sec_ > b.tx_per_sec_;
                        case 2:
                            return a.name_ < b.name_;
                        case 3:
                            return a.state_ < b.state_;
                        case 4:
                            return a.speed_mbps_.value_or(0) > b.speed_mbps_.value_or(0);
                        default:
                            return a.rx_per_sec_ > b.rx_per_sec_;
                    }
                };
                neforce::tim_sort(rows.begin(), rows.end(), [&](size_t left, size_t right) {
                    return reverse_ ? before(right, left) : before(left, right);
                });
                return rows;
            }

            void open_network_detail() {
                const auto rows = filtered_networks();
                if (rows.empty()) {
                    status_ = "No interface selected.";
                    return;
                }
                const auto& item = snap_.networks_[rows[neforce::min(selected_, rows.size() - 1)]];
                detail_lines_.clear();
                detail_lines_.push_back(format("Interface     {}", item.name_));
                detail_lines_.push_back(format("State         {}", item.state_));
                detail_lines_.push_back(format("Address       {}", item.address_));
                detail_lines_.push_back(format("MAC           {}", item.mac_.empty() ? string("—") : item.mac_));
                detail_lines_.push_back(format("MTU           {}", static_cast<uint64_t>(item.mtu_)));
                detail_lines_.push_back(format("Speed         {}", item.speed_mbps_.has_value()
                                                                           ? format("{} Mbit/s", *item.speed_mbps_)
                                                                           : string("—")));
                detail_lines_.push_back(format("Received      {}", bytes(static_cast<double>(item.rx_total_))));
                detail_lines_.push_back(format("Sent          {}", bytes(static_cast<double>(item.tx_total_))));
                detail_lines_.push_back(format("RX rate       {}", cfg_.display_.rate(item.rx_per_sec_)));
                detail_lines_.push_back(format("TX rate       {}", cfg_.display_.rate(item.tx_per_sec_)));
                detail_lines_.push_back(format("Packets       RX {} · TX {}", static_cast<uint64_t>(item.rx_packets_),
                                               static_cast<uint64_t>(item.tx_packets_)));
                detail_lines_.push_back(format("Errors        RX {} · TX {}", static_cast<uint64_t>(item.rx_errors_),
                                               static_cast<uint64_t>(item.tx_errors_)));
                detail_lines_.push_back(
                        format("Dropped       {}", item.drops_.has_value() ? format("{}", *item.drops_) : string("—")));
                popup_title_ = format("INTERFACE · {}", item.name_);
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            NEFORCE_NODISCARD vector<size_t> filtered_connections() const {
                vector<size_t> rows;
                for (size_t index = 0; index < snap_.connections_.connections_.size(); ++index) {
                    if (connection_matches(snap_.connections_.connections_[index], filter_)) {
                        rows.push_back(index);
                    }
                }
                const auto order = [&](size_t left, size_t right) {
                    const auto& a = snap_.connections_.connections_[left];
                    const auto& b = snap_.connections_.connections_[right];
                    switch (network_sort_ % 5) {
                        case 1:
                            return a.tx_per_sec_.value_or(0.0) > b.tx_per_sec_.value_or(0.0);
                        case 2:
                            return a.state_ < b.state_;
                        case 3:
                            return a.local_ < b.local_;
                        case 4:
                            return (a.pids_.empty() ? 0U : a.pids_[0]) < (b.pids_.empty() ? 0U : b.pids_[0]);
                        default:
                            return a.rx_per_sec_.value_or(0.0) > b.rx_per_sec_.value_or(0.0);
                    }
                };
                neforce::tim_sort(rows.begin(), rows.end(), [&](size_t left, size_t right) {
                    return reverse_ ? order(right, left) : order(left, right);
                });
                return rows;
            }

            void open_connection_detail() {
                const auto rows = filtered_connections();
                if (rows.empty()) {
                    status_ = "No connection selected.";
                    return;
                }
                const auto& item =
                        snap_.connections_.connections_[rows[neforce::min(selected_connection_, rows.size() - 1)]];
                detail_lines_.clear();
                detail_lines_.push_back(format("Protocol      {}", item.protocol_));
                detail_lines_.push_back(format("Local         {}", item.local_));
                detail_lines_.push_back(format("Remote        {}", item.remote_));
                detail_lines_.push_back(format("State         {}", item.state_));
                detail_lines_.push_back(
                        format("PID           {}", item.pids_.empty() ? string("—") : format("{}", item.pids_[0])));
                string names;
                for (const auto& name: item.process_names_) {
                    if (!names.empty()) {
                        names.append(", ");
                    }
                    names.append(name);
                }
                detail_lines_.push_back(format("Processes     {}", names.empty() ? string("—") : names));
                detail_lines_.push_back(format("Received      {}", item.received_.has_value()
                                                                           ? bytes(static_cast<double>(*item.received_))
                                                                           : string("—")));
                detail_lines_.push_back(format("Sent          {}", item.sent_.has_value()
                                                                           ? bytes(static_cast<double>(*item.sent_))
                                                                           : string("—")));
                detail_lines_.push_back(format("RX rate       {}", item.rx_per_sec_.has_value()
                                                                           ? cfg_.display_.rate(*item.rx_per_sec_)
                                                                           : string("—")));
                detail_lines_.push_back(format("TX rate       {}", item.tx_per_sec_.has_value()
                                                                           ? cfg_.display_.rate(*item.tx_per_sec_)
                                                                           : string("—")));
                detail_lines_.push_back(format("Recv queue    {}", item.receive_queue_.has_value()
                                                                           ? format("{} B", *item.receive_queue_)
                                                                           : string("—")));
                detail_lines_.push_back(format("Send queue    {}", item.send_queue_.has_value()
                                                                           ? format("{} B", *item.send_queue_)
                                                                           : string("—")));
                detail_lines_.push_back(format("RTT           {}", item.rtt_ms_.has_value()
                                                                           ? format("{:.2f} ms", *item.rtt_ms_)
                                                                           : string("—")));
                detail_lines_.push_back(format("Retransmits   {}", item.retransmits_.has_value()
                                                                           ? format("{}", *item.retransmits_)
                                                                           : string("—")));
                detail_lines_.push_back(format("Inode         {}", static_cast<uint64_t>(item.inode_)));
                string tags;
                for (const auto& tag: item.tags_) {
                    if (!tags.empty()) {
                        tags.append(", ");
                    }
                    tags.append(tag.display());
                }
                detail_lines_.push_back(format("Tags          {}", tags.empty() ? string("—") : tags));
                popup_title_ = format("CONNECTION · {}:{}", item.protocol_, item.state_);
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            void draw_network_page(painter& brush, const rect& area) {
                const auto* net = history_.network(cfg_);
                const int top = neforce::min(12, area.h_ / 3);
                if (net != nullptr) {
                    const bool sync_scale = cfg_.display_.network_sync_scale_;
                    const auto fixed = static_cast<double>(cfg_.display_.network_scale_);
                    const double shared = neforce::max(net->rx_peak_, net->tx_peak_);
                    const double rx_max = fixed > 0.0 ? fixed : (sync_scale ? shared : net->rx_peak_);
                    const double tx_max = fixed > 0.0 ? fixed : (sync_scale ? shared : net->tx_peak_);
                    graph_painter(brush, cfg_.display_.graph_symbol_net_)
                            .chart(rect{area.x_, area.y_, area.w_ / 2 - 1, top}, "Download",
                                   {chart_series{"RX", &net->rx_, colors_.mint_}},
                                   tui::canvas_chart_nice_max(neforce::max(1.0, rx_max)), "B/s", chart_window(),
                                   cfg_.display_.network_logarithmic_);
                    graph_painter(brush, cfg_.display_.graph_symbol_net_)
                            .chart(rect{area.x_ + area.w_ / 2, area.y_, area.w_ / 2, top}, "Upload",
                                   {chart_series{"TX", &net->tx_, colors_.blue_}},
                                   tui::canvas_chart_nice_max(neforce::max(1.0, tx_max)), "B/s", chart_window(),
                                   cfg_.display_.network_logarithmic_);
                }
                const rect interfaces{area.x_, area.y_ + top, area.w_, neforce::max(7, (area.h_ - top) / 2)};
                network_interfaces_rect_ = interfaces;
                const auto interface_rows = filtered_networks();
                brush.box(interfaces,
                          format(" INTERFACES · {}/{} ", interface_rows.size(), snap_.networks_.size()).view(),
                          network_section_ == 1 ? colors_.blue_ : colors_.mint_);
                const int iface_width = neforce::max(10, interfaces.w_ * 14 / 100);
                constexpr int state_width = 9;
                const int address_width = neforce::max(18, interfaces.w_ * 32 / 100);
                constexpr int rate_width = 11;
                string iface_header = align_cell("INTERFACE", iface_width) + align_cell("STATE", state_width) +
                                      align_cell("ADDRESS", address_width) + align_cell("RX/s", rate_width, true) +
                                      align_cell("TX/s", rate_width, true) + align_cell("TOTAL", rate_width, true);
                brush.text(interfaces.x_ + 2, interfaces.y_ + 1,
                           truncate_display(iface_header.view(), interfaces.w_ - 4).view(), colors_.muted_);
                {
                    int hx = interfaces.x_ + 2;
                    const int widths[] = {iface_width, state_width, address_width, rate_width, rate_width, rate_width};
                    const size_t action_indices[] = {2, 3, 99, 0, 1, 99};
                    for (size_t i = 0; i < 6; ++i) {
                        if (widths[i] > 0) {
                            hits_.push_back({{hx, interfaces.y_ + 1, widths[i], 1},
                                             hit_action::interface_sort,
                                             action_indices[i]});
                        }
                        hx += widths[i];
                    }
                }
                if (network_section_ == 1 && !interface_rows.empty() && selected_ >= interface_rows.size()) {
                    selected_ = interface_rows.size() - 1;
                }
                int row = interfaces.y_ + 2;
                const auto visible_interfaces = static_cast<size_t>(neforce::max(0, interfaces.h_ - 3));
                for (size_t offset = 0; offset < interface_rows.size() && offset < visible_interfaces;
                     ++offset, ++row) {
                    const auto& item = snap_.networks_[interface_rows[offset]];
                    hits_.push_back({{interfaces.x_ + 1, row, interfaces.w_ - 2, 1},
                                     hit_action::network_interface_row,
                                     offset});
                    if (network_section_ == 1 && offset == selected_) {
                        brush.fill(rect{interfaces.x_ + 1, row, interfaces.w_ - 2, 1}, " ", colors_.text_,
                                   colors_.selection_);
                    }
                    const string line =
                            align_cell(item.name_, iface_width) + align_cell(item.state_, state_width) +
                            align_cell(item.address_, address_width) +
                            align_cell(cfg_.display_.rate(item.rx_per_sec_), rate_width, true) +
                            align_cell(cfg_.display_.rate(item.tx_per_sec_), rate_width, true) +
                            align_cell(bytes(static_cast<double>(item.rx_total_ + item.tx_total_)), rate_width, true);
                    brush.text(interfaces.x_ + 2, row, truncate_display(line.view(), interfaces.w_ - 4).view(),
                               network_section_ == 1 && offset == selected_ ? colors_.text_
                                                                            : state_color(item.state_, colors_));
                }
                const rect connections{area.x_, interfaces.bottom(), area.w_, area.h_ - interfaces.h_ - top};
                const auto rows = filtered_connections();
                network_connections_rect_ = connections;
                const string status = snap_.connections_.status_.empty() ? "sampling" : snap_.connections_.status_;
                brush.box(connections,
                          format(" CONNECTIONS · {}/{} · {} ", static_cast<uint64_t>(rows.size()),
                                 static_cast<uint64_t>(snap_.connections_.connections_.size()), status)
                                  .view(),
                          network_section_ == 0 ? colors_.blue_ : colors_.muted_);
                if (connections.h_ > 2) {
                    constexpr int proto_width = 6;
                    const int endpoint_width = neforce::max(14, connections.w_ * 22 / 100);
                    constexpr int conn_state_width = 11;
                    constexpr int conn_rate_width = 10;
                    const string conn_header =
                            align_cell("PROTO", proto_width) + align_cell("LOCAL", endpoint_width) +
                            align_cell("REMOTE", endpoint_width) + align_cell("STATE", conn_state_width) +
                            align_cell("RX/s", conn_rate_width, true) + align_cell("TX/s", conn_rate_width, true);
                    brush.text(connections.x_ + 2, connections.y_ + 1,
                               truncate_display(conn_header.view(), connections.w_ - 4).view(), colors_.muted_);
                    {
                        int hx = connections.x_ + 2;
                        const int widths[] = {proto_width,      endpoint_width,  endpoint_width,
                                              conn_state_width, conn_rate_width, conn_rate_width};
                        const size_t action_indices[] = {99, 3, 99, 2, 0, 1};
                        for (size_t i = 0; i < 6; ++i) {
                            if (widths[i] > 0) {
                                hits_.push_back({{hx, connections.y_ + 1, widths[i], 1},
                                                 hit_action::connection_sort,
                                                 action_indices[i]});
                            }
                            hx += widths[i];
                        }
                    }
                    if (!rows.empty() && selected_connection_ >= rows.size()) {
                        selected_connection_ = rows.size() - 1;
                    }
                    const int visible = neforce::max(0, connections.h_ - 3);
                    if (selected_connection_ >= connection_offset_ + static_cast<size_t>(visible)) {
                        connection_offset_ = selected_connection_ + 1 - static_cast<size_t>(visible);
                    }
                    connection_offset_ = neforce::min(selected_connection_, connection_offset_);
                    int y = connections.y_ + 2;
                    for (size_t irow = 0; compare_less(irow, visible) && connection_offset_ + irow < rows.size();
                         ++irow) {
                        const size_t index = connection_offset_ + irow;
                        const auto& item = snap_.connections_.connections_[rows[index]];
                        hits_.push_back(
                                {{connections.x_ + 1, y, connections.w_ - 2, 1}, hit_action::connection_row, index});
                        const bool active = network_section_ == 0 && index == selected_connection_;
                        if (active) {
                            brush.fill(rect{connections.x_ + 1, y, connections.w_ - 2, 1}, " ", colors_.text_,
                                       colors_.selection_);
                        }
                        const string line =
                                align_cell(item.protocol_, proto_width) + align_cell(item.local_, endpoint_width) +
                                align_cell(item.remote_, endpoint_width) + align_cell(item.state_, conn_state_width) +
                                align_cell(item.rx_per_sec_.has_value() ? format("{}/s", bytes(*item.rx_per_sec_))
                                                                        : string("—"),
                                           conn_rate_width, true) +
                                align_cell(item.tx_per_sec_.has_value() ? format("{}/s", bytes(*item.tx_per_sec_))
                                                                        : string("—"),
                                           conn_rate_width, true);
                        brush.text(connections.x_ + 2, y, truncate_display(line.view(), connections.w_ - 4).view(),
                                   active ? colors_.text_ : colors_.muted_);
                        ++y;
                    }
                    const rect connection_scrollbar{connections.right() - 2, connections.y_ + 2, 2, visible};
                    hits_.push_back({connection_scrollbar, hit_action::connections_scroll, 0});
                    brush.scrollbar(connection_scrollbar, rows.size(), static_cast<size_t>(visible),
                                    connection_offset_);
                }
            }

            void draw_system_page(painter& brush, const rect& area) {
                const int stats_height = neforce::min(3, area.h_);
                const rect stats{area.x_, area.y_, area.w_, stats_height};
                brush.box(stats, " SYSTEM ", colors_.mint_);
                const int stat_width = neforce::max(12, stats.w_ / 4);
                brush.text(stats.x_ + 2, stats.y_ + 1, format("UPTIME  {}", duration(snap_.uptime_)).view(),
                           colors_.mint_);
                brush.text(stats.x_ + stat_width, stats.y_ + 1,
                           format("CORES   {}", static_cast<uint64_t>(snap_.cores_.size())).view(), colors_.blue_);
                brush.text(stats.x_ + stat_width * 2, stats.y_ + 1,
                           format("ALERTS  {}", static_cast<uint64_t>(snap_.alerts_.size())).view(),
                           snap_.alerts_.empty() ? colors_.mint_ : colors_.red_);
                if (cfg_.display_.show_battery_ && !snap_.batteries_.empty()) {
                    brush.text(stats.x_ + stat_width * 3, stats.y_ + 1,
                               format("BATTERY {}", snap_.batteries_[0].summary()).view(), colors_.amber_);
                }
                const rect rest{area.x_, stats.bottom(), area.w_, area.h_ - stats_height};
                if (rest.h_ < 4) {
                    return;
                }
                const int left_width = neforce::max(40, rest.w_ * 55 / 100);
                const rect host{rest.x_, rest.y_, left_width - 1, rest.h_};
                const rect right{rest.x_ + left_width, rest.y_, rest.w_ - left_width, rest.h_};
                brush.box(host, " HOST & POWER ", colors_.mint_);
                const json* hardware = snap_.extra_.get("Hardware");
                const auto hardware_value = [&](const char* section, const char* key) {
                    if (hardware == nullptr) {
                        return string("—");
                    }
                    const json* group = hardware->get(section);
                    if (group == nullptr) {
                        return string("—");
                    }
                    const json* value = group->get(key);
                    if (value == nullptr) {
                        return string("—");
                    }
                    return value->is_string() ? value->as_string() : value->dump();
                };
                vector<pair<string, string>> fields;
                fields.push_back({"Host", snap_.hostname_});
                fields.push_back({"OS", snap_.os_});
                fields.push_back({"Kernel", snap_.kernel_});
                fields.push_back({"CPU", snap_.cpu_name_});
                fields.push_back({"Vendor", hardware_value("CPU", "Vendor")});
                fields.push_back({"Architecture", hardware_value("CPU", "Architecture")});
                const string max_clock = hardware_value("CPU", "Max clock");
                const string clocks =
                        max_clock == "—"
                                ? (snap_.cpu_mhz_ > 0 ? format("{} MHz", snap_.cpu_mhz_) : string("—"))
                                : (snap_.cpu_mhz_ > 0 ? format("{} / {}", snap_.cpu_mhz_, max_clock) : max_clock);
                fields.push_back({"Clocks", clocks});
                fields.push_back(
                        {"Load", format("{:.2f} {:.2f} {:.2f}", snap_.load_[0], snap_.load_[1], snap_.load_[2])});
                fields.push_back({"Sampling", format("{:.1f} / {} ms", snap_.collection_ms_,
                                                     static_cast<uint64_t>(cfg_.interval_ms_))});
                const json* cpu_group = hardware != nullptr ? hardware->get("CPU") : nullptr;
                const json* cpu_details = cpu_group != nullptr ? cpu_group->get("Details") : nullptr;
                if (cpu_details != nullptr && cpu_details->is_array()) {
                    for (const auto& item: cpu_details->as_array()) {
                        const json* label = item.get("Label");
                        const json* value = item.get("Value");
                        if (label == nullptr || value == nullptr || !label->is_string() || !value->is_string()) {
                            continue;
                        }
                        const string& text = value->as_string();
                        if (!text.empty() && text != "—") {
                            fields.push_back({label->as_string(), text});
                        }
                    }
                } else {
                    fields.push_back({"Code name", hardware_value("CPU", "Code name")});
                    fields.push_back({"Family", hardware_value("CPU", "Family")});
                    fields.push_back({"Model", hardware_value("CPU", "Model")});
                    fields.push_back({"Stepping", hardware_value("CPU", "Stepping")});
                    fields.push_back({"Ext F/M", format("{}/{}", hardware_value("CPU", "Ext family"),
                                                        hardware_value("CPU", "Ext model"))});
                    fields.push_back({"Revision", hardware_value("CPU", "Revision")});
                    fields.push_back({"Core VID", hardware_value("CPU", "Core VID")});
                    fields.push_back({"Technology", hardware_value("CPU", "Technology")});
                    fields.push_back({"Package", hardware_value("CPU", "Package")});
                    fields.push_back({"Bus speed", hardware_value("CPU", "Bus speed")});
                    fields.push_back({"Multiplier", hardware_value("CPU", "Multiplier")});
                    fields.push_back({"Max TDP", hardware_value("CPU", "Max TDP")});
                }
                fields.push_back({"Cores", hardware_value("CPU", "Cores / Threads")});
                fields.push_back({"BIOS", hardware_value("BIOS", "Brand")});
                fields.push_back({"BIOS ver", hardware_value("BIOS", "Version")});
                fields.push_back({"BIOS date", hardware_value("BIOS", "Date")});
                fields.push_back({"Microcode", hardware_value("BIOS", "Microcode")});
                fields.push_back({"Board", hardware_value("Board", "Vendor")});
                fields.push_back({"Board model", hardware_value("Board", "Model")});
                fields.push_back({"Bus", hardware_value("Board", "Bus")});
                fields.push_back({"Chipset", hardware_value("Board", "Chipset")});
                fields.push_back({"Southbridge", hardware_value("Board", "Southbridge")});
                fields.push_back({"Mem type", hardware_value("Memory", "Type")});
                fields.push_back({"Channel", hardware_value("Memory", "Channel")});
                fields.push_back({"Mem size", hardware_value("Memory", "Size")});
                fields.push_back({"Mem clock", format("{} · {} · MC {}", hardware_value("Memory", "DRAM freq"),
                                                      hardware_value("Memory", "FSB:DRAM"),
                                                      hardware_value("Memory", "Controller freq"))});
                fields.push_back({"LLC / Ring", hardware_value("Memory", "LLC / Ring")});
                fields.push_back({"Timings", hardware_value("Memory", "Timings")});
#if NEFLUX_PLATFORM_LINUX
                if (hardware_value("Memory", "Type") == "—") {
                    fields.push_back({"Mem info", "SMBIOS needs root or EDAC"});
                }
#endif
                fields.push_back({"CACHE", format("L1D {} · L1I {} · L2 {} · L3 {}", hardware_value("CPU", "L1 data"),
                                                  hardware_value("CPU", "L1 inst"), hardware_value("CPU", "L2"),
                                                  hardware_value("CPU", "L3"))});
                fields.push_back({"INSTR", hardware_value("CPU", "Instructions")});
                fields.push_back({"SIMD", hardware_value("CPU", "SIMD")});
                if (!inventory_.service_managers_.empty()) {
                    fields.push_back({"Services", join_strings(inventory_.service_managers_, ", ")});
                }
                int host_row = host.y_ + 1;
                const int value_width = neforce::max(8, host.w_ - 17);
                for (const auto& item: fields) {
                    const auto lines = wrap_display(item.second.view(), value_width);
                    for (size_t line_index = 0; line_index < lines.size() && host_row < host.bottom() - 1;
                         ++line_index) {
                        brush.text(host.x_ + 2, host_row,
                                   line_index == 0 ? truncate_display(item.first.view(), 12).view() : string_view(),
                                   colors_.muted_);
                        brush.text(host.x_ + 14, host_row, lines[line_index].view(), colors_.text_);
                        ++host_row;
                    }
                }
                for (const auto& battery: snap_.batteries_) {
                    if (host_row >= host.bottom() - 1) {
                        break;
                    }
                    brush.text(host.x_ + 2, host_row, truncate_display(battery.summary().view(), host.w_ - 4).view(),
                               colors_.mint_);
                    ++host_row;
                }
                const int sensors_height = neforce::max(7, right.h_ * 30 / 100);
                const rect sensors{right.x_, right.y_, right.w_, sensors_height};
                brush.box(sensors, format(" TEMPERATURES · {} ", cfg_.display_.temperature_symbol()).view(),
                          colors_.amber_);
                if (sensors.h_ > 2) {
                    brush.text(sensors.x_ + 2, sensors.y_ + 1, "SENSOR", colors_.muted_);
                    brush.text_right(sensors.x_, sensors.y_ + 1, sensors.w_ - 2, "NOW / CRIT", colors_.muted_);
                    int row = sensors.y_ + 2;
                    for (const auto& item: snap_.sensors_) {
                        if (row >= sensors.bottom() - 1) {
                            break;
                        }
                        const bool critical = item.critical_.has_value() && item.temperature_ >= *item.critical_;
                        const bool warm = item.critical_.has_value() && item.temperature_ >= *item.critical_ * 0.85F;
                        const color tint = critical ? colors_.red_ : (warm ? colors_.amber_ : colors_.mint_);
                        brush.text(sensors.x_ + 2, row, truncate_display(item.name_.view(), sensors.w_ - 28).view(),
                                   tint);
                        const string value =
                                format("{} / {}", cfg_.display_.temperature(item.temperature_, 1),
                                       item.critical_.has_value() ? cfg_.display_.temperature(*item.critical_, 0)
                                                                  : string("—"));
                        brush.text_right(sensors.x_, row, sensors.w_ - 2, value.view(), tint);
                        ++row;
                    }
                    if (snap_.sensors_.empty()) {
                        brush.text(sensors.x_ + 2, sensors.y_ + 2, "No sensor data · unavailable on this host",
                                   colors_.muted_);
                    }
                }
                const int pressure_height = neforce::max(5, (right.h_ - sensors_height) * 52 / 100);
                const rect pressure{right.x_, sensors.bottom(), right.w_, pressure_height};
                brush.box(pressure, " PRESSURE ", colors_.blue_);
                if (pressure.h_ > 2) {
                    constexpr int resource_width = 10;
                    constexpr int pressure_width = 10;
                    const string pressure_header =
                            align_cell("RESOURCE", resource_width) + align_cell("SOME10", pressure_width, true) +
                            align_cell("SOME60", pressure_width, true) + align_cell("SOME300", pressure_width, true) +
                            align_cell("FULL10", pressure_width, true);
                    brush.text(pressure.x_ + 2, pressure.y_ + 1,
                               truncate_display(pressure_header.view(), pressure.w_ - 4).view(), colors_.muted_);
                    int row = pressure.y_ + 2;
                    if (snap_.pressure_.empty()) {
                        brush.text(pressure.x_ + 2, row,
                                   truncate_display("No pressure stall information on this platform.", pressure.w_ - 4)
                                           .view(),
                                   colors_.muted_);
                        ++row;
                    }
                    for (const auto& item: snap_.pressure_) {
                        if (row >= pressure.bottom() - 1) {
                            break;
                        }
                        const bool hot = item.some_10_.has_value() && *item.some_10_ > 20.0;
                        const string line =
                                align_cell(item.resource_, resource_width) +
                                align_cell(item.some_10_.has_value() ? format("{:.2f}%", *item.some_10_) : string("—"),
                                           pressure_width, true) +
                                align_cell(item.some_60_.has_value() ? format("{:.2f}%", *item.some_60_) : string("—"),
                                           pressure_width, true) +
                                align_cell(item.some_300_.has_value() ? format("{:.2f}%", *item.some_300_)
                                                                      : string("—"),
                                           pressure_width, true) +
                                align_cell(item.full_10_.has_value() ? format("{:.2f}%", *item.full_10_) : string("—"),
                                           pressure_width, true);
                        brush.text(pressure.x_ + 2, row, line.view(), hot ? colors_.red_ : colors_.muted_);
                        ++row;
                    }
                }
                const rect listeners{right.x_, pressure.bottom(), right.w_,
                                     right.h_ - sensors_height - pressure_height};
                listeners_rect_ = listeners;
                brush.box(listeners,
                          format(" LISTENERS · {} ", static_cast<uint64_t>(inventory_.listeners_.size())).view(),
                          colors_.blue_);
                int listen_row = listeners.y_ + 1;
                constexpr int listen_proto_width = 6;
                const int listen_address_width = neforce::max(18, listeners.w_ - 20);
                const string listener_header = align_cell("PROTO", listen_proto_width) +
                                               align_cell("ADDRESS", listen_address_width) +
                                               align_cell("PORT", 6, true);
                brush.text(listeners.x_ + 2, listen_row,
                           truncate_display(listener_header.view(), listeners.w_ - 4).view(), colors_.muted_);
                const int visible = neforce::max(0, listeners.h_ - 3);
                const size_t maximum = inventory_.listeners_.size() > static_cast<size_t>(visible)
                                               ? inventory_.listeners_.size() - static_cast<size_t>(visible)
                                               : 0;
                listener_offset_ = neforce::min(listener_offset_, maximum);
                ++listen_row;
                for (size_t index = listener_offset_;
                     index < inventory_.listeners_.size() && listen_row < listeners.bottom() - 1; ++index) {
                    const auto& item = inventory_.listeners_[index];
                    const string line = align_cell(item.protocol_, listen_proto_width) +
                                        align_cell(item.address_, listen_address_width) +
                                        align_cell(format("{}", static_cast<uint64_t>(item.port_)), 6, true);
                    brush.text(listeners.x_ + 2, listen_row, truncate_display(line.view(), listeners.w_ - 4).view(),
                               colors_.muted_);
                    ++listen_row;
                }
                if (inventory_.listeners_.empty()) {
                    brush.text(listeners.x_ + 2, listeners.y_ + 1, "No local listeners discovered", colors_.muted_);
                } else if (visible > 0) {
                    brush.scrollbar(rect{listeners.right() - 1, listeners.y_ + 2, 1, visible},
                                    inventory_.listeners_.size(), static_cast<size_t>(visible), listener_offset_);
                }
            }

            NEFORCE_NODISCARD static string join_strings(const vector<string>& values, string_view separator) {
                string result;
                for (const auto& value: values) {
                    if (!result.empty()) {
                        result.append(separator);
                    }
                    result.append(value);
                }
                return result;
            }

            NEFORCE_NODISCARD bool workload_matches(const vector<tag>& tags, const string& plain) const {
                if (filter_.empty()) {
                    return true;
                }
                string haystack = lower_ascii(plain.view());
                for (const auto& tag: tags) {
                    haystack.append(" ");
                    haystack.append(lower_ascii(tag.display()));
                }
                for (const auto& word: split_whitespace(filter_.view())) {
                    bool negated = false;
                    string term = word;
                    if (starts_with(term.view(), "!")) {
                        negated = true;
                        term = term.substr(1);
                    }
                    bool matched = false;
                    if (starts_with(term.view(), "tag:")) {
                        const string needle = lower_ascii(term.substr(4));
                        for (const auto& tag: tags) {
                            if (contains_ignore_case(tag.display().view(), needle.view())) {
                                matched = true;
                                break;
                            }
                        }
                    } else {
                        matched = contains_ignore_case(haystack.view(), lower_ascii(term).view());
                    }
                    if (matched == negated) {
                        return false;
                    }
                }
                return true;
            }

            NEFORCE_NODISCARD const process* process_for_pid(const optional<uint32_t>& pid) const {
                if (!pid.has_value() || *pid == 0) {
                    return nullptr;
                }
                for (const auto& item: snap_.processes_) {
                    if (!item.thread_ && item.pid_ == *pid) {
                        return &item;
                    }
                }
                return nullptr;
            }

            NEFORCE_NODISCARD float service_cpu(const service& item) const {
                if (item.cpu_percent_.has_value()) {
                    return *item.cpu_percent_;
                }
                const process* owner = process_for_pid(item.pid_);
                return owner != nullptr ? owner->cpu_ : 0.0F;
            }

            NEFORCE_NODISCARD uint64_t service_memory(const service& item) const {
                if (item.memory_.has_value()) {
                    return *item.memory_;
                }
                const process* owner = process_for_pid(item.pid_);
                return owner != nullptr ? owner->memory_ : 0;
            }

            NEFORCE_NODISCARD string service_tasks_text(const service& item) const {
                if (item.tasks_.has_value()) {
                    return format("{}", *item.tasks_);
                }
                const process* owner = process_for_pid(item.pid_);
                if (owner != nullptr) {
                    if (const json* tasks = owner->extra_.get("threads"); tasks != nullptr) {
                        return tasks->is_string() ? tasks->as_string() : tasks->dump();
                    }
                }
                return "—";
            }

            NEFORCE_NODISCARD vector<size_t> workload_rows(bool containers) const {
                vector<size_t> rows;
                const size_t count = containers ? inventory_.containers_.size() : inventory_.services_.size();
                for (size_t index = 0; index < count; ++index) {
                    if (containers) {
                        const auto& item = inventory_.containers_[index];
                        const string plain =
                                format("{} {} {} {} {}", item.name_, item.image_, item.state_, item.runtime_, item.id_);
                        if (workload_matches(item.tags_, plain)) {
                            rows.push_back(index);
                        }
                    } else {
                        const auto& item = inventory_.services_[index];
                        const string plain =
                                format("{} {} {} {}", item.name_, item.scope_, item.active_, item.description_);
                        if (workload_matches(item.tags_, plain)) {
                            rows.push_back(index);
                        }
                    }
                }
                const auto mode = sorts[neforce::min(sort_index_, sizeof(sorts) / sizeof(sorts[0]) - 1)].mode;
                const auto before = [&](size_t left, size_t right) {
                    bool result = false;
                    if (containers) {
                        const auto& a = inventory_.containers_[left];
                        const auto& b = inventory_.containers_[right];
                        if (mode == sort_mode::memory) {
                            result = a.memory_.value_or(0) > b.memory_.value_or(0);
                        } else if (mode == sort_mode::cpu) {
                            result = a.cpu_.value_or(0.0) > b.cpu_.value_or(0.0);
                        } else {
                            result = a.name_ < b.name_;
                        }
                    } else {
                        const auto& a = inventory_.services_[left];
                        const auto& b = inventory_.services_[right];
                        if (mode == sort_mode::memory) {
                            result = service_memory(a) > service_memory(b);
                        } else if (mode == sort_mode::cpu) {
                            result = service_cpu(a) > service_cpu(b);
                        } else {
                            result = a.name_ < b.name_;
                        }
                    }
                    return result;
                };
                neforce::tim_sort(rows.begin(), rows.end(), [&](size_t left, size_t right) {
                    return reverse_ ? before(right, left) : before(left, right);
                });
                return rows;
            }

            void open_workload_detail(bool containers) {
                const auto rows = workload_rows(containers);
                if (rows.empty()) {
                    status_ = "No workload selected.";
                    return;
                }
                const size_t index = rows[neforce::min(selected_, rows.size() - 1)];
                detail_lines_.clear();
                if (containers) {
                    const auto& item = inventory_.containers_[index];
                    detail_lines_.push_back(format("Name          {}", item.name_));
                    detail_lines_.push_back(format("ID            {}", item.id_));
                    detail_lines_.push_back(format("Runtime       {}", item.runtime_));
                    detail_lines_.push_back(format("Image         {}", item.image_));
                    detail_lines_.push_back(format("State         {}", item.state_));
                    detail_lines_.push_back(format("Status        {}", item.status_));
                    detail_lines_.push_back(format("Command       {}", item.command_));
                    detail_lines_.push_back(format("Ports         {}", item.ports_));
                    detail_lines_.push_back(format("Created       {}", item.created_));
                    detail_lines_.push_back(format("Running for   {}", item.running_for_));
                    detail_lines_.push_back(format(
                            "CPU           {}", item.cpu_.has_value() ? format("{:.1f}%", *item.cpu_) : string("—")));
                    detail_lines_.push_back(
                            format("Memory        {}",
                                   item.memory_.has_value() ? bytes(static_cast<double>(*item.memory_)) : string("—")));
                    detail_lines_.push_back(format("PIDs          {}",
                                                   item.pids_.has_value() ? format("{}", *item.pids_) : string("—")));
                    detail_lines_.push_back(
                            format("Network       {}",
                                   item.network_rx_.has_value() && item.network_tx_.has_value()
                                           ? format("{} / {}", bytes(static_cast<double>(*item.network_rx_)),
                                                    bytes(static_cast<double>(*item.network_tx_)))
                                           : string("—")));
                    detail_lines_.push_back(
                            format("Block I/O     {}",
                                   item.block_read_.has_value() && item.block_write_.has_value()
                                           ? format("{} / {}", bytes(static_cast<double>(*item.block_read_)),
                                                    bytes(static_cast<double>(*item.block_write_)))
                                           : string("—")));
                } else {
                    const auto& item = inventory_.services_[index];
                    const process* owner = process_for_pid(item.pid_);
                    const string cpu_text = item.cpu_percent_.has_value()
                                                    ? format("{:.1f}%", *item.cpu_percent_)
                                                    : (owner != nullptr ? format("{:.1f}%", owner->cpu_) : string("—"));
                    const string memory_text =
                            item.memory_.has_value()
                                    ? bytes(static_cast<double>(*item.memory_))
                                    : (owner != nullptr ? bytes(static_cast<double>(owner->memory_)) : string("—"));
                    const string cpu_time_text =
                            item.cpu_time_ns_.has_value()
                                    ? format("{:.2f}s", static_cast<double>(*item.cpu_time_ns_) / 1e9)
                                    : (owner != nullptr
                                               ? format("{:.2f}s", static_cast<double>(owner->cpu_time_ms_) / 1000.0)
                                               : string("—"));
                    detail_lines_.push_back(format("Name          {}", item.name_));
                    detail_lines_.push_back(format("Scope         {}", item.scope_));
                    detail_lines_.push_back(format("Load          {}", item.load_));
                    detail_lines_.push_back(format("Active        {}", item.active_));
                    detail_lines_.push_back(format("Sub           {}", item.sub_));
                    detail_lines_.push_back(format("Description   {}", item.description_));
                    detail_lines_.push_back(
                            format("Main PID      {}", item.pid_.has_value() ? format("{}", *item.pid_) : string("—")));
                    detail_lines_.push_back(format("CPU           {}", cpu_text));
                    detail_lines_.push_back(format("Memory        {}", memory_text));
                    detail_lines_.push_back(format("CPU time      {}", cpu_time_text));
                    detail_lines_.push_back(format("Tasks         {}", service_tasks_text(item)));
                }
                popup_title_ = containers ? "CONTAINER" : "SERVICE";
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            void jump_to_service_process() {
                if (current_ != view::services) {
                    return;
                }
                const auto rows = workload_rows(false);
                if (rows.empty() || selected_ >= rows.size()) {
                    status_ = "Select a service first.";
                    return;
                }
                const auto& item = inventory_.services_[rows[selected_]];
                if (!item.pid_.has_value() || *item.pid_ == 0) {
                    status_ = "This service has no running main process.";
                    return;
                }
                const process* owner = process_for_pid(item.pid_);
                if (owner == nullptr) {
                    status_ = format("PID {} is no longer in the process snapshot.", static_cast<uint64_t>(*item.pid_));
                    return;
                }
                const auto pid = *item.pid_;
                switch_view(view::processes);
                filter_.clear();
                editing_filter_ = false;
                const auto process_view_rows = process_rows();
                selected_ = 0;
                for (size_t index = 0; index < process_view_rows.size(); ++index) {
                    if (snap_.processes_[process_view_rows[index].index_].pid_ == pid) {
                        selected_ = index;
                        break;
                    }
                }
                table_offset_ = selected_ > 5 ? selected_ - 5 : 0;
                select_process(*owner);
                status_ = format("Service {} → process {} ({})", item.name_, static_cast<uint64_t>(pid), owner->name_);
            }

            void draw_service_side_panel(painter& brush, const rect& area) {
                if (area.h_ < 4 || area.w_ < 20) {
                    return;
                }
                const auto rows = workload_rows(false);
                if (rows.empty() || selected_ >= rows.size()) {
                    brush.box(area, " SERVICE ", colors_.purple_);
                    brush.empty(area.inset(1), "No service selected", "Use ↑/↓ or click a service row.");
                    return;
                }
                const auto& item = inventory_.services_[rows[selected_]];
                brush.box(area, format(" SERVICE · {} ", item.name_).view(), colors_.purple_);
                int y = area.y_ + 1;
                const int bottom = area.bottom() - 1;
                const auto line = [&](const string& text, color tint) {
                    if (y >= bottom) {
                        return;
                    }
                    const auto wrapped = wrap_display(text.view(), neforce::max(8, area.w_ - 4));
                    for (const auto& part: wrapped) {
                        if (y >= bottom) {
                            break;
                        }
                        brush.text(area.x_ + 2, y, part.view(), tint);
                        ++y;
                    }
                };
                const process* owner = item.pid_.has_value() ? process_for_pid(*item.pid_) : nullptr;
                const string cpu_text = item.cpu_percent_.has_value()
                                                ? format("{:.1f}%", *item.cpu_percent_)
                                                : (owner != nullptr ? format("{:.1f}%", owner->cpu_) : string("—"));
                const string memory_text =
                        item.memory_.has_value()
                                ? bytes(static_cast<double>(*item.memory_))
                                : (owner != nullptr ? bytes(static_cast<double>(owner->memory_)) : string("—"));
                line(format("{} · {} · {}", item.scope_, item.active_, item.sub_), state_color(item.active_, colors_));
                line(item.description_, colors_.muted_);
                line(format("PID {} · CPU {} · RAM {}", item.pid_.has_value() ? format("{}", *item.pid_) : string("—"),
                            cpu_text, memory_text),
                     colors_.text_);
                line(format("CPU time {} · tasks {}",
                            item.cpu_time_ns_.has_value() ? format("{:.2f}s", *item.cpu_time_ns_ / 1e9) : string("—"),
                            service_tasks_text(item)),
                     colors_.muted_);
                if (const json* fragment = item.extra_.get("FragmentPath");
                    fragment != nullptr && fragment->is_string() && !fragment->as_string().empty()) {
                    line(format("Unit {}", fragment->as_string()), colors_.muted_);
                }
                if (const json* restarts = item.extra_.get("NRestarts");
                    restarts != nullptr && restarts->is_string() && !restarts->as_string().empty()) {
                    line(format("Restarts {}", restarts->as_string()), colors_.muted_);
                }
                if (owner != nullptr) {
                    line(format("Process {} · I/O {}/s", owner->name_,
                                bytes(owner->read_per_sec_ + owner->write_per_sec_)),
                         colors_.muted_);
                }
            }

            NEFORCE_NODISCARD string vm_memory_text(const virtual_machine& item) const {
                if (item.memory_.has_value()) {
                    return item.max_memory_.has_value() ? format("{} / {}", bytes(static_cast<double>(*item.memory_)),
                                                                 bytes(static_cast<double>(*item.max_memory_)))
                                                        : bytes(static_cast<double>(*item.memory_));
                }
                return item.max_memory_.has_value() ? bytes(static_cast<double>(*item.max_memory_)) : string("—");
            }

            void draw_virtual_machines(painter& brush, const rect& area) {
                const auto& vms = inventory_.virtual_machines_;
                size_t running = 0;
                size_t paused = 0;
                for (const auto& item: vms) {
                    if (contains_ignore_case(item.state_.view(), "running")) {
                        ++running;
                    } else if (contains_ignore_case(item.state_.view(), "paused")) {
                        ++paused;
                    }
                }
                int cursor = area.y_;
                if (area.h_ >= 10) {
                    const rect stats{area.x_, cursor, area.w_, 3};
                    brush.box(stats, " VIRTUAL MACHINES ", colors_.blue_);
                    const int width = neforce::max(14, stats.w_ / 4);
                    brush.text(stats.x_ + 2, stats.y_ + 1,
                               format("VMS {:<5}", static_cast<uint64_t>(vms.size())).view(), colors_.blue_);
                    brush.text(stats.x_ + width, stats.y_ + 1,
                               format("RUNNING {:<5}", static_cast<uint64_t>(running)).view(), colors_.mint_);
                    brush.text(stats.x_ + width * 2, stats.y_ + 1,
                               format("PAUSED {:<5}", static_cast<uint64_t>(paused)).view(),
                               paused > 0 ? colors_.amber_ : colors_.mint_);
                    brush.text(stats.x_ + width * 3, stats.y_ + 1,
                               format("{}", inventory_.vm_managers_.empty()
                                                    ? string("no VM manager")
                                                    : join_strings(inventory_.vm_managers_, " + "))
                                       .view(),
                               colors_.muted_);
                    cursor += 3;
                }
                const rect table{area.x_, cursor, area.w_, area.h_ - (cursor - area.y_)};
                workload_rect_ = table;
                const string manager = inventory_.vm_managers_.empty() ? string("QEMU / libvirt not detected")
                                                                       : join_strings(inventory_.vm_managers_, " + ");
                brush.box(table,
                          format(" VIRTUAL MACHINES · {} · {} ", static_cast<uint64_t>(vms.size()), manager).view(),
                          colors_.blue_);
                if (vms.empty()) {
                    brush.empty(table.inset(1), "No libvirt domains or QEMU processes found",
                                "Install libvirt or start qemu-system-* to populate this page.");
                    return;
                }
                if (selected_ >= vms.size()) {
                    selected_ = vms.size() - 1;
                }
                const int visible = neforce::max(0, table.h_ - 3);
                if (!free_scroll_) {
                    if (selected_ >= table_offset_ + static_cast<size_t>(visible)) {
                        table_offset_ = selected_ + 1 - static_cast<size_t>(visible);
                    }
                    table_offset_ = neforce::min(selected_, table_offset_);
                } else {
                    table_offset_ = vms.size() > static_cast<size_t>(visible)
                                            ? neforce::min(table_offset_, vms.size() - static_cast<size_t>(visible))
                                            : 0;
                }
                constexpr int name_width = 28;
                constexpr int backend_width = 9;
                constexpr int state_width = 11;
                constexpr int vcpu_width = 6;
                constexpr int memory_width = 20;
                constexpr int pid_width = 8;
                const string header = align_cell("NAME", name_width) + align_cell("BACKEND", backend_width) +
                                      align_cell("STATE", state_width) + align_cell("VCPUS", vcpu_width, true) +
                                      align_cell("MEMORY", memory_width, true) + align_cell("PID", pid_width, true);
                brush.text(table.x_ + 2, table.y_ + 1, truncate_display(header.view(), table.w_ - 4).view(),
                           colors_.muted_);
                int row = table.y_ + 2;
                for (size_t offset = table_offset_; offset < vms.size() && row < table.bottom() - 1; ++offset, ++row) {
                    const bool active = offset == selected_;
                    hits_.push_back({{table.x_ + 1, row, table.w_ - 2, 1}, hit_action::workload_row, offset});
                    if (active) {
                        brush.fill(rect{table.x_ + 1, row, table.w_ - 2, 1}, " ", colors_.text_, colors_.selection_);
                    }
                    const auto& item = vms[offset];
                    const string line =
                            align_cell(item.name_, name_width) + align_cell(item.backend_, backend_width) +
                            align_cell(item.state_, state_width) +
                            align_cell(item.vcpus_.has_value() ? format("{}", *item.vcpus_) : string("—"), vcpu_width,
                                       true) +
                            align_cell(vm_memory_text(item), memory_width, true) +
                            align_cell(item.pid_.has_value() ? format("{}", *item.pid_) : string("—"), pid_width, true);
                    const color tint = active ? colors_.text_
                                              : (contains_ignore_case(item.state_.view(), "running") ? colors_.mint_
                                                                                                     : colors_.muted_);
                    brush.text(table.x_ + 2, row, truncate_display(line.view(), table.w_ - 4).view(), tint);
                }
                const rect scrollbar{table.right() - 1, table.y_ + 2, 1, neforce::max(0, table.h_ - 3)};
                hits_.push_back({scrollbar, hit_action::workload_scroll, 0});
                brush.scrollbar(scrollbar, vms.size(), static_cast<size_t>(visible), table_offset_);
            }

            void remember_service_selection() {
                if (current_ != view::services) {
                    return;
                }
                const auto rows = workload_rows(false);
                if (selected_ >= rows.size()) {
                    selected_service_.reset();
                    return;
                }
                const auto& item = inventory_.services_[rows[selected_]];
                selected_service_ = pair<string, string>{item.scope_, item.name_};
            }

            void restore_service_selection() {
                if (current_ != view::services || !selected_service_.has_value()) {
                    return;
                }
                const auto rows = workload_rows(false);
                for (size_t index = 0; index < rows.size(); ++index) {
                    const auto& item = inventory_.services_[rows[index]];
                    if (item.scope_ == selected_service_->first && item.name_ == selected_service_->second) {
                        selected_ = index;
                        return;
                    }
                }
            }

            void draw_workloads(painter& brush, const rect& area, bool containers) {
                const size_t total = containers ? inventory_.containers_.size() : inventory_.services_.size();
                if (!containers) {
                    restore_service_selection();
                }
                const auto rows = workload_rows(containers);
                int cursor = area.y_;
                if (area.h_ >= 10) {
                    const rect stats{area.x_, cursor, area.w_, 3};
                    brush.box(stats, " WORKLOADS ", containers ? colors_.blue_ : colors_.purple_);
                    if (containers) {
                        size_t running = 0;
                        size_t unhealthy = 0;
                        double rx = 0.0;
                        double tx = 0.0;
                        double read = 0.0;
                        double write = 0.0;
                        for (const auto& item: inventory_.containers_) {
                            if (item.state_ == "running" || item.state_ == "Up") {
                                ++running;
                                rx += item.network_rx_per_sec_.value_or(0.0);
                                tx += item.network_tx_per_sec_.value_or(0.0);
                                read += item.block_read_per_sec_.value_or(0.0);
                                write += item.block_write_per_sec_.value_or(0.0);
                            }
                            if (contains_ignore_case(item.status_.view(), "unhealthy") || item.state_ == "restarting") {
                                ++unhealthy;
                            }
                        }
                        const int width = neforce::max(12, stats.w_ / (area.w_ >= 130 ? 7 : 3));
                        brush.text(stats.x_ + 2, stats.y_ + 1,
                                   format("CONTAINERS {:<4}", static_cast<uint64_t>(total)).view(), colors_.blue_);
                        brush.text(stats.x_ + width, stats.y_ + 1,
                                   format("RUNNING {:<4}", static_cast<uint64_t>(running)).view(), colors_.mint_);
                        brush.text(stats.x_ + width * 2, stats.y_ + 1,
                                   format("UNHEALTHY {:<4}", static_cast<uint64_t>(unhealthy)).view(),
                                   unhealthy > 0 ? colors_.red_ : colors_.mint_);
                        if (area.w_ >= 130) {
                            brush.text(stats.x_ + width * 3, stats.y_ + 1, format("NET {}/s", bytes(rx + tx)).view(),
                                       colors_.mint_);
                            brush.text(stats.x_ + width * 4, stats.y_ + 1, format("RX {}/s", bytes(rx)).view(),
                                       colors_.mint_);
                            brush.text(stats.x_ + width * 5, stats.y_ + 1, format("TX {}/s", bytes(tx)).view(),
                                       colors_.blue_);
                            brush.text(stats.x_ + width * 6, stats.y_ + 1,
                                       format("DISK {}/s", bytes(read + write)).view(), colors_.amber_);
                        }
                    } else {
                        size_t active = 0;
                        size_t failed = 0;
                        for (const auto& item: inventory_.services_) {
                            if (item.active_ == "active") {
                                ++active;
                            }
                            if (item.active_ == "failed") {
                                ++failed;
                            }
                        }
                        brush.text(stats.x_ + 2, stats.y_ + 1,
                                   format("SERVICES {:<5}", static_cast<uint64_t>(total)).view(), colors_.purple_);
                        brush.text(stats.x_ + stats.w_ / 3, stats.y_ + 1,
                                   format("ACTIVE {:<5}", static_cast<uint64_t>(active)).view(), colors_.mint_);
                        brush.text(stats.x_ + stats.w_ * 2 / 3, stats.y_ + 1,
                                   format("FAILED {:<5}", static_cast<uint64_t>(failed)).view(),
                                   failed > 0 ? colors_.red_ : colors_.mint_);
                    }
                    cursor += 3;
                }
                rect table{area.x_, cursor, area.w_, area.h_ - (cursor - area.y_)};
                const bool service_panel = !containers && workload_side_panel_ && area.w_ >= 100;
                const int panel_width = service_panel ? neforce::max(32, area.w_ * 32 / 100) : 0;
                if (service_panel) {
                    table.w_ = area.w_ - panel_width - 1;
                }
                workload_rect_ = table;
                const string manager =
                        containers ? (inventory_.runtimes_.empty() ? string("no runtime")
                                                                   : join_strings(inventory_.runtimes_, " + "))
                                   : (inventory_.service_managers_.empty()
                                              ? string("no manager")
                                              : join_strings(inventory_.service_managers_, " + "));
                brush.box(table,
                          format(" {} · {}/{} · {} ", containers ? "CONTAINERS" : "SERVICES",
                                 static_cast<uint64_t>(rows.size()), static_cast<uint64_t>(total), manager)
                                  .view(),
                          containers ? colors_.blue_ : colors_.purple_);
                if (rows.empty()) {
                    brush.empty(table.inset(1),
                                total == 0 ? (containers ? "No local containers" : "No local services")
                                           : "No matching workloads",
                                total == 0 ? (containers ? "Docker Desktop or Podman is not detected."
                                                         : "The service manager returned no services.")
                                           : "Clear or change the filter.");
                    return;
                }
                const int visible = neforce::max(0, table.h_ - 3);
                if (selected_ >= rows.size()) {
                    selected_ = rows.size() - 1;
                }
                if (!free_scroll_) {
                    if (selected_ >= table_offset_ + static_cast<size_t>(visible)) {
                        table_offset_ = selected_ + 1 - static_cast<size_t>(visible);
                    }
                    table_offset_ = neforce::min(selected_, table_offset_);
                } else {
                    table_offset_ = rows.size() > static_cast<size_t>(visible)
                                            ? neforce::min(table_offset_, rows.size() - static_cast<size_t>(visible))
                                            : 0;
                }
                const bool wide = table.w_ >= 140;
                const int name_width = wide ? neforce::max(16, table.w_ * 17 / 100) : 24;
                constexpr int runtime_width = 8;
                constexpr int state_width = 9;
                constexpr int cpu_width = 6;
                const int memory_width = wide ? neforce::max(14, table.w_ * 17 / 100) : 12;
                constexpr int pids_width = 5;
                constexpr int rate_width = 10;
                constexpr int scope_width = 7;
                constexpr int sub_width = 9;
                constexpr int pid_width = 6;
                constexpr int tasks_width = 5;
                string header;
                if (containers) {
                    header = align_cell("NAME", name_width) + align_cell("RUNTIME", runtime_width) +
                             align_cell("STATE", state_width) + align_cell("CPU", cpu_width, true) +
                             align_cell("MEMORY / LIMIT", memory_width, true);
                    if (wide) {
                        header.append(align_cell("PIDS", pids_width, true));
                        header.append(align_cell("NET RX/s", rate_width, true));
                        header.append(align_cell("NET TX/s", rate_width, true));
                    }
                    header.append("IMAGE");
                } else {
                    header = align_cell("NAME", name_width) + align_cell("SCOPE", scope_width) +
                             align_cell("ACTIVE", state_width) + align_cell("SUB", sub_width) +
                             align_cell("PID", pid_width, true);
                    if (wide) {
                        header.append(align_cell("CPU", cpu_width, true));
                    }
                    header.append(align_cell("MEMORY", memory_width, true));
                    if (wide) {
                        header.append(align_cell("TASKS", tasks_width, true));
                        header.append("DESCRIPTION");
                    }
                }
                brush.text(table.x_ + 2, table.y_ + 1, truncate_display(header.view(), table.w_ - 4).view(),
                           colors_.muted_);
                int row = table.y_ + 2;
                for (size_t offset = table_offset_; offset < rows.size() && row < table.bottom() - 1; ++offset, ++row) {
                    const bool active = offset == selected_;
                    hits_.push_back({{table.x_ + 1, row, table.w_ - 2, 1}, hit_action::workload_row, offset});
                    if (active) {
                        brush.fill(rect{table.x_ + 1, row, table.w_ - 2, 1}, " ", colors_.text_, colors_.selection_);
                    }
                    if (containers) {
                        const auto& item = inventory_.containers_[rows[offset]];
                        const string memory =
                                item.memory_.has_value()
                                        ? format("{}{}", bytes(static_cast<double>(*item.memory_)),
                                                 item.memory_limit_.has_value()
                                                         ? format(" / {}",
                                                                  bytes(static_cast<double>(*item.memory_limit_)))
                                                         : string())
                                        : string("—");
                        string line = align_cell(item.name_, name_width) + align_cell(item.runtime_, runtime_width) +
                                      align_cell(item.state_, state_width) +
                                      align_cell(item.cpu_.has_value() ? format("{:.1f}%", *item.cpu_) : string("—"),
                                                 cpu_width, true) +
                                      align_cell(memory, memory_width, true);
                        if (wide) {
                            line.append(align_cell(item.pids_.has_value() ? format("{}", *item.pids_) : string("—"),
                                                   pids_width, true));
                            line.append(align_cell(item.network_rx_per_sec_.has_value()
                                                           ? format("{}/s", bytes(*item.network_rx_per_sec_))
                                                           : string("—"),
                                                   rate_width, true));
                            line.append(align_cell(item.network_tx_per_sec_.has_value()
                                                           ? format("{}/s", bytes(*item.network_tx_per_sec_))
                                                           : string("—"),
                                                   rate_width, true));
                        }
                        line.append(item.image_);
                        brush.text(table.x_ + 2, row, truncate_display(line.view(), table.w_ - 4).view(),
                                   active ? colors_.text_ : state_color(item.state_, colors_));
                    } else {
                        const auto& item = inventory_.services_[rows[offset]];
                        const process* owner = nullptr;
                        if (item.pid_.has_value() && *item.pid_ != 0) {
                            for (const auto& proc: snap_.processes_) {
                                if (!proc.thread_ && proc.pid_ == *item.pid_) {
                                    owner = &proc;
                                    break;
                                }
                            }
                        }
                        const string cpu_text =
                                item.cpu_percent_.has_value()
                                        ? format("{:.1f}%", *item.cpu_percent_)
                                        : (owner != nullptr ? format("{:.1f}%", owner->cpu_) : string("—"));
                        const string memory_text =
                                item.memory_.has_value()
                                        ? bytes(static_cast<double>(*item.memory_))
                                        : (owner != nullptr ? bytes(static_cast<double>(owner->memory_)) : string("—"));
                        string tasks_text = item.tasks_.has_value() ? format("{}", *item.tasks_) : string("—");
                        if (tasks_text == "—" && owner != nullptr) {
                            if (const json* tasks = owner->extra_.get("threads"); tasks != nullptr) {
                                tasks_text = tasks->is_string() ? tasks->as_string() : tasks->dump();
                            }
                        }
                        string line = align_cell(item.name_, name_width) + align_cell(item.scope_, scope_width) +
                                      align_cell(item.active_, state_width) + align_cell(item.sub_, sub_width) +
                                      align_cell(item.pid_.has_value() ? format("{}", *item.pid_) : string("—"),
                                                 pid_width, true);
                        if (wide) {
                            line.append(align_cell(cpu_text, cpu_width, true));
                        }
                        line.append(align_cell(memory_text, memory_width, true));
                        if (wide) {
                            line.append(align_cell(tasks_text, tasks_width, true));
                            line.append(item.description_);
                        }
                        brush.text(table.x_ + 2, row, truncate_display(line.view(), table.w_ - 4).view(),
                                   active ? colors_.text_ : state_color(item.active_, colors_));
                    }
                }
                const rect workload_scrollbar{table.right() - 1, table.y_ + 2, 1, neforce::max(0, table.h_ - 3)};
                hits_.push_back({workload_scrollbar, hit_action::workload_scroll, 0});
                brush.scrollbar(workload_scrollbar, rows.size(), static_cast<size_t>(visible), table_offset_);
                if (service_panel) {
                    draw_service_side_panel(
                            brush, rect{table.right() + 1, table.y_, area.right() - table.right() - 1, table.h_});
                }
            }

            // ---------------------------------------------------------------- actions

            void sync_sort_config() {
                cfg_.sort_index_ = sort_index_;
                cfg_.reverse_sort_ = reverse_;
            }

            NEFORCE_NODISCARD bool state_changes_allowed() const { return !(replay_ || args_.read_only_); }

            NEFORCE_NODISCARD vector<pair<uint32_t, uint64_t>> action_targets() const {
                vector<pair<uint32_t, uint64_t>> targets;
                if (!marked_processes_.empty()) {
                    for (const auto& identity: marked_processes_) {
                        targets.push_back(identity);
                    }
                    return targets;
                }
                if (selected_process_.has_value()) {
                    targets.push_back(*selected_process_);
                }
                return targets;
            }

            void describe_targets(const vector<pair<uint32_t, uint64_t>>& targets) {
                confirm_message_.clear();
                for (size_t index = 0; index < targets.size() && index < 12; ++index) {
                    const auto& identity = targets[index];
                    string name;
                    for (const auto& item: snap_.processes_) {
                        if (item.pid_ == identity.first && item.start_time_ == identity.second) {
                            name = item.name_;
                            break;
                        }
                    }
                    if (!confirm_message_.empty()) {
                        confirm_message_.append("\n");
                    }
                    confirm_message_.append(format("PID {} · {}", static_cast<uint64_t>(identity.first), name));
                }
                if (targets.size() > 12) {
                    confirm_message_.append(format("\n… and {} more", static_cast<uint64_t>(targets.size() - 12)));
                }
            }

            void request_signal(int signal_value, const string& title) {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    return;
                }
                const auto targets = action_targets();
                if (targets.empty()) {
                    status_ = "Select a process first.";
                    return;
                }
                pending_signal_ = signal_value;
                pending_workload_action_.clear();
                pending_title_ = title;
                describe_targets(targets);
                popup_ = popup_kind::confirm;
                popup_title_ = title;
            }

            NEFORCE_NODISCARD int send_signal(uint32_t pid, int signal_value) const {
                return ::neforce::process::send_signal(static_cast<::neforce::process::native_id_type>(pid),
                                                       signal_value)
                               ? 0
                               : -1;
            }

            void execute_pending_signal() {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    popup_ = popup_kind::none;
                    return;
                }
                const auto targets = action_targets();
                size_t succeeded = 0;
                size_t failed = 0;
                for (const auto& identity: targets) {
                    bool present = false;
                    for (const auto& item: snap_.processes_) {
                        if (item.pid_ == identity.first && item.start_time_ == identity.second) {
                            present = true;
                            break;
                        }
                    }
                    if (!present) {
                        ++failed;
                        continue;
                    }
                    if (send_signal(identity.first, pending_signal_) == 0) {
                        ++succeeded;
                    } else {
                        ++failed;
                    }
                }
                status_ = failed == 0 ? format("{} sent to {} process{}", pending_title_,
                                               static_cast<uint64_t>(succeeded), succeeded == 1 ? "" : "es")
                                      : format("{} sent to {} · {} failed", pending_title_,
                                               static_cast<uint64_t>(succeeded), static_cast<uint64_t>(failed));
                marked_processes_.clear();
                popup_ = popup_kind::none;
            }

            NEFORCE_NODISCARD static vector<pair<int, string>> signal_entries() {
                vector<pair<int, string>> items;
#if NEFLUX_PLATFORM_WINDOWS
                items.push_back({15, "SIGTERM Terminate"});
                items.push_back({9, "SIGKILL Kill"});
#else
                items.push_back({SIGINT, "SIGINT Interrupt"});
                items.push_back({SIGABRT, "SIGABRT Abort"});
                items.push_back({SIGFPE, "SIGFPE Floating point"});
                items.push_back({SIGILL, "SIGILL Illegal instruction"});
                items.push_back({SIGSEGV, "SIGSEGV Segmentation fault"});
                items.push_back({SIGTERM, "SIGTERM Terminate"});
                items.push_back({SIGHUP, "SIGHUP Hangup"});
                items.push_back({SIGQUIT, "SIGQUIT Quit"});
                items.push_back({SIGTRAP, "SIGTRAP Trace trap"});
                items.push_back({SIGBUS, "SIGBUS Bus error"});
                items.push_back({SIGKILL, "SIGKILL Kill"});
                items.push_back({SIGUSR1, "SIGUSR1 User 1"});
                items.push_back({SIGUSR2, "SIGUSR2 User 2"});
                items.push_back({SIGPIPE, "SIGPIPE Broken pipe"});
                items.push_back({SIGALRM, "SIGALRM Alarm"});
                items.push_back({SIGCHLD, "SIGCHLD Child"});
                items.push_back({SIGCONT, "SIGCONT Continue"});
                items.push_back({SIGSTOP, "SIGSTOP Stop"});
                items.push_back({SIGTSTP, "SIGTSTP Terminal stop"});
                items.push_back({SIGTTIN, "SIGTTIN Terminal input"});
                items.push_back({SIGTTOU, "SIGTTOU Terminal output"});
                items.push_back({SIGURG, "SIGURG Urgent"});
                items.push_back({SIGXCPU, "SIGXCPU CPU limit"});
                items.push_back({SIGXFSZ, "SIGXFSZ File size limit"});
                items.push_back({SIGVTALRM, "SIGVTALRM Virtual alarm"});
                items.push_back({SIGPROF, "SIGPROF Profiling"});
                items.push_back({SIGWINCH, "SIGWINCH Window change"});
                items.push_back({SIGIO, "SIGIO I/O"});
#    ifdef SIGPWR
                items.push_back({SIGPWR, "SIGPWR Power"});
#    endif
#    ifdef SIGSYS
                items.push_back({SIGSYS, "SIGSYS Bad system call"});
#    endif
#endif
                return items;
            }

            void open_signal_menu() {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    return;
                }
                if (action_targets().empty()) {
                    status_ = "Select a process first.";
                    return;
                }
                popup_ = popup_kind::signal_menu;
                popup_title_ = "Choose a signal";
                popup_entries_.clear();
                for (const auto& item: signal_entries()) {
                    popup_entries_.push_back({format("{:2} {}", item.first, item.second), "", "signal"});
                }
                popup_selected_ = 0;
                popup_scroll_ = 0;
            }

            void apply_nice() {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    popup_ = popup_kind::none;
                    return;
                }
                const auto value = parse_i64(prompt_text_.view());
                if (!value.has_value() || *value < -20 || *value > 19) {
                    status_ = "Nice must be -20..19";
                    return;
                }
                const auto targets = action_targets();
                if (targets.empty()) {
                    status_ = "Select a process first.";
                    return;
                }
                size_t failed = 0;
                for (const auto& identity: targets) {
                    if (!::neforce::process::set_nice(static_cast<::neforce::process::native_id_type>(identity.first),
                                                      static_cast<int32_t>(*value))) {
                        ++failed;
                    }
                }
                status_ = failed == 0 ? format("Nice set to {}", *value)
                                      : format("Nice applied to {} process{}; {} failed",
                                               static_cast<uint64_t>(targets.size() - failed),
                                               targets.size() - failed == 1 ? "" : "es", static_cast<uint64_t>(failed));
                popup_ = popup_kind::none;
            }

            void apply_affinity() {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    popup_ = popup_kind::none;
                    return;
                }
                const auto targets = action_targets();
                if (targets.empty()) {
                    status_ = "Select a process first.";
                    return;
                }
                constexpr int maximum_cpus = 64;
                uint64_t mask = 0;
                int maximum = -1;
                for (const auto& part: split(prompt_text_.view(), ',', true)) {
                    const auto dash = part.find('-');
                    if (dash == string::npos) {
                        const auto value = parse_u64(trim(part.view()).view());
                        if (!value.has_value() || *value >= maximum_cpus) {
                            status_ = "Affinity must be CPU numbers or ranges, e.g. 0-3,6";
                            return;
                        }
                        mask |= 1ULL << *value;
                        maximum = neforce::max(maximum, static_cast<int>(*value));
                    } else {
                        const auto begin = parse_u64(trim(part.view().substr(0, dash)).view());
                        const auto end = parse_u64(trim(part.view().substr(dash + 1)).view());
                        if (!begin.has_value() || !end.has_value() || *begin > *end || *end >= maximum_cpus) {
                            status_ = "Affinity must be CPU numbers or ranges, e.g. 0-3,6";
                            return;
                        }
                        for (uint64_t cpu = *begin; cpu <= *end; ++cpu) {
                            mask |= 1ULL << cpu;
                            maximum = neforce::max(maximum, static_cast<int>(cpu));
                        }
                    }
                }
                if (maximum < 0) {
                    status_ = "Affinity must be CPU numbers or ranges, e.g. 0-3,6";
                    return;
                }
                size_t failed = 0;
                for (const auto& identity: targets) {
                    if (!::neforce::process::set_affinity(
                                static_cast<::neforce::process::native_id_type>(identity.first), mask)) {
                        ++failed;
                    }
                }
                status_ = failed == 0 ? format("Affinity set: {}", prompt_text_)
                                      : format("Affinity applied; {} failed", static_cast<uint64_t>(failed));
                popup_ = popup_kind::none;
            }

            void open_process_environment() {
                if (replay_) {
                    status_ = "Live process inspection is disabled during replay.";
                    return;
                }
                const auto value = selected_process_value();
                if (!value.has_value()) {
                    status_ = "Select a process first.";
                    return;
                }
                detail_lines_.clear();
                popup_title_ = format("ENVIRONMENT · {} · PID {}", value->name_, static_cast<uint64_t>(value->pid_));
#if NEFLUX_PLATFORM_LINUX
                try {
                    const auto text = read_text_file(format("/proc/{}/environ", value->pid_));
                    if (!text.has_value()) {
                        detail_lines_.push_back("Cannot read the process environment; run as its owner.");
                    } else {
                        size_t begin = 0;
                        for (size_t index = 0; index <= text->size(); ++index) {
                            if (index != text->size() && (*text)[index] != '\0') {
                                continue;
                            }
                            if (index > begin) {
                                detail_lines_.push_back(text->substr(begin, index - begin));
                            }
                            begin = index + 1;
                        }
                    }
                } catch (const exception& error) {
                    detail_lines_.push_back(format("Cannot inspect process: {}", error.what()));
                }
#else
                detail_lines_.push_back("Process environments are not exposed by this platform.");
#endif
                if (detail_lines_.empty()) {
                    detail_lines_.push_back("Environment is empty or unreadable.");
                }
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            void open_process_files() {
                if (replay_) {
                    status_ = "Live process inspection is disabled during replay.";
                    return;
                }
                const auto value = selected_process_value();
                if (!value.has_value()) {
                    status_ = "Select a process first.";
                    return;
                }
                detail_lines_.clear();
                popup_title_ = format("OPEN FILES · {} · PID {}", value->name_, static_cast<uint64_t>(value->pid_));
#if NEFLUX_PLATFORM_LINUX
                try {
                    vector<string> lines;
                    for (const auto& entry: ::neforce::path(format("/proc/{}/fd", value->pid_)).children()) {
                        char target[4096] = {};
                        const auto length = ::readlink(entry.str().data(), target, sizeof(target) - 1);
                        string text = format("{:>5}  ", entry.filename());
                        if (length > 0) {
                            text.append(string(target, static_cast<size_t>(length)));
                        } else {
                            text.append("unreadable");
                        }
                        lines.push_back(move(text));
                    }
                    neforce::tim_sort(lines.begin(), lines.end(), [](const string& left, const string& right) { return left < right; });
                    detail_lines_ = move(lines);
                } catch (const exception& error) {
                    detail_lines_.push_back(format("Cannot inspect process: {}", error.what()));
                }
#else
                detail_lines_.push_back("Open file descriptors are not exposed by this platform.");
#endif
                if (detail_lines_.empty()) {
                    detail_lines_.push_back("No open files are readable.");
                }
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            void open_nice_prompt() {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                } else if (action_targets().empty()) {
                    status_ = "Select a process first.";
                } else {
                    popup_ = popup_kind::prompt;
                    popup_title_ = "Set nice (-20..19)";
                    prompt_action_ = prompt_action::nice;
                    prompt_text_ = selected_process_value().has_value() && selected_process_value()->nice_.has_value()
                                           ? format("{}", *selected_process_value()->nice_)
                                           : string("0");
                    prompt_cursor_ = prompt_text_.size();
                }
            }

            void open_affinity_prompt() {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                } else if (action_targets().empty()) {
                    status_ = "Select a process first.";
                } else {
                    popup_ = popup_kind::prompt;
                    popup_title_ = "CPU affinity (e.g. 0-3,6)";
                    prompt_action_ = prompt_action::affinity;
                    prompt_text_.clear();
                    prompt_cursor_ = 0;
                }
            }

            void open_context_detail() {
                if (current_ == view::network) {
                    if (network_section_ == 1) {
                        open_network_detail();
                    } else {
                        open_connection_detail();
                    }
                } else if (current_ == view::containers) {
                    open_workload_detail(true);
                } else if (current_ == view::services) {
                    open_workload_detail(false);
                } else if (current_ == view::vms) {
                    open_vm_detail();
                } else if (current_ == view::explorer) {
                    open_extra_detail();
                } else if (current_ == view::storage) {
                    open_storage_detail();
                } else if (current_ == view::gpu) {
                    gpu_section_ = 2;
                } else {
                    open_detail();
                }
            }

            /**
             * @brief 当前页面的上下文工具菜单（F6）
             *
             * 所有状态变更动作都在 activate_popup 中再次检查
             * state_changes_allowed()，保证 read-only / replay 严格只读。
             */
            void open_tools_menu() {
                popup_ = popup_kind::tools;
                popup_title_ = "Tools";
                popup_entries_.clear();
                popup_selected_ = 0;
                popup_scroll_ = 0;
                popup_parent_ = popup_kind::none;
                const auto add = [this](string label, string action) {
                    popup_entries_.push_back({move(label), "", move(action)});
                };
                add("Details", "tool_detail");
                if (current_ == view::processes || current_ == view::overview) {
                    add("Logs (F7)", "tool_logs");
                    add("Environment", "tool_env");
                    add("Open files", "tool_files");
                    add("Signal", "tool_signal");
                    add("Set nice", "tool_nice");
                    add("CPU affinity", "tool_affinity");
                    add(follow_process_ ? "Stop following" : "Follow process", "tool_follow");
                    add(process_paused_ ? "Resume process list" : "Pause process list", "tool_pause");
                } else if (current_ == view::containers) {
                    add("Container logs", "tool_logs");
                    add("Start container", "container_start");
                    add("Stop container", "container_stop");
                    add("Restart container", "container_restart");
                    add("Remove stopped container", "container_remove");
                } else if (current_ == view::services) {
                    add("Service logs", "tool_logs");
                    add("Open main process", "service_open_process");
                    add("Start service", "service_start");
                    add("Stop service", "service_stop");
                    add("Restart service", "service_restart");
                    add(workload_side_panel_ ? "Hide side panel" : "Show side panel", "workload_side_panel");
                } else if (current_ == view::vms) {
                    add("Start VM", "vm_start");
                    add("Shutdown VM", "vm_shutdown");
                    add("Reboot VM", "vm_reboot");
                    add("Force off VM", "vm_destroy");
                    add("Pause VM", "vm_suspend");
                    add("Resume VM", "vm_resume");
                    add("Open QEMU process", "vm_process");
                } else if (current_ == view::network) {
                    add("Filter by port", "net_filter_port");
                    add("Filter by PID", "net_filter_pid");
                    add("Open owning process", "net_owner");
                    add("Toggle loopback", "net_toggle_loopback");
                } else if (current_ == view::storage) {
                    add("Previous device", "storage_prev");
                    add("Next device", "storage_next");
                } else if (current_ == view::gpu) {
                    add("GPU devices", "gpu_processes");
                    add("Previous GPU", "gpu_prev");
                    add("Next GPU", "gpu_next");
                }
                add("Copy selection (Ctrl+C)", "tool_copy_selection");
            }

            /**
             * @brief 请求容器/服务管理动作（先确认）
             * @param action start/stop/restart/remove
             */
            void request_workload_action(string action) {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    popup_ = popup_kind::none;
                    return;
                }
                const bool containers = current_ == view::containers;
                const auto rows = workload_rows(containers);
                if (rows.empty() || selected_ >= rows.size()) {
                    status_ = containers ? "Select a container first." : "Select a service first.";
                    popup_ = popup_kind::none;
                    return;
                }
                const auto index = rows[selected_];
                const string target =
                        containers ? inventory_.containers_[index].name_ : inventory_.services_[index].name_;
                const string kind = containers ? "container" : "service";
                pending_signal_ = 0;
                pending_workload_action_ = move(action);
                popup_title_ = "Confirm";
                confirm_message_ = format("{} {} '{}'?\n\nThis action changes system state. Confirm with y/Enter, "
                                          "cancel with n/Esc.",
                                          pending_workload_action_, kind, target);
                popup_ = popup_kind::confirm;
            }

            /**
             * @brief 执行容器/服务管理动作
             * @param action start/stop/restart/remove
             */
            void run_workload_action(const string_view action) {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    popup_ = popup_kind::none;
                    return;
                }
                const bool containers = current_ == view::containers;
                const auto rows = workload_rows(containers);
                if (rows.empty() || selected_ >= rows.size()) {
                    status_ = containers ? "Select a container first." : "Select a service first.";
                    popup_ = popup_kind::none;
                    return;
                }
                const auto index = rows[selected_];
                if (containers) {
                    const auto& item = inventory_.containers_[index];
                    const string runtime = item.runtime_.empty() ? string("docker") : item.runtime_;
                    if (action == "remove") {
                        if (item.state_ != "exited" && item.state_ != "created") {
                            status_ = "Only stopped containers can be removed.";
                            popup_ = popup_kind::none;
                            return;
                        }
                        run_command(runtime, {"rm", item.name_}, 5000);
                        status_ = format("Removed container {}", item.name_);
                    } else {
                        run_command(runtime, {string(action), item.name_}, 5000);
                        status_ = format("{} requested for container {}", action, item.name_);
                    }
                } else {
                    const auto& item = inventory_.services_[index];
#if NEFLUX_PLATFORM_WINDOWS
                    if (action == "restart") {
                        run_command("sc", {"stop", item.name_}, 5000);
                        run_command("sc", {"start", item.name_}, 5000);
                    } else {
                        run_command("sc", {string(action), item.name_}, 5000);
                    }
#else
                    run_command("systemctl", {string(action), item.name_}, 5000);
#endif
                    status_ = format("{} requested for service {}", action, item.name_);
                }
                popup_ = popup_kind::none;
            }

            void execute_pending_workload() {
                const string action = move(pending_workload_action_);
                pending_workload_action_.clear();
                popup_ = popup_kind::none;
                if (!action.empty()) {
                    if (starts_with(action.view(), "vm:")) {
                        run_vm_action(action.view().substr(3));
                    } else {
                        run_workload_action(action.view());
                    }
                }
            }

            /**
             * @brief 容器/服务日志（F7）
             */
            void open_workload_logs(const bool containers) {
                vector<string> lines;
                const auto rows = workload_rows(containers);
                if (rows.empty() || selected_ >= rows.size()) {
                    lines.push_back(containers ? "# Select a container first." : "# Select a service first.");
                } else if (containers) {
                    const auto& item = inventory_.containers_[rows[selected_]];
                    const string runtime = item.runtime_.empty() ? string("docker") : item.runtime_;
                    const string text =
                            run_command(runtime, {"logs", "--tail", format("{}", cfg_.log_lines_), item.name_}, 5000);
                    lines = split_lines(text);
                } else {
                    const auto& item = inventory_.services_[rows[selected_]];
#if NEFLUX_PLATFORM_WINDOWS
                    lines.push_back("# Windows SCM does not expose service logs portably.");
                    lines.push_back("Use the service's own log file or Event Viewer.");
#else
                    const string text = run_command(
                            "journalctl", {"-u", item.name_, "-n", format("{}", cfg_.log_lines_), "--no-pager"}, 5000);
                    lines = split_lines(text);
#endif
                }
                if (lines.empty()) {
                    lines.push_back("# No log output available.");
                }
                detail_lines_ = move(lines);
                popup_ = popup_kind::info;
                popup_title_ = "Logs";
                popup_scroll_ = 0;
            }

            void open_logs() {
                if (current_ == view::containers) {
                    open_workload_logs(true);
                    return;
                }
                if (current_ == view::services) {
                    open_workload_logs(false);
                    return;
                }
                const auto value = selected_process_value();
                vector<string> lines;
                if (value.has_value()) {
#if NEFLUX_PLATFORM_WINDOWS
                    lines.push_back("# Process output files are not exposed by the Windows API.");
                    lines.push_back("Use the service or container log tools for persistent output.");
#else
                    const string base = format("/proc/{}/fd/", static_cast<uint64_t>(value->pid_));
                    string text;
                    for (const int fd: {1, 2}) {
                        char target[4096] = {};
                        const string link = base + format("{}", fd);
                        const ssize_t length = readlink(link.data(), target, sizeof(target) - 1);
                        if (length <= 0) {
                            continue;
                        }
                        target[length] = '\0';
                        const string path(target, static_cast<size_t>(length));
                        if (!starts_with(path.view(), "/") || starts_with(path.view(), "/dev/") ||
                            starts_with(path.view(), "/proc/")) {
                            continue;
                        }
                        const auto content = read_text_file(path);
                        if (!content.has_value()) {
                            continue;
                        }
                        const auto all = split_lines(*content);
                        const size_t take = neforce::min(all.size(), cfg_.log_lines_);
                        text.append(format("## fd {} · {}\n", fd, path));
                        for (size_t index = all.size() - take; index < all.size(); ++index) {
                            lines.push_back(all[index]);
                        }
                    }
                    if (lines.empty()) {
                        lines.push_back("# No readable regular stdout/stderr files for this process.");
                        lines.push_back(
                                "Pipes and terminals are not consumed; unsaved terminal output cannot be recovered.");
                    }
#endif
                } else {
                    lines.push_back("# Select a process first.");
                }
                detail_lines_ = move(lines);
                popup_ = popup_kind::info;
                popup_title_ = "Logs";
                popup_scroll_ = 0;
            }

            // ---------------------------------------------------------------- popups

            void draw_popup(painter& parent) {
                if (popup_ == popup_kind::none) {
                    return;
                }
                painter brush = parent.opaque();
                if (popup_ == popup_kind::help) {
                    draw_text_popup(brush, "Help", help_lines());
                    return;
                }
                if (popup_ == popup_kind::detail) {
                    draw_text_popup(brush, popup_title_, detail_lines_);
                    return;
                }
                if (popup_ == popup_kind::info) {
                    draw_text_popup(brush, popup_title_, detail_lines_);
                    return;
                }
                if (popup_ == popup_kind::settings) {
                    draw_settings(brush);
                    return;
                }
                if (popup_ == popup_kind::confirm) {
                    const int width = neforce::min(76, neforce::max(40, brush.width() - 8));
                    const int height = neforce::min(
                            brush.height() - 4,
                            neforce::max(8, static_cast<int>(split_lines(confirm_message_.view()).size()) + 5));
                    const rect area{(brush.width() - width) / 2, (brush.height() - height) / 2, width, height};
                    brush.box(area, popup_title_.view(), colors_.amber_);
                    brush.fill(rect{area.x_ + 1, area.y_ + 1, area.w_ - 2, area.h_ - 2}, " ", colors_.text_,
                               colors_.panel_);
                    const auto lines = split_lines(confirm_message_.view());
                    for (size_t index = 0; index < lines.size() && index + 3 < static_cast<size_t>(height); ++index) {
                        brush.text(area.x_ + 2, area.y_ + 1 + static_cast<int>(index),
                                   truncate_display(lines[index].view(), area.w_ - 4).view(), colors_.text_,
                                   colors_.panel_);
                    }
                    brush.text(area.x_ + 2, area.bottom() - 2, "y/Enter confirm · n/Esc cancel", colors_.border_,
                               colors_.panel_);
                    return;
                }
                if (popup_ == popup_kind::prompt) {
                    const int width = neforce::min(72, neforce::max(40, brush.width() - 8));
                    const rect area{(brush.width() - width) / 2, brush.height() / 2 - 3, width, 8};
                    brush.box(area, popup_title_.view(), colors_.mint_);
                    brush.fill(rect{area.x_ + 1, area.y_ + 1, area.w_ - 2, area.h_ - 2}, " ", colors_.text_,
                               colors_.panel_);
                    brush.text(area.x_ + 2, area.y_ + 2, "> ", colors_.mint_, colors_.panel_);
                    brush.text(area.x_ + 4, area.y_ + 2, prompt_text_.view(), colors_.text_, colors_.panel_);
                    brush.text(area.x_ + 4 + display_width(prompt_text_.view()), area.y_ + 2, "█", colors_.mint_,
                               colors_.panel_);
                    brush.text(area.x_ + 2, area.bottom() - 2, "Enter apply · Esc cancel", colors_.border_,
                               colors_.panel_);
                    return;
                }
                if (popup_ == popup_kind::commands) {
                    draw_text_popup(brush, "Commands", help_lines());
                    return;
                }
                const int width = neforce::min(64, neforce::max(30, brush.width() - 8));
                const int height = neforce::min(static_cast<int>(popup_entries_.size()) + 3, brush.height() - 4);
                const rect area{(brush.width() - width) / 2, (brush.height() - height) / 2, width, height};
                brush.box(area, popup_title_.view(), colors_.mint_);
                brush.fill(rect{area.x_ + 1, area.y_ + 1, area.w_ - 2, area.h_ - 2}, " ", colors_.text_,
                           colors_.panel_);
                const int visible = neforce::max(1, height - 3);
                popup_rect_ = area;
                popup_first_row_ = area.y_ + 1;
                popup_visible_rows_ = static_cast<size_t>(visible);
                if (popup_selected_ >= popup_scroll_ + static_cast<size_t>(visible)) {
                    popup_scroll_ = popup_selected_ + 1 - static_cast<size_t>(visible);
                }
                popup_scroll_ = neforce::min(popup_selected_, popup_scroll_);
                for (int row = 0; row < visible && popup_scroll_ + static_cast<size_t>(row) < popup_entries_.size();
                     ++row) {
                    const size_t index = popup_scroll_ + static_cast<size_t>(row);
                    const auto& item = popup_entries_[index];
                    const int y = area.y_ + 1 + row;
                    const bool active = index == popup_selected_;
                    brush.fill(rect{area.x_ + 1, y, area.w_ - 2, 1}, " ", colors_.text_,
                               active ? colors_.selection_ : colors_.panel_);
                    brush.text(area.x_ + 2, y, item.label_.view(), active ? colors_.mint_ : colors_.text_,
                               active ? colors_.selection_ : colors_.panel_, active);
                    if (!item.key_.empty()) {
                        brush.text_right(area.x_, y, area.w_ - 2, item.key_.view(), colors_.muted_,
                                         active ? colors_.selection_ : colors_.panel_);
                    }
                }
                brush.text(area.x_ + 2, area.bottom() - 2, "↑/↓ select · Enter activate · Esc close", colors_.border_,
                           colors_.panel_);
            }

            void draw_text_popup(painter& brush, const string& title, const vector<string>& lines) {
                const int width = neforce::min(brush.width() - 6, 110);
                vector<string> wrapped;
                for (const auto& line: lines) {
                    if (line.empty()) {
                        wrapped.push_back(string());
                        continue;
                    }
                    const auto parts = wrap_display(line.view(), width - 6);
                    for (size_t index = 0; index < parts.size(); ++index) {
                        wrapped.push_back(index == 0 ? parts[index] : "  " + parts[index]);
                    }
                }
                const int height =
                        neforce::min(brush.height() - 4, neforce::max(9, static_cast<int>(wrapped.size()) + 3));
                const rect area{(brush.width() - width) / 2, (brush.height() - height) / 2, width, height};
                popup_rect_ = area;
                popup_first_row_ = area.y_ + 1;
                popup_visible_rows_ = 0;
                brush.box(area, title.view(), colors_.mint_);
                brush.fill(rect{area.x_ + 1, area.y_ + 1, area.w_ - 2, area.h_ - 2}, " ", colors_.text_,
                           colors_.panel_);
                if (popup_ != popup_kind::detail && popup_ != popup_kind::info) {
                    popup_scroll_ = 0;
                }
                const int visible = height - 3;
                const size_t maximum = wrapped.size() > static_cast<size_t>(visible)
                                               ? wrapped.size() - static_cast<size_t>(visible)
                                               : 0;
                popup_scroll_ = neforce::min(popup_scroll_, maximum);
                for (int row = 0; row < visible && popup_scroll_ + static_cast<size_t>(row) < wrapped.size(); ++row) {
                    const string& line = wrapped[popup_scroll_ + static_cast<size_t>(row)];
                    color tint = colors_.text_;
                    if (!line.empty() && line[0] == '#') {
                        tint = colors_.muted_;
                    }
                    brush.fill(rect{area.x_ + 1, area.y_ + 1 + row, area.w_ - 2, 1}, " ", colors_.text_,
                               colors_.panel_);
                    brush.text(area.x_ + 2, area.y_ + 1 + row, truncate_display(line.view(), area.w_ - 4).view(), tint,
                               colors_.panel_);
                }
                brush.text(area.x_ + 2, area.bottom() - 2,
                           format("↑/↓ scroll · {}/{} · Esc close", popup_scroll_ + 1, maximum + 1).view(),
                           colors_.border_, colors_.panel_);
            }

            void draw_settings(painter& brush) {
                const int width = neforce::min(78, brush.width() - 6);
                const int height = neforce::min(23, brush.height() - 4);
                const rect area{(brush.width() - width) / 2, (brush.height() - height) / 2, width, height};
                popup_rect_ = area;
                popup_first_row_ = area.y_ + 1;
                popup_visible_rows_ = static_cast<size_t>(neforce::max(0, height - 3));
                brush.box(area, " Settings ", colors_.mint_);
                brush.fill(rect{area.x_ + 1, area.y_ + 1, area.w_ - 2, area.h_ - 2}, " ", colors_.text_,
                           colors_.panel_);
                const auto rows = settings_rows();
                const int visible = height - 3;
                if (!free_scroll_) {
                    if (compare_greater_equal(settings_index_, visible)) {
                        popup_scroll_ = settings_index_ + 1 - static_cast<size_t>(visible);
                    } else {
                        popup_scroll_ = 0;
                    }
                } else {
                    popup_scroll_ = rows.size() > static_cast<size_t>(visible)
                                            ? neforce::min(popup_scroll_, rows.size() - static_cast<size_t>(visible))
                                            : 0;
                }
                for (int row = 0; row < visible && popup_scroll_ + static_cast<size_t>(row) < rows.size(); ++row) {
                    const size_t index = popup_scroll_ + static_cast<size_t>(row);
                    const int y = area.y_ + 1 + row;
                    const bool active = index == settings_index_;
                    brush.fill(rect{area.x_ + 1, y, area.w_ - 2, 1}, " ", colors_.text_,
                               active ? colors_.selection_ : colors_.panel_);
                    brush.text(area.x_ + 2, y, rows[index].first.view(), active ? colors_.mint_ : colors_.text_,
                               active ? colors_.selection_ : colors_.panel_);
                    brush.text_right(area.x_, y, area.w_ - 2, rows[index].second.view(), colors_.muted_,
                                     active ? colors_.selection_ : colors_.panel_);
                }
                const rect settings_scrollbar{area.right() - 2, area.y_ + 1, 2, neforce::max(0, height - 3)};
                hits_.push_back({settings_scrollbar, hit_action::settings_scroll, 0});
                brush.scrollbar(settings_scrollbar, rows.size(), static_cast<size_t>(visible), popup_scroll_);
                brush.text(area.x_ + 2, area.bottom() - 2, "Enter choose · ←/→ adjust · w save · Esc close",
                           colors_.border_, colors_.panel_);
            }

            NEFORCE_NODISCARD vector<string> help_lines() const {
                vector<string> lines = {
                        "# nexus-flux — One terminal. Your whole system.",
                        "",
                        "1–9, ←/→, Tab/Shift+Tab      Change page",
                        "↑/↓, Alt+j/k, PgUp/PgDn     Select; scroll; listener list on System",
                        "F3, f, /                    Edit the table filter",
                        "F4                          Sort chooser",
                        "F5                          View options",
                        "Enter, d                    Open details",
                        "F6                          Context tools (details/logs/signals/priority/actions)",
                        "F7, l                       Logs: process output, container logs, service journal",
                        "Shift+←/→                   Horizontal process-column window",
                        ", / .                       Replay: previous / next frame",
                        "Esc                         Close, filter, or open the menu",
                        "F1, :                       Help; all commands",
                        "F2, c, w                    Settings; theme; save configuration",
                        "T, Ctrl+←/→                  Process tree; collapse/expand subtree",
                        "S, r                        Next sort key; reverse sort",
                        "P, F, L                     Pause; follow selected process; lazy CPU sort",
                        "O                           Cycle overview box preset",
                        "g, u                        GPU processes only; own processes only",
                        "Network: N, ↑/↓, Enter, p     Connections/interfaces; select; details; owner",
                        "i                           Show or hide loopback interfaces",
                        "C                           Process columns (90 selectable)",
                        "X, 0, E                     Explorer extras; history; events",
                        "D                           Process/service side panel; GPU device details",
                        "R                           ROS graph for the selected process",
                        "Space/v                     Mark process",
                        "k/K, t, s                   Kill, terminate, or choose a signal",
                        "W, z/Z, -/+                 Chart window; zoom; sampling interval",
                        "[, ]                        Previous or next GPU",
                        "Ctrl+C / Ctrl+Shift+C        Copy selection (OSC52/clipboard); quit when nothing is selected",
                        "q, Ctrl+C (no selection)     Quit",
                        "",
                        "# Filters",
                        "user:alice cpu:>10 !name:rust",
                        "name:/python|torch/ pid:42 | mem:>1024",
                        "",
                        "# Notes",
                        "Read-only and replay disable state changes.",
                        "All data stays local; no server is contacted.",
                };
                if (replay_) {
                    lines.push_back("# Replay: use ←/→ with Alt to seek.");
                }
                return lines;
            }

            NEFORCE_NODISCARD vector<pair<string, string>> settings_rows() const {
                vector<pair<string, string>> rows;
                rows.push_back({"Config file", config_path_});
                rows.push_back(
                        {"Sampling interval", format("{:.2f}s", static_cast<double>(cfg_.interval_ms_) / 1000.0)});
                rows.push_back({"History", format("{} samples", static_cast<uint64_t>(cfg_.history_))});
                rows.push_back({"Chart window", time_label(cfg_.time_window_seconds_)});
                rows.push_back({"Theme", cfg_.theme_});
                rows.push_back({"Layout", cfg_.layout_});
                rows.push_back({"Show threads", cfg_.show_threads_ ? "On" : "Off"});
                rows.push_back({"Show kernel threads", cfg_.show_kernel_threads_ ? "On" : "Off"});
                rows.push_back({"Show loopback", cfg_.show_loopback_ ? "On" : "Off"});
                rows.push_back({"Collect GPU", cfg_.collect_gpu_ ? "On" : "Off"});
                rows.push_back({"ASCII glyphs", cfg_.ascii_ ? "On" : "Off"});
                rows.push_back({"No color", cfg_.no_color_ ? "On" : "Off"});
                rows.push_back({"Alert hold", format("{} s", static_cast<uint64_t>(cfg_.alert_hold_seconds_))});
                rows.push_back({"Log tail lines", format("{}", static_cast<uint64_t>(cfg_.log_lines_))});
                rows.push_back({"CPU warning", format("{:.0f}%", cfg_.cpu_warning_)});
                rows.push_back({"Memory warning", format("{:.0f}%", cfg_.memory_warning_)});
                rows.push_back({"GPU temperature warning", format("{}°C", cfg_.gpu_temperature_warning_)});
                rows.push_back(
                        {"Sort by", sorts[neforce::min(sort_index_, sizeof(sorts) / sizeof(sorts[0]) - 1)].name});
                for (auto item: cfg_.display_.settings()) {
                    if (item.second == "true") {
                        item.second = "On";
                    } else if (item.second == "false") {
                        item.second = "Off";
                    }
                    rows.push_back(move(item));
                }
                rows.push_back({"Tree auto-collapse", cfg_.tree_auto_collapse_ == 0
                                                              ? string("Off")
                                                              : format("{} children", cfg_.tree_auto_collapse_)});
                rows.push_back({"Tree aggregate", cfg_.tree_aggregate_ ? "On" : "Off"});
                rows.push_back({"Overview preset",
                                cfg_.overview_presets_.empty()
                                        ? string("—")
                                        : cfg_.overview_presets_[neforce::min(cfg_.overview_preset_,
                                                                              cfg_.overview_presets_.size() - 1)]});
                return rows;
            }

            NEFORCE_NODISCARD vector<menu_entry> setting_choices(size_t row) const {
                vector<menu_entry> out;
                const auto add = [&out, row](const string& label, const string& code, const string& current) {
                    out.push_back({label, code == current ? "●" : "", format("setting_pick:{}:{}", row, code)});
                };
                if (row == 0) {
                    add("Read-only path", "path", "path");
                    return out;
                }
                if (row == 1) {
                    for (const auto& value: {250U, 500U, 1000U, 2000U, 5000U, 10000U}) {
                        add(format("{:.2f} s", static_cast<double>(value) / 1000.0), format("{}", value),
                            format("{}", cfg_.interval_ms_));
                    }
                } else if (row == 2) {
                    for (const auto value: {60U, 120U, 300U, 600U, 1800U, 3600U}) {
                        add(format("{} samples", value), format("{}", value), format("{}", cfg_.history_));
                    }
                } else if (row == 3) {
                    static constexpr uint64_t values[] = {15, 30, 60, 180, 300, 900, 1800, 3600, 0};
                    size_t current = 0;
                    for (size_t i = 0; i < 9; ++i) {
                        if (values[i] == cfg_.time_window_seconds_) {
                            current = i;
                            break;
                        }
                    }
                    for (size_t i = 0; i < 9; ++i) {
                        add(values[i] == 0 ? "All retained" : time_label(values[i]), format("{}", i),
                            format("{}", current));
                    }
                } else if (row == 4) {
                    for (const auto& name: config::themes()) {
                        add(name, name, cfg_.theme_);
                    }
                } else if (row == 5) {
                    for (const auto& name: {"balanced", "compute", "storage", "minimal"}) {
                        add(name, name, cfg_.layout_);
                    }
                } else if (row >= 6 && row <= 11) {
                    const auto rows = settings_rows();
                    const string current = row < rows.size() && rows[row].second == "On" ? "on" : "off";
                    add("On", "on", current);
                    add("Off", "off", current);
                } else if (row == 12) {
                    for (const auto value: {0U, 1U, 2U, 3U, 5U, 10U}) {
                        add(format("{} s", value), format("{}", value), format("{}", cfg_.alert_hold_seconds_));
                    }
                } else if (row == 13) {
                    for (const auto value: {100U, 250U, 500U, 1000U, 2000U, 5000U}) {
                        add(format("{} lines", value), format("{}", value), format("{}", cfg_.log_lines_));
                    }
                } else if (row == 14 || row == 15) {
                    for (const auto value: {50U, 60U, 70U, 80U, 90U, 95U}) {
                        add(format("{}%", value), format("{}", value),
                            format("{:.0f}", row == 14 ? cfg_.cpu_warning_ : cfg_.memory_warning_));
                    }
                } else if (row == 16) {
                    for (const auto value: {60U, 70U, 75U, 80U, 85U, 90U, 95U}) {
                        add(format("{}°C", value), format("{}", value), format("{}", cfg_.gpu_temperature_warning_));
                    }
                } else if (row == 17) {
                    for (size_t i = 0; i < sizeof(sorts) / sizeof(sorts[0]); ++i) {
                        add(sorts[i].name, format("{}", i), format("{}", sort_index_));
                    }
                } else {
                    const auto rows = settings_rows();
                    const string label = row < rows.size() ? rows[row].first : string();
                    const string current = row < rows.size() ? rows[row].second : string();
                    if (label == "Tree auto-collapse") {
                        for (const auto value: {0U, 2U, 4U, 8U, 16U, 32U, 64U}) {
                            add(value == 0 ? "Off" : format("{} children", value), format("{}", value),
                                format("{}", cfg_.tree_auto_collapse_));
                        }
                    } else if (label == "Tree aggregate") {
                        add("On", "on", cfg_.tree_aggregate_ ? "on" : "off");
                        add("Off", "off", cfg_.tree_aggregate_ ? "on" : "off");
                    } else if (label == "Overview preset") {
                        for (size_t index = 0; index < cfg_.overview_presets_.size(); ++index) {
                            add(cfg_.overview_presets_[index], format("{}", index),
                                format("{}", cfg_.overview_preset_));
                        }
                    } else if (label.contains("graph symbol")) {
                        for (const auto& value: {"default", "braille", "block", "tty", "ascii"}) {
                            if (value == "default" && label == "Graph symbols") {
                                continue;
                            }
                            add(value, value, current);
                        }
                    } else if (label == "Temperature unit") {
                        add("celsius", "celsius", current);
                        add("fahrenheit", "fahrenheit", current);
                        add("kelvin", "kelvin", current);
                    } else if (label == "Expanded overview panel") {
                        for (const auto& value: {"all", "cpu", "memory", "gpu", "storage", "network"}) {
                            add(value, value, current);
                        }
                    } else if (current == "On" || current == "Off") {
                        add("On", "on", current == "On" ? "on" : "off");
                        add("Off", "off", current == "On" ? "on" : "off");
                    } else {
                        add("Decrease", "dec", "none");
                        add("Increase", "inc", "none");
                    }
                }
                return out;
            }

            void open_setting_choice() {
                const auto rows = settings_rows();
                if (settings_index_ >= rows.size()) {
                    return;
                }
                setting_choice_row_ = settings_index_;
                popup_parent_ = popup_kind::settings;
                popup_ = popup_kind::main_menu;
                popup_title_ = format("Setting · {}", rows[settings_index_].first);
                popup_entries_ = setting_choices(settings_index_);
                popup_selected_ = 0;
                for (size_t i = 0; i < popup_entries_.size(); ++i) {
                    if (!popup_entries_[i].key_.empty()) {
                        popup_selected_ = i;
                        break;
                    }
                }
                popup_scroll_ = 0;
            }

            void apply_setting_choice(size_t row, const string& code) {
                const auto as_u64 = [&code]() -> uint64_t { return parse_u64(code.view()).value_or(0); };
                const auto as_double = [&code]() -> double { return parse_f64(code.view()).value_or(0.0); };
                if (row == 1) {
                    cfg_.interval_ms_ = neforce::clamp<uint64_t>(as_u64(), 250, 10000);
                } else if (row == 2) {
                    cfg_.history_ = static_cast<size_t>(neforce::clamp<uint64_t>(as_u64(), 30, 3600));
                } else if (row == 3) {
                    set_time_window(static_cast<size_t>(neforce::clamp<uint64_t>(as_u64(), 0, 8)));
                } else if (row == 4) {
                    apply_theme(code);
                } else if (row == 5) {
                    cfg_.layout_ = code;
                } else if (row >= 6 && row <= 11) {
                    const bool value = code == "on";
                    if (row == 6) {
                        cfg_.show_threads_ = value;
                    } else if (row == 7) {
                        cfg_.show_kernel_threads_ = value;
                    } else if (row == 8) {
                        cfg_.show_loopback_ = value;
                    } else if (row == 9) {
                        cfg_.collect_gpu_ = value;
                    } else if (row == 10) {
                        cfg_.ascii_ = value;
                    } else {
                        cfg_.no_color_ = value;
                        configure_theme();
                    }
                } else if (row == 12) {
                    cfg_.alert_hold_seconds_ = as_u64();
                } else if (row == 13) {
                    cfg_.log_lines_ = static_cast<size_t>(neforce::clamp<uint64_t>(as_u64(), 10, 10000));
                } else if (row == 14) {
                    cfg_.cpu_warning_ = static_cast<float>(as_double());
                } else if (row == 15) {
                    cfg_.memory_warning_ = as_double();
                } else if (row == 16) {
                    cfg_.gpu_temperature_warning_ = static_cast<uint32_t>(as_u64());
                } else if (row == 17) {
                    sort_index_ = static_cast<size_t>(
                            neforce::clamp<uint64_t>(as_u64(), 0, sizeof(sorts) / sizeof(sorts[0]) - 1));
                    reverse_ = !sorts[sort_index_].descending;
                    sync_sort_config();
                } else if (row >= 18) {
                    const auto rows = settings_rows();
                    const string label = row < rows.size() ? rows[row].first : string();
                    const string current = row < rows.size() ? rows[row].second : string();
                    if (label == "Tree auto-collapse") {
                        const auto value = parse_u64(code.view());
                        cfg_.tree_auto_collapse_ = value.has_value() ? static_cast<size_t>(*value) : 0;
                        auto_collapse_done_ = false;
                        apply_tree_auto_collapse();
                    } else if (label == "Tree aggregate") {
                        cfg_.tree_aggregate_ = code == "on";
                    } else if (label == "Overview preset") {
                        const auto value = parse_u64(code.view());
                        if (value.has_value() && *value < cfg_.overview_presets_.size()) {
                            cfg_.overview_preset_ = static_cast<size_t>(*value);
                        }
                    } else if (label == "Expanded overview panel") {
                        cfg_.display_.overview_panel_ = code;
                    } else if (label == "Temperature unit") {
                        cfg_.display_.temperature_unit_ = code;
                    } else if (label == "Graph symbols") {
                        cfg_.display_.graph_symbol_ = code;
                    } else if (label.contains("graph symbol")) {
                        if (label.starts_with("CPU")) {
                            cfg_.display_.graph_symbol_cpu_ = code;
                        } else if (label.starts_with("Memory")) {
                            cfg_.display_.graph_symbol_mem_ = code;
                        } else if (label.starts_with("Network")) {
                            cfg_.display_.graph_symbol_net_ = code;
                        } else if (label.starts_with("Process")) {
                            cfg_.display_.graph_symbol_proc_ = code;
                        } else if (label.starts_with("GPU")) {
                            cfg_.display_.graph_symbol_gpu_ = code;
                        }
                    } else {
                        const size_t saved = settings_index_;
                        settings_index_ = row;
                        if (code == "on" || code == "off") {
                            if ((code == "on") != (current == "On")) {
                                adjust_setting(1);
                            }
                        } else {
                            adjust_setting(code == "inc" ? 1 : -1);
                        }
                        settings_index_ = saved;
                    }
                }
            }

            void open_main_menu() {
                popup_parent_ = popup_kind::none;
                popup_ = popup_kind::main_menu;
                popup_title_ = "nexus-flux";
                const bool reusable = popup_entries_.size() == 9 && popup_entries_[0].action_ == "resume" &&
                                      popup_entries_[7].action_ == "save";
                if (!reusable) {
                    popup_entries_ = {
                            {"Resume", "Esc", "resume"},
                            {"Help", "F1", "help"},
                            {"Settings", "F2", "settings"},
                            {"Sort", "F4", "sort"},
                            {"Chart window", "W", "time"},
                            {"Commands", ":", "commands"},
                            {"Theme: " + cfg_.theme_, "c", "theme"},
                            {"Save config", "w", "save"},
                            {"Quit", "q", "quit"},
                    };
                } else {
                    popup_entries_[6].label_ = "Theme: " + cfg_.theme_;
                }
                popup_selected_ = 0;
                popup_scroll_ = 0;
            }

            void open_theme_menu(popup_kind parent = popup_kind::none) {
                popup_parent_ = parent;
                popup_ = popup_kind::theme_menu;
                popup_title_ = "Theme · preview";
                popup_entries_.clear();
                const auto names = config::themes();
                for (const auto& name: names) {
                    popup_entries_.push_back({name, name == cfg_.theme_ && cfg_.theme_file_.empty() ? "●" : "",
                                              format("theme:{}", name)});
                }
                for (const auto& file: theme_files()) {
                    const auto name = ::neforce::path(file).filename();
                    popup_entries_.push_back(
                            {name, file == cfg_.theme_file_ ? "●" : "", format("theme_file:{}", file)});
                }
                popup_selected_ = 0;
                for (size_t index = 0; index < names.size(); ++index) {
                    if (names[index] == cfg_.theme_) {
                        popup_selected_ = index;
                        break;
                    }
                }
                popup_scroll_ = 0;
            }

            void apply_theme(const string& name) {
                cfg_.theme_ = name;
                cfg_.theme_file_.clear();
                colors_ = make_palette(cfg_.theme_, cfg_.custom_colors_);
                if (cfg_.no_color_) {
                    colors_ = monochrome_palette(colors_);
                }
                for (auto& entry: popup_entries_) {
                    if (starts_with(entry.action_.view(), "theme_file:")) {
                        entry.key_.clear();
                        continue;
                    }
                    const string prefix = "theme:";
                    if (starts_with(entry.action_.view(), prefix.view())) {
                        entry.key_ = entry.action_.substr(prefix.size()) == cfg_.theme_ ? "●" : "";
                    }
                }
            }

            void apply_theme_file(const string& file) {
                cfg_.theme_file_ = file;
                configure_theme();
                for (auto& entry: popup_entries_) {
                    const string prefix = "theme_file:";
                    if (starts_with(entry.action_.view(), prefix.view())) {
                        entry.key_ = entry.action_.substr(prefix.size()) == cfg_.theme_file_ ? "●" : "";
                    }
                }
            }

            void open_sort_menu() {
                popup_ = popup_kind::sort;
                popup_entries_.clear();
                if (current_ == view::network) {
                    popup_title_ = "Network sort";
                    popup_entries_ = {{"NETWORK RX", "d", "net_sort_rx"},
                                      {"NETWORK TX", "u", "net_sort_tx"},
                                      {"NAME", "n", "net_sort_name"}};
                    popup_selected_ = neforce::min<size_t>(network_sort_, 2);
                    popup_scroll_ = 0;
                    return;
                }
                popup_title_ = "Sort by";
                if (current_ == view::containers || current_ == view::services) {
                    const auto add_mode = [this](sort_mode mode, string label) {
                        for (size_t index = 0; index < sizeof(sorts) / sizeof(sorts[0]); ++index) {
                            if (sorts[index].mode == mode) {
                                popup_entries_.push_back({move(label), sorts[index].descending ? "↓" : "↑",
                                                          format("sort:{}", static_cast<uint64_t>(index))});
                                return;
                            }
                        }
                    };
                    add_mode(sort_mode::cpu, "CPU");
                    add_mode(sort_mode::memory, "RAM");
                    add_mode(sort_mode::name, "NAME");
                } else {
                    for (const auto& item: sorts) {
                        popup_entries_.push_back({item.name, item.descending ? "↓" : "↑", "sort"});
                    }
                }
                popup_entries_.push_back({"Reverse current order", "r", "reverse"});
                popup_selected_ = neforce::min(sort_index_, popup_entries_.size() - 1);
                popup_scroll_ = 0;
            }

            void show_ros_entries(const string& kind, const string& output) {
                ros_kind_ = kind;
                ros_entries_.clear();
                popup_entries_.clear();
                for (const auto& line: split_lines(output)) {
                    const string text = trim(line.view());
                    if (text.empty() || starts_with(text.view(), "[")) {
                        continue;
                    }
                    ros_entries_.push_back(text);
                    if (kind == "topics") {
                        popup_entries_.push_back({text, "", format("ros:topic:{}", text)});
                    } else if (kind == "nodes") {
                        popup_entries_.push_back({text, "", format("ros:node:{}", text)});
                    } else {
                        popup_entries_.push_back({text, "", format("ros:service:{}", text)});
                    }
                }
                popup_title_ = format("ROS {} · {} entries", kind, static_cast<uint64_t>(ros_entries_.size()));
                popup_ = popup_kind::main_menu;
                popup_selected_ = 0;
                popup_scroll_ = 0;
                if (ros_entries_.empty()) {
                    detail_lines_ = split_lines(output);
                    if (detail_lines_.empty()) {
                        detail_lines_.push_back("No entries");
                    }
                    popup_ = popup_kind::info;
                }
            }

            void show_ros_target_menu(const string& kind, const string& name) {
                popup_entries_.clear();
                if (kind == "topic") {
                    popup_title_ = format("ROS topic · {}", name);
                    popup_entries_ = {
                            {"Info · publishers / subscribers", "", format("ros:info:{}", name)},
                            {"Echo first message", "", format("ros:echo:{}", name)},
                            {"Hz / receive rate (3 s)", "", format("ros:hz:{}", name)},
                    };
                } else if (kind == "node") {
                    popup_title_ = format("ROS node · {}", name);
                    popup_entries_ = {{"Node info", "", format("ros:nodeinfo:{}", name)},
                                      {"Parameters", "", format("ros:params:{}", name)}};
                } else {
                    popup_title_ = format("ROS service · {}", name);
                    popup_entries_ = {{"Service details", "", format("ros:serviceinfo:{}", name)}};
                }
                popup_ = popup_kind::main_menu;
                popup_selected_ = 0;
                popup_scroll_ = 0;
            }

            void run_ros_action(const string& action) {
                if (!ros_session_.has_value()) {
                    return;
                }
                if (starts_with(action.view(), "ros:topic:")) {
                    show_ros_target_menu("topic", action.substr(10));
                    return;
                }
                if (starts_with(action.view(), "ros:node:")) {
                    show_ros_target_menu("node", action.substr(9));
                    return;
                }
                if (starts_with(action.view(), "ros:service:")) {
                    show_ros_target_menu("service", action.substr(12));
                    return;
                }
                if (starts_with(action.view(), "ros:info:") || starts_with(action.view(), "ros:echo:") ||
                    starts_with(action.view(), "ros:hz:") || starts_with(action.view(), "ros:nodeinfo:") ||
                    starts_with(action.view(), "ros:serviceinfo:") || starts_with(action.view(), "ros:params:")) {
                    const bool info = starts_with(action.view(), "ros:info:");
                    const bool echo = starts_with(action.view(), "ros:echo:");
                    const bool hz = starts_with(action.view(), "ros:hz:");
                    const bool node_info = starts_with(action.view(), "ros:nodeinfo:");
                    const bool parameters = starts_with(action.view(), "ros:params:");
                    const auto first = action.find(':', 4);
                    const string name = first == string::npos ? string() : action.substr(first + 1);
                    try {
                        string output;
                        if (node_info) {
                            output = run_ros_query(*ros_session_, ros_query::node_info, name);
                        } else if (parameters) {
                            output = run_ros_query(*ros_session_, ros_query::parameters, name);
                        } else if (echo) {
                            output = run_ros_query(*ros_session_, ros_query::topic_echo, name);
                        } else if (hz) {
                            output = run_ros_query(*ros_session_, ros_query::topic_hz, name);
                        } else if (info) {
                            output = run_ros_query(*ros_session_, ros_query::topic_info, name);
                        } else {
                            output = run_ros_query(*ros_session_, ros_query::service_info, name);
                        }
                        detail_lines_ = split_lines(output.view());
                    } catch (const exception& error) {
                        detail_lines_ = {format("ROS query failed: {}", error.what())};
                    }
                    if (detail_lines_.empty()) {
                        detail_lines_.push_back("No output");
                    }
                    popup_title_ = name.empty() ? "ROS" : name;
                    popup_ = popup_kind::info;
                    popup_scroll_ = 0;
                    return;
                }
                string title;
                auto type = ros_query::context;
                if (action == "ros:summary") {
                    detail_lines_ = split_lines(ros_summary(*ros_session_).view());
                    popup_title_ = "ROS graph";
                    popup_ = popup_kind::info;
                    popup_scroll_ = 0;
                    return;
                }
                if (action == "ros:topics") {
                    type = ros_query::topics;
                    title = "ROS topics";
                } else if (action == "ros:nodes") {
                    type = ros_query::nodes;
                    title = "ROS nodes";
                } else if (action == "ros:services") {
                    type = ros_query::services;
                    title = "ROS services";
                } else if (action == "ros:environment") {
                    type = ros_query::environment;
                    title = "ROS environment";
                } else if (action == "ros:context") {
                    type = ros_query::context;
                    title = "ROS communication context";
                } else {
                    return;
                }
                try {
                    const string output = run_ros_query(*ros_session_, type);
                    if (type == ros_query::topics || type == ros_query::nodes || type == ros_query::services) {
                        show_ros_entries(type == ros_query::topics ? "topics"
                                                                   : (type == ros_query::nodes ? "nodes" : "services"),
                                         output);
                        return;
                    }
                    detail_lines_ = split_lines(output.view());
                } catch (const exception& error) {
                    detail_lines_ = {format("ROS query failed: {}", error.what())};
                }
                if (detail_lines_.empty()) {
                    detail_lines_.push_back("No output");
                }
                popup_title_ = title;
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            void open_columns_menu() {
                popup_ = popup_kind::columns;
                popup_title_ = "Process columns";
                if (cfg_.columns_.empty()) {
                    for (const char* key: {"pid", "user", "cpu", "memory", "vram", "gpu", "state", "command"}) {
                        cfg_.columns_.push_back(key);
                    }
                }
                popup_entries_.clear();
                for (const auto& definition: column_catalog()) {
                    bool active = false;
                    for (const auto& column: cfg_.columns_) {
                        if (column == definition.key_) {
                            active = true;
                            break;
                        }
                    }
                    popup_entries_.push_back(
                            {format("{} {:<16} {}", active ? "[x]" : "[ ]", definition.title_, definition.key_), "",
                             format("col:{}", definition.key_)});
                }

                popup_selected_ = 0;
                popup_scroll_ = 0;
            }

            void open_network_context() {
                popup_parent_ = popup_kind::none;
                popup_ = popup_kind::main_menu;
                popup_entries_.clear();
                if (network_section_ == 1) {
                    popup_title_ = "Network interface";
                    popup_entries_ = {
                            {"Interface", "Enter", "net_interface_detail"},
                            {"Sort RX", "d", "net_sort_rx"},
                            {"Sort TX", "u", "net_sort_tx"},
                            {"Sort name", "n", "net_sort_name"},
                            {"Toggle loopback", "i", "net_toggle_loopback"},
                    };
                } else {
                    popup_title_ = "Network connection";
                    popup_entries_ = {
                            {"Connection", "Enter", "net_connection_detail"}, {"Filter port", "p", "net_filter_port"},
                            {"Filter this PID", "o", "net_filter_pid"},       {"Show owner", "P", "net_owner"},
                            {"Toggle loopback", "i", "net_toggle_loopback"},
                    };
                }
                popup_selected_ = 0;
                popup_scroll_ = 0;
            }

            void open_storage_context(bool filesystem_row) {
                storage_section_ = filesystem_row ? 0 : 1;
                popup_parent_ = popup_kind::none;
                popup_ = popup_kind::main_menu;
                popup_title_ = filesystem_row ? "Filesystem" : "Block device";
                popup_entries_ = {
                        {"Details", "Enter", "storage_detail"},
                        {"Previous row", "↑", "storage_prev"},
                        {"Next row", "↓", "storage_next"},
                };
                popup_selected_ = 0;
                popup_scroll_ = 0;
            }

            void open_gpu_context() {
                popup_parent_ = popup_kind::none;
                popup_ = popup_kind::main_menu;
                popup_title_ = "GPU device";
                popup_entries_ = {
                        {"Device details", "Enter", "gpu_detail"},
                        {"Show processes", "P", "gpu_processes"},
                        {"Previous GPU", "[", "gpu_prev"},
                        {"Next GPU", "]", "gpu_next"},
                };
                popup_selected_ = 0;
                popup_scroll_ = 0;
            }

            void open_view_menu() {
                popup_ = popup_kind::view_menu;
                popup_title_ = "View";
                popup_entries_ = {
                        {"Overview", "1", "view1"},   {"Processes", "2", "view2"}, {"GPU", "3", "view3"},
                        {"Storage", "4", "view4"},    {"Network", "5", "view5"},   {"System", "6", "view6"},
                        {"Containers", "7", "view7"}, {"Services", "8", "view8"},  {"VMs", "9", "view9"},
                        {"Explorer", "X", "view10"},  {"History", "0", "view11"},  {"Events", "E", "view12"},
                        {"Toggle tree", "T", "tree"},
                };
                popup_selected_ = view_index(current_);
                popup_scroll_ = 0;
            }

            void open_time_menu() {
                popup_ = popup_kind::time_menu;
                popup_title_ = "Chart time window";
                popup_entries_.clear();
                for (const uint64_t seconds: {15ULL, 30ULL, 60ULL, 180ULL, 300ULL, 900ULL, 1800ULL, 3600ULL}) {
                    popup_entries_.push_back(
                            {time_label(seconds), cfg_.time_window_seconds_ == seconds ? "●" : "", "time"});
                }
                popup_entries_.push_back({"All retained samples", "0", "time"});
                popup_selected_ = 0;
                popup_scroll_ = 0;
            }

            void open_detail() {
                detail_lines_ = build_detail();
                if (detail_lines_.empty()) {
                    return;
                }
                popup_ = popup_kind::detail;
                popup_title_ = "Details";
                popup_scroll_ = 0;
            }

            NEFORCE_NODISCARD vector<string> build_detail() const {
                vector<string> lines;
                if (current_ == view::processes || current_ == view::overview || selected_process_.has_value()) {
                    const auto value = selected_process_value();
                    if (!value.has_value()) {
                        return lines;
                    }
                    const auto& item = *value;
                    lines.push_back(format("# {} · PID {}", item.name_, static_cast<uint64_t>(item.pid_)));
                    lines.push_back(format("Command       {}", item.command_));
                    lines.push_back(format("User          {}", item.user_));
                    lines.push_back(format("State         {}", item.state_));
                    lines.push_back(format("CPU           {:.1f}%", item.cpu_));
                    lines.push_back(format("CPU time      {}", duration(item.cpu_time_ms_ / 1000)));
                    lines.push_back(format("Memory        {} (virtual {})", bytes(static_cast<double>(item.memory_)),
                                           bytes(static_cast<double>(item.virtual_memory_))));
                    lines.push_back(format("I/O           read {}/s · write {}/s", bytes(item.read_per_sec_),
                                           bytes(item.write_per_sec_)));
                    lines.push_back(format("I/O totals    read {} · write {}",
                                           bytes(static_cast<double>(item.read_total_)),
                                           bytes(static_cast<double>(item.write_total_))));
                    lines.push_back(format("Start         {}", format("{}", static_cast<uint64_t>(item.start_time_))));
                    lines.push_back(format("Runtime       {}", duration(item.runtime_)));
                    lines.push_back(format("Nice          {}",
                                           item.nice_.has_value() ? format("{}", *item.nice_) : string("—")));
                    lines.push_back(format("Kernel thread {}", item.kernel_thread_ ? "yes" : "no"));
                    if (item.gpu_memory_.has_value() || item.gpu_percent_.has_value()) {
                        lines.push_back(
                                format("GPU           {} · {}",
                                       item.gpu_percent_.has_value() ? format("{}%", *item.gpu_percent_) : string("—"),
                                       item.gpu_memory_.has_value() ? bytes(static_cast<double>(*item.gpu_memory_))
                                                                    : string("—")));
                    }
                    lines.push_back("");
                    lines.push_back(
                            format("# History CPU {:.1f}% · RAM {} · GPU {}",
                                   process_history_.cpu_.empty() ? 0.0 : process_history_.cpu_.back(),
                                   process_history_.ram_.empty() ? string("—") : bytes(process_history_.ram_.back()),
                                   process_history_.gpu_.empty() ? string("—")
                                                                 : format("{:.0f}%", process_history_.gpu_.back())));
                    return lines;
                }
                if (current_ == view::storage) {
                    if (selected_ >= snap_.disks_.size()) {
                        return lines;
                    }
                    const auto& item = snap_.disks_[selected_];
                    lines.push_back(format("# {} · {}", item.name_, item.mount_));
                    lines.push_back(format("Filesystem    {}", item.filesystem_));
                    lines.push_back(format("Kind          {}", item.kind_));
                    lines.push_back(format("Total         {}", bytes(static_cast<double>(item.total_))));
                    lines.push_back(format("Used          {}", bytes(static_cast<double>(item.used_))));
                    lines.push_back(format("Available     {}", bytes(static_cast<double>(item.available_))));
                    lines.push_back(format("Removable     {}", item.removable_ ? "yes" : "no"));
                    return lines;
                }
                if (current_ == view::containers || current_ == view::services) {
                    const bool containers = current_ == view::containers;
                    if (selected_ >= (containers ? inventory_.containers_.size() : inventory_.services_.size())) {
                        return lines;
                    }
                    if (containers) {
                        const auto& item = inventory_.containers_[selected_];
                        lines.push_back(format("# {} · {}", item.name_, item.runtime_));
                        lines.push_back(format("ID            {}", item.id_));
                        lines.push_back(format("Image         {}", item.image_));
                        lines.push_back(format("State         {}", item.state_));
                        lines.push_back(format("Status        {}", item.status_));
                        lines.push_back(format("Ports         {}", item.ports_));
                        lines.push_back(format("Command       {}", item.command_));
                        lines.push_back(format("CPU           {}",
                                               item.cpu_.has_value() ? format("{:.1f}%", *item.cpu_) : string("—")));
                        lines.push_back(format("Memory        {}", item.memory_.has_value()
                                                                           ? bytes(static_cast<double>(*item.memory_))
                                                                           : string("—")));
                    } else {
                        const auto& item = inventory_.services_[selected_];
                        lines.push_back(format("# {} · {}", item.name_, item.scope_));
                        lines.push_back(format("Load          {}", item.load_));
                        lines.push_back(format("Active        {}", item.active_));
                        lines.push_back(format("Sub           {}", item.sub_));
                        lines.push_back(format("Description   {}", item.description_));
                        lines.push_back(format("PID           {}",
                                               item.pid_.has_value() ? format("{}", *item.pid_) : string("—")));
                        lines.push_back(format("CPU           {}", item.cpu_percent_.has_value()
                                                                           ? format("{:.1f}%", *item.cpu_percent_)
                                                                           : string("—")));
                        lines.push_back(format("Memory        {}", item.memory_.has_value()
                                                                           ? bytes(static_cast<double>(*item.memory_))
                                                                           : string("—")));
                    }
                    return lines;
                }
                if (current_ == view::network) {
                    lines.push_back(format("# Network · {} interfaces", static_cast<uint64_t>(snap_.networks_.size())));
                    lines.push_back(
                            format("RX            {}/s",
                                   bytes(history_.network_totals_[cfg_.show_loopback_ ? 1 : 0].rx_.empty()
                                                 ? 0.0
                                                 : history_.network_totals_[cfg_.show_loopback_ ? 1 : 0].rx_.back())));
                    lines.push_back(
                            format("TX            {}/s",
                                   bytes(history_.network_totals_[cfg_.show_loopback_ ? 1 : 0].tx_.empty()
                                                 ? 0.0
                                                 : history_.network_totals_[cfg_.show_loopback_ ? 1 : 0].tx_.back())));
                    for (const auto& item: snap_.networks_) {
                        lines.push_back(format("{}  {}  {}", item.name_, item.state_, item.address_));
                    }
                    return lines;
                }
                if (current_ == view::gpu && !snap_.gpus_.empty()) {
                    const auto& item = snap_.gpus_[neforce::min(selected_gpu_, snap_.gpus_.size() - 1)];
                    lines.push_back(format("# {} · {}", item.name_, item.vendor_));
                    lines.push_back(format("UUID          {}", item.uuid_));
                    lines.push_back(format("Backend       {}", item.backend_));
                    lines.push_back(format("Driver        {}", item.driver_));
                    if (const json* vbios = item.extra_.get("vbios_version"); vbios != nullptr && vbios->is_string()) {
                        lines.push_back(format("BIOS          {}", vbios->as_string()));
                    }
                    if (const json* generation = item.extra_.get("pcie_link_generation");
                        generation != nullptr && generation->is_integer()) {
                        const json* width = item.extra_.get("pcie_link_width");
                        lines.push_back(format("PCIe          Gen{} x{}", generation->as_integer(),
                                               width != nullptr && width->is_integer() ? width->as_integer() : 0));
                    }
                    if (item.cuda_driver_version_.has_value()) {
                        lines.push_back(format("CUDA Driver   {}", *item.cuda_driver_version_));
                    }
                    if (item.nvml_version_.has_value()) {
                        lines.push_back(format("NVML          {}", *item.nvml_version_));
                    }
                    lines.push_back(format("GPU           {}", item.utilization_.has_value()
                                                                       ? format("{}%", *item.utilization_)
                                                                       : string("—")));
                    lines.push_back(
                            format("VRAM          {} / {}",
                                   item.memory_used_.has_value() ? bytes(static_cast<double>(*item.memory_used_))
                                                                 : string("—"),
                                   item.memory_total_.has_value() ? bytes(static_cast<double>(*item.memory_total_))
                                                                  : string("—")));
                    lines.push_back(
                            format("Temperature   {}", item.temperature_.has_value()
                                                               ? cfg_.display_.temperature(*item.temperature_, 0)
                                                               : string("—")));
                    lines.push_back(format("Power         {}", item.power_watts_.has_value()
                                                                       ? format("{:.1f} W", *item.power_watts_)
                                                                       : string("—")));
                    lines.push_back(format("Power limit   {}", item.power_limit_watts_.has_value()
                                                                       ? format("{:.1f} W", *item.power_limit_watts_)
                                                                       : string("—")));
                    lines.push_back(format("Clock         {}", item.clock_mhz_.has_value()
                                                                       ? format("{} MHz", *item.clock_mhz_)
                                                                       : string("—")));
                    lines.push_back(format("Memory clock  {}", item.memory_clock_mhz_.has_value()
                                                                       ? format("{} MHz", *item.memory_clock_mhz_)
                                                                       : string("—")));
                    return lines;
                }
                lines.push_back("# Snapshot");
                lines.push_back(format("Host          {}", snap_.hostname_));
                lines.push_back(format("OS            {}", snap_.os_));
                lines.push_back(format("Kernel        {}", snap_.kernel_));
                lines.push_back(format("CPU           {}", snap_.cpu_name_));
                lines.push_back(format("Processes     {}", static_cast<uint64_t>(snap_.processes_.size())));
                lines.push_back(format("Collection    {:.1f} ms", snap_.collection_ms_));
                return lines;
            }

            void activate_popup() {
                if (popup_selected_ >= popup_entries_.size()) {
                    return;
                }
                const string action = popup_entries_[popup_selected_].action_;
                if (action == "resume") {
                    popup_ = popup_kind::none;
                } else if (starts_with(action.view(), "setting_pick:")) {
                    const auto row_end = action.find(':', 13);
                    const auto row_value =
                            row_end == string::npos ? none : parse_u64(action.view().substr(13, row_end - 13));
                    if (row_value.has_value() && row_end != string::npos) {
                        apply_setting_choice(static_cast<size_t>(*row_value), action.substr(row_end + 1));
                        settings_index_ = static_cast<size_t>(*row_value);
                        popup_parent_ = popup_kind::none;
                        free_scroll_ = false;
                        popup_ = popup_kind::settings;
                        popup_scroll_ = 0;
                    }
                } else if (action == "vm_detail") {
                    popup_ = popup_kind::none;
                    open_vm_detail();
                } else if (action == "vm_process") {
                    popup_ = popup_kind::none;
                    jump_to_vm_process();
                } else if (action == "vm_start" || action == "vm_shutdown" || action == "vm_reboot" ||
                           action == "vm_destroy" || action == "vm_suspend" || action == "vm_resume") {
                    request_vm_action(string(action.view().substr(3)));
                } else if (action == "workload_side_panel") {
                    popup_ = popup_kind::none;
                    workload_side_panel_ = !workload_side_panel_;
                    status_ = workload_side_panel_ ? "Workload side panel on · D close" : "Workload side panel off";
                } else if (action == "service_open_process") {
                    popup_ = popup_kind::none;
                    jump_to_service_process();
                } else if (action == "tool_detail") {
                    popup_ = popup_kind::none;
                    open_context_detail();
                } else if (action == "tool_logs") {
                    popup_ = popup_kind::none;
                    open_logs();
                } else if (action == "tool_env") {
                    popup_ = popup_kind::none;
                    open_process_environment();
                } else if (action == "tool_files") {
                    popup_ = popup_kind::none;
                    open_process_files();
                } else if (action == "tool_signal") {
                    popup_ = popup_kind::none;
                    open_signal_menu();
                } else if (action == "tool_nice") {
                    popup_ = popup_kind::none;
                    open_nice_prompt();
                } else if (action == "tool_affinity") {
                    popup_ = popup_kind::none;
                    open_affinity_prompt();
                } else if (action == "tool_follow") {
                    popup_ = popup_kind::none;
                    follow_process_ = !follow_process_;
                    status_ = follow_process_ ? "Following selected process · F stop" : "Follow disabled";
                } else if (action == "tool_pause") {
                    popup_ = popup_kind::none;
                    process_paused_ = !process_paused_;
                    status_ = process_paused_ ? "Process list paused · P resume" : "Process list resumed";
                } else if (action == "tool_copy_selection") {
                    popup_ = popup_kind::none;
                    if (tui::application::has_selection()) {
                        status_ = tui::application::copy_selection() ? "Selection copied"
                                                                     : "Copy failed: no clipboard backend";
                    } else {
                        status_ = "No selection; drag over the screen to select text.";
                    }
                } else if (action == "container_start" || action == "container_stop" || action == "container_restart" ||
                           action == "container_remove") {
                    request_workload_action(string(action.view().substr(10)));
                } else if (action == "service_start" || action == "service_stop" || action == "service_restart") {
                    request_workload_action(string(action.view().substr(8)));
                } else if (action == "detail") {
                    popup_ = popup_kind::none;
                    open_detail();
                } else if (action == "follow") {
                    popup_ = popup_kind::none;
                    follow_process_ = true;
                    status_ = "Following selected process · F stop";
                } else if (action == "pause") {
                    popup_ = popup_kind::none;
                    process_paused_ = !process_paused_;
                    status_ = process_paused_ ? "Process list paused · P resume" : "Process list resumed";
                } else if (action == "terminate") {
                    popup_ = popup_kind::none;
                    request_signal(SIGTERM, "Terminate");
                } else if (action == "kill_process") {
                    popup_ = popup_kind::none;
#if NEFLUX_PLATFORM_WINDOWS
                    request_signal(SIGTERM, "Kill");
#else
                    request_signal(SIGKILL, "Kill");
#endif
                } else if (action == "signal_menu") {
                    open_signal_menu();
                } else if (action == "net_interface_detail") {
                    popup_ = popup_kind::none;
                    network_section_ = 1;
                    open_network_detail();
                } else if (action == "net_connection_detail") {
                    popup_ = popup_kind::none;
                    network_section_ = 0;
                    open_connection_detail();
                } else if (action == "net_filter_port" || action == "net_filter_pid") {
                    const auto rows = filtered_connections();
                    if (!rows.empty() && selected_connection_ < rows.size()) {
                        const auto& item = snap_.connections_.connections_[rows[selected_connection_]];
                        if (action == "net_filter_pid") {
                            filter_ = item.pids_.empty() ? string() : format("pid:{}", item.pids_[0]);
                        } else {
                            const auto colon = item.local_.rfind(':');
                            filter_ =
                                    colon == string::npos ? string() : format("port:{}", item.local_.substr(colon + 1));
                        }
                    }
                    popup_ = popup_kind::none;
                } else if (action == "net_owner") {
                    popup_ = popup_kind::none;
                    const auto rows = filtered_connections();
                    if (!rows.empty() && selected_connection_ < rows.size()) {
                        const auto& item = snap_.connections_.connections_[rows[selected_connection_]];
                        if (!item.pids_.empty()) {
                            for (const auto& process: snap_.processes_) {
                                if (process.pid_ == item.pids_[0]) {
                                    select_process(process);
                                    open_detail();
                                    break;
                                }
                            }
                        }
                    }
                } else if (action == "net_toggle_loopback") {
                    popup_ = popup_kind::none;
                    cfg_.show_loopback_ = !cfg_.show_loopback_;
                    status_ = cfg_.show_loopback_ ? "Loopback interfaces shown" : "Loopback interfaces hidden";
                } else if (action == "net_sort_rx" || action == "net_sort_tx" || action == "net_sort_name") {
                    network_section_ = 0;
                    network_sort_ = action == "net_sort_tx" ? 1 : (action == "net_sort_name" ? 2 : 0);
                    popup_ = popup_kind::none;
                } else if (action == "storage_detail") {
                    popup_ = popup_kind::none;
                    open_storage_detail();
                } else if (action == "storage_prev" || action == "storage_next") {
                    const auto count =
                            static_cast<int64_t>(storage_section_ == 0 ? snap_.disks_.size() : snap_.disk_io_.size());
                    if (count > 0) {
                        const auto current = static_cast<int64_t>(storage_section_ == 0 ? selected_ : selected_io_);
                        const auto next = current + (action == "storage_next" ? 1 : -1);
                        const auto target =
                                static_cast<size_t>(neforce::clamp(next, static_cast<int64_t>(0), count - 1));
                        if (storage_section_ == 0) {
                            selected_ = target;
                        } else {
                            storage_device_auto_ = false;
                            selected_io_ = target;
                        }
                    }
                    popup_ = popup_kind::none;
                } else if (action == "gpu_detail" || action == "gpu_processes") {
                    popup_ = popup_kind::none;
                    gpu_section_ = 2;
                } else if (action == "gpu_prev" || action == "gpu_next") {
                    const auto count = static_cast<int64_t>(snap_.gpus_.size());
                    if (count > 0) {
                        const auto next = static_cast<int64_t>(selected_gpu_) + (action == "gpu_next" ? 1 : -1);
                        selected_gpu_ = static_cast<size_t>(neforce::clamp(next, static_cast<int64_t>(0), count - 1));
                    }
                    popup_ = popup_kind::none;
                } else if (action == "help") {
                    popup_ = popup_kind::help;
                    popup_scroll_ = 0;
                } else if (action == "settings") {
                    popup_parent_ = popup_kind::main_menu;
                    free_scroll_ = false;
                    popup_ = popup_kind::settings;
                    settings_index_ = 0;
                } else if (action == "sort") {
                    open_sort_menu();
                } else if (action == "reverse") {
                    reverse_ = !reverse_;
                    sync_sort_config();
                    popup_ = popup_kind::none;
                } else if (action == "time") {
                    if (popup_ == popup_kind::time_menu) {
                        set_time_window(popup_selected_);
                    } else {
                        open_time_menu();
                    }
                } else if (action == "theme") {
                    open_theme_menu(popup_kind::main_menu);
                } else if (starts_with(action.view(), "theme:")) {
                    apply_theme(action.substr(6));
                } else if (starts_with(action.view(), "theme_file:")) {
                    apply_theme_file(action.substr(11));
                } else if (action == "save") {
                    save_config();
                    popup_ = popup_kind::none;
                } else if (action == "quit") {
                    quit_ = true;
                    tui::application::request_quit(0);
                } else if (action == "tree") {
                    tree_ = !tree_;
                    cfg_.tree_ = tree_;
                    auto_collapse_done_ = false;
                    if (tree_) {
                        apply_tree_auto_collapse();
                    }
                    popup_ = popup_kind::none;
                } else if (starts_with(action.view(), "view")) {
                    const auto index_value = parse_u64(action.substr(4).view());
                    const int index = index_value.has_value() ? static_cast<int>(*index_value) : 0;
                    if (index >= 1 && index <= 9) {
                        switch_view(views[index - 1].value);
                    } else if (index == 10) {
                        switch_view(view::explorer);
                    } else if (index == 11) {
                        switch_view(view::history);
                    } else if (index == 12) {
                        switch_view(view::events);
                    }
                    popup_ = popup_kind::none;
                } else if (starts_with(action.view(), "ros:")) {
                    run_ros_action(action);
                } else if (starts_with(action.view(), "col:")) {
                    const string key = action.substr(4);
                    bool found = false;
                    for (size_t index = 0; index < cfg_.columns_.size(); ++index) {
                        if (cfg_.columns_[index] == key) {
                            cfg_.columns_.erase(cfg_.columns_.begin() + static_cast<ptrdiff_t>(index));
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        if (cfg_.columns_.size() >= 90) {
                            status_ = "At most 90 process columns are supported.";
                        } else {
                            cfg_.columns_.push_back(key);
                        }
                    }
                    open_columns_menu();
                } else if (action == "signal") {
                    const auto entries = signal_entries();
                    if (popup_selected_ < entries.size()) {
                        const int value = entries[popup_selected_].first;
                        popup_ = popup_kind::none;
                        request_signal(value, entries[popup_selected_].second);
                    }
                } else if (starts_with(action.view(), "sort:")) {
                    const auto index = parse_u64(action.view().substr(5));
                    if (index.has_value() && *index < sizeof(sorts) / sizeof(sorts[0])) {
                        sort_index_ = static_cast<size_t>(*index);
                        reverse_ = !sorts[sort_index_].descending;
                        sync_sort_config();
                    }
                    popup_ = popup_kind::none;
                } else if (starts_with(action.view(), "sort")) {
                    sort_index_ = popup_selected_;
                    const bool default_desc =
                            sorts[neforce::min(sort_index_, sizeof(sorts) / sizeof(sorts[0]) - 1)].descending;
                    reverse_ = !default_desc;
                    sync_sort_config();
                    popup_ = popup_kind::none;
                }
            }

            void set_time_window(size_t index) {
                static constexpr uint64_t values[] = {15, 30, 60, 180, 300, 900, 1800, 3600, 0};
                if (index >= 9) {
                    return;
                }
                cfg_.time_window_seconds_ = values[index];
                const uint64_t needed =
                        cfg_.time_window_seconds_ == 0
                                ? cfg_.history_
                                : (cfg_.time_window_seconds_ * 1000 + cfg_.interval_ms_ - 1) / cfg_.interval_ms_;
                cfg_.history_ = neforce::max(cfg_.history_,
                                             static_cast<size_t>(neforce::min(needed, static_cast<uint64_t>(3600))));
                popup_ = popup_kind::none;
            }

            void switch_view(view target) {
                if (sampler_ != nullptr && !replay_) {
                    sampler_->activate(target);
                }
                current_ = target;
                selected_ = 0;
                table_offset_ = 0;
                free_scroll_ = false;
                storage_device_auto_ = true;
                detail_open_ = false;
                selected_process_.reset();
                selected_service_.reset();
                status_.clear();
            }

            void save_config() {
                try {
                    cfg_.save(config_path_);
                    status_ = format("Saved {}", config_path_);
                } catch (const neforce::exception& failure) {
                    status_ = failure.what();
                }
            }

            void adjust_setting(int direction) {
                if (settings_index_ < settings_rows().size()) {
                    const string label = settings_rows()[settings_index_].first;
                    if (label == "Tree auto-collapse") {
                        static constexpr size_t values[] = {0, 2, 4, 8, 16, 32, 64};
                        size_t current = 0;
                        for (size_t index = 0; index < 7; ++index) {
                            if (values[index] == cfg_.tree_auto_collapse_) {
                                current = index;
                                break;
                            }
                        }
                        current = static_cast<size_t>(neforce::clamp(static_cast<int64_t>(current) + direction,
                                                                     static_cast<int64_t>(0), static_cast<int64_t>(6)));
                        cfg_.tree_auto_collapse_ = values[current];
                        auto_collapse_done_ = false;
                        apply_tree_auto_collapse();
                        return;
                    }
                    if (label == "Tree aggregate") {
                        cfg_.tree_aggregate_ = !cfg_.tree_aggregate_;
                        return;
                    }
                    if (label == "Overview preset" && !cfg_.overview_presets_.empty()) {
                        const int64_t count = static_cast<int64_t>(cfg_.overview_presets_.size());
                        cfg_.overview_preset_ = static_cast<size_t>(
                                neforce::clamp(static_cast<int64_t>(cfg_.overview_preset_) + direction,
                                               static_cast<int64_t>(0), count - 1));
                        return;
                    }
                }
                if (settings_index_ == 1) {
                    const int64_t next =
                            static_cast<int64_t>(cfg_.interval_ms_) + static_cast<int64_t>(direction * 250);
                    cfg_.interval_ms_ = static_cast<uint64_t>(
                            neforce::clamp(next, static_cast<int64_t>(250), static_cast<int64_t>(10000)));
                } else if (settings_index_ == 2) {
                    const int64_t next = static_cast<int64_t>(cfg_.history_) + static_cast<int64_t>(direction * 30);
                    cfg_.history_ = static_cast<size_t>(
                            neforce::clamp(next, static_cast<int64_t>(30), static_cast<int64_t>(3600)));
                } else if (settings_index_ == 3) {
                    static constexpr uint64_t values[] = {15, 30, 60, 180, 300, 900, 1800, 3600, 0};
                    size_t current = 0;
                    for (size_t i = 0; i < 9; ++i) {
                        if (values[i] == cfg_.time_window_seconds_) {
                            current = i;
                            break;
                        }
                    }
                    const int64_t next = static_cast<int64_t>(current) + direction;
                    set_time_window(static_cast<size_t>(
                            neforce::clamp(next, static_cast<int64_t>(0), static_cast<int64_t>(8))));
                } else if (settings_index_ == 4) {
                    cfg_.cycle_theme(direction > 0);
                    colors_ = make_palette(cfg_.theme_, cfg_.custom_colors_);
                    if (cfg_.no_color_) {
                        colors_ = monochrome_palette(colors_);
                    }
                } else if (settings_index_ == 5) {
                    const vector<string> layouts = {"balanced", "compute", "storage", "minimal"};
                    size_t current = 0;
                    for (size_t i = 0; i < layouts.size(); ++i) {
                        if (layouts[i] == cfg_.layout_) {
                            current = i;
                            break;
                        }
                    }
                    const int64_t next = static_cast<int64_t>(current) + direction;
                    cfg_.layout_ = layouts[static_cast<size_t>(
                            neforce::clamp(next, static_cast<int64_t>(0), static_cast<int64_t>(3)))];
                } else if (settings_index_ == 6) {
                    cfg_.show_threads_ = !cfg_.show_threads_;
                } else if (settings_index_ == 7) {
                    cfg_.show_kernel_threads_ = !cfg_.show_kernel_threads_;
                } else if (settings_index_ == 8) {
                    cfg_.show_loopback_ = !cfg_.show_loopback_;
                } else if (settings_index_ == 9) {
                    cfg_.collect_gpu_ = !cfg_.collect_gpu_;
                } else if (settings_index_ == 10) {
                    cfg_.ascii_ = !cfg_.ascii_;
                } else if (settings_index_ == 11) {
                    cfg_.no_color_ = !cfg_.no_color_;
                    colors_ = make_palette(cfg_.theme_, cfg_.custom_colors_);
                    if (cfg_.no_color_) {
                        colors_ = monochrome_palette(colors_);
                    }
                } else if (settings_index_ == 12) {
                    const int64_t next = static_cast<int64_t>(cfg_.alert_hold_seconds_) + direction;
                    cfg_.alert_hold_seconds_ = static_cast<uint64_t>(
                            neforce::clamp(next, static_cast<int64_t>(0), static_cast<int64_t>(3600)));
                } else if (settings_index_ == 13) {
                    const int64_t next = static_cast<int64_t>(cfg_.log_lines_) + static_cast<int64_t>(direction * 100);
                    cfg_.log_lines_ = static_cast<size_t>(
                            neforce::clamp(next, static_cast<int64_t>(1), static_cast<int64_t>(100000)));
                } else if (settings_index_ == 14) {
                    cfg_.cpu_warning_ = static_cast<float>(
                            neforce::clamp(static_cast<double>(cfg_.cpu_warning_) + direction * 5.0, 0.0, 100.0));
                } else if (settings_index_ == 15) {
                    cfg_.memory_warning_ = neforce::clamp(cfg_.memory_warning_ + direction * 5.0, 0.0, 100.0);
                } else if (settings_index_ == 16) {
                    const int64_t next =
                            static_cast<int64_t>(cfg_.gpu_temperature_warning_) + static_cast<int64_t>(direction * 5);
                    cfg_.gpu_temperature_warning_ = static_cast<uint32_t>(
                            neforce::clamp(next, static_cast<int64_t>(0), static_cast<int64_t>(150)));
                } else if (settings_index_ >= 17) {
                    vector<string> networks;
                    for (const auto& item: snap_.networks_) {
                        networks.push_back(item.name_);
                    }
                    vector<string> sensors;
                    for (const auto& item: snap_.sensors_) {
                        sensors.push_back(item.name_);
                    }
                    cfg_.display_.adjust(settings_index_ - 17, direction > 0, networks, sensors);
                }
            }

            bool handle_popup_key(const tui::key_event& event) {
                const auto key = event.key;
                if (popup_ == popup_kind::confirm) {
                    if (key == tui::key_event::type::enter ||
                        (key == tui::key_event::type::printable && (event.cp == U'y' || event.cp == U'Y'))) {
                        if (!pending_workload_action_.empty()) {
                            execute_pending_workload();
                        } else {
                            execute_pending_signal();
                        }
                        return true;
                    }
                    if (key == tui::key_event::type::escape ||
                        (key == tui::key_event::type::printable && (event.cp == U'n' || event.cp == U'N'))) {
                        pending_workload_action_.clear();
                        pending_signal_ = 0;
                        popup_ = popup_kind::none;
                        return true;
                    }
                    return true;
                }
                if (popup_ == popup_kind::prompt) {
                    if (key == tui::key_event::type::escape) {
                        popup_ = popup_kind::none;
                        return true;
                    }
                    if (key == tui::key_event::type::enter) {
                        if (prompt_action_ == prompt_action::nice) {
                            apply_nice();
                        } else if (prompt_action_ == prompt_action::affinity) {
                            apply_affinity();
                        }
                        return true;
                    }
                    if (key == tui::key_event::type::backspace) {
                        if (prompt_cursor_ > 0 && !prompt_text_.empty()) {
                            prompt_text_.erase(prompt_cursor_ - 1, 1);
                            --prompt_cursor_;
                        }
                        return true;
                    }
                    if (key == tui::key_event::type::left) {
                        if (prompt_cursor_ > 0) {
                            --prompt_cursor_;
                        }
                        return true;
                    }
                    if (key == tui::key_event::type::right) {
                        if (prompt_cursor_ < prompt_text_.size()) {
                            ++prompt_cursor_;
                        }
                        return true;
                    }
                    if (key == tui::key_event::type::printable) {
                        string encoded;
                        const uint32_t value = event.cp.value();
                        if (value < 0x80) {
                            encoded.push_back(static_cast<char>(value));
                        } else if (value < 0x800) {
                            encoded.push_back(static_cast<char>(0xC0 | (value >> 6)));
                            encoded.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                        } else {
                            encoded.push_back(static_cast<char>(0xE0 | (value >> 12)));
                            encoded.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                            encoded.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                        }
                        prompt_text_ =
                                prompt_text_.substr(0, prompt_cursor_) + encoded + prompt_text_.substr(prompt_cursor_);
                        prompt_cursor_ += encoded.size();
                        return true;
                    }
                    return true;
                }
                if (popup_ == popup_kind::settings) {
                    if (key == tui::key_event::type::escape) {
                        if (popup_parent_ == popup_kind::main_menu) {
                            open_main_menu();
                        } else {
                            popup_ = popup_kind::none;
                            popup_parent_ = popup_kind::none;
                        }
                        return true;
                    }
                    if (key == tui::key_event::type::up) {
                        free_scroll_ = false;
                        if (settings_index_ > 0) {
                            --settings_index_;
                        }
                        return true;
                    }
                    if (key == tui::key_event::type::down) {
                        free_scroll_ = false;
                        settings_index_ = neforce::min(settings_index_ + 1, settings_rows().size() - 1);
                        return true;
                    }
                    if (key == tui::key_event::type::enter) {
                        open_setting_choice();
                        return true;
                    }
                    if (key == tui::key_event::type::left) {
                        adjust_setting(-1);
                        return true;
                    }
                    if (key == tui::key_event::type::right) {
                        adjust_setting(1);
                        return true;
                    }
                    if (key == tui::key_event::type::printable && event.cp == U'w') {
                        save_config();
                        return true;
                    }
                    return false;
                }
                if (key == tui::key_event::type::escape) {
                    if (popup_parent_ == popup_kind::main_menu) {
                        open_main_menu();
                    } else if (popup_parent_ == popup_kind::settings) {
                        free_scroll_ = false;
                        popup_ = popup_kind::settings;
                        popup_parent_ = popup_kind::none;
                    } else {
                        popup_ = popup_kind::none;
                        popup_parent_ = popup_kind::none;
                    }
                    return true;
                }
                if (key == tui::key_event::type::up) {
                    if (popup_selected_ > 0) {
                        --popup_selected_;
                    } else if (popup_scroll_ > 0) {
                        --popup_scroll_;
                    }
                    return true;
                }
                if (key == tui::key_event::type::down || key == tui::key_event::type::tab) {
                    if (popup_ == popup_kind::help || popup_ == popup_kind::detail || popup_ == popup_kind::info) {
                        ++popup_scroll_;
                    } else if (popup_selected_ + 1 < popup_entries_.size()) {
                        ++popup_selected_;
                    }
                    return true;
                }
                if (key == tui::key_event::type::page_up) {
                    popup_scroll_ = popup_scroll_ > 10 ? popup_scroll_ - 10 : 0;
                    return true;
                }
                if (key == tui::key_event::type::page_down) {
                    popup_scroll_ += 10;
                    return true;
                }
                if (key == tui::key_event::type::enter) {
                    if (popup_ == popup_kind::help || popup_ == popup_kind::detail || popup_ == popup_kind::info) {
                        popup_ = popup_kind::none;
                        return true;
                    }
                    activate_popup();
                    return true;
                }
                if (key == tui::key_event::type::printable) {
                    if (popup_ == popup_kind::sort) {
                        if (event.cp == U'r') {
                            reverse_ = !reverse_;
                            sync_sort_config();
                            popup_ = popup_kind::none;
                            return true;
                        }
                    }
                    if (popup_ == popup_kind::help || popup_ == popup_kind::detail) {
                        if (event.cp == U'q') {
                            popup_ = popup_kind::none;
                            return true;
                        }
                    }
                }
                return false;
            }

            bool handle_filter_key(const tui::key_event& event) {
                const auto key = event.key;
                if (key == tui::key_event::type::escape) {
                    filter_ = filter_before_;
                    editing_filter_ = false;
                    return true;
                }
                if (key == tui::key_event::type::enter) {
                    editing_filter_ = false;
                    return true;
                }
                if (key == tui::key_event::type::backspace) {
                    if (filter_cursor_ > 0 && !filter_.empty()) {
                        filter_.erase(filter_cursor_ - 1, 1);
                        --filter_cursor_;
                    }
                    return true;
                }
                if (key == tui::key_event::type::delete_) {
                    if (filter_cursor_ < filter_.size()) {
                        filter_.erase(filter_cursor_, 1);
                    }
                    return true;
                }
                if (key == tui::key_event::type::left) {
                    if (filter_cursor_ > 0) {
                        --filter_cursor_;
                    }
                    return true;
                }
                if (key == tui::key_event::type::right) {
                    if (filter_cursor_ < filter_.size()) {
                        ++filter_cursor_;
                    }
                    return true;
                }
                if (key == tui::key_event::type::home) {
                    filter_cursor_ = 0;
                    return true;
                }
                if (key == tui::key_event::type::end) {
                    filter_cursor_ = filter_.size();
                    return true;
                }
                if (key == tui::key_event::type::printable) {
                    string encoded;
                    const uint32_t value = event.cp.value();
                    if (value < 0x80) {
                        encoded.push_back(static_cast<char>(value));
                    } else if (value < 0x800) {
                        encoded.push_back(static_cast<char>(0xC0 | (value >> 6)));
                        encoded.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                    } else {
                        encoded.push_back(static_cast<char>(0xE0 | (value >> 12)));
                        encoded.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                        encoded.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                    }
                    filter_ = filter_.substr(0, filter_cursor_) + encoded + filter_.substr(filter_cursor_);
                    filter_cursor_ += encoded.size();
                    return true;
                }
                return false;
            }

            bool handle_global_key(const tui::key_event& event) {
                if (event.action == tui::key_action::release) {
                    return false;
                }
                const auto key = event.key;
                if (key == tui::key_event::type::up || key == tui::key_event::type::down ||
                    key == tui::key_event::type::page_up || key == tui::key_event::type::page_down ||
                    key == tui::key_event::type::home || key == tui::key_event::type::end) {
                    free_scroll_ = false;
                }
                if (key == tui::key_event::type::printable &&
                    (event.mods & tui::key_modifier::ctrl) != tui::key_modifier::none) {
                    if (event.cp == U'c') {
                        const bool shift = (event.mods & tui::key_modifier::shift) != tui::key_modifier::none;
                        if (tui::application::has_selection()) {
                            status_ = tui::application::copy_selection() ? "Selection copied"
                                                                         : "Copy failed: no clipboard backend";
                            return true;
                        }
                        if (shift) {
                            status_ = "No selection; drag over the screen to select text.";
                            return true;
                        }
                        quit_ = true;
                        tui::application::request_quit(0);
                        return true;
                    }
                }
                if (key == tui::key_event::type::escape) {
                    const bool double_press = monotonic_ms() - last_escape_ms_ < 500;
                    last_escape_ms_ = monotonic_ms();
                    if (double_press) {
                        quit_ = true;
                        tui::application::request_quit(0);
                        return true;
                    }
                    if (!filter_.empty()) {
                        filter_.clear();
                        return true;
                    }
                    if (detail_open_) {
                        detail_open_ = false;
                        return true;
                    }
                    open_main_menu();
                    return true;
                }
                if (key == tui::key_event::type::F1) {
                    popup_ = popup_kind::help;
                    popup_scroll_ = 0;
                    return true;
                }
                if (key == tui::key_event::type::F2) {
                    popup_parent_ = popup_kind::none;
                    free_scroll_ = false;
                    popup_ = popup_kind::settings;
                    settings_index_ = 0;
                    return true;
                }
                if (key == tui::key_event::type::F3) {
                    begin_filter();
                    return true;
                }
                if (key == tui::key_event::type::F4) {
                    open_sort_menu();
                    return true;
                }
                if (key == tui::key_event::type::F5) {
                    open_view_menu();
                    return true;
                }
                if (key == tui::key_event::type::F6) {
                    open_tools_menu();
                    return true;
                }
                if (key == tui::key_event::type::enter) {
                    if (current_ == view::network) {
                        if (network_section_ == 1) {
                            open_network_detail();
                        } else {
                            open_connection_detail();
                        }
                    } else if (current_ == view::containers) {
                        open_workload_detail(true);
                    } else if (current_ == view::services) {
                        open_workload_detail(false);
                    } else if (current_ == view::vms) {
                        open_vm_detail();
                    } else if (current_ == view::explorer) {
                        open_extra_detail();
                    } else if (current_ == view::storage) {
                        open_storage_detail();
                    } else if (current_ == view::gpu) {
                        gpu_section_ = 2;
                    } else {
                        open_detail();
                    }
                    return true;
                }
                if (key == tui::key_event::type::tab) {
                    const size_t index = view_index(current_);
                    switch_view(views[(index + 1) % view_count].value);
                    return true;
                }
                if (key == tui::key_event::type::tab_reverse) {
                    const size_t index = view_index(current_);
                    switch_view(views[(index + view_count - 1) % view_count].value);
                    return true;
                }
                if ((key == tui::key_event::type::left || key == tui::key_event::type::right) &&
                    (event.mods & tui::key_modifier::shift) != tui::key_modifier::none && current_ == view::processes) {
                    const auto all = cfg_.columns_.empty() ? 8U : static_cast<uint32_t>(cfg_.columns_.size());
                    const int delta = key == tui::key_event::type::right ? 1 : -1;
                    const int64_t next = static_cast<int64_t>(column_offset_) + delta;
                    column_offset_ = static_cast<size_t>(
                            neforce::clamp(next, static_cast<int64_t>(0), static_cast<int64_t>(all - 1)));
                    status_ = format("Columns start at {} / {}", static_cast<uint64_t>(column_offset_ + 1),
                                     static_cast<uint64_t>(all));
                    return true;
                }
                if ((key == tui::key_event::type::left || key == tui::key_event::type::right) &&
                    (event.mods & tui::key_modifier::ctrl) != tui::key_modifier::none && tree_ &&
                    current_ == view::processes) {
                    const auto rows = process_rows();
                    if (selected_ < rows.size()) {
                        const auto pid = snap_.processes_[rows[selected_].index_].pid_;
                        if (key == tui::key_event::type::left) {
                            collapsed_.insert(pid);
                        } else {
                            collapsed_.erase(pid);
                        }
                        status_ = key == tui::key_event::type::left ? "Subtree collapsed" : "Subtree expanded";
                    }
                    return true;
                }
                if (key == tui::key_event::type::left || key == tui::key_event::type::right) {
                    if (key == tui::key_event::type::right) {
                        switch_view(views[(view_index(current_) + 1) % view_count].value);
                    } else {
                        switch_view(views[(view_index(current_) + view_count - 1) % view_count].value);
                    }
                    return true;
                }
                if (key == tui::key_event::type::up) {
                    if (current_ == view::history) {
                        if (selected_ > 0) {
                            --selected_;
                        }
                    } else if (current_ == view::events || current_ == view::explorer) {
                        if (selected_ > 0) {
                            --selected_;
                        }
                    } else if (current_ == view::storage) {
                        if (storage_section_ == 0) {
                            if (selected_ > 0) {
                                --selected_;
                            }
                        } else {
                            storage_device_auto_ = false;
                            if (selected_io_ > 0) {
                                --selected_io_;
                            }
                        }
                    } else if (current_ == view::network) {
                        if (network_section_ == 1) {
                            if (selected_ > 0) {
                                --selected_;
                            }
                        } else if (selected_connection_ > 0) {
                            --selected_connection_;
                        }
                    } else if (current_ == view::containers || current_ == view::services || current_ == view::vms) {
                        if (selected_ > 0) {
                            --selected_;
                        }
                        if (current_ == view::services) {
                            remember_service_selection();
                        }
                    } else {
                        const auto rows = process_rows();
                        move_selection(-1, rows.size());
                        remember_selection(rows);
                    }
                    return true;
                }
                if (key == tui::key_event::type::down) {
                    if (current_ == view::history) {
                        const size_t count = history_keys().size();
                        if (selected_ + 1 < count) {
                            ++selected_;
                        }
                    } else if (current_ == view::events) {
                        size_t count = 0;
                        for (const auto& item: alert_events_) {
                            if (filter_.empty() || event_matches(item, filter_)) {
                                ++count;
                            }
                        }
                        if (selected_ + 1 < count) {
                            ++selected_;
                        }
                    } else if (current_ == view::explorer) {
                        const size_t count = extra_rows().size();
                        if (selected_ + 1 < count) {
                            ++selected_;
                        }
                    } else if (current_ == view::storage) {
                        if (storage_section_ == 0) {
                            const size_t count = snap_.disks_.size();
                            if (selected_ + 1 < count) {
                                ++selected_;
                            }
                        } else {
                            storage_device_auto_ = false;
                            if (selected_io_ + 1 < snap_.disk_io_.size()) {
                                ++selected_io_;
                            }
                        }
                    } else if (current_ == view::network) {
                        if (network_section_ == 1) {
                            const size_t count = filtered_networks().size();
                            if (selected_ + 1 < count) {
                                ++selected_;
                            }
                        } else {
                            const size_t count = filtered_connections().size();
                            if (selected_connection_ + 1 < count) {
                                ++selected_connection_;
                            }
                        }
                    } else if (current_ == view::containers) {
                        const size_t count = inventory_.containers_.size();
                        if (selected_ + 1 < count) {
                            ++selected_;
                        }
                    } else if (current_ == view::services) {
                        const size_t count = inventory_.services_.size();
                        if (selected_ + 1 < count) {
                            ++selected_;
                        }
                        remember_service_selection();
                    } else if (current_ == view::vms) {
                        const size_t count = inventory_.virtual_machines_.size();
                        if (selected_ + 1 < count) {
                            ++selected_;
                        }
                    } else {
                        const auto rows = process_rows();
                        move_selection(1, rows.size());
                        remember_selection(rows);
                    }
                    return true;
                }
                if (key == tui::key_event::type::page_up) {
                    if (current_ == view::system) {
                        listener_offset_ = listener_offset_ > 10 ? listener_offset_ - 10 : 0;
                        return true;
                    }
                    const auto rows = process_rows();
                    move_selection(-20, rows.size());
                    remember_selection(rows);
                    return true;
                }
                if (key == tui::key_event::type::page_down) {
                    if (current_ == view::system) {
                        const size_t visible = static_cast<size_t>(neforce::max(0, listeners_rect_.h_ - 3));
                        const size_t maximum = inventory_.listeners_.size() > visible
                                                       ? inventory_.listeners_.size() - visible
                                                       : 0;
                        listener_offset_ = neforce::min(maximum, listener_offset_ + 10);
                        return true;
                    }
                    const auto rows = process_rows();
                    move_selection(20, rows.size());
                    remember_selection(rows);
                    return true;
                }
                if (key == tui::key_event::type::home) {
                    selected_ = 0;
                    remember_selection(process_rows());
                    return true;
                }
                if (key == tui::key_event::type::end) {
                    const auto rows = process_rows();
                    selected_ = rows.empty() ? 0 : rows.size() - 1;
                    remember_selection(rows);
                    return true;
                }
                if (key == tui::key_event::type::F7) {
                    open_logs();
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'e') {
                    open_process_environment();
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'o') {
                    open_process_files();
                    return true;
                }
                if (key == tui::key_event::type::printable && (event.cp == U'k' || event.cp == U'K')) {
#if NEFLUX_PLATFORM_WINDOWS
                    request_signal(SIGTERM, "Kill");
#else
                    request_signal(SIGKILL, "Kill");
#endif
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U't') {
                    request_signal(SIGTERM, "Terminate");
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U's') {
                    open_signal_menu();
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'S' && current_ == view::network) {
                    if (network_section_ == 1) {
                        interface_sort_ = (interface_sort_ + 1) % 5;
                        static const char* names[] = {"RX/s", "TX/s", "name", "state", "speed"};
                        status_ = format("Interface sort: {}", names[interface_sort_]);
                    } else {
                        network_sort_ = (network_sort_ + 1) % 5;
                        static const char* names[] = {"RX/s", "TX/s", "state", "local port", "PID"};
                        status_ = format("Connection sort: {}", names[network_sort_]);
                    }
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'N' && current_ == view::storage) {
                    storage_section_ = storage_section_ == 0 ? 1 : 0;
                    status_ = storage_section_ == 0 ? "Storage: filesystems" : "Storage: block devices";
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'A' && current_ == view::storage) {
                    storage_device_auto_ = true;
                    selected_io_ = 0;
                    status_ = "Storage chart: all physical devices";
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'N' && current_ == view::network) {
                    network_section_ = network_section_ == 0 ? 1 : 0;
                    selected_ = 0;
                    selected_connection_ = 0;
                    status_ = network_section_ == 0 ? "Network: connections" : "Network: interfaces";
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'S') {
                    constexpr size_t count = sizeof(sorts) / sizeof(sorts[0]);
                    sort_index_ = (sort_index_ + 1) % count;
                    reverse_ = !sorts[sort_index_].descending;
                    sync_sort_config();
                    selected_ = 0;
                    table_offset_ = 0;
                    status_ = format("Sort: {} · {}", sorts[sort_index_].name, reverse_ ? "descending" : "ascending");
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'r' && !replay_) {
                    reverse_ = !reverse_;
                    sync_sort_config();
                    status_ = reverse_ ? "Sort descending" : "Sort ascending";
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'g') {
                    gpu_only_ = !gpu_only_;
                    selected_ = 0;
                    status_ = gpu_only_ ? "Showing GPU processes only" : "Showing all processes";
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'u') {
                    own_only_ = !own_only_;
                    selected_ = 0;
                    status_ = own_only_ ? "Showing own processes only" : "Showing all users";
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'i') {
                    cfg_.show_loopback_ = !cfg_.show_loopback_;
                    status_ = cfg_.show_loopback_ ? "Loopback shown" : "Loopback hidden";
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'n') {
                    if (!state_changes_allowed()) {
                        status_ = replay_ ? "Replay: state changes are disabled."
                                          : "Read-only: state changes are disabled.";
                    } else if (action_targets().empty()) {
                        status_ = "Select a process first.";
                    } else {
                        popup_ = popup_kind::prompt;
                        popup_title_ = "Set nice (-20..19)";
                        prompt_action_ = prompt_action::nice;
                        prompt_text_ =
                                selected_process_value().has_value() && selected_process_value()->nice_.has_value()
                                        ? format("{}", *selected_process_value()->nice_)
                                        : string("0");
                        prompt_cursor_ = prompt_text_.size();
                    }
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'a') {
                    if (!state_changes_allowed()) {
                        status_ = replay_ ? "Replay: state changes are disabled."
                                          : "Read-only: state changes are disabled.";
                    } else if (action_targets().empty()) {
                        status_ = "Select a process first.";
                    } else {
                        popup_ = popup_kind::prompt;
                        popup_title_ = "CPU affinity (e.g. 0-3,6)";
                        prompt_action_ = prompt_action::affinity;
                        prompt_text_.clear();
                        prompt_cursor_ = 0;
                    }
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'l') {
                    open_logs();
                    return true;
                }
                if (key == tui::key_event::type::printable &&
                    (event.cp == U' ' || event.cp == U'v' || event.cp == U'V' || event.cp == U'U')) {
                    if (event.cp == U'U') {
                        marked_processes_.clear();
                        status_ = "Marks cleared";
                        return true;
                    }
                    if (event.cp == U'V') {
                        const auto value = selected_process_value();
                        if (value.has_value()) {
                            set<uint32_t> descendants;
                            vector<uint32_t> pending{value->pid_};
                            while (!pending.empty()) {
                                const uint32_t parent = pending.back();
                                pending.pop_back();
                                for (const auto& item: snap_.processes_) {
                                    if (item.parent_.has_value() && *item.parent_ == parent &&
                                        descendants.insert(item.pid_).second) {
                                        pending.push_back(item.pid_);
                                    }
                                }
                            }
                            for (const auto& item: snap_.processes_) {
                                if (item.pid_ == value->pid_ || descendants.count(item.pid_) != 0) {
                                    marked_processes_.insert({item.pid_, item.start_time_});
                                }
                            }
                            status_ = format("{} processes marked", static_cast<uint64_t>(marked_processes_.size()));
                        }
                        return true;
                    }
                    if (selected_process_.has_value()) {
                        if (!marked_processes_.insert(*selected_process_).second) {
                            marked_processes_.erase(*selected_process_);
                        }
                        status_ = format("{} processes marked", static_cast<uint64_t>(marked_processes_.size()));
                    }
                    return true;
                }
                if (key != tui::key_event::type::printable) {
                    return false;
                }
                const auto cp = event.cp;
                if (cp >= U'1' && cp <= U'9') {
                    switch_view(views[cp.value() - U'1'].value);
                    return true;
                }
                if (cp == U'0') {
                    switch_view(view::history);
                    return true;
                }
                if (cp == U'E') {
                    switch_view(view::events);
                    return true;
                }
                if (cp == U'X') {
                    switch_view(view::explorer);
                    return true;
                }
                if (cp == U'q') {
                    quit_ = true;
                    tui::application::request_quit(0);
                    return true;
                }
                if (cp == U'?') {
                    open_main_menu();
                    return true;
                }
                if (cp == U':') {
                    open_commands();
                    return true;
                }
                if (cp == U'f' || cp == U'/') {
                    begin_filter();
                    return true;
                }
                if (cp == U'L') {
                    cfg_.display_.cpu_lazy_sort_ = !cfg_.display_.cpu_lazy_sort_;
                    status_ = cfg_.display_.cpu_lazy_sort_ ? "CPU sort: lazy" : "CPU sort: direct";
                    return true;
                }
                if (cp == U'O' && current_ == view::overview) {
                    cycle_overview_preset();
                    return true;
                }
                if (cp == U'P') {
                    process_paused_ = !process_paused_;
                    status_ = process_paused_ ? "Process list paused · P resume" : "Process list resumed";
                    return true;
                }
                if (cp == U'F') {
                    follow_process_ = !follow_process_;
                    if (follow_process_) {
                        const auto rows = process_rows();
                        remember_selection(rows);
                        status_ = "Following selected process · F stop";
                    } else {
                        status_ = "Process follow disabled";
                    }
                    return true;
                }
                if (cp == U'c') {
                    open_theme_menu();
                    return true;
                }
                if (cp == U'w') {
                    save_config();
                    return true;
                }
                if (cp == U'W') {
                    open_time_menu();
                    return true;
                }
                if (cp == U'z' || cp == U'Z') {
                    const uint64_t current = cfg_.time_window_seconds_ == 0 ? cfg_.history_ * cfg_.interval_ms_ / 1000
                                                                            : cfg_.time_window_seconds_;
                    const uint64_t limit = neforce::max<uint64_t>(5, 3600ULL * cfg_.interval_ms_ / 1000);
                    const uint64_t target = cp == U'z' ? neforce::max<uint64_t>(5, current / 2)
                                                       : neforce::min<uint64_t>(limit, current * 2);
                    cfg_.time_window_seconds_ = target;
                    const uint64_t needed = (target * 1000 + cfg_.interval_ms_ - 1) / cfg_.interval_ms_;
                    cfg_.history_ =
                            neforce::max(cfg_.history_, static_cast<size_t>(neforce::min<uint64_t>(needed, 3600)));
                    status_ = format("Chart window: {}", time_label(target));
                    return true;
                }
                if (cp == U'T') {
                    tree_ = !tree_;
                    cfg_.tree_ = tree_;
                    auto_collapse_done_ = false;
                    if (tree_) {
                        apply_tree_auto_collapse();
                    }
                    return true;
                }
                if (cp == U'G' && current_ == view::gpu) {
                    gpu_section_ = 2;
                    return true;
                }
                if (cp == U'D' && current_ == view::services) {
                    const auto rows = workload_rows(false);
                    if (!rows.empty() && selected_ >= rows.size()) {
                        selected_ = 0;
                    }
                    workload_side_panel_ = !workload_side_panel_;
                    status_ = workload_side_panel_ ? "Workload side panel on · D close" : "Workload side panel off";
                    return true;
                }
                if (cp == U'D' && current_ == view::gpu) {
                    gpu_section_ = gpu_section_ == 2 ? 0 : 2;
                    return true;
                }
                if (cp == U'D' && (current_ == view::processes || current_ == view::overview)) {
                    if (!selected_process_.has_value()) {
                        const auto rows = process_rows();
                        remember_selection(rows);
                    }
                    process_side_panel_ = !process_side_panel_;
                    status_ = process_side_panel_ ? "Process side panel on · D close" : "Process side panel off";
                    return true;
                }
                if (cp == U'[' || cp == U']') {
                    if (!snap_.gpus_.empty()) {
                        if (cp == U']') {
                            selected_gpu_ = (selected_gpu_ + 1) % snap_.gpus_.size();
                        } else {
                            selected_gpu_ = (selected_gpu_ + snap_.gpus_.size() - 1) % snap_.gpus_.size();
                        }
                    }
                    return true;
                }
                if (cp == U'-' || cp == U'+') {
                    if (cp == U'+') {
                        cfg_.interval_ms_ = neforce::min(static_cast<uint64_t>(10000), cfg_.interval_ms_ + 250);
                    } else {
                        cfg_.interval_ms_ = neforce::max(static_cast<uint64_t>(250), cfg_.interval_ms_ - 250);
                    }
                    status_ = format("Interval {:.2f}s", static_cast<double>(cfg_.interval_ms_) / 1000.0);
                    return true;
                }
                if (cp == U'r' && replay_) {
                    sampler_->seek(1);
                    return true;
                }
                if (replay_ && (cp == U',' || cp == U'.' || cp == U'<' || cp == U'>')) {
                    const bool forward = cp == U'.' || cp == U'>';
                    sampler_->seek(forward ? 1 : -1);
                    status_ = format("Replay frame {} / {}", static_cast<uint64_t>(sampler_->replay_index() + 1),
                                     static_cast<uint64_t>(sampler_->replay_size()));
                    return true;
                }
                if (cp == U'p') {
                    if (current_ == view::services) {
                        jump_to_service_process();
                        return true;
                    }
                    if (current_ == view::network) {
                        const auto rows = filtered_connections();
                        if (!rows.empty()) {
                            const auto& item =
                                    snap_.connections_
                                            .connections_[rows[neforce::min(selected_connection_, rows.size() - 1)]];
                            if (!item.pids_.empty()) {
                                for (const auto& proc: snap_.processes_) {
                                    if (proc.pid_ == item.pids_[0]) {
                                        select_process(proc);
                                        open_detail();
                                        return true;
                                    }
                                }
                            }
                            status_ = "The connection owner is not in the current process sample.";
                        }
                    }
                    return true;
                }
                return false;
            }

            void begin_filter() {
                if (current_ != view::processes && current_ != view::network && current_ != view::containers &&
                    current_ != view::services) {
                    return;
                }
                filter_before_ = filter_;
                filter_cursor_ = filter_.size();
                editing_filter_ = true;
            }

            void open_process_menu() {
                if (!selected_process_.has_value()) {
                    return;
                }
                string name = format("PID {}", selected_process_->first);
                for (const auto& item: snap_.processes_) {
                    if (item.pid_ == selected_process_->first && item.start_time_ == selected_process_->second) {
                        name = format("{} · {}", item.pid_, item.name_);
                        break;
                    }
                }
                popup_parent_ = popup_kind::none;
                popup_ = popup_kind::main_menu;
                popup_title_ = name;
                popup_entries_ = {
                        {"Details", "Enter", "detail"},  {"Follow", "F", "follow"},     {"Pause list", "P", "pause"},
                        {"Terminate", "t", "terminate"}, {"Kill", "k", "kill_process"}, {"Signal", "s", "signal_menu"},
                };
                popup_selected_ = 0;
                popup_scroll_ = 0;
            }

            void open_vm_detail() {
                if (current_ != view::vms || selected_ >= inventory_.virtual_machines_.size()) {
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                detail_lines_.clear();
                detail_lines_.push_back(format("Name          {}", item.name_));
                detail_lines_.push_back(format("ID            {}", item.id_.empty() ? "—" : item.id_));
                detail_lines_.push_back(format("UUID          {}", item.uuid_.empty() ? "—" : item.uuid_));
                detail_lines_.push_back(format("Backend       {}", item.backend_));
                detail_lines_.push_back(format("State         {}", item.state_));
                detail_lines_.push_back(format("Autostart     {}", item.autostart_.empty() ? "—" : item.autostart_));
                detail_lines_.push_back(
                        format("vCPUs         {}", item.vcpus_.has_value() ? format("{}", *item.vcpus_) : "—"));
                detail_lines_.push_back(format("Memory        {}", vm_memory_text(item)));
                detail_lines_.push_back(
                        format("QEMU PID      {}", item.pid_.has_value() ? format("{}", *item.pid_) : "—"));
                detail_lines_.push_back(
                        format("Description   {}", item.description_.empty() ? "—" : item.description_));
                if (const json* command = item.extra_.get("command"); command != nullptr && command->is_string()) {
                    detail_lines_.push_back(format("Command       {}", command->as_string()));
                }
                popup_ = popup_kind::info;
                popup_title_ = "VIRTUAL MACHINE";
                popup_scroll_ = 0;
            }

            void open_vm_menu() {
                if (current_ != view::vms || selected_ >= inventory_.virtual_machines_.size()) {
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                popup_ = popup_kind::main_menu;
                popup_title_ = format("VM · {}", item.name_);
                popup_entries_ = {
                        {"Details", "Enter", "vm_detail"}, {"Start", "", "vm_start"},
                        {"Shutdown", "", "vm_shutdown"},   {"Reboot", "", "vm_reboot"},
                        {"Force off", "", "vm_destroy"},   {"Pause", "", "vm_suspend"},
                        {"Resume", "", "vm_resume"},       {"Open QEMU process", "", "vm_process"},
                };
                popup_selected_ = 0;
                popup_scroll_ = 0;
                popup_parent_ = popup_kind::none;
            }

            void request_vm_action(string action) {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    popup_ = popup_kind::none;
                    return;
                }
                if (selected_ >= inventory_.virtual_machines_.size()) {
                    status_ = "Select a virtual machine first.";
                    popup_ = popup_kind::none;
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                pending_signal_ = 0;
                pending_workload_action_ = "vm:" + action;
                popup_title_ = "Confirm";
                confirm_message_ = format("{} VM '{}'?\n\nThis action changes system state. Confirm with y/Enter, "
                                          "cancel with n/Esc.",
                                          action, item.name_);
                popup_ = popup_kind::confirm;
            }

            void jump_to_vm_process() {
                if (selected_ >= inventory_.virtual_machines_.size()) {
                    return;
                }
                const auto& vm = inventory_.virtual_machines_[selected_];
                if (!vm.pid_.has_value()) {
                    status_ = "This virtual machine has no QEMU host process.";
                    return;
                }
                const auto pid = *vm.pid_;
                const process* owner = process_for_pid(pid);
                if (owner == nullptr) {
                    status_ = format("PID {} is no longer in the process snapshot.", static_cast<uint64_t>(pid));
                    return;
                }
                switch_view(view::processes);
                filter_.clear();
                editing_filter_ = false;
                const auto process_view_rows = process_rows();
                selected_ = 0;
                for (size_t index = 0; index < process_view_rows.size(); ++index) {
                    if (snap_.processes_[process_view_rows[index].index_].pid_ == pid) {
                        selected_ = index;
                        break;
                    }
                }
                table_offset_ = selected_ > 5 ? selected_ - 5 : 0;
                select_process(*owner);
                status_ = format("VM {} → process {}", vm.name_, static_cast<uint64_t>(pid));
            }

            void run_vm_action(const string_view action) {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    popup_ = popup_kind::none;
                    return;
                }
                if (selected_ >= inventory_.virtual_machines_.size()) {
                    status_ = "Select a virtual machine first.";
                    popup_ = popup_kind::none;
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                try {
                    if (item.backend_ == "libvirt") {
                        run_command("virsh", {string(action), item.name_}, 5000);
                        status_ = format("{} requested for VM {}", action, item.name_);
                    } else if (item.backend_ == "QEMU" && item.pid_.has_value()) {
                        int signal = 0;
                        if (action == "shutdown") {
                            signal = 15;
                        } else if (action == "destroy") {
                            signal = 9;
                        } else if (action == "suspend") {
                            signal = 19;
                        } else if (action == "resume") {
                            signal = 18;
                        }
                        if (signal == 0) {
                            status_ = "Start is not available for an unmanaged QEMU process.";
                        } else if (send_signal(*item.pid_, signal) == 0) {
                            status_ = format("{} sent to QEMU {}", action, static_cast<uint64_t>(*item.pid_));
                        } else {
                            status_ = format("Failed to send {} to QEMU {}", action, static_cast<uint64_t>(*item.pid_));
                        }
                    } else {
                        status_ = "This VM backend does not support management actions.";
                    }
                } catch (const exception& error) {
                    status_ = format("VM action failed: {}", error.what());
                }
                popup_ = popup_kind::none;
            }

            void open_workload_menu() {
                const bool containers = current_ == view::containers;
                const auto rows = workload_rows(containers);
                if (rows.empty() || selected_ >= rows.size()) {
                    status_ = containers ? "Select a container first." : "Select a service first.";
                    return;
                }
                const auto index = rows[selected_];
                const string name =
                        containers ? inventory_.containers_[index].name_ : inventory_.services_[index].name_;
                open_tools_menu();
                popup_title_ = format("{} · {}", containers ? "CONTAINER" : "SERVICE", name);
            }

            void open_commands() {
                popup_ = popup_kind::commands;
                popup_title_ = "Commands";
                popup_scroll_ = 0;
            }

            bool handle_mouse(const tui::mouse_event& event) {
                if (tick_ != nullptr && event.action != tui::mouse_action::release) {
                    *tick_ = tick_->value() + 1;
                }
                if (dragging_process_scrollbar_ && (current_ == view::processes || current_ == view::overview)) {
                    if (event.action == tui::mouse_action::release) {
                        dragging_process_scrollbar_ = false;
                    } else if (event.action == tui::mouse_action::move || event.action == tui::mouse_action::press) {
                        const auto rows = process_rows();
                        const int track = neforce::max(1, process_table_rect_.h_ - 2);
                        if (!rows.empty()) {
                            const int relative = neforce::clamp(event.y - process_table_rect_.y_ - 1, 0, track - 1);
                            const double ratio = static_cast<double>(relative) / static_cast<double>(track - 1);
                            const size_t visible = static_cast<size_t>(neforce::max(0, process_table_rect_.h_ - 4));
                            const size_t maximum = rows.size() > visible ? rows.size() - visible : 0;
                            table_offset_ = static_cast<size_t>(neforce::round(ratio * static_cast<double>(maximum)));
                            free_scroll_ = true;
                        }
                    }
                    return true;
                }
                if (dragging_scroll_action_ != hit_action::none) {
                    if (event.action == tui::mouse_action::release) {
                        dragging_scroll_action_ = hit_action::none;
                    } else if (event.action == tui::mouse_action::move || event.action == tui::mouse_action::press) {
                        if (dragging_scroll_rect_.h_ > 1) {
                            const int relative = neforce::clamp<int>(event.y - dragging_scroll_rect_.y_, 0,
                                                                     dragging_scroll_rect_.h_ - 1);
                            const double ratio =
                                    static_cast<double>(relative) / static_cast<double>(dragging_scroll_rect_.h_ - 1);
                            size_t count = 0;
                            switch (dragging_scroll_action_) {
                                case hit_action::history_scroll:
                                    count = history_keys().size();
                                    break;
                                case hit_action::events_scroll:
                                    for (const auto& item: alert_events_) {
                                        if (filter_.empty() || event_matches(item, filter_)) {
                                            ++count;
                                        }
                                    }
                                    break;
                                case hit_action::explorer_scroll:
                                    count = extra_rows().size();
                                    break;
                                case hit_action::connections_scroll:
                                    count = filtered_connections().size();
                                    break;
                                case hit_action::workload_scroll:
                                    count = workload_rows(current_ == view::containers).size();
                                    break;
                                case hit_action::settings_scroll:
                                    count = settings_rows().size();
                                    break;
                                default:
                                    break;
                            }
                            if (count > 0) {
                                const auto target =
                                        static_cast<size_t>(neforce::round(ratio * static_cast<double>(count - 1)));
                                if (dragging_scroll_action_ == hit_action::connections_scroll) {
                                    selected_connection_ = target;
                                } else if (dragging_scroll_action_ == hit_action::history_scroll) {
                                    const size_t visible =
                                            static_cast<size_t>(neforce::max(0, dragging_scroll_rect_.h_ + 1));
                                    const size_t maximum = count > visible ? count - visible : 0;
                                    table_offset_ = neforce::min(target, maximum);
                                    free_scroll_ = true;
                                } else if (dragging_scroll_action_ == hit_action::settings_scroll) {
                                    const size_t maximum =
                                            count > popup_visible_rows_ ? count - popup_visible_rows_ : 0;
                                    popup_scroll_ = neforce::min(target, maximum);
                                    free_scroll_ = true;
                                } else {
                                    selected_ = target;
                                }
                            }
                        }
                    }
                    return true;
                }
                if (popup_ != popup_kind::none) {
                    const bool menu = popup_ == popup_kind::main_menu || popup_ == popup_kind::sort ||
                                      popup_ == popup_kind::view_menu || popup_ == popup_kind::time_menu ||
                                      popup_ == popup_kind::signal_menu || popup_ == popup_kind::columns ||
                                      popup_ == popup_kind::theme_menu || popup_ == popup_kind::tools;
                    if (event.action == tui::mouse_action::wheel) {
                        if (menu) {
                            if (event.button == tui::mouse_button::wheelup) {
                                if (popup_selected_ > 0) {
                                    --popup_selected_;
                                }
                            } else if (popup_selected_ + 1 < popup_entries_.size()) {
                                ++popup_selected_;
                            }
                        } else if (popup_ == popup_kind::settings) {
                            const size_t count = settings_rows().size();
                            const size_t visible = popup_visible_rows_;
                            const size_t maximum = count > visible ? count - visible : 0;
                            const int delta = event.button == tui::mouse_button::wheelup ? -1 : 1;
                            const int64_t target = static_cast<int64_t>(popup_scroll_) + delta;
                            popup_scroll_ = static_cast<size_t>(
                                    neforce::clamp(target, static_cast<int64_t>(0), static_cast<int64_t>(maximum)));
                            free_scroll_ = true;
                        } else {
                            popup_scroll_ = event.button == tui::mouse_button::wheelup
                                                    ? (popup_scroll_ > 3 ? popup_scroll_ - 3 : 0)
                                                    : popup_scroll_ + 3;
                        }
                        return true;
                    }
                    if (event.action == tui::mouse_action::press && event.button == tui::mouse_button::left) {
                        if (popup_rect_.contains(event.x, event.y)) {
                            const int row = event.y - popup_first_row_;
                            if (row < 0) {
                                return true;
                            }
                            if (menu) {
                                const size_t index = popup_scroll_ + static_cast<size_t>(row);
                                if (index < popup_entries_.size()) {
                                    popup_selected_ = index;
                                    activate_popup();
                                }
                            } else if (popup_ == popup_kind::settings) {
                                const size_t index = popup_scroll_ + static_cast<size_t>(row);
                                if (index < settings_rows().size()) {
                                    settings_index_ = index;
                                    free_scroll_ = false;
                                    open_setting_choice();
                                }
                            }
                            return true;
                        }
                        popup_ = popup_kind::none;
                    }
                }
                if (event.action == tui::mouse_action::press && event.button == tui::mouse_button::right &&
                    current_ == view::network) {
                    if (network_interfaces_rect_.contains(event.x, event.y) &&
                        event.y > network_interfaces_rect_.y_ + 1) {
                        const int index = event.y - network_interfaces_rect_.y_ - 2;
                        if (index >= 0 && static_cast<size_t>(index) < filtered_networks().size()) {
                            network_section_ = 1;
                            selected_ = static_cast<size_t>(index);
                            open_network_context();
                            return true;
                        }
                    }
                    if (network_connections_rect_.contains(event.x, event.y) &&
                        event.y > network_connections_rect_.y_ + 1) {
                        const int index = event.y - network_connections_rect_.y_ - 2;
                        const auto rows = filtered_connections();
                        if (index >= 0) {
                            const size_t target = connection_offset_ + static_cast<size_t>(index);
                            if (target < rows.size()) {
                                network_section_ = 0;
                                selected_connection_ = target;
                                open_network_context();
                                return true;
                            }
                        }
                    }
                }
                if (event.action == tui::mouse_action::press && event.button == tui::mouse_button::right &&
                    current_ == view::storage) {
                    if (storage_filesystems_rect_.contains(event.x, event.y) &&
                        event.y > storage_filesystems_rect_.y_ + 1) {
                        const int index = event.y - storage_filesystems_rect_.y_ - 2;
                        if (index >= 0 && static_cast<size_t>(index) < snap_.disks_.size()) {
                            selected_ = static_cast<size_t>(index);
                            storage_section_ = 0;
                            open_storage_context(true);
                            return true;
                        }
                    }
                    if (storage_io_rect_.contains(event.x, event.y) && event.y > storage_io_rect_.y_ + 1) {
                        const int index = event.y - storage_io_rect_.y_ - 2;
                        if (index >= 0 && static_cast<size_t>(index) < snap_.disk_io_.size()) {
                            storage_device_auto_ = false;
                            selected_io_ = static_cast<size_t>(index);
                            storage_section_ = 1;
                            open_storage_context(false);
                            return true;
                        }
                    }
                }
                if (event.action == tui::mouse_action::press && event.button == tui::mouse_button::right &&
                    current_ == view::gpu) {
                    for (const auto& hit: hits_) {
                        if (hit.action_ == hit_action::gpu_device && hit.area_.contains(event.x, event.y)) {
                            selected_gpu_ = hit.index_;
                            open_gpu_context();
                            return true;
                        }
                    }
                }
                if (event.action == tui::mouse_action::press && event.button == tui::mouse_button::right &&
                    current_ == view::vms && workload_rect_.contains(event.x, event.y)) {
                    const int row = event.y - workload_rect_.y_ - 2;
                    if (row >= 0) {
                        const size_t target = table_offset_ + static_cast<size_t>(row);
                        if (target < inventory_.virtual_machines_.size()) {
                            selected_ = target;
                            free_scroll_ = false;
                            open_vm_menu();
                            return true;
                        }
                    }
                }
                if (event.action == tui::mouse_action::press && event.button == tui::mouse_button::right &&
                    (current_ == view::services || current_ == view::containers) &&
                    workload_rect_.contains(event.x, event.y)) {
                    const int row = event.y - workload_rect_.y_ - 2;
                    const auto rows = workload_rows(current_ == view::containers);
                    if (row >= 0) {
                        const size_t target = table_offset_ + static_cast<size_t>(row);
                        if (target < rows.size()) {
                            selected_ = target;
                            free_scroll_ = false;
                            if (current_ == view::services) {
                                remember_service_selection();
                            }
                            open_workload_menu();
                            return true;
                        }
                    }
                }
                if (event.action == tui::mouse_action::press && event.button == tui::mouse_button::right &&
                    (current_ == view::processes || current_ == view::overview) &&
                    process_table_rect_.contains(event.x, event.y)) {
                    const int row = event.y - process_table_rect_.y_ - 2;
                    const auto rows = process_rows();
                    if (row >= 0) {
                        const size_t target = table_offset_ + static_cast<size_t>(row);
                        if (target < rows.size()) {
                            selected_ = target;
                            remember_selection(rows);
                            open_process_menu();
                            return true;
                        }
                    }
                }
                if (event.action == tui::mouse_action::press && event.button == tui::mouse_button::left) {
                    for (auto hit = hits_.rbegin(); hit != hits_.rend(); ++hit) {
                        if (!hit->area_.contains(event.x, event.y)) {
                            continue;
                        }
                        if (hit->action_ == hit_action::tab && hit->index_ < view_count) {
                            switch_view(views[hit->index_].value);
                            return true;
                        }
                        if (hit->action_ == hit_action::view_overflow) {
                            open_view_menu();
                            return true;
                        }
                        if (hit->action_ == hit_action::footer_action) {
                            switch (hit->index_) {
                                case 0:
                                    open_main_menu();
                                    break;
                                case 1:
                                    begin_filter();
                                    break;
                                case 2:
                                    open_sort_menu();
                                    break;
                                case 3:
                                    open_view_menu();
                                    break;
                                case 4:
                                    popup_ = popup_kind::help;
                                    popup_scroll_ = 0;
                                    break;
                                case 5:
                                    popup_parent_ = popup_kind::none;
                                    free_scroll_ = false;
                                    popup_ = popup_kind::settings;
                                    settings_index_ = 0;
                                    break;
                                case 6:
                                    open_commands();
                                    break;
                                case 7:
                                    quit_ = true;
                                    tui::application::request_quit(0);
                                    break;
                                default:
                                    break;
                            }
                            return true;
                        }
                        if ((hit->action_ == hit_action::history_scroll || hit->action_ == hit_action::events_scroll ||
                             hit->action_ == hit_action::explorer_scroll ||
                             hit->action_ == hit_action::connections_scroll ||
                             hit->action_ == hit_action::workload_scroll ||
                             hit->action_ == hit_action::settings_scroll) &&
                            hit->area_.h_ > 1) {
                            const int relative = neforce::clamp<int>(event.y - hit->area_.y_, 0, hit->area_.h_ - 1);
                            const double ratio = static_cast<double>(relative) / static_cast<double>(hit->area_.h_ - 1);
                            size_t count = 0;
                            if (hit->action_ == hit_action::history_scroll) {
                                count = history_keys().size();
                            } else if (hit->action_ == hit_action::events_scroll) {
                                for (const auto& item: alert_events_) {
                                    if (filter_.empty() || event_matches(item, filter_)) {
                                        ++count;
                                    }
                                }
                            } else if (hit->action_ == hit_action::explorer_scroll) {
                                count = extra_rows().size();
                            } else if (hit->action_ == hit_action::connections_scroll) {
                                count = filtered_connections().size();
                            } else if (hit->action_ == hit_action::settings_scroll) {
                                count = settings_rows().size();
                            } else if (current_ == view::vms) {
                                count = inventory_.virtual_machines_.size();
                            } else {
                                count = workload_rows(current_ == view::containers).size();
                            }
                            if (count > 0) {
                                const auto target =
                                        static_cast<size_t>(neforce::round(ratio * static_cast<double>(count - 1)));
                                if (hit->action_ == hit_action::connections_scroll) {
                                    selected_connection_ = target;
                                } else if (hit->action_ == hit_action::history_scroll) {
                                    const size_t visible = static_cast<size_t>(neforce::max(0, hit->area_.h_ + 1));
                                    const size_t maximum = count > visible ? count - visible : 0;
                                    table_offset_ = neforce::min(target, maximum);
                                    free_scroll_ = true;
                                } else if (hit->action_ == hit_action::settings_scroll) {
                                    const size_t maximum =
                                            count > popup_visible_rows_ ? count - popup_visible_rows_ : 0;
                                    popup_scroll_ = neforce::min(target, maximum);
                                    free_scroll_ = true;
                                } else {
                                    selected_ = target;
                                    if (current_ == view::services) {
                                        remember_service_selection();
                                    }
                                }
                                dragging_scroll_action_ = hit->action_;
                                dragging_scroll_rect_ = hit->area_;
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::process_scrollbar &&
                            (current_ == view::processes || current_ == view::overview)) {
                            const auto rows = process_rows();
                            if (!rows.empty() && hit->area_.h_ > 1) {
                                const int relative = neforce::clamp<int>(event.y - hit->area_.y_, 0, hit->area_.h_ - 1);
                                const double ratio =
                                        static_cast<double>(relative) / static_cast<double>(hit->area_.h_ - 1);
                                const size_t visible = static_cast<size_t>(neforce::max(0, process_table_rect_.h_ - 4));
                                const size_t maximum = rows.size() > visible ? rows.size() - visible : 0;
                                table_offset_ =
                                        static_cast<size_t>(neforce::round(ratio * static_cast<double>(maximum)));
                                free_scroll_ = true;
                                dragging_process_scrollbar_ = true;
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::process_tree_toggle &&
                            (current_ == view::processes || current_ == view::overview)) {
                            const auto rows = process_rows();
                            for (const auto& row: rows) {
                                const auto& item = snap_.processes_[row.index_];
                                if (item.pid_ != hit->pid_ || item.start_time_ != hit->start_time_) {
                                    continue;
                                }
                                if (collapsed_.count(item.pid_) != 0) {
                                    collapsed_.erase(item.pid_);
                                } else {
                                    collapsed_.insert(item.pid_);
                                }
                                break;
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::process_row &&
                            (current_ == view::processes || current_ == view::overview)) {
                            const auto rows = process_rows();
                            size_t row_index = rows.size();
                            for (size_t index = 0; index < rows.size(); ++index) {
                                const auto& item = snap_.processes_[rows[index].index_];
                                if (item.pid_ == hit->pid_ && item.start_time_ == hit->start_time_) {
                                    row_index = index;
                                    break;
                                }
                            }
                            if (row_index < rows.size()) {
                                const bool double_click = monotonic_ms() - last_click_ms_ < 400 &&
                                                          last_click_pid_ == hit->pid_ &&
                                                          last_click_start_time_ == hit->start_time_;
                                selected_ = row_index;
                                free_scroll_ = false;
                                remember_selection(rows);
                                last_click_ms_ = monotonic_ms();
                                last_click_row_ = static_cast<int>(hit->index_);
                                last_click_pid_ = hit->pid_;
                                last_click_start_time_ = hit->start_time_;
                                if (double_click) {
                                    open_detail();
                                }
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::workload_row && current_ == view::vms) {
                            if (hit->index_ < inventory_.virtual_machines_.size()) {
                                const bool double_click = monotonic_ms() - last_click_ms_ < 400 &&
                                                          compare_equal(last_click_row_, hit->index_);
                                selected_ = hit->index_;
                                free_scroll_ = false;
                                last_click_ms_ = monotonic_ms();
                                last_click_row_ = static_cast<int>(hit->index_);
                                if (double_click) {
                                    open_vm_detail();
                                }
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::workload_row &&
                            (current_ == view::containers || current_ == view::services)) {
                            const auto rows = workload_rows(current_ == view::containers);
                            if (hit->index_ < rows.size()) {
                                const bool double_click = monotonic_ms() - last_click_ms_ < 400 &&
                                                          compare_equal(last_click_row_, hit->index_);
                                selected_ = hit->index_;
                                free_scroll_ = false;
                                if (current_ == view::services) {
                                    remember_service_selection();
                                }
                                last_click_ms_ = monotonic_ms();
                                last_click_row_ = static_cast<int>(hit->index_);
                                if (double_click) {
                                    open_workload_detail(current_ == view::containers);
                                }
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::network_interface_row && current_ == view::network) {
                            network_section_ = 1;
                            selected_ = hit->index_;
                            return true;
                        }
                        if (hit->action_ == hit_action::connection_row && current_ == view::network) {
                            const bool double_click = monotonic_ms() - last_click_ms_ < 400 &&
                                                      compare_equal(last_click_row_, hit->index_);
                            network_section_ = 0;
                            selected_connection_ = hit->index_;
                            last_click_ms_ = monotonic_ms();
                            last_click_row_ = static_cast<int>(hit->index_);
                            if (double_click) {
                                open_connection_detail();
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::storage_fs_row && current_ == view::storage) {
                            storage_section_ = 0;
                            selected_ = hit->index_;
                            return true;
                        }
                        if (hit->action_ == hit_action::storage_io_row && current_ == view::storage) {
                            storage_section_ = 1;
                            storage_device_auto_ = false;
                            selected_io_ = hit->index_;
                            return true;
                        }
                        if (hit->action_ == hit_action::history_row && current_ == view::history) {
                            selected_ = hit->index_;
                            free_scroll_ = false;
                            return true;
                        }
                        if (hit->action_ == hit_action::event_row && current_ == view::events) {
                            selected_ = hit->index_;
                            return true;
                        }
                        if (hit->action_ == hit_action::explorer_row && current_ == view::explorer) {
                            const bool double_click = monotonic_ms() - last_click_ms_ < 400 &&
                                                      compare_equal(last_click_row_, hit->index_);
                            selected_ = hit->index_;
                            last_click_ms_ = monotonic_ms();
                            last_click_row_ = static_cast<int>(hit->index_);
                            if (double_click) {
                                open_extra_detail();
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::interface_sort && current_ == view::network &&
                            hit->index_ < 5) {
                            if (network_section_ == 1 && interface_sort_ == hit->index_) {
                                reverse_ = !reverse_;
                            } else {
                                network_section_ = 1;
                                interface_sort_ = hit->index_;
                                reverse_ = false;
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::connection_sort && current_ == view::network &&
                            hit->index_ < 5) {
                            if (network_section_ == 0 && network_sort_ == hit->index_) {
                                reverse_ = !reverse_;
                            } else {
                                network_section_ = 0;
                                network_sort_ = hit->index_;
                                reverse_ = false;
                            }
                            return true;
                        }
                        if ((hit->action_ == hit_action::gpu_prev || hit->action_ == hit_action::gpu_next) &&
                            (current_ == view::gpu || current_ == view::overview) && !snap_.gpus_.empty()) {
                            const auto count = static_cast<int64_t>(snap_.gpus_.size());
                            const int64_t delta = hit->action_ == hit_action::gpu_next ? 1 : -1;
                            selected_gpu_ = static_cast<size_t>(neforce::clamp(
                                    static_cast<int64_t>(selected_gpu_) + delta, static_cast<int64_t>(0), count - 1));
                            return true;
                        }
                        if (hit->action_ == hit_action::gpu_device &&
                            (current_ == view::gpu || current_ == view::overview) && hit->index_ < snap_.gpus_.size()) {
                            selected_gpu_ = hit->index_;
                            if (current_ == view::overview) {
                                status_ = format("GPU {} selected · {} devices", hit->index_, snap_.gpus_.size());
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::gpu_process_row && current_ == view::gpu) {
                            for (const auto& process: snap_.processes_) {
                                if (process.pid_ != hit->index_) {
                                    continue;
                                }
                                select_process(process);
                                open_detail();
                                break;
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::process_sort &&
                            (current_ == view::processes || current_ == view::overview)) {
                            const auto columns = active_columns();
                            if (hit->index_ >= columns.size()) {
                                return true;
                            }
                            const auto key = columns[hit->index_].key_;
                            size_t target = 0;
                            if (key == "pid" || key == "ppid") {
                                target = 8;
                            } else if (key == "user" || key == "uid") {
                                target = 10;
                            } else if (key == "name" || key == "command") {
                                target = 9;
                            } else if (key == "state") {
                                target = 17;
                            } else if (key == "nice") {
                                target = 11;
                            } else if (key == "memory") {
                                target = 1;
                            } else if (key == "vram") {
                                target = 3;
                            } else if (key == "gpu") {
                                target = 2;
                            } else if (key == "read") {
                                target = 4;
                            } else if (key == "write") {
                                target = 5;
                            } else if (key == "io") {
                                target = 6;
                            } else if (key == "time") {
                                target = 7;
                            } else {
                                target = 0;
                            }
                            if (target == sort_index_) {
                                reverse_ = !reverse_;
                            } else {
                                sort_index_ = target;
                                reverse_ = !sorts[target].descending;
                            }
                            sync_sort_config();
                            return true;
                        }
                    }
                }
                if (event.action == tui::mouse_action::wheel) {
                    if (body_.contains(event.x, event.y)) {
                        const int delta = event.button == tui::mouse_button::wheelup ? -1 : 1;
                        if (current_ == view::system && listeners_rect_.contains(event.x, event.y)) {
                            const size_t visible = static_cast<size_t>(neforce::max(0, listeners_rect_.h_ - 3));
                            const size_t maximum =
                                    inventory_.listeners_.size() > visible ? inventory_.listeners_.size() - visible : 0;
                            const int64_t target = static_cast<int64_t>(listener_offset_) + delta;
                            listener_offset_ = static_cast<size_t>(
                                    neforce::clamp(target, static_cast<int64_t>(0), static_cast<int64_t>(maximum)));
                            return true;
                        }
                        if (current_ == view::network) {
                            const size_t count = filtered_connections().size();
                            if (count > 0) {
                                const int64_t target = static_cast<int64_t>(selected_connection_) + delta;
                                selected_connection_ = static_cast<size_t>(neforce::clamp(
                                        target, static_cast<int64_t>(0), static_cast<int64_t>(count) - 1));
                            }
                        } else if (current_ == view::vms) {
                            const size_t count = inventory_.virtual_machines_.size();
                            const size_t visible = static_cast<size_t>(neforce::max(0, workload_rect_.h_ - 3));
                            const size_t maximum = count > visible ? count - visible : 0;
                            const int64_t target = static_cast<int64_t>(table_offset_) + delta;
                            table_offset_ = static_cast<size_t>(
                                    neforce::clamp(target, static_cast<int64_t>(0), static_cast<int64_t>(maximum)));
                            free_scroll_ = true;
                        } else if (current_ == view::containers || current_ == view::services) {
                            const size_t count = workload_rows(current_ == view::containers).size();
                            const size_t visible = static_cast<size_t>(neforce::max(0, workload_rect_.h_ - 3));
                            const size_t maximum = count > visible ? count - visible : 0;
                            const int64_t target = static_cast<int64_t>(table_offset_) + delta;
                            table_offset_ = static_cast<size_t>(
                                    neforce::clamp(target, static_cast<int64_t>(0), static_cast<int64_t>(maximum)));
                            free_scroll_ = true;
                        } else if (current_ == view::storage) {
                            if (!snap_.disks_.empty()) {
                                const int64_t target = static_cast<int64_t>(selected_) + delta;
                                selected_ = static_cast<size_t>(
                                        neforce::clamp(target, static_cast<int64_t>(0),
                                                       static_cast<int64_t>(snap_.disks_.size()) - 1));
                            }
                        } else if (current_ == view::history) {
                            const size_t count = history_keys().size();
                            const size_t visible = static_cast<size_t>(neforce::max(0, body_.h_ - 2));
                            const size_t maximum = count > visible ? count - visible : 0;
                            const int64_t target = static_cast<int64_t>(table_offset_) + delta;
                            table_offset_ = static_cast<size_t>(
                                    neforce::clamp(target, static_cast<int64_t>(0), static_cast<int64_t>(maximum)));
                            free_scroll_ = true;
                        } else if (current_ == view::events) {
                            size_t count = 0;
                            for (const auto& item: alert_events_) {
                                if (filter_.empty() || event_matches(item, filter_)) {
                                    ++count;
                                }
                            }
                            if (count > 0) {
                                const int64_t target = static_cast<int64_t>(selected_) + delta;
                                selected_ = static_cast<size_t>(neforce::clamp(target, static_cast<int64_t>(0),
                                                                               static_cast<int64_t>(count) - 1));
                            }
                        } else if (current_ == view::processes || current_ == view::overview) {
                            const auto rows = process_rows();
                            const size_t visible = static_cast<size_t>(neforce::max(0, process_table_rect_.h_ - 4));
                            const size_t maximum = rows.size() > visible ? rows.size() - visible : 0;
                            const int64_t target = static_cast<int64_t>(table_offset_) + delta;
                            table_offset_ = static_cast<size_t>(
                                    neforce::clamp(target, static_cast<int64_t>(0), static_cast<int64_t>(maximum)));
                            free_scroll_ = true;
                        }
                    }
                    return true;
                }
                if (event.action == tui::mouse_action::press && event.button == tui::mouse_button::left) {
                    if (event.y == 1) {
                        int x = 0;
                        for (size_t index = 0; index < view_count; ++index) {
                            const int width =
                                    display_width(format("[{}] {}", views[index].key, views[index].name).view()) + 2;
                            if (event.x >= x && event.x < x + width) {
                                switch_view(views[index].value);
                                return true;
                            }
                            x += width;
                        }
                    }
                    if (current_ == view::explorer && explorer_rect_.contains(event.x, event.y)) {
                        const auto rows = extra_rows();
                        const int index = event.y - explorer_rect_.y_ - 2;
                        if (index >= 0) {
                            const size_t target = table_offset_ + static_cast<size_t>(index);
                            if (target < rows.size()) {
                                const bool double_click =
                                        monotonic_ms() - last_click_ms_ < 400 && last_click_row_ == index;
                                selected_ = target;
                                last_click_ms_ = monotonic_ms();
                                last_click_row_ = index;
                                if (double_click) {
                                    open_extra_detail();
                                }
                                return true;
                            }
                        }
                    }
                    if (current_ == view::network && network_interfaces_rect_.contains(event.x, event.y) &&
                        event.y > network_interfaces_rect_.y_ + 1) {
                        const auto rows = filtered_networks();
                        const int index = event.y - network_interfaces_rect_.y_ - 2;
                        if (index >= 0 && static_cast<size_t>(index) < rows.size()) {
                            network_section_ = 1;
                            selected_ = static_cast<size_t>(index);
                            return true;
                        }
                    }
                    if (current_ == view::network && network_connections_rect_.contains(event.x, event.y) &&
                        event.y > network_connections_rect_.y_ + 1) {
                        network_section_ = 0;
                        const auto rows = filtered_connections();
                        const int index = event.y - network_connections_rect_.y_ - 2;
                        if (index >= 0 && static_cast<size_t>(index) < rows.size()) {
                            const size_t target = connection_offset_ + static_cast<size_t>(index);
                            if (target < rows.size()) {
                                const bool double_click =
                                        monotonic_ms() - last_click_ms_ < 400 && last_click_row_ == index;
                                selected_connection_ = target;
                                last_click_ms_ = monotonic_ms();
                                last_click_row_ = index;
                                if (double_click) {
                                    open_connection_detail();
                                }
                                return true;
                            }
                        }
                    }
                    if ((current_ == view::containers || current_ == view::services) &&
                        workload_rect_.contains(event.x, event.y)) {
                        const auto rows = workload_rows(current_ == view::containers);
                        const int index = event.y - workload_rect_.y_ - 2;
                        if (index >= 0 && static_cast<size_t>(index) < rows.size()) {
                            const size_t target = table_offset_ + static_cast<size_t>(index);
                            if (target < rows.size()) {
                                const bool double_click =
                                        monotonic_ms() - last_click_ms_ < 400 && last_click_row_ == index;
                                selected_ = target;
                                last_click_ms_ = monotonic_ms();
                                last_click_row_ = index;
                                if (double_click) {
                                    open_workload_detail(current_ == view::containers);
                                }
                                return true;
                            }
                        }
                    }
                    if (body_.contains(event.x, event.y)) {
                        if ((current_ == view::processes || current_ == view::overview) &&
                            process_table_rect_.contains(event.x, event.y)) {
                            const auto rows = process_rows();
                            const int index = event.y - process_table_rect_.y_ - 2;
                            if (index >= 0) {
                                const size_t target = table_offset_ + static_cast<size_t>(index);
                                if (target < rows.size()) {
                                    const bool double_click =
                                            monotonic_ms() - last_click_ms_ < 400 && last_click_row_ == index;
                                    selected_ = target;
                                    remember_selection(rows);
                                    last_click_ms_ = monotonic_ms();
                                    last_click_row_ = index;
                                    if (double_click) {
                                        open_detail();
                                    }
                                    return true;
                                }
                            }
                        }
                        if (current_ == view::storage) {
                            if (storage_filesystems_rect_.contains(event.x, event.y) &&
                                event.y > storage_filesystems_rect_.y_ + 1) {
                                const int index = event.y - storage_filesystems_rect_.y_ - 2;
                                if (index >= 0 && static_cast<size_t>(index) < snap_.disks_.size()) {
                                    selected_ = static_cast<size_t>(index);
                                    return true;
                                }
                            }
                            if (storage_io_rect_.contains(event.x, event.y) && event.y > storage_io_rect_.y_ + 1) {
                                const int index = event.y - storage_io_rect_.y_ - 2;
                                if (index >= 0 && static_cast<size_t>(index) < snap_.disk_io_.size()) {
                                    selected_ = static_cast<size_t>(index);
                                    if (event.y >= storage_io_rect_.y_ + 2 && monotonic_ms() - last_click_ms_ < 400 &&
                                        last_click_row_ == index) {
                                        open_storage_detail();
                                    }
                                    last_click_ms_ = monotonic_ms();
                                    last_click_row_ = index;
                                    return true;
                                }
                            }
                        }
                    }
                }
                return false;
            }

            NEFORCE_NODISCARD ::neforce::neflux::plot_window chart_window() const {
                if (cfg_.time_window_seconds_ > 0) {
                    return history_.time_window(cfg_.time_window_seconds_);
                }
                return history_.window(cfg_.history_);
            }

            NEFORCE_NODISCARD ::neforce::neflux::plot_window storage_chart_window() const {
                if (history_.timestamps_.size() < 2) {
                    return chart_window();
                }
                const double span = history_.timestamps_.back() - history_.timestamps_.front();
                const uint64_t seconds = neforce::max<uint64_t>(3, static_cast<uint64_t>(span) + 1);
                return history_.time_window(seconds);
            }
        };

    } // namespace

    namespace {

        view parse_view(const string& name) {
            if (name == "processes") {
                return view::processes;
            }
            if (name == "gpu") {
                return view::gpu;
            }
            if (name == "storage") {
                return view::storage;
            }
            if (name == "network") {
                return view::network;
            }
            if (name == "system") {
                return view::system;
            }
            if (name == "containers") {
                return view::containers;
            }
            if (name == "services") {
                return view::services;
            }
            if (name == "vms") {
                return view::vms;
            }
            if (name == "history") {
                return view::history;
            }
            if (name == "events") {
                return view::events;
            }
            if (name == "explorer") {
                return view::explorer;
            }
            return view::overview;
        }

        tui::theme theme_for(const palette& colors) {
            tui::theme theme = tui::dark_theme;
            theme.bg = colors.bg_;
            theme.fg = colors.text_;
            theme.primary = colors.mint_;
            theme.secondary = colors.blue_;
            theme.border = colors.border_;
            theme.muted = colors.muted_;
            return theme;
        }

        string color_hex(const color& value) { return format("#{:02x}{:02x}{:02x}", value.R(), value.G(), value.B()); }

    } // namespace

    string render_plaintext(const snapshot& value, const inventory& data, const config& cfg, const string& view_name,
                            int width, int height) {
        flux_component component(nullptr, cfg, "", cli_args{});
        component.seed(value, data, parse_view(view_name));
        tui::headless app(&component, width, height);
        const palette colors = palette_from_config(cfg);
        app.set_theme(theme_for(colors));
        app.render();
        return app.plaintext();
    }

    string render_svg_history(const vector<snapshot>& values, const inventory& data, const config& cfg,
                              const string& view_name, const string& gpu_panel, int width, int height) {
        cli_args args;
        args.gpu_panel_ = gpu_panel;
        flux_component component(nullptr, cfg, "", move(args));
        component.seed_terminal_size(width, height);
        component.seed_history(values, data, parse_view(view_name));
        tui::headless app(&component, width, height);
        palette colors = palette_from_config(cfg);
        app.set_theme(theme_for(colors));
        app.render();
        const auto& frame = app.frame();
        const uint32_t svg_width = static_cast<uint32_t>(width) * 8 + 32;
        const uint32_t svg_height = static_cast<uint32_t>(height) * 18 + 32;
        string out;
        out.append(R"(<svg xmlns="http://www.w3.org/2000/svg" width=")")
                .append(format("{}", svg_width))
                .append("\" height=\"")
                .append(format("{}", svg_height))
                .append("\" viewBox=\"0 0 ")
                .append(format("{}", svg_width))
                .append(" ")
                .append(format("{}", svg_height))
                .append(R"("><rect width="100%" height="100%" fill=")")
                .append(color_hex(colors.bg_))
                .append(R"("/><g font-family="DejaVu Sans Mono,monospace" font-size="13" xml:space="preserve">)");
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto& cell = frame.fast_cell_at(x, y);
                const uint32_t px = 16 + static_cast<uint32_t>(x) * 8;
                const uint32_t py = 16 + static_cast<uint32_t>(y) * 18;
                color background = cell.background;
                if (background.A() == 0) {
                    background = colors.bg_;
                }
                out.append("<rect x=\"")
                        .append(format("{}", px))
                        .append("\" y=\"")
                        .append(format("{}", py))
                        .append(R"(" width="8" height="18" fill=")")
                        .append(color_hex(background))
                        .append("\"/>");
            }
        }
        const auto escape_text = [](const string& input) {
            string result;
            for (const char c: input) {
                if (c == '&') {
                    result.append("&amp;");
                } else if (c == '<') {
                    result.append("&lt;");
                } else if (c == '>') {
                    result.append("&gt;");
                } else if (c == '"') {
                    result.append("&quot;");
                } else {
                    result.push_back(c);
                }
            }
            return result;
        };
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto& cell = frame.fast_cell_at(x, y);
                if (cell.character.empty()) {
                    continue;
                }
                size_t index = 0;
                const auto glyph = codepoint::decode_utf8(reinterpret_cast<const byte_t*>(cell.character.data()), index,
                                                          cell.character.size());
                const uint32_t code = glyph.value();
                const uint32_t px = 16 + static_cast<uint32_t>(x) * 8;
                const uint32_t py = 16 + static_cast<uint32_t>(y) * 18;
                if (code >= 0x2800 && code <= 0x28FF) {
                    const uint32_t dots = code - 0x2800;
                    static constexpr int positions[8][2] = {{2, 2}, {2, 6},  {2, 10}, {6, 2},
                                                            {6, 6}, {6, 10}, {2, 14}, {6, 14}};
                    for (int bit = 0; bit < 8; ++bit) {
                        if ((dots & (1U << bit)) == 0) {
                            continue;
                        }
                        out.append("<circle cx=\"")
                                .append(format("{}", px + static_cast<uint32_t>(positions[bit][0])))
                                .append("\" cy=\"")
                                .append(format("{}", py + static_cast<uint32_t>(positions[bit][1])))
                                .append(R"(" r="0.85" fill=")")
                                .append(color_hex(cell.foreground))
                                .append("\"/>");
                    }
                    continue;
                }
                if (cell.character.empty() || cell.character == " ") {
                    continue;
                }
                const string symbol(cell.character.view());
                color foreground = cell.foreground;
                if (foreground.A() == 0) {
                    foreground = colors.text_;
                }
                out.append("<text x=\"")
                        .append(format("{}", px))
                        .append("\" y=\"")
                        .append(format("{}", py + 14))
                        .append("\" fill=\"")
                        .append(color_hex(foreground))
                        .append("\"");
                if (cell.bold) {
                    out.append(" font-weight=\"bold\"");
                }
                out.append(">").append(escape_text(symbol)).append("</text>");
            }
        }
        out.append("</g></svg>");
        return out;
    }

    string render_svg(const snapshot& value, const inventory& data, config cfg, const string& view_name,
                      const string& gpu_panel, int width, int height) {
        vector<snapshot> values;
        values.push_back(value);
        return render_svg_history(values, data, move(cfg), view_name, gpu_panel, width, height);
    }

    vector<uint32_t> headless_probe_backgrounds(string_view input, int x, int y, int width, int height) {
        config cfg;
        cfg.view_ = 1;
        flux_component component(nullptr, cfg, string{}, cli_args{});
        tui::headless app(&component, 120, 40);
        app.render();
        size_t begin = 0;
        for (size_t index = 0; index <= input.size(); ++index) {
            if (index != input.size() && input[index] != '|') {
                continue;
            }
            app.feed(input.substr(begin, index - begin));
            app.tick();
            begin = index + 1;
        }
        vector<uint32_t> out;
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                if (x + column < 0 || y + row < 0 || x + column >= 120 || y + row >= 40) {
                    out.push_back(0);
                    continue;
                }
                const auto& cell = app.frame().fast_cell_at(x + column, y + row);
                out.push_back((static_cast<uint32_t>(cell.background.A()) << 24) |
                              (static_cast<uint32_t>(cell.background.R()) << 16) |
                              (static_cast<uint32_t>(cell.background.G()) << 8) |
                              static_cast<uint32_t>(cell.background.B()));
            }
        }
        return out;
    }

    uint32_t headless_probe_background(string_view input, int x, int y) {
        config cfg;
        cfg.view_ = 1;
        flux_component component(nullptr, cfg, string{}, cli_args{});
        tui::headless app(&component, 120, 40);
        app.render();
        size_t begin = 0;
        for (size_t index = 0; index <= input.size(); ++index) {
            if (index != input.size() && input[index] != '|') {
                continue;
            }
            app.feed(input.substr(begin, index - begin));
            app.tick();
            begin = index + 1;
        }
        if (x < 0 || y < 0 || x >= 120 || y >= 40) {
            return 0;
        }
        const auto& cell = app.frame().fast_cell_at(x, y);
        return (static_cast<uint32_t>(cell.background.A()) << 24) | (static_cast<uint32_t>(cell.background.R()) << 16) |
               (static_cast<uint32_t>(cell.background.G()) << 8) | static_cast<uint32_t>(cell.background.B());
    }

    uint32_t headless_probe_seeded_background(const snapshot& value, const inventory& data, string_view view_name,
                                              string_view input, int x, int y) {
        config cfg;
        flux_component component(nullptr, cfg, string{}, cli_args{});
        component.seed(value, data, parse_view(string(view_name)));
        tui::headless app(&component, 120, 40);
        app.render();
        size_t begin = 0;
        for (size_t index = 0; index <= input.size(); ++index) {
            if (index != input.size() && input[index] != '|') {
                continue;
            }
            app.feed(input.substr(begin, index - begin));
            app.tick();
            begin = index + 1;
        }
        if (x < 0 || y < 0 || x >= 120 || y >= 40) {
            return 0;
        }
        const auto& cell = app.frame().fast_cell_at(x, y);
        return (static_cast<uint32_t>(cell.background.A()) << 24) | (static_cast<uint32_t>(cell.background.R()) << 16) |
               (static_cast<uint32_t>(cell.background.G()) << 8) | static_cast<uint32_t>(cell.background.B());
    }

    string headless_probe_seeded(const snapshot& value, const inventory& data, string_view view_name,
                                 string_view input, const int width, const int height) {
        config cfg;
        flux_component component(nullptr, cfg, string{}, cli_args{});
        component.seed(value, data, parse_view(string(view_name)));
        tui::headless app(&component, width, height);
        app.render();
        size_t begin = 0;
        for (size_t index = 0; index <= input.size(); ++index) {
            if (index != input.size() && input[index] != '|') {
                continue;
            }
            const auto segment = input.substr(begin, index - begin);
            if (segment == "#esc") {
                tui::key_event key;
                key.key = tui::key_event::type::escape;
                app.feed_key(key);
            } else {
                app.feed(segment);
            }
            app.tick();
            begin = index + 1;
        }
        return app.plaintext();
    }

    string headless_probe_service_refresh(const snapshot& value, const inventory& first, const inventory& second,
                                          string_view before, string_view after, const int width, const int height) {
        config cfg;
        flux_component component(nullptr, cfg, string{}, cli_args{});
        component.seed(value, first, view::services);
        tui::headless app(&component, width, height);
        app.render();
        const auto feed = [&app](string_view input) {
            size_t begin = 0;
            for (size_t index = 0; index <= input.size(); ++index) {
                if (index != input.size() && input[index] != '|') {
                    continue;
                }
                app.feed(input.substr(begin, index - begin));
                app.tick();
                begin = index + 1;
            }
        };
        feed(before);
        component.seed(value, second, view::services);
        app.render();
        feed(after);
        return app.plaintext();
    }

    string headless_probe_sized(const int width, const int height, string_view input) {
        config cfg;
        cfg.view_ = 1;
        flux_component component(nullptr, cfg, string{}, cli_args{});
        tui::headless app(&component, width, height);
        app.render();
        size_t begin = 0;
        for (size_t index = 0; index <= input.size(); ++index) {
            if (index != input.size() && input[index] != '|') {
                continue;
            }
            const auto segment = input.substr(begin, index - begin);
            if (segment == "#esc") {
                tui::key_event key;
                key.key = tui::key_event::type::escape;
                app.feed_key(key);
                app.tick();
            } else {
                app.feed(segment);
                app.tick();
            }
            begin = index + 1;
        }
        return app.plaintext();
    }

    string headless_probe_resized(const int width, const int height, const int new_width, const int new_height) {
        config cfg;
        cfg.view_ = 1;
        flux_component component(nullptr, cfg, string{}, cli_args{});
        tui::headless app(&component, width, height);
        app.render();
        app.resize(new_width, new_height);
        app.render();
        return app.plaintext();
    }

    string headless_probe(string_view input) {
        config cfg;
        cfg.view_ = 1;
        flux_component component(nullptr, cfg, string{}, cli_args{});
        tui::headless app(&component, 120, 40);
        app.render();
        string transcript;
        size_t begin = 0;
        for (size_t index = 0; index <= input.size(); ++index) {
            if (index != input.size() && input[index] != '|') {
                continue;
            }
            const auto segment = input.substr(begin, index - begin);
            if (segment == "#wait") {
                neforce::this_thread::sleep_for_ms(650);
            } else if (segment == "#esc") {
                tui::key_event key;
                key.key = tui::key_event::type::escape;
                app.feed_key(key);
                app.tick();
            } else {
                app.feed(segment);
                app.tick();
            }
            transcript.append(app.plaintext());
            transcript.append("\n<<<FRAME>>>\n");
            begin = index + 1;
        }
        return transcript;
    }

    string headless_probe_ansi(string_view input) {
        config cfg;
        cfg.view_ = 1;
        flux_component component(nullptr, cfg, string{}, cli_args{});
        tui::headless app(&component, 120, 40);
        app.render();
        size_t begin = 0;
        for (size_t index = 0; index <= input.size(); ++index) {
            if (index != input.size() && input[index] != '|') {
                continue;
            }
            const auto segment = input.substr(begin, index - begin);
            if (segment == "#wait") {
                neforce::this_thread::sleep_for_ms(650);
            } else if (segment == "#esc") {
                tui::key_event key;
                key.key = tui::key_event::type::escape;
                app.feed_key(key);
                app.tick();
            } else {
                app.feed(segment);
                app.tick();
            }
            begin = index + 1;
        }
        return app.ansi();
    }

    int run_interactive(const cli_args& args, config cfg, const string& config_path) {
        cli_args copy = args;
        const bool replay_mode = args.replay_.has_value();
        sampler source(cfg, args.replay_, args.read_only_, args.full_sampling_ || args.record_.has_value());
        const palette colors = palette_from_config(cfg);
        const tui::theme theme = theme_for(colors);
        const auto app = make_unique<tui::application>();
        app->with_component<flux_component>(&source, cfg, config_path, copy)
                .with_theme(theme)
                .with_fps(30)
                .with_title("nexus-flux")
                .with_two_stage_quit(false);
        static_cast<void>(replay_mode);
        return app->run();
    }

} // namespace neforce::neflux::ui
