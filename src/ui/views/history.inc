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

