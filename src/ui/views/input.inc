            void activate_popup() {
                reconcile_current_selection();
                selection_restore_pending_ = false;
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
                        settings_free_scroll_ = false;
                        popup_ = popup_kind::settings;
                        settings_scroll_ = 0;
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
                } else if (action == "vm_side_panel") {
                    popup_ = popup_kind::none;
                    sync_vm_edit();
                    vm_side_panel_ = !vm_side_panel_;
                    status_ = vm_side_panel_ ? "VM side panel on · D close" : "VM side panel off";
                } else if (action == "vm_apply_config") {
                    popup_ = popup_kind::none;
                    apply_vm_config();
                } else if (action == "vm_autostart") {
                    popup_ = popup_kind::none;
                    toggle_vm_autostart();
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
                    status_ = follow_process_ ? "Follow highlight on · F off" : "Follow highlight off";
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
                } else if (action == "mark_process") {
                    toggle_mark_selected();
                    popup_ = popup_kind::none;
                } else if (action == "mark_descendants") {
                    mark_selected_descendants();
                    popup_ = popup_kind::none;
                } else if (action == "clear_marks") {
                    clear_process_marks();
                    popup_ = popup_kind::none;
                } else if (action == "own_only") {
                    own_only_ = !own_only_;
                    status_ = own_only_ ? "Showing own processes only" : "Showing all users";
                    popup_ = popup_kind::none;
                } else if (action == "gpu_only") {
                    gpu_only_ = !gpu_only_;
                    status_ = gpu_only_ ? "Showing GPU processes only" : "Showing all processes";
                    popup_ = popup_kind::none;
                } else if (action == "threads") {
                    cfg_.collection_.show_threads_ = !cfg_.collection_.show_threads_;
                    status_ = cfg_.collection_.show_threads_ ? "Threads shown" : "Threads hidden";
                    popup_ = popup_kind::none;
                } else if (action == "kernel_threads") {
                    cfg_.collection_.show_kernel_threads_ = !cfg_.collection_.show_kernel_threads_;
                    status_ = cfg_.collection_.show_kernel_threads_ ? "Kernel threads shown" : "Kernel threads hidden";
                    popup_ = popup_kind::none;
                } else if (action == "columns") {
                    open_columns_menu();
                } else if (action == "sort_cpu" || action == "sort_memory") {
                    sort_index_ = action == "sort_memory" ? 1 : 0;
                    reverse_ = true;
                    sync_sort_config();
                    popup_ = popup_kind::none;
                } else if (action == "sort") {
                    open_sort_menu();
                } else if (action == "process_side_panel") {
                    process_side_panel_ = !process_side_panel_;
                    status_ = process_side_panel_ ? "Process side panel on · D close" : "Process side panel off";
                    popup_ = popup_kind::none;
                } else if (action == "labels") {
                    popup_ = popup_kind::none;
                    show_process_labels();
                } else if (action == "filter_label") {
                    filter_by_label();
                } else if (action == "io_priority") {
                    popup_ = popup_kind::prompt;
                    popup_title_ = "I/O priority (0..7)";
                    prompt_action_ = prompt_action::io_priority;
                    prompt_text_ = "4";
                    prompt_cursor_ = prompt_text_.size();
                } else if (action == "file_locks") {
                    popup_ = popup_kind::none;
                    show_process_locks();
                } else if (action == "trace_process") {
                    popup_ = popup_kind::none;
                    show_process_trace();
                } else if (action == "backtrace_process") {
                    popup_ = popup_kind::none;
                    show_process_backtrace();
                } else if (action == "diagnose_process") {
                    popup_ = popup_kind::none;
                    show_process_diagnostics();
                } else if (action == "kernel_messages") {
                    popup_ = popup_kind::none;
                    show_kernel_messages();
                } else if (action == "follow") {
                    popup_ = popup_kind::none;
                    follow_process_ = true;
                    status_ = "Follow highlight on · F off";
                } else if (action == "pause") {
                    popup_ = popup_kind::none;
                    process_paused_ = !process_paused_;
                    status_ = process_paused_ ? "Process list paused · P resume" : "Process list resumed";
                } else if (action == "terminate") {
                    popup_ = popup_kind::none;
                    request_signal(system_signal::signal::terminate, "Terminate");
                } else if (action == "kill_process") {
                    popup_ = popup_kind::none;
                    request_signal(system_signal::signal::kill, "Kill");
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
                } else if (action == "net_labels") {
                    popup_ = popup_kind::none;
                    show_connection_labels();
                } else if (action == "net_filter_label") {
                    filter_connection_label();
                } else if (action == "net_toggle_loopback") {
                    popup_ = popup_kind::none;
                    cfg_.show_loopback_ = !cfg_.show_loopback_;
                    status_ = cfg_.show_loopback_ ? "Loopback interfaces shown" : "Loopback interfaces hidden";
                } else if (action == "net_sort_rx" || action == "net_sort_tx" || action == "net_sort_name") {
                    if (network_section_ == 1) {
                        interface_sort_ = action == "net_sort_tx" ? 1 : (action == "net_sort_name" ? 2 : 0);
                    } else {
                        network_sort_ = action == "net_sort_tx" ? 1 : 0;
                    }
                    popup_ = popup_kind::none;
                } else if (action == "net_sort_state") {
                    if (network_section_ == 1) {
                        interface_sort_ = 3;
                    } else {
                        network_sort_ = 2;
                    }
                    popup_ = popup_kind::none;
                } else if (action == "net_sort_speed") {
                    interface_sort_ = 4;
                    popup_ = popup_kind::none;
                } else if (action == "net_sort_local") {
                    network_sort_ = 3;
                    popup_ = popup_kind::none;
                } else if (action == "net_sort_pid") {
                    network_sort_ = 4;
                    popup_ = popup_kind::none;
                } else if (action == "net_toggle_section") {
                    network_section_ = network_section_ == 0 ? 1 : 0;
                    selected_ = 0;
                    selected_connection_ = 0;
                    selection_restore_pending_ = true;
                    status_ = network_section_ == 0 ? "Network: connections" : "Network: interfaces";
                    popup_ = popup_kind::none;
                } else if (action == "net_routes") {
                    popup_ = popup_kind::none;
                    show_network_routes();
                } else if (action == "net_diagnose") {
                    popup_ = popup_kind::none;
                    show_network_diagnostics();
                } else if (action == "net_test") {
                    string target;
                    const auto rows = filtered_connections();
                    if (!rows.empty() && selected_connection_ < rows.size()) {
                        const auto& item = snap_.connections_.connections_[rows[selected_connection_]];
                        const string& endpoint = item.state_ == "LISTEN" ? item.local_ : item.remote_;
                        const auto colon = endpoint.rfind(':');
                        if (colon != string::npos) {
                            string host = endpoint.substr(0, colon);
                            const string port = endpoint.substr(colon + 1);
                            if (host == "0.0.0.0" || host == "*" || host.empty()) {
                                host = "127.0.0.1";
                            } else if (host == "::" || host == "[::]") {
                                host = "[::1]";
                            }
                            if (port != "0") {
                                target = host + ":" + port;
                            }
                        }
                    }
                    popup_ = popup_kind::prompt;
                    popup_title_ = "Test URL or host:port";
                    prompt_action_ = prompt_action::network_target;
                    prompt_text_ = target;
                    prompt_cursor_ = prompt_text_.size();
                } else if (action == "net_capture") {
                    popup_ = popup_kind::prompt;
                    popup_title_ = "Packet capture output (.pcap)";
                    prompt_action_ = prompt_action::capture_path;
                    prompt_text_ = "/tmp/nexus-flux-capture.pcap";
                    prompt_cursor_ = prompt_text_.size();
                } else if (action == "net_open_capture") {
                    popup_ = popup_kind::prompt;
                    popup_title_ = "Open PCAP file";
                    prompt_action_ = prompt_action::capture_open;
                    prompt_text_ = "/tmp/nexus-flux-capture.pcap";
                    prompt_cursor_ = prompt_text_.size();
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
                    settings_free_scroll_ = false;
                    popup_ = popup_kind::settings;
                    settings_index_ = 0;
                    settings_scroll_ = 0;
                } else if (action == "sort_cpu" || action == "sort_memory") {
                    sort_index_ = action == "sort_memory" ? 1 : 0;
                    reverse_ = true;
                    sync_sort_config();
                    popup_ = popup_kind::none;
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
                        const auto value = entries[popup_selected_].first;
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
                                : (cfg_.time_window_seconds_ * 1000 + cfg_.collection_.interval_ms_ - 1) / cfg_.collection_.interval_ms_;
                cfg_.history_ = neforce::max(cfg_.history_,
                                             static_cast<size_t>(neforce::min(needed, static_cast<uint64_t>(3600))));
                popup_ = popup_kind::none;
            }

            void switch_view(view target) {
                if (source_ != nullptr && !replay_) {
                    activate_source(target);
                }
                current_ = target;
                selected_ = 0;
                table_offset_ = 0;
                free_scroll_ = false;
                storage_device_auto_ = true;
                detail_open_ = false;
                selection_restore_pending_ = true;
                close_item_detail_side_panel();
                process_side_panel_ = false;
                workload_side_panel_ = false;
                vm_side_panel_ = false;
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
                            static_cast<int64_t>(cfg_.collection_.interval_ms_) + static_cast<int64_t>(direction * 250);
                    cfg_.collection_.interval_ms_ = static_cast<uint64_t>(
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
                    cfg_.collection_.show_threads_ = !cfg_.collection_.show_threads_;
                } else if (settings_index_ == 7) {
                    cfg_.collection_.show_kernel_threads_ = !cfg_.collection_.show_kernel_threads_;
                } else if (settings_index_ == 8) {
                    cfg_.show_loopback_ = !cfg_.show_loopback_;
                } else if (settings_index_ == 9) {
                    cfg_.collection_.collect_gpu_ = !cfg_.collection_.collect_gpu_;
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
                } else if (settings_index_ == 17) {
                    sort_index_ = (sort_index_ + (direction > 0 ? 1 : sizeof(sorts) / sizeof(sorts[0]) - 1)) %
                                  (sizeof(sorts) / sizeof(sorts[0]));
                    reverse_ = !sorts[sort_index_].descending;
                    sync_sort_config();
                } else if (settings_index_ >= 18) {
                    vector<string> networks;
                    for (const auto& item: snap_.networks_) {
                        networks.push_back(item.name_);
                    }
                    vector<string> sensors;
                    for (const auto& item: snap_.sensors_) {
                        sensors.push_back(item.name_);
                    }
                    cfg_.display_.adjust(settings_index_ - 18, direction > 0, networks, sensors);
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
                        pending_signal_active_ = false;
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
                        } else if (prompt_action_ == prompt_action::network_target) {
                            run_network_check(prompt_text_);
                        } else if (prompt_action_ == prompt_action::capture_path) {
                            run_network_capture(prompt_text_);
                        } else if (prompt_action_ == prompt_action::capture_open) {
                            show_capture_file(prompt_text_);
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
                        settings_free_scroll_ = false;
                        if (settings_index_ > 0) {
                            --settings_index_;
                        }
                        return true;
                    }
                    if (key == tui::key_event::type::down) {
                        settings_free_scroll_ = false;
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
                        settings_free_scroll_ = false;
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
                reconcile_current_selection();
                selection_restore_pending_ = false;
                const auto key = event.key;
                if (key == tui::key_event::type::up || key == tui::key_event::type::down ||
                    key == tui::key_event::type::page_up || key == tui::key_event::type::page_down ||
                    key == tui::key_event::type::home || key == tui::key_event::type::end) {
                    free_scroll_ = false;
                }
                if (list_view() && (key == tui::key_event::type::up || key == tui::key_event::type::down ||
                                    key == tui::key_event::type::page_up || key == tui::key_event::type::page_down ||
                                    key == tui::key_event::type::home || key == tui::key_event::type::end)) {
                    if (key == tui::key_event::type::home) {
                        const size_t count = current_list_count();
                        current_list_index() = 0;
                        if (count > 0) {
                            remember_current_list_selection();
                        }
                    } else if (key == tui::key_event::type::end) {
                        const size_t count = current_list_count();
                        current_list_index() = count == 0 ? 0 : count - 1;
                        if (count > 0) {
                            remember_current_list_selection();
                        }
                    } else {
                        const int delta = key == tui::key_event::type::up
                                                  ? -1
                                                  : (key == tui::key_event::type::down
                                                             ? 1
                                                             : (key == tui::key_event::type::page_up ? -20 : 20));
                        move_current_list(delta);
                    }
                    return true;
                }
                if (current_ == view::gpu &&
                    (key == tui::key_event::type::up || key == tui::key_event::type::down ||
                     key == tui::key_event::type::page_up || key == tui::key_event::type::page_down ||
                     key == tui::key_event::type::home || key == tui::key_event::type::end)) {
                    const auto& gpu_rows = snap_.gpus_[neforce::min(selected_gpu_, snap_.gpus_.size() - 1)];
                    const size_t count = gpu_rows.processes_.size();
                    if (count == 0) {
                        selected_gpu_process_ = 0;
                    } else if (key == tui::key_event::type::home) {
                        selected_gpu_process_ = 0;
                    } else if (key == tui::key_event::type::end) {
                        selected_gpu_process_ = count - 1;
                    } else {
                        const int delta = key == tui::key_event::type::up
                                                  ? -1
                                                  : (key == tui::key_event::type::down
                                                             ? 1
                                                             : (key == tui::key_event::type::page_up ? -10 : 10));
                        const int64_t next = static_cast<int64_t>(selected_gpu_process_) + delta;
                        selected_gpu_process_ = static_cast<size_t>(
                                neforce::clamp(next, static_cast<int64_t>(0), static_cast<int64_t>(count) - 1));
                    }
                    return true;
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
                if (item_detail_panel_ && key == tui::key_event::type::printable && event.cp == U'D') {
                    close_item_detail_side_panel();
                    return true;
                }
                if (current_ == view::vms && key == tui::key_event::type::printable) {
                    const auto vm_key = event.cp;
                    if (vm_key == U'D') {
                        sync_vm_edit();
                        vm_side_panel_ = !vm_side_panel_;
                        status_ = vm_side_panel_ ? "VM side panel on · D close" : "VM side panel off";
                        return true;
                    }
                    if (vm_key == U's') {
                        handle_vm_control(0);
                        return true;
                    }
                    if (vm_key == U'x') {
                        handle_vm_control(1);
                        return true;
                    }
                    if (vm_key == U'X') {
                        handle_vm_control(3);
                        return true;
                    }
                    if (vm_key == U'r') {
                        handle_vm_control(2);
                        return true;
                    }
                    if (vm_key == U'a') {
                        toggle_vm_autostart();
                        return true;
                    }
                    if (vm_key == U'w') {
                        apply_vm_config();
                        return true;
                    }
                    if (vm_key == U'+' || vm_key == U'=') {
                        adjust_vm_vcpus(1);
                        return true;
                    }
                    if (vm_key == U'-' || vm_key == U'_') {
                        adjust_vm_vcpus(-1);
                        return true;
                    }
                    if (vm_key == U']') {
                        adjust_vm_memory(256);
                        return true;
                    }
                    if (vm_key == U'[') {
                        adjust_vm_memory(-256);
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
                    if (item_detail_panel_) {
                        close_item_detail_side_panel();
                        return true;
                    }
                    if (vm_side_panel_ && current_ == view::vms) {
                        vm_side_panel_ = false;
                        vm_side_panel_rect_ = rect{};
                        return true;
                    }
                    if ((current_ == view::processes || current_ == view::overview) && process_side_panel_) {
                        process_side_panel_ = false;
                        process_panel_rect_ = rect{};
                        return true;
                    }
                    if (current_ == view::services && workload_side_panel_) {
                        workload_side_panel_ = false;
                        workload_side_panel_rect_ = rect{};
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
                    settings_free_scroll_ = false;
                    popup_ = popup_kind::settings;
                    settings_index_ = 0;
                    settings_scroll_ = 0;
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
                        open_gpu_process_detail();
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
                        const size_t maximum =
                                inventory_.listeners_.size() > visible ? inventory_.listeners_.size() - visible : 0;
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
                    request_signal(system_signal::signal::kill, "Kill");
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U't') {
                    request_signal(system_signal::signal::terminate, "Terminate");
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
                    selection_restore_pending_ = true;
                    status_ = network_section_ == 0 ? "Network: connections" : "Network: interfaces";
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'S') {
                    constexpr size_t count = sizeof(sorts) / sizeof(sorts[0]);
                    sort_index_ = (sort_index_ + 1) % count;
                    reverse_ = !sorts[sort_index_].descending;
                    sync_sort_config();
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
                    status_ = gpu_only_ ? "Showing GPU processes only" : "Showing all processes";
                    return true;
                }
                if (key == tui::key_event::type::printable && event.cp == U'u') {
                    own_only_ = !own_only_;
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
                        status_ = "Follow highlight on · F off";
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
                    const uint64_t current = cfg_.time_window_seconds_ == 0 ? cfg_.history_ * cfg_.collection_.interval_ms_ / 1000
                                                                            : cfg_.time_window_seconds_;
                    const uint64_t limit = neforce::max<uint64_t>(5, 3600ULL * cfg_.collection_.interval_ms_ / 1000);
                    const uint64_t target = cp == U'z' ? neforce::max<uint64_t>(5, current / 2)
                                                       : neforce::min<uint64_t>(limit, current * 2);
                    cfg_.time_window_seconds_ = target;
                    const uint64_t needed = (target * 1000 + cfg_.collection_.interval_ms_ - 1) / cfg_.collection_.interval_ms_;
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
                        cfg_.collection_.interval_ms_ = neforce::min(static_cast<uint64_t>(10000), cfg_.collection_.interval_ms_ + 250);
                    } else {
                        cfg_.collection_.interval_ms_ = neforce::max(static_cast<uint64_t>(250), cfg_.collection_.interval_ms_ - 250);
                    }
                    status_ = format("Interval {:.2f}s", static_cast<double>(cfg_.collection_.interval_ms_) / 1000.0);
                    return true;
                }
                if (cp == U'r' && replay_) {
                    source_->seek(1);
                    return true;
                }
                if (replay_ && (cp == U',' || cp == U'.' || cp == U'<' || cp == U'>')) {
                    const bool forward = cp == U'.' || cp == U'>';
                    source_->seek(forward ? 1 : -1);
                    status_ = format("Replay frame {} / {}", static_cast<uint64_t>(source_->replay_index() + 1),
                                     static_cast<uint64_t>(source_->replay_size()));
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

