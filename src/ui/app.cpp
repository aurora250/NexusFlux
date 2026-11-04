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
#include <NeFlux/source.hpp>
#include <NeFlux/session.hpp>
#include <NeFlux/util.hpp>
#include "platform/platform_backend.hpp"
#include <NeForce/core/async/atomic.hpp>
#include <NeForce/core/async/io_context.hpp>
#include <NeForce/core/async/mutex.hpp>
#include <NeForce/core/async/thread.hpp>
#include <NeForce/core/async/this_thread.hpp>
#include <NeForce/core/system/console.hpp>
#include <NeForce/core/system/environment.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/core/system/signal.hpp>
#include <NeForce/tui/application.hpp>
#include <NeForce/tui/component/component.hpp>
#include <NeForce/tui/dom/element.hpp>
#include <NeForce/tui/headless.hpp>
#include <NeForce/tui/screen.hpp>
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
            vm_control,
            settings_scroll,
        };

        struct hit_target {
            rect area_;
            hit_action action_ = hit_action::none;
            size_t index_ = 0;
            uint32_t pid_ = 0;
            uint64_t start_time_ = 0;
        };

        struct list_selection {
            optional<string> key_;
            size_t row_ = 0;
            bool valid_ = false;
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

        view parse_view(const string& name);

        class flux_component final : public tui::component<> {
        public:
            flux_component(telemetry_source* source, config cfg, string config_path, cli_args args) :
            source_(source),
            cfg_(move(cfg)),
            config_path_(move(config_path)),
            args_(move(args)) {
                replay_ = source != nullptr && source->replay_active();
                tree_ = cfg_.tree_;
                auto_collapse_done_ = false;
                if (args_.full_sampling_ || args_.record_.has_value()) {
                    interest_.gpu_processes_ = true;
                    interest_.connections_ = true;
                    interest_.containers_ = true;
                    interest_.services_ = true;
                    interest_.extras_ = true;
                }
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
                if (source_ != nullptr && !replay_) {
                    activate_source(current_);
                }
                schedule_poll();
            }

            void activate_source(view target) {
                if (source_ == nullptr || replay_) {
                    return;
                }
                if (target == view::processes || target == view::gpu || target == view::containers ||
                    target == view::services || target == view::history || target == view::events) {
                    interest_.gpu_processes_ = true;
                }
                if (target == view::network) {
                    interest_.connections_ = true;
                    interest_.extras_ = true;
                }
                if (target == view::containers || target == view::vms) {
                    interest_.containers_ = true;
                }
                if (target == view::services) {
                    interest_.services_ = true;
                }
                if (target == view::storage || target == view::system) {
                    interest_.extras_ = true;
                }
                source_->set_interest(interest_);
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
                selection_restore_pending_ = true;
                inventory_ = move(data);
                status_.clear();
                apply_tree_auto_collapse();
            }

            void seed(snapshot value, inventory data, view target) {
                snap_ = move(value);
                inventory_ = move(data);
                current_ = target;
                seeded_ = true;
                selection_restore_pending_ = true;
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
            telemetry_source* source_ = nullptr;
            source_interest interest_;
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
            list_selection connection_selection_;
            list_selection interface_selection_;
            list_selection container_selection_;
            list_selection service_selection_;
            list_selection vm_selection_;
            bool selection_restore_pending_ = true;
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
            size_t process_panel_scroll_ = 0;
            size_t process_panel_scroll_max_ = 0;
            rect process_panel_rect_{};
            bool workload_side_panel_ = false;
            rect workload_side_panel_rect_{};
            bool vm_side_panel_ = false;
            rect vm_side_panel_rect_{};
            string vm_edit_key_;
            uint32_t vm_edit_vcpus_ = 0;
            uint64_t vm_edit_memory_mib_ = 0;
            bool vm_edit_autostart_ = false;
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
            rect gpu_process_rect_{};
            size_t selected_gpu_process_ = 0;
            size_t gpu_process_offset_ = 0;
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
            bool item_detail_panel_ = false;
            bool item_detail_refresh_ = false;
            string item_detail_title_;
            vector<string> item_detail_lines_;
            rect item_detail_panel_rect_{};
            rect item_detail_anchor_rect_{};
            uint64_t item_detail_outside_ms_ = 0;
            int item_detail_outside_x_ = -1;
            int item_detail_outside_y_ = -1;
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
            system_signal::signal pending_signal_ = system_signal::signal::terminate;
            bool pending_signal_active_ = false;
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
                    constexpr int column = 22;
                    int used = 1 + 11 + 2 + static_cast<int>(display_width(snap_.hostname_.view()));
                    if (used < column) {
                        spans.push_back({string(static_cast<size_t>(column - used), ' '), {}, {}});
                    }
                    spans.push_back({"· discovering", muted_style, {}});
                }

                string right =
                        format("[{}] · [-]{:.2f}s[+] · up {}", time_label(cfg_.time_window_seconds_),
                               static_cast<double>(cfg_.collection_.interval_ms_) / 1000.0, duration(snap_.uptime_));
                if (replay_) {
                    right = format("REPLAY {}/{} · {}", source_->replay_index() + 1, source_->replay_size(), right);
                } else if (source_ != nullptr) {
                    right = format("{} · {}", source_->label(), right);
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
                if (source_ == nullptr) {
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
                if (quit_ || source_ == nullptr) {
                    return;
                }
                snapshot incoming;
                bool changed = false;
                while (source_->take_snapshot(incoming)) {
                    apply_snapshot(move(incoming));
                    changed = true;
                }
                inventory found;
                if (source_->take_inventory(found)) {
                    inventory_ = move(found);
                    snap_.apply_inventory(inventory_);
                    annotate(snap_);
                    changed = true;
                }
                if (const auto error = source_->take_error(); error.has_value()) {
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
                if (selected_process_.has_value()) {
                    restore_process_selection();
                }
                if (snap_.timestamp_ != 0) {
                    status_.clear();
                }
                if (snap_.gpus_.size() <= selected_gpu_) {
                    selected_gpu_ = snap_.gpus_.empty() ? 0 : snap_.gpus_.size() - 1;
                }
            }

            NEFORCE_NODISCARD bool visible_process(const process& value) const {
                if (value.thread_ && !cfg_.collection_.show_threads_) {
                    return false;
                }
                if (value.kernel_thread_ && !cfg_.collection_.show_kernel_threads_) {
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
                process_panel_scroll_ = 0;
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
                refresh_item_detail_side_panel();
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
                draw_item_detail_side_panel(brush, body_);
                selection_restore_pending_ = false;
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

// View implementations are split into class fragments to keep each page isolated.
#include "views/overview.inc"

#include "views/process.inc"

#include "views/history.inc"

#include "views/gpu.inc"

#include "views/storage_network.inc"

#include "views/system.inc"

#include "views/workloads.inc"

#include "views/actions.inc"

#include "views/input.inc"

#include "views/menus.inc"

#include "views/mouse.inc"

            NEFORCE_NODISCARD ::neforce::neflux::plot_window chart_window() const {
                if (cfg_.time_window_seconds_ > 0) {
                    return history_.time_window(cfg_.time_window_seconds_);
                }
                return history_.window(cfg_.history_);
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

    string headless_probe_seeded(const snapshot& value, const inventory& data, string_view view_name, string_view input,
                                 const int width, const int height) {
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

    string headless_probe_snapshot_refresh(const snapshot& first_value, const snapshot& second_value,
                                           const inventory& data, string_view view_name, string_view before,
                                           string_view after, const int width, const int height,
                                           string_view second_view_name) {
        config cfg;
        const auto target = parse_view(string(view_name));
        const auto second_target = second_view_name.empty() ? target : parse_view(string(second_view_name));
        flux_component component(nullptr, cfg, string{}, cli_args{});
        component.seed(first_value, data, target);
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
        component.seed(second_value, data, second_target);
        app.render();
        feed(after);
        return app.plaintext();
    }

    string headless_probe_view_refresh(const snapshot& value, const inventory& first, const inventory& second,
                                       string_view view_name, string_view before, string_view after, const int width,
                                       const int height) {
        config cfg;
        const auto target = parse_view(string(view_name));
        flux_component component(nullptr, cfg, string{}, cli_args{});
        component.seed(value, first, target);
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
        component.seed(value, second, target);
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
        string source_error;
        auto source = create_source(args, cfg, source_error);
        if (source == nullptr) {
            sys_console::instance().print_error(format("Cannot select machine: {}\n", source_error));
            return 1;
        }
        if (!source->start(source_error)) {
            sys_console::instance().print_error(format("Cannot start telemetry source: {}\n", source_error));
            return 1;
        }
        const palette colors = palette_from_config(cfg);
        const tui::theme theme = theme_for(colors);
        const auto app = make_unique<tui::application>();
        app->with_component<flux_component>(source.get(), cfg, config_path, copy)
                .with_theme(theme)
                // The monitor has no frame-driven animation: redraw on sampled state changes only.
                .with_fps(0)
                .with_title("nexus-flux")
                .with_two_stage_quit(false);
        static_cast<void>(replay_mode);
        return app->run();
    }

} // namespace neforce::neflux::ui
