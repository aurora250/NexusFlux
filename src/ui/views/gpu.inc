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
                const bool rich = area.h_ >= 26 && area.w_ >= 100;

                string driver;
                string cuda;
                string nvml;
                size_t total_processes = 0;
                for (const auto& item: snap_.gpus_) {
                    if (driver.empty() && !item.driver_.empty()) {
                        driver = item.driver_;
                    }
                    if (cuda.empty() && item.cuda_driver_version_.has_value()) {
                        cuda = *item.cuda_driver_version_;
                    }
                    if (nvml.empty() && item.nvml_version_.has_value()) {
                        nvml = *item.nvml_version_;
                    }
                    total_processes += item.processes_.size();
                }
                int top = area.y_;
                if (rich) {
                    const rect header{area.x_, top, area.w_, 4};
                    brush.box(header,
                              format(" GPU TELEMETRY · {} devices · {} processes ",
                                     static_cast<uint64_t>(snap_.gpus_.size()), static_cast<uint64_t>(total_processes))
                                      .view(),
                              colors_.blue_);
                    const string versions =
                            format("Driver {} · CUDA {} · NVML {} · status {}", driver.empty() ? "—" : driver,
                                   cuda.empty() ? "—" : cuda, nvml.empty() ? "—" : nvml,
                                   snap_.gpu_status_.empty() ? "ok" : snap_.gpu_status_);
                    brush.text(header.x_ + 2, header.y_ + 1, truncate_display(versions.view(), header.w_ - 4).view(),
                               colors_.muted_);
                    string names;
                    for (const auto& item: snap_.gpus_) {
                        if (!names.empty()) {
                            names.append(" · ");
                        }
                        names.append(format("GPU{} {}", item.id_, item.name_.empty() ? item.vendor_ : item.name_));
                    }
                    brush.text(header.x_ + 2, header.y_ + 2, truncate_display(names.view(), header.w_ - 4).view(),
                               colors_.text_);
                    top += 4;
                }

                const int card_height = neforce::max(5, neforce::min(rich ? 7 : 9, (area.bottom() - top) / 3));
                const size_t page_start = index / 3 * 3;
                const size_t page_end = neforce::min(snap_.gpus_.size(), page_start + 3);
                const int card_width = neforce::max(28, area.w_ / static_cast<int>(page_end - page_start));
                int x = area.x_;
                const auto fan_text = [](const gpu& item) -> string {
                    if (item.fan_.has_value() && *item.fan_ > 0) {
                        return format("{}%", *item.fan_);
                    }
                    if (const json* rpm = item.extra_.get("fan_rpm");
                        rpm != nullptr && (rpm->is_integer() || rpm->is_number()) && rpm->as_integer() > 0) {
                        return format("{} rpm", rpm->as_integer());
                    }
                    return "—";
                };
                const auto memory_text = [](const gpu& item) -> string {
                    if (item.memory_used_.has_value() && item.memory_total_.has_value()) {
                        return format("{} {} / {}", item.memory_label(),
                                      item.memory_used_.has_value() && *item.memory_used_ > 0
                                              ? bytes(static_cast<double>(*item.memory_used_))
                                              : string("—"),
                                      bytes(static_cast<double>(*item.memory_total_)));
                    }
                    return "—";
                };
                for (size_t i = page_start; i < page_end; ++i) {
                    const auto& item = snap_.gpus_[i];
                    const rect card{x, top, card_width - 1, card_height};
                    const bool active = i == index;
                    hits_.push_back({card, hit_action::gpu_device, i});
                    brush.box(card, format(" GPU {} · {} ", item.id_, item.vendor_).view(),
                              active ? colors_.blue_ : colors_.muted_);
                    if (card.h_ > 2) {
                        brush.text(card.x_ + 2, card.y_ + 1, truncate_display(item.name_.view(), card.w_ - 4).view(),
                                   item.utilization_.has_value() ? colors_.text_ : colors_.muted_);
                    }
                    const bool has_telemetry = item.utilization_.has_value() || item.memory_total_.has_value() ||
                                               item.temperature_.has_value() || item.clock_mhz_.has_value();
                    if (card.h_ > 3) {
                        const string line =
                                has_telemetry
                                        ? format("UTIL {} · {} · {}",
                                                 item.utilization_.has_value() ? format("{}%", *item.utilization_)
                                                                               : string("—"),
                                                 memory_text(item),
                                                 item.temperature_.has_value()
                                                         ? cfg_.display_.temperature(*item.temperature_, 0)
                                                         : string("—"))
                                        : (item.process_status_.empty() ? snap_.gpu_status_ : item.process_status_);
                        brush.text(card.x_ + 2, card.y_ + 2, truncate_display(line.view(), card.w_ - 4).view(),
                                   colors_.muted_);
                    }
                    if (card.h_ > 4 && has_telemetry) {
                        const string power = item.power_watts_.has_value() && *item.power_watts_ > 0.0F
                                                     ? format("{:.0f}/", *item.power_watts_) +
                                                               (item.power_limit_watts_.has_value()
                                                                        ? format("{:.0f} W", *item.power_limit_watts_)
                                                                        : string("— W"))
                                                     : string("— W");
                        const string line = format(
                                "{} · {} · fan {} · {} proc · core {} · MEM {}", item.performance_state_.value_or("—"),
                                power, fan_text(item), static_cast<uint64_t>(item.processes_.size()),
                                item.clock_mhz_.has_value() ? format("{} MHz", *item.clock_mhz_) : string("—"),
                                item.memory_clock_mhz_.has_value() ? format("{} MHz", *item.memory_clock_mhz_)
                                                                   : string("—"));
                        brush.text(card.x_ + 2, card.y_ + 3, truncate_display(line.view(), card.w_ - 4).view(),
                                   colors_.muted_);
                    }
                    if (card.h_ > 5) {
                        brush.track(card.x_ + 2, card.y_ + 4, neforce::max(1, card.w_ - 4),
                                    item.utilization_.has_value() ? optional<double>(*item.utilization_) : none, 100.0,
                                    colors_.blue_);
                    }
                    x += card_width;
                }
                const auto process_by_pid = [&] {
                    map<uint32_t, const process*> result;
                    for (const auto& item: snap_.processes_) {
                        result[item.pid_] = &item;
                    }
                    return result;
                }();
                const auto gpu_history = history_.gpu_details_.find(device.uuid_);
                const auto series_max = [](const deque<double>& values) {
                    double result = 0.0;
                    for (const double value: values) {
                        result = neforce::max(result, value);
                    }
                    return result;
                };
                const auto draw_gpu_processes = [&](const rect& table) {
                    if (table.h_ < 3 || table.w_ < 40) {
                        return;
                    }
                    gpu_process_rect_ = table;
                    brush.box(table, format(" GPU {} · {} processes ", device.id_, device.processes_.size()).view(),
                              colors_.blue_);
                    const auto columns = active_columns();
                    if (columns.empty()) {
                        return;
                    }
                    int fixed = 0;
                    bool has_fill = false;
                    for (const auto& item: columns) {
                        fixed += item.width_;
                        has_fill = has_fill || item.width_ == 0;
                    }
                    const int fill_width =
                            has_fill ? neforce::max(8, table.w_ - fixed - static_cast<int>(columns.size()) - 5) : 0;
                    int x = table.x_ + 2;
                    for (const auto& column: columns) {
                        const int width = column.width_ == 0 ? fill_width : column.width_;
                        if (x + width + 1 > table.right() - 2) {
                            break;
                        }
                        const string title = align_cell(column.title_, width, column.right_);
                        brush.text(x, table.y_ + 1, truncate_display(title.view(), width + 1).view(), colors_.muted_);
                        x += width + 1;
                    }
                    if (device.processes_.empty()) {
                        brush.text(table.x_ + 2, table.y_ + 2,
                                   device.process_status_.empty() ? string_view("No processes")
                                                                  : device.process_status_.view(),
                                   colors_.muted_);
                        return;
                    }
                    const int visible = neforce::max(0, table.h_ - 3);
                    selected_gpu_process_ = neforce::min(selected_gpu_process_, device.processes_.size() - 1);
                    if (selected_gpu_process_ >= gpu_process_offset_ + static_cast<size_t>(visible)) {
                        gpu_process_offset_ = selected_gpu_process_ + 1 - static_cast<size_t>(visible);
                    }
                    gpu_process_offset_ = neforce::min(gpu_process_offset_, selected_gpu_process_);
                    int row = table.y_ + 2;
                    for (size_t index = gpu_process_offset_;
                         index < device.processes_.size() && row < table.bottom() - 1; ++index, ++row) {
                        const auto& item = device.processes_[index];
                        const process* owner = nullptr;
                        const auto found = process_by_pid.find(item.pid_);
                        if (found != process_by_pid.end()) {
                            owner = found->second;
                        }
                        process value;
                        if (owner != nullptr) {
                            value = *owner;
                        } else {
                            value.pid_ = item.pid_;
                            value.name_ = "—";
                            value.command_ = "—";
                            value.user_ = "—";
                            value.state_ = "GPU";
                        }
                        value.gpu_memory_ = item.memory_;
                        value.gpu_percent_ = item.utilization_;
                        const bool active = index == selected_gpu_process_;
                        if (active) {
                            brush.fill(rect{table.x_ + 1, row, table.w_ - 2, 1}, " ", colors_.text_,
                                       colors_.selection_);
                        }
                        hits_.push_back({{table.x_ + 1, row, table.w_ - 2, 1}, hit_action::gpu_process_row, item.pid_});
                        int cx = table.x_ + 2;
                        for (const auto& column: columns) {
                            const int width = column.width_ == 0 ? fill_width : column.width_;
                            if (cx + width + 1 > table.right() - 2) {
                                break;
                            }
                            const auto cell = column_value(value, column.key_);
                            const string text = align_cell(cell.first, width, column.right_);
                            brush.text(cx, row, truncate_display(text.view(), width + 1).view(),
                                       active ? colors_.text_ : cell.second);
                            cx += width + 1;
                        }
                    }
                };

                if (gpu_section_ == 2) {
                    const rect details{area.x_, top + card_height, area.w_, area.bottom() - top - card_height};
                    if (details.h_ < 5) {
                        return;
                    }
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
                    rows.push_back({"NVML", device.nvml_version_.value_or("—")});
                    rows.push_back({"CUDA", device.cuda_driver_version_.value_or("—")});
                    rows.push_back(
                            {"GPU", device.utilization_.has_value() ? format("{}%", *device.utilization_) : "—"});
                    rows.push_back({"Memory", memory_text(device)});
                    rows.push_back({"Memory bus", device.memory_utilization_.has_value()
                                                          ? format("{}%", *device.memory_utilization_)
                                                          : "—"});
                    rows.push_back({"Temperature", device.temperature_.has_value()
                                                           ? cfg_.display_.temperature(*device.temperature_, 0)
                                                           : "—"});
                    rows.push_back({"Fan", fan_text(device)});
                    rows.push_back({"Power", device.power_watts_.has_value()
                                                     ? format("{:.0f} / {:.0f} W", *device.power_watts_,
                                                              device.power_limit_watts_.value_or(*device.power_watts_))
                                                     : "—"});
                    rows.push_back(
                            {"Clock", device.clock_mhz_.has_value() ? format("{} MHz", *device.clock_mhz_) : "—"});
                    rows.push_back({"Memory clock", device.memory_clock_mhz_.has_value()
                                                            ? format("{} MHz", *device.memory_clock_mhz_)
                                                            : "—"});
                    rows.push_back({"PCIe", device.pcie_rx_kib_.has_value() && device.pcie_tx_kib_.has_value()
                                                    ? format("RX {} / TX {}",
                                                             bytes(static_cast<double>(*device.pcie_rx_kib_) * 1024.0),
                                                             bytes(static_cast<double>(*device.pcie_tx_kib_) * 1024.0))
                                                    : "—"});
                    if (const json* generation = device.extra_.get("pcie_link_generation");
                        generation != nullptr && (generation->is_integer() || generation->is_number())) {
                        const auto width = device.extra_.get("pcie_link_width");
                        rows.push_back(
                                {"PCIe link", format("Gen{} x{}", generation->as_integer(),
                                                     width != nullptr && (width->is_integer() || width->is_number())
                                                             ? width->as_integer()
                                                             : 0)});
                    }
                    if (const json* vbios = device.extra_.get("vbios_version");
                        vbios != nullptr && vbios->is_string()) {
                        rows.push_back({"VBIOS", vbios->as_string()});
                    }
                    if (const json* maximum = device.extra_.get("max_clock_mhz");
                        maximum != nullptr && (maximum->is_integer() || maximum->is_number())) {
                        rows.push_back({"Max clock", format("{} MHz", maximum->as_integer())});
                    }
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
                    draw_gpu_processes(processes);
                    return;
                }

                int metrics_height = 0;
                if (rich) {
                    const int remaining = area.bottom() - (top + card_height);
                    metrics_height = neforce::max(0, neforce::min(14, remaining - 6));
                    if (metrics_height >= 8) {
                        const int row_height = metrics_height / 2;
                        const int column_width = (area.w_ + 3) / 4;
                        const auto panel = [&](int column, int row, const string& title, color tint) {
                            const rect box{area.x_ + column * column_width, top + card_height + row * row_height,
                                           column_width - (column == 3 ? 0 : 1), row_height};
                            brush.box(box, format(" {} ", title).view(), tint);
                            return box.inset(1);
                        };
                        const auto panel_line = [&](const rect& inner, int& y, const string& text, color tint) {
                            if (y >= inner.bottom()) {
                                return;
                            }
                            brush.text(inner.x_, y, truncate_display(text.view(), inner.w_).view(), tint);
                            ++y;
                        };
                        const auto pstate = device.performance_state_.value_or("—");
                        {
                            const rect inner = panel(0, 0, "GPU LOAD", colors_.blue_);
                            int y = inner.y_;
                            panel_line(inner, y, format("{:3}%  {}", device.utilization_.value_or(0), pstate),
                                       colors_.text_);
                            if (y < inner.bottom()) {
                                brush.track(inner.x_, y, inner.w_,
                                            device.utilization_.has_value() ? optional<double>(*device.utilization_)
                                                                            : none,
                                            100.0, colors_.blue_);
                                ++y;
                            }
                            if (gpu_history != history_.gpu_details_.end() && y + 2 < inner.bottom()) {
                                graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                        .trend(rect{inner.x_, y, inner.w_, inner.bottom() - y},
                                               {chart_series{"GPU", &gpu_history->second.utilization_, colors_.blue_}},
                                               100.0, chart_window(), "%", false);
                            }
                        }
                        {
                            const rect inner = panel(1, 0, "VRAM", colors_.purple_);
                            int y = inner.y_;
                            panel_line(inner, y, memory_text(device), colors_.text_);
                            panel_line(inner, y,
                                       format("{:.0f}% used · bus {}%", device.memory_percent().value_or(0.0),
                                              device.memory_utilization_.value_or(0)),
                                       colors_.muted_);
                            if (y < inner.bottom()) {
                                brush.track(inner.x_, y, inner.w_,
                                            device.memory_percent().has_value() ? device.memory_percent() : none, 100.0,
                                            colors_.purple_);
                                ++y;
                            }
                            if (gpu_history != history_.gpu_details_.end() && y + 2 < inner.bottom()) {
                                graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                        .trend(rect{inner.x_, y, inner.w_, inner.bottom() - y},
                                               {chart_series{"VRAM", &gpu_history->second.vram_, colors_.purple_}},
                                               100.0, chart_window(), "%", false);
                            }
                        }
                        {
                            const rect inner = panel(2, 0, "ENGINES", colors_.amber_);
                            int y = inner.y_;
                            panel_line(inner, y,
                                       format("ENC {}%  DEC {}%", device.encoder_.value_or(0),
                                              device.decoder_.value_or(0)),
                                       colors_.text_);
                            panel_line(inner, y,
                                       format("MEM {}% · {} proc", device.memory_utilization_.value_or(0),
                                              device.processes_.size()),
                                       colors_.muted_);
                            if (gpu_history != history_.gpu_details_.end() && y + 2 < inner.bottom()) {
                                graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                        .trend(rect{inner.x_, y, inner.w_, inner.bottom() - y},
                                               {chart_series{"ENC", &gpu_history->second.encoder_, colors_.mint_},
                                                chart_series{"DEC", &gpu_history->second.decoder_, colors_.blue_}},
                                               100.0, chart_window(), "%", false);
                            }
                        }
                        {
                            const rect inner = panel(3, 0, "POWER", colors_.mint_);
                            int y = inner.y_;
                            panel_line(inner, y,
                                       device.power_watts_.has_value() && *device.power_watts_ > 0.0F
                                               ? format("{:.0f} / {:.0f} W", *device.power_watts_,
                                                        device.power_limit_watts_.value_or(*device.power_watts_))
                                               : string("— W"),
                                       colors_.text_);
                            panel_line(inner, y, format("{} · fan {}", pstate, fan_text(device)), colors_.muted_);
                            if (gpu_history != history_.gpu_details_.end() && y + 2 < inner.bottom()) {
                                graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                        .trend(rect{inner.x_, y, inner.w_, inner.bottom() - y},
                                               {chart_series{"W", &gpu_history->second.power_, colors_.mint_}},
                                               neforce::max(1.0,
                                                            static_cast<double>(device.power_limit_watts_.value_or(1))),
                                               chart_window(), "W", false);
                            }
                        }
                        {
                            const rect inner = panel(0, 1, "TEMPERATURE", colors_.red_);
                            int y = inner.y_;
                            panel_line(inner, y,
                                       device.temperature_.has_value()
                                               ? cfg_.display_.temperature(*device.temperature_, 0)
                                               : string("—"),
                                       colors_.text_);
                            panel_line(inner, y, format("fan {} · {}", fan_text(device), pstate), colors_.muted_);
                            if (gpu_history != history_.gpu_details_.end() && y + 2 < inner.bottom()) {
                                graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                        .trend(rect{inner.x_, y, inner.w_, inner.bottom() - y},
                                               {chart_series{"°C", &gpu_history->second.temperature_, colors_.red_}},
                                               110.0, chart_window(), "°C", false);
                            }
                        }
                        {
                            const rect inner = panel(1, 1, "PCIE", colors_.blue_);
                            int y = inner.y_;
                            panel_line(inner, y,
                                       format("RX {}",
                                              device.pcie_rx_kib_.has_value()
                                                      ? format("{}/s", bytes(static_cast<double>(*device.pcie_rx_kib_) *
                                                                             1024.0))
                                                      : string("—")),
                                       colors_.mint_);
                            panel_line(inner, y,
                                       format("TX {}",
                                              device.pcie_tx_kib_.has_value()
                                                      ? format("{}/s", bytes(static_cast<double>(*device.pcie_tx_kib_) *
                                                                             1024.0))
                                                      : string("—")),
                                       colors_.blue_);
                            if (const json* generation = device.extra_.get("pcie_link_generation");
                                generation != nullptr && (generation->is_integer() || generation->is_number())) {
                                const auto width = device.extra_.get("pcie_link_width");
                                panel_line(inner, y,
                                           format("Gen{} x{}", generation->as_integer(),
                                                  width != nullptr && (width->is_integer() || width->is_number())
                                                          ? width->as_integer()
                                                          : 0),
                                           colors_.muted_);
                            }
                            if (gpu_history != history_.gpu_details_.end() && y + 2 < inner.bottom()) {
                                graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                        .trend(rect{inner.x_, y, inner.w_, inner.bottom() - y},
                                               {chart_series{"RX", &gpu_history->second.pcie_rx_, colors_.mint_},
                                                chart_series{"TX", &gpu_history->second.pcie_tx_, colors_.blue_}},
                                               neforce::max(1.0,
                                                            neforce::max(series_max(gpu_history->second.pcie_rx_),
                                                                         series_max(gpu_history->second.pcie_tx_)) *
                                                                    1.1),
                                               chart_window(), "KiB/s", false);
                            }
                        }
                        {
                            const rect inner = panel(2, 1, "CLOCKS", colors_.purple_);
                            int y = inner.y_;
                            panel_line(inner, y,
                                       device.clock_mhz_.has_value() ? format("CORE {} MHz", *device.clock_mhz_)
                                                                     : string("CORE —"),
                                       colors_.text_);
                            panel_line(inner, y,
                                       device.memory_clock_mhz_.has_value()
                                               ? format("MEM {} MHz", *device.memory_clock_mhz_)
                                               : string("MEM —"),
                                       colors_.muted_);
                            if (const json* maximum = device.extra_.get("max_clock_mhz");
                                maximum != nullptr && (maximum->is_integer() || maximum->is_number())) {
                                panel_line(inner, y, format("MAX {} MHz", maximum->as_integer()), colors_.muted_);
                            }
                            if (gpu_history != history_.gpu_details_.end() && y + 2 < inner.bottom()) {
                                graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                        .trend(rect{inner.x_, y, inner.w_, inner.bottom() - y},
                                               {chart_series{"CORE", &gpu_history->second.clock_, colors_.purple_},
                                                chart_series{"MEM", &gpu_history->second.memory_clock_, colors_.blue_}},
                                               3000.0, chart_window(), "MHz", false);
                            }
                        }
                        {
                            const rect inner = panel(3, 1, "DEVICE", colors_.muted_);
                            int y = inner.y_;
                            panel_line(inner, y, format("Driver {}", device.driver_.empty() ? "—" : device.driver_),
                                       colors_.text_);
                            panel_line(inner, y,
                                       format("NVML {} · CUDA {}", device.nvml_version_.value_or("—"),
                                              device.cuda_driver_version_.value_or("—")),
                                       colors_.muted_);
                            if (const json* vbios = device.extra_.get("vbios_version");
                                vbios != nullptr && vbios->is_string()) {
                                panel_line(inner, y, format("VBIOS {}", vbios->as_string()), colors_.muted_);
                            }
                            if (!device.process_status_.empty()) {
                                panel_line(inner, y, device.process_status_, colors_.muted_);
                            }
                        }
                        metrics_height = row_height * 2;
                    } else {
                        metrics_height = 0;
                    }
                } else {
                    const auto found = history_.gpu_details_.find(device.uuid_);
                    const int chart_height = neforce::max(0, neforce::min(10, area.bottom() - top - card_height - 5));
                    if (found != history_.gpu_details_.end() && chart_height >= 4) {
                        graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                .chart(rect{area.x_, top + card_height, area.w_ / 2 - 1, chart_height},
                                       "GPU utilization",
                                       {chart_series{"GPU", &found->second.utilization_, colors_.blue_}}, 100.0, "%",
                                       chart_window());
                        graph_painter(brush, cfg_.display_.graph_symbol_gpu_)
                                .chart(rect{area.x_ + area.w_ / 2, top + card_height, area.w_ / 2, chart_height},
                                       format("VRAM ({})", device.memory_label()).view(),
                                       {chart_series{"VRAM", &found->second.vram_, colors_.purple_}}, 100.0, "%",
                                       chart_window());
                        metrics_height = chart_height;
                    }
                }

                const rect table{area.x_, top + card_height + metrics_height, area.w_,
                                 area.bottom() - top - card_height - metrics_height};
                draw_gpu_processes(table);
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

