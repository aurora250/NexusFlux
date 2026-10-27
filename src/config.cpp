#include <NeFlux/common.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/config.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/exception/exception.hpp>
#include <NeForce/core/file/filesystem.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/file/toml/toml_parser.hpp>
#include <NeForce/core/file/toml/toml_value.hpp>
#include <NeForce/core/system/environment.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/core/memory/memory.hpp>
namespace neforce::neflux {

    namespace {

        NEFORCE_NORETURN void fail(const string& message) {
            char buffer[256] = {};
            const size_t count = message.size() < sizeof(buffer) - 1 ? message.size() : sizeof(buffer) - 1;
            memory_copy(buffer, message.data(), count);
            buffer[count] = '\0';
            throw value_exception(buffer);
        }

        string quote(const string& value) {
            string out = "\"";
            for (const char c: value) {
                if (c == '"' || c == '\\') {
                    out.push_back('\\');
                }
                out.push_back(c);
            }
            out.push_back('"');
            return out;
        }

        string toml_bool(bool value) { return value ? "true" : "false"; }

        string format_double(double value) {
            string out = format_number(value, 15);
            if (!out.contains('.') && out.find('e') == string::npos && out.find('E') == string::npos) {
                out.append(".0");
            }
            return out;
        }

        string format_float(float value) { return format_double(value); }

        const toml_value* member(const toml_table& table, const char* key) { return table.get_member(key); }

        bool read_bool(const toml_table& table, const char* key, bool& out) {
            const toml_value* value = member(table, key);
            if (value == nullptr) {
                return false;
            }
            if (!value->is_boolean()) {
                fail(format("Invalid boolean for {}", key));
            }
            out = value->as_boolean()->get_value();
            return true;
        }

        bool read_string(const toml_table& table, const char* key, string& out) {
            const toml_value* value = member(table, key);
            if (value == nullptr) {
                return false;
            }
            if (!value->is_string()) {
                fail(format("Invalid string for {}", key));
            }
            out = value->as_string()->get_value();
            return true;
        }

        bool read_integer(const toml_table& table, const char* key, int64_t& out) {
            const toml_value* value = member(table, key);
            if (value == nullptr) {
                return false;
            }
            if (!value->is_integer()) {
                fail(format("Invalid integer for {}", key));
            }
            out = value->as_integer()->get_value();
            return true;
        }

        bool read_float(const toml_table& table, const char* key, double& out) {
            const toml_value* value = member(table, key);
            if (value == nullptr) {
                return false;
            }
            if (value->is_float()) {
                out = value->as_float()->get_value();
                return true;
            }
            if (value->is_integer()) {
                out = static_cast<double>(value->as_integer()->get_value());
                return true;
            }
            fail(format("Invalid float for {}", key));
        }

        bool read_string_array(const toml_table& table, const char* key, vector<string>& out) {
            const toml_value* value = member(table, key);
            if (value == nullptr) {
                return false;
            }
            if (!value->is_array()) {
                fail(format("Invalid array for {}", key));
            }
            out.clear();
            for (const auto& element: value->as_array()->get_elements()) {
                if (!element->is_string()) {
                    fail(format("Invalid string in {}", key));
                }
                out.push_back(element->as_string()->get_value());
            }
            return true;
        }

        void check_keys(const toml_table& table, const char* section, const vector<const char*>& allowed) {
            for (const auto& entry: table.get_members()) {
                bool known = false;
                for (const char* key: allowed) {
                    if (entry.first == key) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    fail(format("Unknown configuration field: {}.{}", section, entry.first));
                }
            }
        }

        layout_node parse_layout(const toml_table& table, size_t depth) {
            if (depth >= 6) {
                fail("Layout nesting exceeds 6 levels");
            }
            layout_node node;
            if (const toml_value* panel_value = table.get_member("panel"); panel_value != nullptr) {
                if (!panel_value->is_string()) {
                    fail("Layout panel must be a string");
                }
                node.kind_ = layout_node::kind::panel;
                node.panel_ = panel_value->as_string()->get_value();
                if (!(node.panel_ == "cpu" || node.panel_ == "memory" || node.panel_ == "gpu" ||
                      node.panel_ == "storage" || node.panel_ == "network" || node.panel_ == "processes")) {
                    fail(format("Unknown panel: {}", node.panel_));
                }
                return node;
            }
            const toml_value* direction = table.get_member("direction");
            const toml_value* children = table.get_member("children");
            if (direction == nullptr || !direction->is_string() || children == nullptr || !children->is_array()) {
                fail("Invalid layout split");
            }
            node.kind_ = layout_node::kind::split;
            node.direction_ = direction->as_string()->get_value();
            if (node.direction_ != "horizontal" && node.direction_ != "vertical") {
                fail("Layout direction must be horizontal or vertical");
            }
            const auto& elements = children->as_array()->get_elements();
            if (elements.empty() || elements.size() > 8) {
                fail("Layout split requires 1..=8 children");
            }
            for (const auto& element: elements) {
                if (!element->is_table()) {
                    fail("Layout child must be a table");
                }
                node.children_.push_back(parse_layout(*element->as_table(), depth + 1));
            }
            if (const toml_value* weights = table.get_member("weights"); weights != nullptr) {
                if (!weights->is_array()) {
                    fail("Layout weights must be an array");
                }
                for (const auto& element: weights->as_array()->get_elements()) {
                    if (!element->is_integer() || element->as_integer()->get_value() <= 0) {
                        fail("Layout weights must be positive");
                    }
                    node.weights_.push_back(static_cast<uint16_t>(element->as_integer()->get_value()));
                }
                if (node.weights_.size() != node.children_.size()) {
                    fail("Layout weights must match children");
                }
            }
            return node;
        }

