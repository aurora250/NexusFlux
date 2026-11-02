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
                const rect inner = area.inset(1);
                if (inner.h_ < 3 || inner.w_ < 20) {
                    return;
                }
                struct panel_line {
                    string left_;
                    string right_;
                    color left_color_{};
                    color right_color_{};
                    bool split_ = false;
                    bool heading_ = false;
                };
                vector<panel_line> lines;
                const int label_width = neforce::min(18, neforce::max(11, inner.w_ * 30 / 100));
                const int value_width = neforce::max(8, inner.w_ - label_width - 1);
                const auto add_line = [&](string text, color tint) {
                    panel_line line;
                    line.left_ = move(text);
                    line.left_color_ = tint;
                    lines.push_back(move(line));
                };
                const auto add_wrapped_line = [&](string_view text, color tint) {
                    const auto parts = wrap_display(text, neforce::max(8, inner.w_ - 2));
                    if (parts.empty()) {
                        add_line("—", colors_.muted_);
                        return;
                    }
                    for (const auto& part: parts) {
                        add_line(part, tint);
                    }
                };
                const auto add_heading = [&](string text) {
                    panel_line line;
                    line.left_ = move(text);
                    line.left_color_ = colors_.mint_;
                    line.heading_ = true;
                    lines.push_back(move(line));
                };
                const auto add_field = [&](string label, string field_value, color tint) {
                    const auto parts = wrap_display(field_value.view(), value_width);
                    panel_line line;
                    line.left_ = move(label);
                    line.left_color_ = colors_.muted_;
                    line.right_color_ = tint;
                    line.split_ = true;
                    if (parts.empty()) {
                        line.right_ = "—";
                        lines.push_back(move(line));
                        return;
                    }
                    line.right_ = parts.front();
                    lines.push_back(move(line));
                    for (size_t index = 1; index < parts.size(); ++index) {
                        panel_line continuation;
                        continuation.left_color_ = colors_.muted_;
                        continuation.right_ = parts[index];
                        continuation.right_color_ = tint;
                        continuation.split_ = true;
                        lines.push_back(move(continuation));
                    }
                };

                add_wrapped_line(format("PID {} · Selected", static_cast<uint64_t>(item.pid_)).view(), colors_.mint_);
                add_wrapped_line(item.name_.empty() ? string_view("—") : item.name_.view(), colors_.text_);
                add_wrapped_line(
                        format("CPU {:.1f}% · RAM {}", item.cpu_, bytes(static_cast<double>(item.memory_))).view(),
                        colors_.muted_);
                add_wrapped_line(
                        format("GPU {} · VRAM {}",
                               item.gpu_percent_.has_value() ? format("{}%", *item.gpu_percent_) : string("—"),
                               item.gpu_memory_.has_value() ? bytes(static_cast<double>(*item.gpu_memory_))
                                                            : string("—"))
                                .view(),
                        colors_.muted_);
                add_heading("");
                add_heading("LABELS");
                string labels;
                for (const auto& tag: item.tags_) {
                    if (!labels.empty()) {
                        labels.push_back(' ');
                    }
                    labels.append(format("[{}]", tag.display()));
                }
                add_wrapped_line(labels.empty() ? string_view("—") : labels.view(), colors_.blue_);
                add_heading("");
                add_heading("COMMAND");
                const auto command_lines = wrap_display(item.command_.empty() ? item.name_.view() : item.command_.view(),
                                                        neforce::max(8, inner.w_ - 2));
                if (command_lines.empty()) {
                    add_line("—", colors_.muted_);
                } else {
                    for (const auto& part: command_lines) {
                        add_line(part, colors_.text_);
                    }
                }
                add_heading("");
                add_heading("TABLE FIELDS");
                add_field("FIELD", "VALUE", colors_.muted_);
                for (const auto& column: active_columns()) {
                    const auto cell = column_value(item, column.key_);
                    add_field(column.title_, cell.first, cell.second);
                }
                add_heading("");
                add_heading("ALL SAMPLED FIELDS");
                add_field("FIELD", "VALUE", colors_.muted_);
                for (const auto& column: column_catalog()) {
                    const auto cell = column_value(item, column.key_);
                    add_field(column.title_, cell.first, cell.second);
                }

                const bool show_charts = inner.h_ >= 14;
                const int fixed_height = show_charts ? neforce::min(12, neforce::max(8, inner.h_ * 38 / 100)) : 0;
                const int top_bottom = inner.bottom() - fixed_height;
                const int hint_y = top_bottom - 1;
                const int content_bottom = hint_y;
                const int visible = neforce::max(0, content_bottom - inner.y_);
                process_panel_scroll_max_ = lines.size() > static_cast<size_t>(visible)
                                                    ? lines.size() - static_cast<size_t>(visible)
                                                    : 0;
                process_panel_scroll_ = neforce::min(process_panel_scroll_, process_panel_scroll_max_);
                int row = inner.y_;
                for (size_t index = process_panel_scroll_; index < lines.size() && row < content_bottom; ++index, ++row) {
                    const auto& line = lines[index];
                    if (line.split_) {
                        brush.text(inner.x_, row,
                                   truncate_display(line.left_.view(), label_width).view(), line.left_color_,
                                   none, line.heading_);
                        if (label_width + 1 < inner.w_) {
                            brush.text(inner.x_ + label_width + 1, row,
                                       truncate_display(line.right_.view(), value_width).view(),
                                       line.right_color_);
                        }
                    } else {
                        brush.text(inner.x_, row, truncate_display(line.left_.view(), inner.w_).view(),
                                   line.left_color_, none, line.heading_);
                    }
                }
                if (hint_y >= inner.y_) {
                    brush.text(inner.x_, hint_y,
                               truncate_display("Drag select · Ctrl+C · Wheel scroll", inner.w_).view(),
                               colors_.muted_);
                }
                if (process_panel_scroll_max_ > 0) {
                    const rect scrollbar{inner.right() - 1, inner.y_, 1,
                                         neforce::max(1, content_bottom - inner.y_)};
                    brush.scrollbar(scrollbar, lines.size(), static_cast<size_t>(visible), process_panel_scroll_);
                }
                if (!show_charts || fixed_height < 8) {
                    return;
                }
                brush.text(inner.x_, top_bottom,
                           truncate_display(format("Since selection · {} samples", process_history_.cpu_.size()).view(),
                                            inner.w_)
                                   .view(),
                           colors_.mint_);
                const auto has_finite = [](const deque<double>& values) {
                    for (const double value: values) {
                        if (neforce::is_finite(value)) {
                            return true;
                        }
                    }
                    return false;
                };
                const auto max_finite = [](const deque<double>& values) {
                    double result = 1.0;
                    for (const double value: values) {
                        if (neforce::is_finite(value)) {
                            result = neforce::max(result, value);
                        }
                    }
                    return tui::canvas_chart_nice_max(result);
                };
                const rect charts{inner.x_, top_bottom + 1, inner.w_, inner.bottom() - top_bottom - 1};
                if (charts.h_ < 4 || charts.w_ < 8) {
                    return;
                }
                const int row_height = neforce::max(3, charts.h_ / 2);
                const int column_width = charts.w_ / 2;
                const auto chart_window = process_history_.cpu_.empty()
                                                  ? this->chart_window()
                                                  : history_.window(process_history_.cpu_.size());
                const auto draw_chart = [&](const rect& cell, const string& title, const deque<double>& values,
                                            double maximum, const string& unit, const string& empty_text, color tint) {
                    if (cell.h_ < 3 || cell.w_ < 8) {
                        return;
                    }
                    brush.box(cell, format(" {} ", title).view(), tint);
                    if (!has_finite(values)) {
                        brush.text(cell.x_ + 1, cell.y_ + 1,
                                   truncate_display(empty_text.view(), cell.w_ - 2).view(), colors_.muted_);
                        return;
                    }
                    graph_painter(brush, cfg_.display_.graph_symbol_proc_)
                            .trend(cell.inset(1), {chart_series{"value", &values, tint}}, maximum, chart_window,
                                   unit.view(), false);
                };
                const rect cpu_chart{charts.x_, charts.y_, column_width - 1, row_height};
                const rect ram_chart{charts.x_ + column_width, charts.y_, charts.w_ - column_width, row_height};
                const rect gpu_chart{charts.x_, charts.y_ + row_height, column_width - 1,
                                     charts.h_ - row_height};
                const rect vram_chart{charts.x_ + column_width, charts.y_ + row_height,
                                      charts.w_ - column_width, charts.h_ - row_height};
                draw_chart(cpu_chart, "CPU (%)", process_history_.cpu_, 100.0, "%", "No samples", colors_.mint_);
                draw_chart(ram_chart, "RAM (B)", process_history_.ram_, max_finite(process_history_.ram_), "B",
                           "No samples", colors_.blue_);
                draw_chart(gpu_chart, "GPU (%)", process_history_.gpu_, 100.0, "%",
                           "No samples · unavailable", colors_.amber_);
                draw_chart(vram_chart, "VRAM (B)", process_history_.vram_, max_finite(process_history_.vram_), "B",
                           "No samples · unavailable", colors_.purple_);
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
                restore_process_selection();
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
                if (process_side_panel_ && area.w_ >= 96 && selected_process_.has_value()) {
                    const int panel_width = neforce::clamp(area.w_ * 30 / 100, 40, 48);
                    const int left = neforce::max(56, area.w_ - panel_width);
                    const auto view = layout_process_table(rect{table.x_, table.y_, left - 1, table.h_}, rows);
                    paint_process_table(brush, view);
                    process_panel_rect_ = rect{table.x_ + left, table.y_, table.w_ - left, table.h_};
                    draw_process_side_panel(brush, process_panel_rect_);
                } else {
                    process_panel_rect_ = rect{};
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
                        {"pid", "PID", 6, true},
                        {"ppid", "PPID", 7, true},
                        {"user", "USER", 9, false},
                        {"uid", "UID", 6, true},
                        {"cpu", "CPU%", 6, true},
                        {"cpu_normalized", "CPU/T%", 8, true},
                        {"memory", "MEMORY", 9, true},
                        {"memory_percent", "RAM%", 6, true},
                        {"vms", "VMS", 9, true},
                        {"shared", "SHARED", 9, true},
                        {"vram", "GPU-MEM", 9, true},
                        {"gpu", "GPU", 5, true},
                        {"state", "STATE", 13, false},
                        {"name", "NAME", 20, false},
                        {"command", "COMMAND", 0, false},
                        {"comm", "COMM", 16, false},
                        {"threads", "THR", 5, true},
                        {"priority", "PRI", 5, true},
                        {"nice", "NICE", 5, true},
                        {"processor", "CPU#", 5, true},
                        {"runtime", "ELAPSED", 10, false},
                        {"time", "CPU TIME", 10, false},
                        {"read", "READ", 10, true},
                        {"write", "WRITE", 10, true},
                        {"read_total", "TOTAL READ", 11, true},
                        {"write_total", "TOTAL WRITE", 11, true},
                        {"rx", "RX/s", 10, true},
                        {"tx", "TX/s", 10, true},
                        {"tag", "TAGS", 18, false},
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
                    for (const char* key:
                         {"pid", "user", "cpu", "memory", "vram", "gpu", "read", "write", "state", "command", "tag"}) {
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
                    const json* value = item.extra_.get("shared_bytes");
                    return {value != nullptr && (value->is_integer() || value->is_number())
                                    ? bytes(value->as_number())
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
                    const json* comm = item.extra_.get("comm");
                    return {comm != nullptr && comm->is_string() ? comm->as_string() : item.name_, colors_.muted_};
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
                    key == "major_faults") {
                    const auto value = extra_text(item, "stat", key.view().data());
                    return {value.has_value() ? *value : string("—"), colors_.muted_};
                }
                if (key == "utime" || key == "stime") {
                    const auto value = extra_text(item, "stat", key == "utime" ? "utime_ticks" : "stime_ticks");
                    return {value.has_value() ? *value : string("—"), colors_.muted_};
                }
                if (key == "rt_priority") {
                    const auto value = extra_text(item, "stat", "rt_priority");
                    return {value.has_value() ? *value : string("—"), colors_.muted_};
                }
                if (key == "pss" || key == "uss" || key == "swap") {
                    if (key == "uss") {
                        const auto clean = extra_text(item, "smaps_rollup", "Private_Clean");
                        const auto dirty = extra_text(item, "smaps_rollup", "Private_Dirty");
                        const auto number = [](const optional<string>& text) -> uint64_t {
                            if (!text.has_value()) {
                                return 0;
                            }
                            const auto fields = split_whitespace(text->view());
                            return fields.empty() ? 0 : parse_u64(fields[0].view()).value_or(0) * 1024;
                        };
                        if (!clean.has_value() && !dirty.has_value()) {
                            return {"—", colors_.purple_};
                        }
                        return {bytes(static_cast<double>(number(clean) + number(dirty))), colors_.purple_};
                    }
                    const char* field = key == "pss" ? "Pss" : "Swap";
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
                    const json* value = item.extra_.get("oom");
                    return {value != nullptr ? (value->is_string() ? value->as_string() : value->dump()) : string("—"),
                            colors_.muted_};
                }
                if (key == "cgroup" || key == "wchan" || key == "security" || key == "exe" || key == "cwd" ||
                    key == "io_priority") {
                    const json* value = item.extra_.get(key);
                    if (value == nullptr || value->is_null()) {
                        return {"—", colors_.muted_};
                    }
                    const string text = value->is_string() ? value->as_string() : value->dump();
                    return {truncate_display(text.view(), 60), colors_.muted_};
                }
                if (key == "autogroup") {
                    const json* value = item.extra_.get("autogroup");
                    return {value != nullptr ? (value->is_string() ? value->as_string() : value->dump()) : string("—"),
                            colors_.muted_};
                }
                if (key == "fd_count") {
                    const json* value = item.extra_.get("fd_count");
                    return {value != nullptr ? (value->is_string() ? value->as_string() : value->dump()) : string("—"),
                            colors_.muted_};
                }
                if (key == "context_switches") {
                    const auto voluntary = extra_text(item, "status", "voluntary_ctxt_switches");
                    const auto involuntary = extra_text(item, "status", "nonvoluntary_ctxt_switches");
                    if (!voluntary.has_value() && !involuntary.has_value()) {
                        return {"—", colors_.muted_};
                    }
                    const uint64_t total = (voluntary.has_value() ? parse_u64(voluntary->view()).value_or(0) : 0) +
                                           (involuntary.has_value() ? parse_u64(involuntary->view()).value_or(0) : 0);
                    return {format("{}", total), colors_.muted_};
                }
                if (key == "sid" || key == "pgrp" || key == "tty" || key == "tpgid" || key == "last_cpu" ||
                    key == "policy") {
                    const char* field = key == "sid"        ? "session"
                                        : key == "last_cpu" ? "processor"
                                        : key == "policy"   ? "scheduling"
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
                    const json* value = item.extra_.get(key == "oom_score" ? "oom" : "oom_adj");
                    return {value != nullptr ? (value->is_string() ? value->as_string() : value->dump())
                                             : string("—"),
                            colors_.muted_};
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
                const int fill_width =
                        has_fill ? neforce::max(8, area.w_ - fixed - static_cast<int>(columns.size()) - 2) : 0;
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

