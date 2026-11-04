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
                                                     static_cast<uint64_t>(cfg_.collection_.interval_ms_))});
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
                const auto breakdown_value = [&](const char* key) -> optional<double> {
                    const json* value = snap_.cpu_breakdown_.get(key);
                    if (value == nullptr || (!value->is_number() && !value->is_integer())) {
                        return none;
                    }
                    return value->as_number();
                };
                const int cpu_times_height = neforce::max(7, right.h_ * 22 / 100);
                const rect cpu_times{right.x_, right.y_, right.w_, cpu_times_height};
                brush.box(cpu_times, " CPU TIMES ", colors_.blue_);
                if (cpu_times.h_ > 2) {
                    const pair<const char*, color> rows[] = {
                            {"user", colors_.mint_},
                            {"system", colors_.blue_},
                            {"iowait", colors_.amber_},
                            {"steal", colors_.red_},
                    };
                    const bool has_breakdown =
                            breakdown_value("user").has_value() || breakdown_value("system").has_value() ||
                            breakdown_value("iowait").has_value() || breakdown_value("steal").has_value();
                    if (!has_breakdown) {
                        brush.text(cpu_times.x_ + 2, cpu_times.y_ + 1, "CPU time breakdown unavailable",
                                   colors_.muted_);
                    } else {
                        int cpu_row = cpu_times.y_ + 1;
                        const int bar_x = cpu_times.x_ + 21;
                        const int bar_width = neforce::max(1, cpu_times.w_ - 27);
                        for (const auto& item: rows) {
                            if (cpu_row >= cpu_times.bottom() - 1) {
                                break;
                            }
                            const auto value = breakdown_value(item.first);
                            brush.text(cpu_times.x_ + 2, cpu_row, item.first, colors_.muted_);
                            brush.text(cpu_times.x_ + 11, cpu_row,
                                       (value.has_value() ? format("{:5.1f}%", *value) : string("    —")).view(),
                                       value.has_value() ? item.second : colors_.muted_);
                            if (value.has_value()) {
                                brush.track(bar_x, cpu_row, bar_width, value, 100.0, item.second);
                            }
                            ++cpu_row;
                        }
                    }
                }
                const int sensors_height = neforce::max(7, right.h_ * 24 / 100);
                const rect sensors{right.x_, cpu_times.bottom(), right.w_, sensors_height};
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
                const int pressure_height = neforce::max(5, (right.h_ - cpu_times_height - sensors_height) * 55 / 100);
                const rect pressure{right.x_, sensors.bottom(), right.w_, pressure_height};
                brush.box(pressure, " PRESSURE ", colors_.blue_);
                if (pressure.h_ > 2) {
                    constexpr int resource_width = 10;
                    constexpr int pressure_width = 10;
                    const bool pressure_wide = pressure.w_ >= 72;
                    string pressure_header =
                            align_cell("RESOURCE", resource_width) + align_cell("SOME10", pressure_width, true);
                    if (pressure_wide) {
                        pressure_header += align_cell("SOME60", pressure_width, true) +
                                           align_cell("SOME300", pressure_width, true);
                    }
                    pressure_header += align_cell("FULL10", pressure_width, true);
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
                        string line =
                                align_cell(item.resource_, resource_width) +
                                align_cell(item.some_10_.has_value() ? format("{:.2f}%", *item.some_10_) : string("—"),
                                           pressure_width, true);
                        if (pressure_wide) {
                            line += align_cell(item.some_60_.has_value() ? format("{:.2f}%", *item.some_60_)
                                                                         : string("—"),
                                               pressure_width, true) +
                                    align_cell(item.some_300_.has_value() ? format("{:.2f}%", *item.some_300_)
                                                                          : string("—"),
                                               pressure_width, true);
                        }
                        line += align_cell(item.full_10_.has_value() ? format("{:.2f}%", *item.full_10_) : string("—"),
                                           pressure_width, true);
                        brush.text(pressure.x_ + 2, row, line.view(), hot ? colors_.red_ : colors_.muted_);
                        ++row;
                    }
                }
                const rect listeners{right.x_, pressure.bottom(), right.w_,
                                     right.h_ - cpu_times_height - sensors_height - pressure_height};
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