        void write_layout_node(string& out, const layout_node& node, const string& prefix) {
            if (node.kind_ == layout_node::kind::panel) {
                out.append(prefix).append("panel = ").append(quote(node.panel_)).append("\n");
                return;
            }
            out.append(prefix).append("direction = ").append(quote(node.direction_)).append("\n");
            if (!node.weights_.empty()) {
                out.append(prefix).append("weights = [");
                for (size_t i = 0; i < node.weights_.size(); ++i) {
                    if (i != 0) {
                        out.append(", ");
                    }
                    out.append(format("{}", node.weights_[i]));
                }
                out.append("]\n");
            }
            for (const auto& child: node.children_) {
                out.append(prefix).append("[[custom_layout.children]]\n");
                write_layout_node(out, child, prefix + "  ");
            }
        }

        void write_string_array(string& out, const char* key, const vector<string>& values) {
            out.append(key);
            out.append(" = [");
            for (size_t i = 0; i < values.size(); ++i) {
                if (i != 0) {
                    out.append(", ");
                }
                out.append(quote(values[i]));
            }
            out.append("]\n");
        }

    } // namespace

    vector<string> config::themes() {
        return {"midnight", "nord", "ember", "black", "white", "graphite", "ocean", "forest", "sand"};
    }

    void config::cycle_theme(bool forward) {
        const auto names = themes();
        size_t current = 0;
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == theme_) {
                current = i;
                break;
            }
        }
        const size_t step = forward ? 1 : names.size() - 1;
        theme_ = names[(current + step) % names.size()];
    }

    string config::path() {
        const string base = environment::config_directory();
        if (base.empty()) {
            return "neflux/config.toml";
        }
        return (::neforce::path(base) / "neflux" / "config.toml").str();
    }

    config config::load(const string& config_path) {
        if (!::neforce::path(config_path).exists()) {
            return config{};
        }
        const auto text = read_text_file(config_path);
        if (!text.has_value()) {
            fail(format("Cannot read configuration: {}", config_path));
        }
        return from_toml(*text);
    }

    void config::validate() const {
        display_.validate();
        if (time_window_seconds_ > 36000) {
            fail("time_window_seconds must be 0..=36000");
        }
        if (interval_ms_ < 250 || interval_ms_ > 10000) {
            fail("interval_ms must be 250..=10000");
        }
        if (history_ < 30 || history_ > 3600) {
            fail("history must be 30..=3600 samples");
        }
        const auto names = themes();
        bool theme_ok = false;
        for (const auto& name: names) {
            if (name == theme_) {
                theme_ok = true;
                break;
            }
        }
        if (!theme_ok) {
            fail("theme must be one of: midnight, nord, ember, black, white, graphite, ocean, forest, sand");
        }
        if (view_ >= 12) {
            fail("view must be 0..=11");
        }
        if (!(layout_ == "balanced" || layout_ == "compute" || layout_ == "storage" || layout_ == "minimal")) {
            fail("invalid layout");
        }
        if (tree_auto_collapse_ > 10000) {
            fail("tree_auto_collapse must be 0..=10000");
        }
        if (overview_presets_.empty()) {
            fail("overview_presets must not be empty");
        }
        if (overview_preset_ >= overview_presets_.size()) {
            fail("overview_preset is out of range");
        }
        for (const auto& preset: overview_presets_) {
            size_t begin = 0;
            while (begin <= preset.size()) {
                size_t end = preset.find(',', begin);
                if (end == string::npos) {
                    end = preset.size();
                }
                string_view token = preset.view().substr(begin, end - begin);
                token.trim();
                if (token.empty()) {
                    fail("overview preset contains an empty box");
                }
                const string name(token.data(), token.size());
                if (!(name == "cpu" || name == "memory" || name == "gpu" || name == "storage" ||
                      name == "network" || name == "processes")) {
                    fail(format("Unknown overview box: {}", name));
                }
                begin = end + 1;
            }
        }
        if (alert_history_ < 1 || alert_history_ > 10000) {
            fail("alert_history must be 1..=10000");
        }
        if (log_lines_ < 1 || log_lines_ > 100000) {
            fail("log_lines must be 1..=100000");
        }
        if (record_max_mib_ < 1 || record_max_mib_ > 4096) {
            fail("record_max_mib must be 1..=4096");
        }
        if (alert_hold_seconds_ > 3600) {
            fail("alert_hold_seconds must be 0..=3600");
        }
        if (probes_.size() > 64 || monitored_folders_.size() > 64) {
            fail("At most 64 probes/folders");
        }
        if (alert_commands_.size() > 8) {
            fail("At most 8 alert commands");
        }
        for (const auto& command: alert_commands_) {
            if (command.empty() || command[0].empty()) {
                fail("Alert command needs a program");
            }
        }
        if (columns_.size() > 90) {
            fail("Too many process columns");
        }
        for (const auto& probe: probes_) {
            if (probe.name_.empty() || probe.command_.empty() || probe.command_[0].empty() ||
                probe.timeout_seconds_ == 0 || probe.timeout_seconds_ > 30) {
                fail("Invalid local probe");
            }
        }
        if (cpu_warning_ < 0.0F || cpu_warning_ > 100.0F) {
            fail("cpu_warning must be 0..=100");
        }
        if (memory_warning_ < 0.0 || memory_warning_ > 100.0) {
            fail("memory_warning must be 0..=100");
        }
        for (const auto& member_entry: custom_colors_.as_object()) {
            const auto& key = member_entry.first;
            if (!(key == "background" || key == "panel" || key == "text" || key == "accent" || key == "border" ||
                  key == "muted" || key == "selection" || key == "blue" || key == "purple" || key == "amber" ||
                  key == "red" || key == "cpu_start" || key == "cpu_mid" || key == "cpu_end" || key == "mem_start" ||
                  key == "mem_mid" || key == "mem_end" || key == "net_start" || key == "net_mid" || key == "net_end" ||
                  key == "gpu_start" || key == "gpu_mid" || key == "gpu_end" || key == "temp_start" ||
                  key == "temp_mid" || key == "temp_end" || key == "proc_start" || key == "proc_mid" ||
                  key == "proc_end" || key == "meter_bg" || key == "graph_text" || key == "followed_bg" ||
                  key == "followed_fg" || key == "pause_bg" || key == "follow_bg" || key == "banner_bg" ||
                  key == "banner_fg" || key == "proc_pause_bg" || key == "proc_follow_bg" ||
                  key == "proc_banner_bg" || key == "proc_banner_fg")) {
                fail(format("Unknown color: {}", key));
            }
            const auto& value = member_entry.second.as_string();
            if (value.size() != 7 || value[0] != '#') {
                fail("Color must be #RRGGBB");
            }
            for (size_t i = 1; i < value.size(); ++i) {
                const char c = value[i];
                const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                if (!hex) {
                    fail("Color must be #RRGGBB");
                }
            }
        }
    }

    string config::to_toml() const {
        string out;
        out.append("interval_ms = ").append(format("{}", interval_ms_)).append("\n");
        out.append("collect_gpu = ").append(toml_bool(collect_gpu_)).append("\n");
        out.append("theme = ").append(quote(theme_)).append("\n");
        if (!theme_file_.empty()) {
            out.append("theme_file = ").append(quote(theme_file_)).append("\n");
        }
        out.append("view = ").append(format("{}", view_)).append("\n");
        out.append("sort_index = ").append(format("{}", sort_index_)).append("\n");
        out.append("reverse_sort = ").append(toml_bool(reverse_sort_)).append("\n");
        out.append("tree = ").append(toml_bool(tree_)).append("\n");
        out.append("tree_auto_collapse = ").append(format("{}", tree_auto_collapse_)).append("\n");
        out.append("tree_aggregate = ").append(toml_bool(tree_aggregate_)).append("\n");
        out.append("history = ").append(format("{}", history_)).append("\n");
        out.append("time_window_seconds = ").append(format("{}", time_window_seconds_)).append("\n");
        out.append("show_loopback = ").append(toml_bool(show_loopback_)).append("\n");
        out.append("cpu_warning = ").append(format_float(cpu_warning_)).append("\n");
        out.append("memory_warning = ").append(format_double(memory_warning_)).append("\n");
        out.append("gpu_temperature_warning = ").append(format("{}", gpu_temperature_warning_)).append("\n");
        out.append("show_threads = ").append(toml_bool(show_threads_)).append("\n");
        out.append("show_kernel_threads = ").append(toml_bool(show_kernel_threads_)).append("\n");
        out.append("ascii = ").append(toml_bool(ascii_)).append("\n");
        out.append("no_color = ").append(toml_bool(no_color_)).append("\n");
        out.append("layout = ").append(quote(layout_)).append("\n");
        out.append("overview_preset = ").append(format("{}", overview_preset_)).append("\n");
        write_string_array(out, "overview_presets", overview_presets_);
        write_string_array(out, "columns", columns_);
        out.append("custom_colors = {");
        bool first_color = true;
        for (const auto& entry: custom_colors_.as_object()) {
            if (!first_color) {
                out.append(", ");
            }
            first_color = false;
            out.append(entry.first).append(" = ").append(quote(entry.second.as_string()));
        }
        out.append("}\n");
        write_string_array(out, "monitored_folders", monitored_folders_);
        out.append("process_network = ").append(toml_bool(process_network_)).append("\n");
        out.append("alert_hold_seconds = ").append(format("{}", alert_hold_seconds_)).append("\n");
        out.append("alert_history = ").append(format("{}", alert_history_)).append("\n");
        out.append("alert_commands = [");
        for (size_t i = 0; i < alert_commands_.size(); ++i) {
            if (i != 0) {
                out.append(", ");
            }
            out.append("[");
            for (size_t j = 0; j < alert_commands_[i].size(); ++j) {
                if (j != 0) {
                    out.append(", ");
                }
                out.append(quote(alert_commands_[i][j]));
            }
            out.append("]");
        }
        out.append("]\n");
        out.append("log_lines = ").append(format("{}", log_lines_)).append("\n");
        out.append("record_max_mib = ").append(format("{}", record_max_mib_)).append("\n");

        for (const auto& probe: probes_) {
            out.append("\n[[probes]]\n");
            out.append("name = ").append(quote(probe.name_)).append("\n");
            write_string_array(out, "command", probe.command_);
            out.append("timeout_seconds = ").append(format("{}", probe.timeout_seconds_)).append("\n");
        }

        if (custom_layout_.has_value()) {
            out.append("\n[custom_layout]\n");
            write_layout_node(out, *custom_layout_, "");
        }
        out.append("\ncheck_updates = ").append(toml_bool(check_updates_)).append("\n");
        if (ignored_update_version_.has_value()) {
            out.append("ignored_update_version = ").append(quote(*ignored_update_version_)).append("\n");
        }
        out.append("\n[display]\n");
        out.append("cpu_graph = ").append(quote(display_.cpu_graph_)).append("\n");
        out.append("cpu_graph_lower = ").append(quote(display_.cpu_graph_lower_)).append("\n");
        out.append("temperature_unit = ").append(quote(display_.temperature_unit_)).append("\n");
        out.append("cpu_sensor = ").append(quote(display_.cpu_sensor_)).append("\n");
        out.append("network_interface = ").append(quote(display_.network_interface_)).append("\n");
        out.append("network_bits = ").append(toml_bool(display_.network_bits_)).append("\n");
        out.append("network_binary = ").append(toml_bool(display_.network_binary_)).append("\n");
        out.append("network_logarithmic = ").append(toml_bool(display_.network_logarithmic_)).append("\n");
        out.append("network_start_zero = ").append(toml_bool(display_.network_start_zero_)).append("\n");
        out.append("network_sync_scale = ").append(toml_bool(display_.network_sync_scale_)).append("\n");
        out.append("network_scale = ").append(format("{}", display_.network_scale_)).append("\n");
        out.append("network_swap = ").append(toml_bool(display_.network_swap_)).append("\n");
        out.append("memory_graphs = ").append(toml_bool(display_.memory_graphs_)).append("\n");
        out.append("show_swap = ").append(toml_bool(display_.show_swap_)).append("\n");
        out.append("show_battery = ").append(toml_bool(display_.show_battery_)).append("\n");
        out.append("process_basename = ").append(toml_bool(display_.process_basename_)).append("\n");
        out.append("group_processes = ").append(toml_bool(display_.group_processes_)).append("\n");
        out.append("overview_panel = ").append(quote(display_.overview_panel_)).append("\n");
        out.append("transparent_background = ").append(toml_bool(display_.transparent_background_)).append("\n");
        out.append("graph_symbol = ").append(quote(display_.graph_symbol_)).append("\n");
        out.append("cpu_lazy_sort = ").append(toml_bool(display_.cpu_lazy_sort_)).append("\n");
        out.append("rounded_corners = ").append(toml_bool(display_.rounded_corners_)).append("\n");
        out.append("process_colors = ").append(toml_bool(display_.process_colors_)).append("\n");
        out.append("process_gradient = ").append(toml_bool(display_.process_gradient_)).append("\n");
        out.append("graph_gradient = ").append(toml_bool(display_.graph_gradient_)).append("\n");
        out.append("gpu_in_cpu_box = ").append(toml_bool(display_.gpu_in_cpu_box_)).append("\n");
        out.append("multi_gpu_overview = ").append(toml_bool(display_.multi_gpu_overview_)).append("\n");
        out.append("graph_symbol_cpu = ").append(quote(display_.graph_symbol_cpu_)).append("\n");
        out.append("graph_symbol_mem = ").append(quote(display_.graph_symbol_mem_)).append("\n");
        out.append("graph_symbol_net = ").append(quote(display_.graph_symbol_net_)).append("\n");
        out.append("graph_symbol_proc = ").append(quote(display_.graph_symbol_proc_)).append("\n");
        out.append("graph_symbol_gpu = ").append(quote(display_.graph_symbol_gpu_)).append("\n");
        return out;
    }

    config config::from_toml(const string& text) {
        toml_parser parser(text);
        const auto root = parser.parse();
        if (root == nullptr) {
            fail("Invalid configuration");
        }
        config value;

        check_keys(*root, "root",
                   {"interval_ms",
                    "collect_gpu",
                    "theme",
                    "theme_file",
                    "view",
                    "sort_index",
                    "reverse_sort",
                    "tree",
                    "tree_auto_collapse",
                    "tree_aggregate",
                    "history",
                    "time_window_seconds",
                    "show_loopback",
                    "cpu_warning",
                    "memory_warning",
                    "gpu_temperature_warning",
                    "show_threads",
                    "show_kernel_threads",
                    "ascii",
                    "no_color",
                    "layout",
                    "overview_preset",
                    "overview_presets",
                    "custom_layout",
                    "columns",
                    "custom_colors",
                    "monitored_folders",
                    "process_network",
                    "alert_hold_seconds",
                    "alert_history",
                    "alert_commands",
                    "log_lines",
                    "record_max_mib",
                    "probes",
                    "display",
                    "check_updates",
                    "ignored_update_version"});

        int64_t number = 0;
        if (read_integer(*root, "interval_ms", number)) {
            value.interval_ms_ = static_cast<uint64_t>(number);
        }
        read_bool(*root, "collect_gpu", value.collect_gpu_);
        read_string(*root, "theme", value.theme_);
        read_string(*root, "theme_file", value.theme_file_);
        if (read_integer(*root, "view", number)) {
            value.view_ = static_cast<size_t>(number);
        }
        if (read_integer(*root, "sort_index", number)) {
            value.sort_index_ = static_cast<size_t>(number);
        }
        read_bool(*root, "reverse_sort", value.reverse_sort_);
        read_bool(*root, "tree", value.tree_);
        if (read_integer(*root, "tree_auto_collapse", number)) {
            value.tree_auto_collapse_ = static_cast<size_t>(neforce::max<int64_t>(0, number));
        }
        read_bool(*root, "tree_aggregate", value.tree_aggregate_);
        if (read_integer(*root, "history", number)) {
            value.history_ = static_cast<size_t>(number);
        }
        if (read_integer(*root, "time_window_seconds", number)) {
            value.time_window_seconds_ = static_cast<uint64_t>(number);
        }
        read_bool(*root, "show_loopback", value.show_loopback_);
        double real = 0.0;
        if (read_float(*root, "cpu_warning", real)) {
            value.cpu_warning_ = static_cast<float>(real);
        }
        if (read_float(*root, "memory_warning", real)) {
            value.memory_warning_ = real;
        }
        if (read_integer(*root, "gpu_temperature_warning", number)) {
            value.gpu_temperature_warning_ = static_cast<uint32_t>(number);
        }
        read_bool(*root, "show_threads", value.show_threads_);
        read_bool(*root, "show_kernel_threads", value.show_kernel_threads_);
        read_bool(*root, "ascii", value.ascii_);
        read_bool(*root, "no_color", value.no_color_);
        read_string(*root, "layout", value.layout_);
        if (read_integer(*root, "overview_preset", number)) {
            value.overview_preset_ = static_cast<size_t>(neforce::max<int64_t>(0, number));
        }
        read_string_array(*root, "overview_presets", value.overview_presets_);
        if (const toml_value* layout = root->get_member("custom_layout"); layout != nullptr && layout->is_table()) {
            value.custom_layout_ = parse_layout(*layout->as_table(), 0);
        }
        read_string_array(*root, "columns", value.columns_);
        if (const toml_value* colors = root->get_member("custom_colors"); colors != nullptr && colors->is_table()) {
            value.custom_colors_ = json::object();
            for (const auto& entry: colors->as_table()->get_members()) {
                if (!entry.second->is_string()) {
                    fail("custom_colors values must be strings");
                }
                value.custom_colors_.set(entry.first, json(entry.second->as_string()->get_value()));
            }
        }
        read_string_array(*root, "monitored_folders", value.monitored_folders_);
        read_bool(*root, "process_network", value.process_network_);
        if (read_integer(*root, "alert_hold_seconds", number)) {
            value.alert_hold_seconds_ = static_cast<uint64_t>(number);
        }
        if (read_integer(*root, "alert_history", number)) {
            value.alert_history_ = static_cast<size_t>(number);
        }
        if (const toml_value* commands = root->get_member("alert_commands");
            commands != nullptr && commands->is_array()) {
            for (const auto& item: commands->as_array()->get_elements()) {
                if (!item->is_array()) {
                    fail("alert_commands must be arrays of strings");
                }
                vector<string> command;
                for (const auto& argument: item->as_array()->get_elements()) {
                    if (!argument->is_string()) {
                        fail("alert_commands must be arrays of strings");
                    }
                    command.push_back(argument->as_string()->get_value());
                }
                value.alert_commands_.push_back(move(command));
            }
        }
        if (read_integer(*root, "log_lines", number)) {
            value.log_lines_ = static_cast<size_t>(number);
        }
        if (read_integer(*root, "record_max_mib", number)) {
            value.record_max_mib_ = static_cast<uint64_t>(number);
        }
        if (const toml_value* probes = root->get_member("probes"); probes != nullptr && probes->is_array()) {
            for (const auto& item: probes->as_array()->get_elements()) {
                if (!item->is_table()) {
                    fail("probes must be tables");
                }
                probe_config probe;
                if (!read_string(*item->as_table(), "name", probe.name_)) {
                    fail("probe requires a name");
                }
                if (!read_string_array(*item->as_table(), "command", probe.command_)) {
                    fail("probe requires a command");
                }
                if (read_integer(*item->as_table(), "timeout_seconds", number)) {
                    probe.timeout_seconds_ = static_cast<uint64_t>(number);
                }
                value.probes_.push_back(move(probe));
            }
        }
        if (const toml_value* display = root->get_member("display"); display != nullptr && display->is_table()) {
            const toml_table& table = *display->as_table();
            check_keys(table, "display",
                       {"cpu_graph",          "cpu_graph_lower",    "temperature_unit",       "cpu_sensor",
                        "network_interface",  "network_bits",       "network_binary",         "network_logarithmic",
                        "network_start_zero", "network_sync_scale", "network_scale",          "network_swap",
                        "memory_graphs",      "show_swap",          "show_battery",           "process_basename",
                        "group_processes",    "overview_panel",     "transparent_background", "graph_symbol",
                        "cpu_lazy_sort",      "rounded_corners",    "process_colors",         "process_gradient",
                        "graph_gradient",     "gpu_in_cpu_box",     "multi_gpu_overview",     "graph_symbol_cpu",
                        "graph_symbol_mem",   "graph_symbol_net",   "graph_symbol_proc",      "graph_symbol_gpu"});
            display_config& d = value.display_;
            read_string(table, "cpu_graph", d.cpu_graph_);
            read_string(table, "cpu_graph_lower", d.cpu_graph_lower_);
            read_string(table, "temperature_unit", d.temperature_unit_);
            read_string(table, "cpu_sensor", d.cpu_sensor_);
            read_string(table, "network_interface", d.network_interface_);
            read_bool(table, "network_bits", d.network_bits_);
            read_bool(table, "network_binary", d.network_binary_);
            read_bool(table, "network_logarithmic", d.network_logarithmic_);
            read_bool(table, "network_start_zero", d.network_start_zero_);
            read_bool(table, "network_sync_scale", d.network_sync_scale_);
            if (read_integer(table, "network_scale", number)) {
                d.network_scale_ = static_cast<uint64_t>(number);
            }
            read_bool(table, "network_swap", d.network_swap_);
            read_bool(table, "memory_graphs", d.memory_graphs_);
            read_bool(table, "show_swap", d.show_swap_);
            read_bool(table, "show_battery", d.show_battery_);
            read_bool(table, "process_basename", d.process_basename_);
            read_bool(table, "group_processes", d.group_processes_);
            read_string(table, "overview_panel", d.overview_panel_);
            read_bool(table, "transparent_background", d.transparent_background_);
            read_string(table, "graph_symbol", d.graph_symbol_);
            read_bool(table, "cpu_lazy_sort", d.cpu_lazy_sort_);
            read_bool(table, "rounded_corners", d.rounded_corners_);
            read_bool(table, "process_colors", d.process_colors_);
            read_bool(table, "process_gradient", d.process_gradient_);
            read_bool(table, "graph_gradient", d.graph_gradient_);
            read_bool(table, "gpu_in_cpu_box", d.gpu_in_cpu_box_);
            read_bool(table, "multi_gpu_overview", d.multi_gpu_overview_);
            read_string(table, "graph_symbol_cpu", d.graph_symbol_cpu_);
            read_string(table, "graph_symbol_mem", d.graph_symbol_mem_);
            read_string(table, "graph_symbol_net", d.graph_symbol_net_);
            read_string(table, "graph_symbol_proc", d.graph_symbol_proc_);
            read_string(table, "graph_symbol_gpu", d.graph_symbol_gpu_);
        }
        read_bool(*root, "check_updates", value.check_updates_);
        if (const toml_value* ignored = root->get_member("ignored_update_version");
            ignored != nullptr && ignored->is_string()) {
            value.ignored_update_version_ = ignored->as_string()->get_value();
        }
        value.validate();
        return value;
    }

    void config::save(const string& config_path) const {
        validate();
        const auto parent = ::neforce::path(config_path).parent_path();
        if (!parent.empty()) {
            filesystem::create_directories(parent);
        }
        const string temporary = format("{}.{}.tmp", config_path, process::current_id());
        if (!write_text_file(temporary, to_toml())) {
            throw file_exception("Cannot write configuration");
        }
        if (!filesystem::rename(::neforce::path(temporary), ::neforce::path(config_path))) {
            fail(format("Cannot replace configuration: {}", config_path));
        }
    }

    double display_config::temperature_value(double celsius) const noexcept {
        if (temperature_unit_ == "fahrenheit") {
            return celsius * 1.8 + 32.0;
        }
        if (temperature_unit_ == "kelvin") {
            return celsius + 273.15;
        }
        return celsius;
    }

    string display_config::temperature_symbol() const {
        if (temperature_unit_ == "fahrenheit") {
            return "°F";
        }
        if (temperature_unit_ == "kelvin") {
            return " K";
        }
        return "°C";
    }

    string display_config::temperature(double celsius, size_t digits) const {
        string out = format_fixed(temperature_value(celsius), static_cast<int>(digits));
        out.append(temperature_symbol());
        return out;
    }

    string display_config::rate(double bytes_per_second) const {
        static constexpr const char* binary_byte[] = {"B/s", "KiB/s", "MiB/s", "GiB/s", "TiB/s"};
        static constexpr const char* decimal_byte[] = {"B/s", "kB/s", "MB/s", "GB/s", "TB/s"};
        static constexpr const char* binary_bit[] = {"bit/s", "Kibit/s", "Mibit/s", "Gibit/s", "Tibit/s"};
        static constexpr const char* decimal_bit[] = {"bit/s", "Kbit/s", "Mbit/s", "Gbit/s", "Tbit/s"};
        const char* const* units = nullptr;
        if (network_bits_) {
            units = network_binary_ ? binary_bit : decimal_bit;
        } else {
            units = network_binary_ ? binary_byte : decimal_byte;
        }
        double value = bytes_per_second < 0 ? 0 : bytes_per_second;
        if (network_bits_) {
            value *= 8.0;
        }
        const double base = network_binary_ ? 1024.0 : 1000.0;
        size_t index = 0;
        while (value >= base && index < 4) {
            value /= base;
            ++index;
        }
        return index == 0 ? format("{:.0f} {}", value, units[index]) : format("{:.1f} {}", value, units[index]);
    }

    vector<pair<string, string>> display_config::settings() const {
        vector<pair<string, string>> rows;
        rows.push_back({"CPU graph", cpu_graph_});
        rows.push_back({"CPU lower graph", cpu_graph_lower_});
        rows.push_back({"Temperature unit", temperature_unit_});
        rows.push_back({"CPU sensor", cpu_sensor_.empty() ? "Auto" : cpu_sensor_});
        rows.push_back({"Network interface", network_interface_.empty() ? "All visible" : network_interface_});
        rows.push_back({"Network bits/s", network_bits_ ? "true" : "false"});
        rows.push_back({"Network binary units", network_binary_ ? "true" : "false"});
        rows.push_back({"Network logarithmic", network_logarithmic_ ? "true" : "false"});
        rows.push_back({"Network totals since start", network_start_zero_ ? "true" : "false"});
        rows.push_back({"Network shared scale", network_sync_scale_ ? "true" : "false"});
        rows.push_back(
                {"Network graph maximum", network_scale_ == 0 ? "Auto" : rate(static_cast<double>(network_scale_))});
        rows.push_back({"Upload above download", network_swap_ ? "true" : "false"});
        rows.push_back({"Memory graphs", memory_graphs_ ? "true" : "false"});
        rows.push_back({"Show swap", show_swap_ ? "true" : "false"});
        rows.push_back({"Show battery", show_battery_ ? "true" : "false"});
        rows.push_back({"Process basename only", process_basename_ ? "true" : "false"});
        rows.push_back({"Group processes by program", group_processes_ ? "true" : "false"});
        rows.push_back({"Expanded overview panel", overview_panel_});
        rows.push_back({"Transparent background", transparent_background_ ? "true" : "false"});
        rows.push_back({"Graph symbols", graph_symbol_});
        rows.push_back({"Lazy CPU sort", cpu_lazy_sort_ ? "true" : "false"});
        rows.push_back({"Rounded corners", rounded_corners_ ? "true" : "false"});
        rows.push_back({"Process CPU colors", process_colors_ ? "true" : "false"});
        rows.push_back({"Process color gradient", process_gradient_ ? "true" : "false"});
        rows.push_back({"CPU graph symbol", graph_symbol_cpu_});
        rows.push_back({"Memory graph symbol", graph_symbol_mem_});
        rows.push_back({"Network graph symbol", graph_symbol_net_});
        rows.push_back({"Process graph symbol", graph_symbol_proc_});
        rows.push_back({"GPU graph symbol", graph_symbol_gpu_});
        rows.push_back({"Graph gradient", graph_gradient_ ? "true" : "false"});
        rows.push_back({"GPU in CPU box", gpu_in_cpu_box_ ? "true" : "false"});
        rows.push_back({"Multi-GPU overview", multi_gpu_overview_ ? "true" : "false"});
        return rows;
    }

    void display_config::adjust(size_t index, bool forward, const vector<string>& networks,
                                const vector<string>& sensors) {
        const auto cycle = [forward](string& value, const vector<string>& choices) {
            size_t current = 0;
            for (size_t i = 0; i < choices.size(); ++i) {
                if (choices[i] == value) {
                    current = i;
                    break;
                }
            }
            const size_t step = forward ? 1 : choices.size() - 1;
            value = choices[(current + step) % choices.size()];
        };
        const vector<string> cpu_fields = {"total", "user",    "system", "nice", "iowait",
                                           "irq",   "softirq", "steal",  "idle"};
        switch (index) {
            case 0:
                cycle(cpu_graph_, cpu_fields);
                break;
            case 1: {
                vector<string> fields = {"none"};
                for (const auto& field: cpu_fields) {
                    fields.push_back(field);
                }
                cycle(cpu_graph_lower_, fields);
                break;
            }
            case 2:
                cycle(temperature_unit_, {"celsius", "fahrenheit", "kelvin"});
                break;
            case 3:
            case 4: {
                vector<string> names = {""};
                for (const auto& name: index == 3 ? sensors : networks) {
                    names.push_back(name);
                }
                cycle(index == 3 ? cpu_sensor_ : network_interface_, names);
                break;
            }
            case 5:
                network_bits_ = !network_bits_;
                break;
            case 6:
                network_binary_ = !network_binary_;
                break;
            case 7:
                network_logarithmic_ = !network_logarithmic_;
                break;
            case 8:
                network_start_zero_ = !network_start_zero_;
                break;
            case 9:
                network_sync_scale_ = !network_sync_scale_;
                break;
            case 10: {
                constexpr uint64_t values[] = {0, 125000, 1250000, 12500000, 125000000, 1250000000, 12500000000ULL};
                size_t current = 0;
                for (size_t i = 0; i < 7; ++i) {
                    if (values[i] == network_scale_) {
                        current = i;
                        break;
                    }
                }
                const size_t step = forward ? 1 : 6;
                network_scale_ = values[(current + step) % 7];
                break;
            }
            case 11:
                network_swap_ = !network_swap_;
                break;
            case 12:
                memory_graphs_ = !memory_graphs_;
                break;
            case 13:
                show_swap_ = !show_swap_;
                break;
            case 14:
                show_battery_ = !show_battery_;
                break;
            case 15:
                process_basename_ = !process_basename_;
                break;
            case 16:
                group_processes_ = !group_processes_;
                break;
            case 17:
                cycle(overview_panel_, {"all", "cpu", "memory", "gpu", "storage", "network"});
                break;
            case 18:
                transparent_background_ = !transparent_background_;
                break;
            case 19:
                cycle(graph_symbol_, {"braille", "block", "tty", "ascii"});
                break;
            case 20:
                cpu_lazy_sort_ = !cpu_lazy_sort_;
                break;
            case 21:
                rounded_corners_ = !rounded_corners_;
                break;
            case 22:
                process_colors_ = !process_colors_;
                break;
            case 23:
                process_gradient_ = !process_gradient_;
                break;
            case 24:
                cycle(graph_symbol_cpu_, {"default", "braille", "block", "tty", "ascii"});
                break;
            case 25:
                cycle(graph_symbol_mem_, {"default", "braille", "block", "tty", "ascii"});
                break;
            case 26:
                cycle(graph_symbol_net_, {"default", "braille", "block", "tty", "ascii"});
                break;
            case 27:
                cycle(graph_symbol_proc_, {"default", "braille", "block", "tty", "ascii"});
                break;
            case 28:
                cycle(graph_symbol_gpu_, {"default", "braille", "block", "tty", "ascii"});
                break;
            case 29:
                graph_gradient_ = !graph_gradient_;
                break;
            case 30:
                gpu_in_cpu_box_ = !gpu_in_cpu_box_;
                break;
            case 31:
                multi_gpu_overview_ = !multi_gpu_overview_;
                break;
            default:
                break;
        }
    }

    void display_config::validate() const {
        const auto valid_symbol = [](const string& value) {
            return value == "braille" || value == "block" || value == "tty" || value == "ascii";
        };
        if (!valid_symbol(graph_symbol_)) {
            fail("Invalid graph symbol; expected braille, block, tty, or ascii");
        }
        for (const auto* value:
             {&graph_symbol_cpu_, &graph_symbol_mem_, &graph_symbol_net_, &graph_symbol_proc_, &graph_symbol_gpu_}) {
            if (*value != "default" && !valid_symbol(*value)) {
                fail("Invalid per-panel graph symbol");
            }
        }
        if (!(overview_panel_ == "all" || overview_panel_ == "cpu" || overview_panel_ == "memory" ||
              overview_panel_ == "gpu" || overview_panel_ == "storage" || overview_panel_ == "network")) {
            fail("Invalid overview panel");
        }
        const vector<string> fields = {"total", "user", "system", "nice", "iowait", "irq", "softirq", "steal", "idle"};
        bool upper_ok = false;
        for (const auto& field: fields) {
            if (field == cpu_graph_) {
                upper_ok = true;
                break;
            }
        }
        if (!upper_ok) {
            fail("Invalid CPU graph metric");
        }
        bool lower_ok = cpu_graph_lower_ == "none";
        for (const auto& field: fields) {
            if (field == cpu_graph_lower_) {
                lower_ok = true;
                break;
            }
        }
        if (!lower_ok) {
            fail("Invalid lower CPU graph metric");
        }
        if (!(temperature_unit_ == "celsius" || temperature_unit_ == "fahrenheit" || temperature_unit_ == "kelvin")) {
            fail("Invalid temperature unit");
        }
        for (const char c: network_interface_) {
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
                fail("Invalid network interface");
            }
        }
    }

    bool layout_node::contains_panel(const string& name) const {
        if (kind_ == kind::panel) {
            return panel_ == name;
        }
        for (const auto& child: children_) {
            if (child.contains_panel(name)) {
                return true;
            }
        }
        return false;
    }

} // namespace neforce::neflux
