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
                restore_process_selection();
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
                const auto boxes = active_overview_boxes();
                if (boxes.empty()) {
                    brush.box(area, " OVERVIEW ", colors_.muted_);
                    brush.text(area.x_ + 2, area.y_ + 1, "No overview boxes in the active preset.", colors_.muted_);
                    return;
                }
                const bool show_cpu = overview_has_box(boxes, "cpu");
                const bool show_processes = overview_has_box(boxes, "processes");
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
                const auto draw_cards = [&](const rect& cards) {
                    if (cards.h_ < 4 || cards.w_ < 10 || card_names.empty()) {
                        return;
                    }
                    int columns = 1;
                    if (cards.w_ >= 120) {
                        columns = neforce::min(4, static_cast<int>(card_names.size()));
                    } else if (cards.w_ >= 56) {
                        columns = 2;
                    }
                    columns = neforce::max(1, columns);
                    const int count = static_cast<int>(card_names.size());
                    int rows = (count + columns - 1) / columns;
                    if (cards.h_ < rows * 4 && columns < count && cards.w_ / count >= 10) {
                        columns = count;
                        rows = 1;
                    }
                    const int cell_height = neforce::max(4, cards.h_ / rows);
                    for (int index = 0; index < count; ++index) {
                        const int column = index % columns;
                        const int row = index / columns;
                        const int x = cards.x_ + column * (cards.w_ / columns);
                        const int width = column + 1 == columns
                                                  ? cards.right() - x
                                                  : cards.w_ / columns - 1;
                        const int y = cards.y_ + row * cell_height;
                        if (y >= cards.bottom()) {
                            break;
                        }
                        const int height = row + 1 == rows ? cards.bottom() - y : cell_height;
                        if (height < 4 || width < 10) {
                            continue;
                        }
                        const string_view name = card_names[static_cast<size_t>(index)];
                        if (name == "memory") {
                            draw_memory_panel(brush, rect{x, y, width, height});
                        } else if (name == "gpu") {
                            draw_gpu_card(brush, rect{x, y, width, height});
                        } else if (name == "storage") {
                            draw_storage_card(brush, rect{x, y, width, height});
                        } else {
                            draw_network_card(brush, rect{x, y, width, height});
                        }
                    }
                };

                const bool show_memory = overview_has_box(boxes, "memory");
                if (area.w_ < 56 || area.h_ < 20) {
                    if (show_cpu) {
                        if (show_memory && area.h_ >= 14) {
                            const int cpu_height = neforce::clamp(area.h_ * 60 / 100, 8, area.h_ - 5);
                            draw_cpu_panel(brush, rect{area.x_, area.y_, area.w_, cpu_height});
                            draw_memory_panel(brush,
                                              rect{area.x_, area.y_ + cpu_height + 1, area.w_,
                                                   area.h_ - cpu_height - 1});
                        } else {
                            draw_cpu_panel(brush, area);
                        }
                    } else if (show_processes) {
                        paint_process_table(brush, layout_process_table(area, process_rows()));
                    } else {
                        draw_cards(area);
                    }
                    return;
                }
                const int total_height = area.h_;
                const int core_rows = static_cast<int>((snap_.cores_.size() + 1) / 2);
                int cursor_y = area.y_;
                int cpu_height = 0;
                if (show_cpu) {
                    const int target = neforce::max(9, neforce::min(core_rows + 5, 24));
                    if (card_names.empty() && !show_processes) {
                        cpu_height = total_height;
                    } else if (show_processes && total_height >= 34) {
                        cpu_height = neforce::clamp(target, 10, neforce::max(10, total_height * 55 / 100));
                    } else if (total_height < 32) {
                        cpu_height = neforce::clamp(total_height * 55 / 100, 8, 14);
                    } else {
                        cpu_height = neforce::clamp(target, 10, neforce::max(10, total_height * 55 / 100));
                    }
                    cpu_height = neforce::min(cpu_height, total_height);
                    draw_cpu_panel(brush, rect{area.x_, cursor_y, area.w_, cpu_height});
                    cursor_y += cpu_height + 1;
                }
                if (card_names.empty() && !show_processes) {
                    if (!show_cpu) {
                        brush.box(area, " OVERVIEW ", colors_.muted_);
                        brush.text(area.x_ + 2, area.y_ + 1, "No overview boxes selected.", colors_.muted_);
                    }
                    return;
                }
                int process_height = 0;
                if (show_processes && total_height >= 36) {
                    process_height = neforce::clamp(total_height / 4, 8, 16);
                }
                int cards_height =
                        area.bottom() - cursor_y - process_height - (process_height > 0 ? 1 : 0);
                if (card_names.empty()) {
                    cards_height = 0;
                }
                if (cards_height >= 4) {
                    draw_cards(rect{area.x_, cursor_y, area.w_, cards_height});
                    cursor_y += cards_height + 1;
                }
                if (process_height > 0) {
                    const int process_y = cursor_y;
                    process_height = area.bottom() - process_y;
                    if (process_height >= 4) {
                        paint_process_table(
                                brush, layout_process_table(rect{area.x_, process_y, area.w_, process_height}, process_rows()));
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

            void draw_core_grid(painter& brush, const rect& area, size_t max_columns = 0, string_view cpu_times = {}) {
                if (area.h_ < 3 || area.w_ < 8) {
                    return;
                }
                const int count = static_cast<int>(snap_.cores_.size());
                const string title =
                        count > 0 ? format("Per-core history · {}", cpu_core_summary()) : string("Per-core history");
                const int status_width = cpu_times.empty() ? 0 : display_width(cpu_times);
                brush.text(area.x_, area.y_,
                           truncate_display(title.view(), neforce::max(0, area.w_ - status_width - 1)).view(),
                           colors_.muted_);
                if (!cpu_times.empty()) {
                    brush.text_right(area.x_, area.y_, area.w_, cpu_times, colors_.muted_);
                }
                if (count == 0) {
                    return;
                }
                const size_t rows_available = static_cast<size_t>(area.h_ - 1);
                size_t columns = static_cast<size_t>(neforce::max(1, (count + static_cast<int>(rows_available) - 1) /
                                                                             static_cast<int>(rows_available)));
                if (max_columns > 0) {
                    columns = neforce::min(columns, max_columns);
                }
                const int rows = (count + static_cast<int>(columns) - 1) / static_cast<int>(columns);
                const int cell_width = neforce::max(1, area.w_ / static_cast<int>(columns));
                for (int index = 0; index < count; ++index) {
                    const int column = index / rows;
                    const int row = index % rows;
                    if (static_cast<size_t>(column) >= columns) {
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
                const auto time_value = [&](const char* key) -> optional<double> {
                    const json* value = snap_.cpu_breakdown_.get(key);
                    if (value == nullptr || (!value->is_number() && !value->is_integer())) {
                        return none;
                    }
                    return value->as_number();
                };
                string cpu_times_status;
                const auto user_time = time_value("user");
                const auto system_time = time_value("system");
                const auto iowait_time = time_value("iowait");
                const auto steal_time = time_value("steal");
                if (user_time.has_value() || system_time.has_value() || iowait_time.has_value() ||
                    steal_time.has_value()) {
                    const auto text = [](const optional<double>& value) {
                        return value.has_value() ? format("{:.1f}", *value) : string("—");
                    };
                    cpu_times_status = format("CPU TIMES u{} s{} io{} st{}", text(user_time), text(system_time),
                                              text(iowait_time), text(steal_time));
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
                    draw_core_grid(brush, rect{inner.x_, grid_top, inner.w_, content_bottom - grid_top},
                                   snap_.cores_.size() > 16 ? 2 : 0, cpu_times_status.view());
                } else {
                    draw_core_grid(brush,
                                   rect{inner.x_ + inner.w_ - core_width, inner.y_ + 1, core_width,
                                        content_bottom - inner.y_ - 1},
                                   snap_.cores_.size() > 16 ? 2 : 0, cpu_times_status.view());
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
                    if (y + 4 < inner.bottom()) {
                        const auto& device = snap_.gpus_[chart_index];
                        const auto found = history_.gpu_details_.find(device.uuid_);
                        if (found != history_.gpu_details_.end()) {
                            const int available = inner.bottom() - y;
                            const int half = neforce::max(2, available / 2);
                            graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                    .trend(rect{inner.x_, y, inner.w_, half},
                                           {chart_series{"GPU", &found->second.utilization_, colors_.blue_}}, 100.0,
                                           chart_window(), "%", false);
                            graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                    .trend(rect{inner.x_, y + half, inner.w_, inner.bottom() - y - half},
                                           {chart_series{"VRAM", &found->second.vram_, colors_.purple_}}, 100.0,
                                           chart_window(), "%", false);
                            brush.text(inner.x_, y, "GPU", colors_.blue_);
                            brush.text(inner.x_, y + half, "VRAM", colors_.purple_);
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
                if (y + 4 < inner.bottom()) {
                    const auto found = history_.gpu_details_.find(device.uuid_);
                    if (found != history_.gpu_details_.end()) {
                        const int available = inner.bottom() - y;
                        const int half = neforce::max(2, available / 2);
                        graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                .trend(rect{inner.x_, y, inner.w_, half},
                                       {chart_series{"GPU", &found->second.utilization_, colors_.blue_}}, 100.0,
                                       chart_window(), "%", false);
                        graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                .trend(rect{inner.x_, y + half, inner.w_, inner.bottom() - y - half},
                                       {chart_series{device.memory_label(), &found->second.vram_, colors_.purple_}},
                                       100.0, chart_window(), "%", false);
                        brush.text(inner.x_, y, "GPU", colors_.blue_);
                        brush.text(inner.x_, y + half, device.memory_label().view(), colors_.purple_);
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
                    ++y;
                }
                if (y + 4 < inner.bottom() && (!history_.disk_read_.empty() || !history_.disk_write_.empty())) {
                    const int available = inner.bottom() - y;
                    const int half = neforce::max(2, available / 2);
                    const auto peak = [](const deque<double>& values) {
                        double maximum = 1.0;
                        for (const double value: values) {
                            if (neforce::is_finite(value)) {
                                maximum = neforce::max(maximum, value);
                            }
                        }
                        return tui::canvas_chart_nice_max(maximum);
                    };
                    graph_painter(brush, cfg_.display_.graph_symbol_proc_)
                            .trend(rect{inner.x_, y, inner.w_, half},
                                   {chart_series{"Read", &history_.disk_read_, colors_.mint_}},
                                   peak(history_.disk_read_), chart_window(), "B/s", false);
                    graph_painter(brush, cfg_.display_.graph_symbol_proc_)
                            .trend(rect{inner.x_, y + half, inner.w_, inner.bottom() - y - half},
                                   {chart_series{"Write", &history_.disk_write_, colors_.blue_}},
                                   peak(history_.disk_write_), chart_window(), "B/s", false);
                    brush.text(inner.x_, y, "R", colors_.mint_);
                    brush.text(inner.x_, y + half, "W", colors_.blue_);
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

