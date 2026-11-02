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

            void request_signal(system_signal::signal signal_value, const string& title) {
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
                pending_signal_active_ = true;
                pending_workload_action_.clear();
                pending_title_ = title;
                describe_targets(targets);
                popup_ = popup_kind::confirm;
                popup_title_ = title;
            }

            NEFORCE_NODISCARD int send_signal(uint32_t pid, system_signal::signal signal_value) const {
                return system_signal::send_to_process(static_cast<int>(pid), signal_value) ? 0 : -1;
            }

            void execute_pending_signal() {
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    popup_ = popup_kind::none;
                    return;
                }
                if (!pending_signal_active_ && pending_workload_action_.empty()) {
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
                pending_signal_active_ = false;
                popup_ = popup_kind::none;
            }

            NEFORCE_NODISCARD static vector<pair<system_signal::signal, string>> signal_entries() {
                const struct entry {
                    system_signal::signal signal_;
                    const char* description_;
                } catalog[] = {
                        {system_signal::signal::interrupt, "Interrupt"},
                        {system_signal::signal::terminate, "Terminate"},
                        {system_signal::signal::kill, "Kill"},
                        {system_signal::signal::quit, "Quit"},
                        {system_signal::signal::abort, "Abort"},
                        {system_signal::signal::trap, "Trace trap"},
                        {system_signal::signal::illegal_instr, "Illegal instruction"},
                        {system_signal::signal::floating_point, "Floating point"},
                        {system_signal::signal::segment_fault, "Segmentation fault"},
                        {system_signal::signal::bus_error, "Bus error"},
                        {system_signal::signal::hangup, "Hangup"},
                        {system_signal::signal::user1, "User 1"},
                        {system_signal::signal::user2, "User 2"},
                        {system_signal::signal::pipe_broken, "Broken pipe"},
                        {system_signal::signal::alarm, "Alarm"},
                        {system_signal::signal::child, "Child"},
                        {system_signal::signal::resume, "Continue"},
                        {system_signal::signal::stop, "Stop"},
                        {system_signal::signal::terminal_stop, "Terminal stop"},
                        {system_signal::signal::terminal_input, "Terminal input"},
                        {system_signal::signal::terminal_output, "Terminal output"},
                        {system_signal::signal::urgent, "Urgent"},
                        {system_signal::signal::cpu_limit, "CPU limit"},
                        {system_signal::signal::file_limit, "File size limit"},
                        {system_signal::signal::virtual_alarm, "Virtual alarm"},
                        {system_signal::signal::profiling, "Profiling"},
                        {system_signal::signal::window_change, "Window change"},
                        {system_signal::signal::io, "I/O"},
                        {system_signal::signal::power, "Power"},
                        {system_signal::signal::bad_syscall, "Bad system call"},
                };
                vector<pair<system_signal::signal, string>> items;
                for (const auto& item: catalog) {
                    const int native = system_signal::signal_number(item.signal_);
                    if (native <= 0) {
                        continue;
                    }
                    items.push_back(
                            {item.signal_, format("SIG{} {}", system_signal::signal_name(item.signal_), item.description_)});
                }
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
                    popup_entries_.push_back(
                            {format("{:2} {}", system_signal::signal_number(item.first), item.second), "", "signal"});
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
                    neforce::tim_sort(lines.begin(), lines.end(),
                                      [](const string& left, const string& right) { return left < right; });
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
                reconcile_current_selection();
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
                reconcile_current_selection();
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
                    add("Send signal", "tool_signal");
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
                    add("Apply VM config", "vm_apply_config");
                    add("Toggle VM autostart", "vm_autostart");
                    add(vm_side_panel_ ? "Hide VM side panel" : "Show VM side panel", "vm_side_panel");
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
                reconcile_current_selection();
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
                pending_signal_active_ = false;
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
                reconcile_current_selection();
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
                reconcile_current_selection();
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
                            if (string_view{value} == "default" && label == "Graph symbols") {
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
                reconcile_current_selection();
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

            NEFORCE_NODISCARD bool select_gpu_process(const uint32_t pid) {
                if (snap_.gpus_.empty()) {
                    status_ = "No GPU telemetry.";
                    return false;
                }
                const auto& device = snap_.gpus_[neforce::min(selected_gpu_, snap_.gpus_.size() - 1)];
                for (size_t index = 0; index < device.processes_.size(); ++index) {
                    if (device.processes_[index].pid_ != pid) {
                        continue;
                    }
                    selected_gpu_process_ = index;
                    for (const auto& item: snap_.processes_) {
                        if (item.pid_ == pid) {
                            select_process(item);
                            return true;
                        }
                    }
                    status_ = format("PID {} is not in the current process sample.", static_cast<uint64_t>(pid));
                    return false;
                }
                status_ = format("PID {} is not in this GPU process sample.", static_cast<uint64_t>(pid));
                return false;
            }

            void open_gpu_process_detail() {
                if (snap_.gpus_.empty()) {
                    status_ = "No GPU telemetry.";
                    return;
                }
                const auto& device = snap_.gpus_[neforce::min(selected_gpu_, snap_.gpus_.size() - 1)];
                if (device.processes_.empty()) {
                    status_ = "This GPU has no sampled processes.";
                    return;
                }
                selected_gpu_process_ = neforce::min(selected_gpu_process_, device.processes_.size() - 1);
                if (select_gpu_process(device.processes_[selected_gpu_process_].pid_)) {
                    open_detail();
                }
            }

            void open_gpu_process_menu() {
                if (snap_.gpus_.empty()) {
                    status_ = "No GPU telemetry.";
                    return;
                }
                const auto& device = snap_.gpus_[neforce::min(selected_gpu_, snap_.gpus_.size() - 1)];
                if (device.processes_.empty()) {
                    status_ = "This GPU has no sampled processes.";
                    return;
                }
                selected_gpu_process_ = neforce::min(selected_gpu_process_, device.processes_.size() - 1);
                if (select_gpu_process(device.processes_[selected_gpu_process_].pid_)) {
                    open_process_menu();
                }
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

