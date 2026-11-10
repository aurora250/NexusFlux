            bool handle_mouse(const tui::mouse_event& event) {
                reconcile_current_selection();
                selection_restore_pending_ = false;
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
                                    settings_scroll_ = neforce::min(target, maximum);
                                    settings_free_scroll_ = true;
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
                            const int64_t target = static_cast<int64_t>(settings_scroll_) + delta;
                            settings_scroll_ = static_cast<size_t>(
                                    neforce::clamp(target, static_cast<int64_t>(0), static_cast<int64_t>(maximum)));
                            settings_free_scroll_ = true;
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
                                const size_t index = settings_scroll_ + static_cast<size_t>(row);
                                if (index < settings_rows().size()) {
                                    settings_index_ = index;
                                    settings_free_scroll_ = false;
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
                        if (hit.action_ == hit_action::gpu_process_row && hit.area_.contains(event.x, event.y)) {
                            if (select_gpu_process(hit.index_)) {
                                open_process_menu();
                            }
                            return true;
                        }
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
                    if (consume_detail_panel_outside_click(event)) {
                        return true;
                    }
                    for (auto hit = hits_.rbegin(); hit != hits_.rend(); ++hit) {
                        if (!hit->area_.contains(event.x, event.y)) {
                            continue;
                        }
                        if (hit->action_ == hit_action::none) {
                            return true;
                        }
                        if (hit->action_ == hit_action::tab && hit->index_ < view_count) {
                            switch_view(views[hit->index_].value);
                            return true;
                        }
                        if (hit->action_ == hit_action::view_overflow) {
                            open_view_menu();
                            return true;
                        }
                        if (hit->action_ == hit_action::vm_control) {
                            handle_vm_control(hit->index_);
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
                                    settings_free_scroll_ = false;
                                    popup_ = popup_kind::settings;
                                    settings_index_ = 0;
                                    settings_scroll_ = 0;
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
                                    settings_scroll_ = neforce::min(target, maximum);
                                    settings_free_scroll_ = true;
                                } else {
                                    selected_ = target;
                                    remember_current_list_selection();
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
                                    if (current_ == view::processes) {
                                        close_item_detail_side_panel();
                                        process_side_panel_ = true;
                                        process_panel_scroll_ = 0;
                                    } else {
                                        open_detail();
                                        show_item_detail_side_panel();
                                    }
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
                                remember_current_list_selection();
                                last_click_ms_ = monotonic_ms();
                                last_click_row_ = static_cast<int>(hit->index_);
                                if (double_click) {
                                    open_vm_side_panel();
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
                                remember_current_list_selection();
                                last_click_ms_ = monotonic_ms();
                                last_click_row_ = static_cast<int>(hit->index_);
                                if (double_click) {
                                    open_workload_detail(current_ == view::containers);
                                    show_item_detail_side_panel();
                                }
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::network_interface_row && current_ == view::network) {
                            const bool double_click = monotonic_ms() - last_click_ms_ < 400 &&
                                                      compare_equal(last_click_row_, hit->index_);
                            network_section_ = 1;
                            selected_ = hit->index_;
                            remember_current_list_selection();
                            last_click_ms_ = monotonic_ms();
                            last_click_row_ = static_cast<int>(hit->index_);
                            if (double_click) {
                                open_network_detail();
                                show_item_detail_side_panel();
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::connection_row && current_ == view::network) {
                            const bool double_click = monotonic_ms() - last_click_ms_ < 400 &&
                                                      compare_equal(last_click_row_, hit->index_);
                            network_section_ = 0;
                            selected_connection_ = hit->index_;
                            remember_current_list_selection();
                            last_click_ms_ = monotonic_ms();
                            last_click_row_ = static_cast<int>(hit->index_);
                            if (double_click) {
                                open_connection_detail();
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::storage_fs_row && current_ == view::storage) {
                            const bool double_click = monotonic_ms() - last_click_ms_ < 400 &&
                                                      compare_equal(last_click_row_, hit->index_);
                            storage_section_ = 0;
                            selected_ = hit->index_;
                            last_click_ms_ = monotonic_ms();
                            last_click_row_ = static_cast<int>(hit->index_);
                            if (double_click) {
                                open_storage_detail();
                                show_item_detail_side_panel();
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::storage_io_row && current_ == view::storage) {
                            const bool double_click = monotonic_ms() - last_click_ms_ < 400 &&
                                                      compare_equal(last_click_row_, hit->index_);
                            storage_section_ = 1;
                            storage_device_auto_ = false;
                            selected_io_ = hit->index_;
                            last_click_ms_ = monotonic_ms();
                            last_click_row_ = static_cast<int>(hit->index_);
                            if (double_click) {
                                open_storage_detail();
                                show_item_detail_side_panel();
                            }
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
                            selected_gpu_process_ = 0;
                            gpu_process_offset_ = 0;
                            if (current_ == view::overview) {
                                status_ = format("GPU {} selected · {} devices", hit->index_, snap_.gpus_.size());
                            }
                            return true;
                        }
                        if (hit->action_ == hit_action::gpu_process_row && current_ == view::gpu) {
                            const auto& device = snap_.gpus_[neforce::min(selected_gpu_, snap_.gpus_.size() - 1)];
                            for (size_t index = 0; index < device.processes_.size(); ++index) {
                                if (device.processes_[index].pid_ != hit->index_) {
                                    continue;
                                }
                                const bool double_click = monotonic_ms() - last_click_ms_ < 400 &&
                                                          compare_equal(last_click_row_, static_cast<int>(index));
                                selected_gpu_process_ = index;
                                for (const auto& process: snap_.processes_) {
                                    if (process.pid_ == hit->index_) {
                                        select_process(process);
                                        break;
                                    }
                                }
                                last_click_ms_ = monotonic_ms();
                                last_click_row_ = static_cast<int>(index);
                                if (double_click) {
                                    open_gpu_process_detail();
                                }
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
                    if (process_side_panel_ && process_panel_rect_.contains(event.x, event.y) &&
                        (current_ == view::processes || current_ == view::overview)) {
                        const int delta = event.button == tui::mouse_button::wheelup ? -3 : 3;
                        const int64_t target = static_cast<int64_t>(process_panel_scroll_) + delta;
                        process_panel_scroll_ = static_cast<size_t>(
                                neforce::clamp(target, static_cast<int64_t>(0),
                                               static_cast<int64_t>(process_panel_scroll_max_)));
                        return true;
                    }
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
                                    show_item_detail_side_panel();
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
                            const bool double_click = monotonic_ms() - last_click_ms_ < 400 && last_click_row_ == index;
                            network_section_ = 1;
                            selected_ = static_cast<size_t>(index);
                            last_click_ms_ = monotonic_ms();
                            last_click_row_ = index;
                            if (double_click) {
                                open_network_detail();
                                show_item_detail_side_panel();
                            }
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
                                    show_item_detail_side_panel();
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
                                    show_item_detail_side_panel();
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
                                        if (current_ == view::processes) {
                                            close_item_detail_side_panel();
                                            process_side_panel_ = true;
                                            process_panel_scroll_ = 0;
                                        } else {
                                            open_detail();
                                            show_item_detail_side_panel();
                                        }
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
                                    const bool double_click =
                                            monotonic_ms() - last_click_ms_ < 400 && last_click_row_ == index;
                                    storage_section_ = 0;
                                    selected_ = static_cast<size_t>(index);
                                    last_click_ms_ = monotonic_ms();
                                    last_click_row_ = index;
                                    if (double_click) {
                                        open_storage_detail();
                                        show_item_detail_side_panel();
                                    }
                                    return true;
                                }
                            }
                            if (storage_io_rect_.contains(event.x, event.y) && event.y > storage_io_rect_.y_ + 1) {
                                const int index = event.y - storage_io_rect_.y_ - 2;
                                if (index >= 0 && static_cast<size_t>(index) < snap_.disk_io_.size()) {
                                    const bool double_click =
                                            monotonic_ms() - last_click_ms_ < 400 && last_click_row_ == index;
                                    storage_section_ = 1;
                                    storage_device_auto_ = false;
                                    selected_io_ = static_cast<size_t>(index);
                                    last_click_ms_ = monotonic_ms();
                                    last_click_row_ = index;
                                    if (double_click) {
                                        open_storage_detail();
                                        show_item_detail_side_panel();
                                    }
                                    return true;
                                }
                            }
                        }
                    }
                }
                return false;
            }

