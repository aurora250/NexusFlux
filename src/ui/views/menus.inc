            void open_process_menu() {
                if (!selected_process_.has_value()) {
                    return;
                }
                string name = format("PID {}", selected_process_->first);
                for (const auto& item: snap_.processes_) {
                    if (item.pid_ == selected_process_->first && item.start_time_ == selected_process_->second) {
                        name = format("{} · {}", item.pid_, item.name_);
                        break;
                    }
                }
                popup_parent_ = popup_kind::none;
                popup_ = popup_kind::main_menu;
                popup_title_ = name;
                popup_entries_ = {
                        {"Details", "Enter", "detail"},  {"Follow", "F", "follow"},     {"Pause list", "P", "pause"},
                        {"Terminate", "t", "terminate"}, {"Kill", "k", "kill_process"}, {"Send signal", "s", "signal_menu"},
                };
                popup_selected_ = 0;
                popup_scroll_ = 0;
            }

            void open_vm_detail() {
                reconcile_current_selection();
                if (current_ != view::vms || selected_ >= inventory_.virtual_machines_.size()) {
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                detail_lines_.clear();
                detail_lines_.push_back(format("Name          {}", item.name_));
                detail_lines_.push_back(format("ID            {}", item.id_.empty() ? "—" : item.id_));
                detail_lines_.push_back(format("UUID          {}", item.uuid_.empty() ? "—" : item.uuid_));
                detail_lines_.push_back(format("Backend       {}", item.backend_));
                detail_lines_.push_back(format("State         {}", item.state_));
                detail_lines_.push_back(format("Autostart     {}", item.autostart_.empty() ? "—" : item.autostart_));
                detail_lines_.push_back(
                        format("vCPUs         {}", item.vcpus_.has_value() ? format("{}", *item.vcpus_) : "—"));
                detail_lines_.push_back(format("Memory        {}", vm_memory_text(item)));
                detail_lines_.push_back(
                        format("QEMU PID      {}", item.pid_.has_value() ? format("{}", *item.pid_) : "—"));
                detail_lines_.push_back(
                        format("Description   {}", item.description_.empty() ? "—" : item.description_));
                if (const json* command = item.extra_.get("command"); command != nullptr && command->is_string()) {
                    detail_lines_.push_back(format("Command       {}", command->as_string()));
                }
                popup_ = popup_kind::info;
                popup_title_ = "VIRTUAL MACHINE";
                popup_scroll_ = 0;
            }

            void open_vm_menu() {
                reconcile_current_selection();
                if (current_ != view::vms || selected_ >= inventory_.virtual_machines_.size()) {
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                popup_ = popup_kind::main_menu;
                popup_title_ = format("VM · {}", item.name_);
                popup_entries_ = {
                        {"Details", "Enter", "vm_detail"},
                        {"Start", "", "vm_start"},
                        {"Shutdown", "", "vm_shutdown"},
                        {"Reboot", "", "vm_reboot"},
                        {"Force off", "", "vm_destroy"},
                        {"Pause", "", "vm_suspend"},
                        {"Resume", "", "vm_resume"},
                        {"Apply config", "", "vm_apply_config"},
                        {"Toggle autostart", "", "vm_autostart"},
                        {"Side panel", "", "vm_side_panel"},
                        {"Open QEMU process", "", "vm_process"},
                };
                popup_selected_ = 0;
                popup_scroll_ = 0;
                popup_parent_ = popup_kind::none;
            }

            void request_vm_action(string action) {
                if (remote_mode()) {
                    status_ = "Remote mode: VM control is host-only.";
                    popup_ = popup_kind::none;
                    return;
                }
                reconcile_current_selection();
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    popup_ = popup_kind::none;
                    return;
                }
                if (selected_ >= inventory_.virtual_machines_.size()) {
                    status_ = "Select a virtual machine first.";
                    popup_ = popup_kind::none;
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                pending_signal_active_ = false;
                pending_workload_action_ = "vm:" + action;
                popup_title_ = "Confirm";
                confirm_message_ = format("{} VM '{}'?\n\nThis action changes system state. Confirm with y/Enter, "
                                          "cancel with n/Esc.",
                                          action, item.name_);
                popup_ = popup_kind::confirm;
            }

            void jump_to_vm_process() {
                reconcile_current_selection();
                if (selected_ >= inventory_.virtual_machines_.size()) {
                    return;
                }
                const auto& vm = inventory_.virtual_machines_[selected_];
                if (!vm.pid_.has_value()) {
                    status_ = "This virtual machine has no QEMU host process.";
                    return;
                }
                const auto pid = *vm.pid_;
                const process* owner = process_for_pid(pid);
                if (owner == nullptr) {
                    status_ = format("PID {} is no longer in the process snapshot.", static_cast<uint64_t>(pid));
                    return;
                }
                switch_view(view::processes);
                filter_.clear();
                editing_filter_ = false;
                const auto process_view_rows = process_rows();
                selected_ = 0;
                for (size_t index = 0; index < process_view_rows.size(); ++index) {
                    if (snap_.processes_[process_view_rows[index].index_].pid_ == pid) {
                        selected_ = index;
                        break;
                    }
                }
                table_offset_ = selected_ > 5 ? selected_ - 5 : 0;
                select_process(*owner);
                status_ = format("VM {} → process {}", vm.name_, static_cast<uint64_t>(pid));
            }

            void run_vm_action(const string_view action) {
                if (remote_mode()) {
                    status_ = "Remote mode: VM control is host-only.";
                    popup_ = popup_kind::none;
                    return;
                }
                reconcile_current_selection();
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    popup_ = popup_kind::none;
                    return;
                }
                if (selected_ >= inventory_.virtual_machines_.size()) {
                    status_ = "Select a virtual machine first.";
                    popup_ = popup_kind::none;
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                try {
                    if (item.backend_ == "libvirt") {
                        run_command("virsh", {string(action), item.name_}, 5000);
                        status_ = format("{} requested for VM {}", action, item.name_);
                    } else if (item.backend_ == "QEMU" && item.pid_.has_value()) {
                        optional<system_signal::signal> signal;
                        if (action == "shutdown") {
                            signal = system_signal::signal::terminate;
                        } else if (action == "destroy") {
                            signal = system_signal::signal::kill;
                        } else if (action == "suspend") {
                            signal = system_signal::signal::stop;
                        } else if (action == "resume") {
                            signal = system_signal::signal::resume;
                        }
                        if (!signal.has_value()) {
                            status_ = "Start is not available for an unmanaged QEMU process.";
                        } else if (system_signal::send_to_process(static_cast<int>(*item.pid_), *signal)) {
                            status_ = format("{} sent to QEMU {}", action, static_cast<uint64_t>(*item.pid_));
                        } else {
                            status_ = format("Failed to send {} to QEMU {}", action, static_cast<uint64_t>(*item.pid_));
                        }
                    } else {
                        status_ = "This VM backend does not support management actions.";
                    }
                } catch (const exception& error) {
                    status_ = format("VM action failed: {}", error.what());
                }
                popup_ = popup_kind::none;
            }

            void toggle_vm_autostart() {
                if (remote_mode()) {
                    status_ = "Remote mode: VM control is host-only.";
                    popup_ = popup_kind::none;
                    return;
                }
                sync_vm_edit();
                if (selected_ >= inventory_.virtual_machines_.size()) {
                    return;
                }
                if (inventory_.virtual_machines_[selected_].backend_ != "libvirt") {
                    status_ = "Autostart is only configurable for libvirt domains.";
                    return;
                }
                vm_edit_autostart_ = !vm_edit_autostart_;
                status_ = vm_edit_autostart_ ? "VM autostart pending: on (w apply)"
                                             : "VM autostart pending: off (w apply)";
            }

            void apply_vm_config() {
                if (remote_mode()) {
                    status_ = "Remote mode: VM control is host-only.";
                    popup_ = popup_kind::none;
                    return;
                }
                if (!state_changes_allowed()) {
                    status_ =
                            replay_ ? "Replay: state changes are disabled." : "Read-only: state changes are disabled.";
                    return;
                }
                sync_vm_edit();
                if (selected_ >= inventory_.virtual_machines_.size()) {
                    status_ = "Select a virtual machine first.";
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                if (item.backend_ != "libvirt") {
                    status_ = "QEMU configuration is read-only for unmanaged processes; use libvirt for edits.";
                    return;
                }
                try {
                    bool changed = false;
                    if (vm_edit_vcpus_ != 0 && (!item.vcpus_.has_value() || vm_edit_vcpus_ != *item.vcpus_)) {
                        run_command("virsh",
                                    {"setvcpus", item.name_, format("{}", static_cast<uint64_t>(vm_edit_vcpus_)),
                                     "--config"},
                                    5000);
                        changed = true;
                    }
                    const uint64_t current_mib = vm_mib(vm_config_memory(item));
                    if (vm_edit_memory_mib_ >= 128 && vm_edit_memory_mib_ != current_mib) {
                        const uint64_t kib = vm_edit_memory_mib_ * 1024;
                        if (item.max_memory_.has_value() &&
                            vm_edit_memory_mib_ * 1024ULL * 1024ULL > *item.max_memory_) {
                            run_command("virsh", {"setmaxmem", item.name_, format("{}", kib), "--config"}, 5000);
                        }
                        run_command("virsh", {"setmem", item.name_, format("{}", kib), "--config"}, 5000);
                        changed = true;
                    }
                    const bool current_autostart = contains_ignore_case(item.autostart_.view(), "enable");
                    if (vm_edit_autostart_ != current_autostart) {
                        if (vm_edit_autostart_) {
                            run_command("virsh", {"autostart", item.name_}, 5000);
                        } else {
                            run_command("virsh", {"autostart", item.name_, "--disable"}, 5000);
                        }
                        changed = true;
                    }
                    status_ = changed ? format("VM configuration applied for {}", item.name_)
                                      : "No VM configuration changes.";
                } catch (const exception& error) {
                    status_ = format("VM configuration failed: {}", error.what());
                }
            }

            void handle_vm_control(const size_t control) {
                if (remote_mode()) {
                    status_ = "Remote mode: VM control is host-only.";
                    popup_ = popup_kind::none;
                    return;
                }
                sync_vm_edit();
                if (selected_ >= inventory_.virtual_machines_.size()) {
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                const bool managed = item.backend_ == "libvirt";
                switch (control) {
                    case 0:
                        if (!managed) {
                            status_ = "Start is not available for an unmanaged QEMU process.";
                        } else {
                            request_vm_action("start");
                        }
                        break;
                    case 1:
                        request_vm_action("shutdown");
                        break;
                    case 2:
                        if (!managed) {
                            status_ = "Reboot is not available for an unmanaged QEMU process.";
                        } else {
                            request_vm_action("reboot");
                        }
                        break;
                    case 3:
                        request_vm_action("destroy");
                        break;
                    case 4:
                        request_vm_action("suspend");
                        break;
                    case 5:
                        request_vm_action("resume");
                        break;
                    case 6:
                        apply_vm_config();
                        break;
                    case 7:
                        toggle_vm_autostart();
                        break;
                    case 8:
                        jump_to_vm_process();
                        break;
                    case 9:
                        adjust_vm_vcpus(-1);
                        break;
                    case 10:
                        adjust_vm_vcpus(1);
                        break;
                    case 11:
                        adjust_vm_memory(-256);
                        break;
                    case 12:
                        adjust_vm_memory(256);
                        break;
                    default:
                        break;
                }
            }

            void open_workload_menu() {
                reconcile_current_selection();
                const bool containers = current_ == view::containers;
                const auto rows = workload_rows(containers);
                if (rows.empty() || selected_ >= rows.size()) {
                    status_ = containers ? "Select a container first." : "Select a service first.";
                    return;
                }
                const auto index = rows[selected_];
                const string name =
                        containers ? inventory_.containers_[index].name_ : inventory_.services_[index].name_;
                open_tools_menu();
                popup_title_ = format("{} · {}", containers ? "CONTAINER" : "SERVICE", name);
            }

            void open_commands() {
                popup_ = popup_kind::commands;
                popup_title_ = "Commands";
                popup_scroll_ = 0;
            }

