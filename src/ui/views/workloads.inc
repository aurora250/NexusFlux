            NEFORCE_NODISCARD static string join_strings(const vector<string>& values, string_view separator) {
                string result;
                for (const auto& value: values) {
                    if (!result.empty()) {
                        result.append(separator);
                    }
                    result.append(value);
                }
                return result;
            }

            NEFORCE_NODISCARD bool workload_matches(const vector<tag>& tags, const string& plain) const {
                if (filter_.empty()) {
                    return true;
                }
                string haystack = lower_ascii(plain.view());
                for (const auto& tag: tags) {
                    haystack.append(" ");
                    haystack.append(lower_ascii(tag.display()));
                }
                for (const auto& word: split_whitespace(filter_.view())) {
                    bool negated = false;
                    string term = word;
                    if (starts_with(term.view(), "!")) {
                        negated = true;
                        term = term.substr(1);
                    }
                    bool matched = false;
                    if (starts_with(term.view(), "tag:")) {
                        const string needle = lower_ascii(term.substr(4));
                        for (const auto& tag: tags) {
                            if (contains_ignore_case(tag.display().view(), needle.view())) {
                                matched = true;
                                break;
                            }
                        }
                    } else {
                        matched = contains_ignore_case(haystack.view(), lower_ascii(term).view());
                    }
                    if (matched == negated) {
                        return false;
                    }
                }
                return true;
            }

            NEFORCE_NODISCARD const process* process_for_pid(const optional<uint32_t>& pid) const {
                if (!pid.has_value() || *pid == 0) {
                    return nullptr;
                }
                for (const auto& item: snap_.processes_) {
                    if (!item.thread_ && item.pid_ == *pid) {
                        return &item;
                    }
                }
                return nullptr;
            }

            NEFORCE_NODISCARD float service_cpu(const service& item) const {
                if (item.cpu_percent_.has_value()) {
                    return *item.cpu_percent_;
                }
                const process* owner = process_for_pid(item.pid_);
                return owner != nullptr ? owner->cpu_ : 0.0F;
            }

            NEFORCE_NODISCARD uint64_t service_memory(const service& item) const {
                if (item.memory_.has_value()) {
                    return *item.memory_;
                }
                const process* owner = process_for_pid(item.pid_);
                return owner != nullptr ? owner->memory_ : 0;
            }

            NEFORCE_NODISCARD string service_tasks_text(const service& item) const {
                if (item.tasks_.has_value()) {
                    return format("{}", *item.tasks_);
                }
                const process* owner = process_for_pid(item.pid_);
                if (owner != nullptr) {
                    if (const json* tasks = owner->extra_.get("threads"); tasks != nullptr) {
                        return tasks->is_string() ? tasks->as_string() : tasks->dump();
                    }
                }
                return "—";
            }

            NEFORCE_NODISCARD vector<size_t> workload_rows(bool containers) const {
                vector<size_t> rows;
                const size_t count = containers ? inventory_.containers_.size() : inventory_.services_.size();
                for (size_t index = 0; index < count; ++index) {
                    if (containers) {
                        const auto& item = inventory_.containers_[index];
                        const string plain =
                                format("{} {} {} {} {}", item.name_, item.image_, item.state_, item.runtime_, item.id_);
                        if (workload_matches(item.tags_, plain)) {
                            rows.push_back(index);
                        }
                    } else {
                        const auto& item = inventory_.services_[index];
                        const string plain =
                                format("{} {} {} {}", item.name_, item.scope_, item.active_, item.description_);
                        if (workload_matches(item.tags_, plain)) {
                            rows.push_back(index);
                        }
                    }
                }
                const auto mode = sorts[neforce::min(sort_index_, sizeof(sorts) / sizeof(sorts[0]) - 1)].mode;
                const auto before = [&](size_t left, size_t right) {
                    bool result = false;
                    if (containers) {
                        const auto& a = inventory_.containers_[left];
                        const auto& b = inventory_.containers_[right];
                        if (mode == sort_mode::memory) {
                            result = a.memory_.value_or(0) > b.memory_.value_or(0);
                        } else if (mode == sort_mode::cpu) {
                            result = a.cpu_.value_or(0.0) > b.cpu_.value_or(0.0);
                        } else {
                            result = a.name_ < b.name_;
                        }
                    } else {
                        const auto& a = inventory_.services_[left];
                        const auto& b = inventory_.services_[right];
                        if (mode == sort_mode::memory) {
                            result = service_memory(a) > service_memory(b);
                        } else if (mode == sort_mode::cpu) {
                            result = service_cpu(a) > service_cpu(b);
                        } else {
                            result = a.name_ < b.name_;
                        }
                    }
                    return result;
                };
                neforce::tim_sort(rows.begin(), rows.end(), [&](size_t left, size_t right) {
                    return reverse_ ? before(right, left) : before(left, right);
                });
                return rows;
            }

            void open_workload_detail(bool containers) {
                reconcile_current_selection();
                const auto rows = workload_rows(containers);
                if (rows.empty()) {
                    status_ = "No workload selected.";
                    return;
                }
                const size_t index = rows[neforce::min(selected_, rows.size() - 1)];
                detail_lines_.clear();
                if (containers) {
                    const auto& item = inventory_.containers_[index];
                    detail_lines_.push_back(format("Name          {}", item.name_));
                    detail_lines_.push_back(format("ID            {}", item.id_));
                    detail_lines_.push_back(format("Runtime       {}", item.runtime_));
                    detail_lines_.push_back(format("Image         {}", item.image_));
                    detail_lines_.push_back(format("State         {}", item.state_));
                    detail_lines_.push_back(format("Status        {}", item.status_));
                    detail_lines_.push_back(format("Command       {}", item.command_));
                    detail_lines_.push_back(format("Ports         {}", item.ports_));
                    detail_lines_.push_back(format("Created       {}", item.created_));
                    detail_lines_.push_back(format("Running for   {}", item.running_for_));
                    detail_lines_.push_back(format(
                            "CPU           {}", item.cpu_.has_value() ? format("{:.1f}%", *item.cpu_) : string("—")));
                    detail_lines_.push_back(
                            format("Memory        {}",
                                   item.memory_.has_value() ? bytes(static_cast<double>(*item.memory_)) : string("—")));
                    detail_lines_.push_back(format("PIDs          {}",
                                                   item.pids_.has_value() ? format("{}", *item.pids_) : string("—")));
                    detail_lines_.push_back(
                            format("Network       {}",
                                   item.network_rx_.has_value() && item.network_tx_.has_value()
                                           ? format("{} / {}", bytes(static_cast<double>(*item.network_rx_)),
                                                    bytes(static_cast<double>(*item.network_tx_)))
                                           : string("—")));
                    detail_lines_.push_back(
                            format("Block I/O     {}",
                                   item.block_read_.has_value() && item.block_write_.has_value()
                                           ? format("{} / {}", bytes(static_cast<double>(*item.block_read_)),
                                                    bytes(static_cast<double>(*item.block_write_)))
                                           : string("—")));
                } else {
                    const auto& item = inventory_.services_[index];
                    const process* owner = process_for_pid(item.pid_);
                    const string cpu_text = item.cpu_percent_.has_value()
                                                    ? format("{:.1f}%", *item.cpu_percent_)
                                                    : (owner != nullptr ? format("{:.1f}%", owner->cpu_) : string("—"));
                    const string memory_text =
                            item.memory_.has_value()
                                    ? bytes(static_cast<double>(*item.memory_))
                                    : (owner != nullptr ? bytes(static_cast<double>(owner->memory_)) : string("—"));
                    const string cpu_time_text =
                            item.cpu_time_ns_.has_value()
                                    ? format("{:.2f}s", static_cast<double>(*item.cpu_time_ns_) / 1e9)
                                    : (owner != nullptr
                                               ? format("{:.2f}s", static_cast<double>(owner->cpu_time_ms_) / 1000.0)
                                               : string("—"));
                    detail_lines_.push_back(format("Name          {}", item.name_));
                    detail_lines_.push_back(format("Scope         {}", item.scope_));
                    detail_lines_.push_back(format("Load          {}", item.load_));
                    detail_lines_.push_back(format("Active        {}", item.active_));
                    detail_lines_.push_back(format("Sub           {}", item.sub_));
                    detail_lines_.push_back(format("Description   {}", item.description_));
                    detail_lines_.push_back(
                            format("Main PID      {}", item.pid_.has_value() ? format("{}", *item.pid_) : string("—")));
                    detail_lines_.push_back(format("CPU           {}", cpu_text));
                    detail_lines_.push_back(format("Memory        {}", memory_text));
                    detail_lines_.push_back(format("CPU time      {}", cpu_time_text));
                    detail_lines_.push_back(format("Tasks         {}", service_tasks_text(item)));
                }
                popup_title_ = containers ? "CONTAINER" : "SERVICE";
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            void jump_to_service_process() {
                reconcile_current_selection();
                if (current_ != view::services) {
                    return;
                }
                const auto rows = workload_rows(false);
                if (rows.empty() || selected_ >= rows.size()) {
                    status_ = "Select a service first.";
                    return;
                }
                const auto& item = inventory_.services_[rows[selected_]];
                if (!item.pid_.has_value() || *item.pid_ == 0) {
                    status_ = "This service has no running main process.";
                    return;
                }
                const process* owner = process_for_pid(item.pid_);
                if (owner == nullptr) {
                    status_ = format("PID {} is no longer in the process snapshot.", static_cast<uint64_t>(*item.pid_));
                    return;
                }
                const auto pid = *item.pid_;
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
                status_ = format("Service {} → process {} ({})", item.name_, static_cast<uint64_t>(pid), owner->name_);
            }

            void draw_service_side_panel(painter& brush, const rect& area) {
                if (area.h_ < 4 || area.w_ < 20) {
                    return;
                }
                const auto rows = workload_rows(false);
                if (rows.empty() || selected_ >= rows.size()) {
                    brush.box(area, " SERVICE ", colors_.purple_);
                    brush.empty(area.inset(1), "No service selected", "Use ↑/↓ or click a service row.");
                    return;
                }
                const auto& item = inventory_.services_[rows[selected_]];
                brush.box(area, format(" SERVICE · {} ", item.name_).view(), colors_.purple_);
                int y = area.y_ + 1;
                const int bottom = area.bottom() - 1;
                const auto line = [&](const string& text, color tint) {
                    if (y >= bottom) {
                        return;
                    }
                    const auto wrapped = wrap_display(text.view(), neforce::max(8, area.w_ - 4));
                    for (const auto& part: wrapped) {
                        if (y >= bottom) {
                            break;
                        }
                        brush.text(area.x_ + 2, y, part.view(), tint);
                        ++y;
                    }
                };
                const process* owner = item.pid_.has_value() ? process_for_pid(*item.pid_) : nullptr;
                const string cpu_text = item.cpu_percent_.has_value()
                                                ? format("{:.1f}%", *item.cpu_percent_)
                                                : (owner != nullptr ? format("{:.1f}%", owner->cpu_) : string("—"));
                const string memory_text =
                        item.memory_.has_value()
                                ? bytes(static_cast<double>(*item.memory_))
                                : (owner != nullptr ? bytes(static_cast<double>(owner->memory_)) : string("—"));
                line(format("{} · {} · {}", item.scope_, item.active_, item.sub_), state_color(item.active_, colors_));
                line(item.description_, colors_.muted_);
                line(format("PID {} · CPU {} · RAM {}", item.pid_.has_value() ? format("{}", *item.pid_) : string("—"),
                            cpu_text, memory_text),
                     colors_.text_);
                line(format("CPU time {} · tasks {}",
                            item.cpu_time_ns_.has_value() ? format("{:.2f}s", *item.cpu_time_ns_ / 1e9) : string("—"),
                            service_tasks_text(item)),
                     colors_.muted_);
                if (const json* fragment = item.extra_.get("FragmentPath");
                    fragment != nullptr && fragment->is_string() && !fragment->as_string().empty()) {
                    line(format("Unit {}", fragment->as_string()), colors_.muted_);
                }
                if (const json* restarts = item.extra_.get("NRestarts");
                    restarts != nullptr && restarts->is_string() && !restarts->as_string().empty()) {
                    line(format("Restarts {}", restarts->as_string()), colors_.muted_);
                }
                if (owner != nullptr) {
                    line(format("Process {} · I/O {}/s", owner->name_,
                                bytes(owner->read_per_sec_ + owner->write_per_sec_)),
                         colors_.muted_);
                }
            }

            NEFORCE_NODISCARD string vm_memory_text(const virtual_machine& item) const {
                if (item.memory_.has_value()) {
                    return item.max_memory_.has_value() ? format("{} / {}", bytes(static_cast<double>(*item.memory_)),
                                                                 bytes(static_cast<double>(*item.max_memory_)))
                                                        : bytes(static_cast<double>(*item.memory_));
                }
                return item.max_memory_.has_value() ? bytes(static_cast<double>(*item.max_memory_)) : string("—");
            }

            void reconcile_selection(list_selection& selection, size_t& index, const vector<string>& keys) {
                if (keys.empty()) {
                    index = 0;
                    selection.key_.reset();
                    selection.row_ = 0;
                    selection.valid_ = false;
                    return;
                }
                const size_t original = index;
                const size_t maximum = keys.size() - 1;
                bool resolved = false;
                if (selection_restore_pending_ && selection.key_.has_value()) {
                    for (size_t cursor = 0; cursor < keys.size(); ++cursor) {
                        if (keys[cursor] == *selection.key_) {
                            index = cursor;
                            resolved = true;
                            break;
                        }
                    }
                }
                if (!resolved && selection.valid_ && index != selection.row_ && selection.key_.has_value()) {
                    index = neforce::min(index, maximum);
                    selection.key_ = keys[index];
                    resolved = true;
                }
                if (!resolved && selection.key_.has_value()) {
                    for (size_t cursor = 0; cursor < keys.size(); ++cursor) {
                        if (keys[cursor] == *selection.key_) {
                            index = cursor;
                            resolved = true;
                            break;
                        }
                    }
                }
                if (!resolved) {
                    index = neforce::min(index, maximum);
                    selection.key_ = keys[index];
                }
                selection.row_ = index;
                selection.valid_ = true;
                if (index != original) {
                    free_scroll_ = false;
                }
            }

            NEFORCE_NODISCARD vector<string> connection_keys() const {
                vector<string> keys;
                const auto rows = filtered_connections();
                keys.reserve(rows.size());
                for (const auto row: rows) {
                    keys.push_back(connection_identity(snap_.connections_.connections_[row]));
                }
                return keys;
            }

            NEFORCE_NODISCARD vector<string> interface_keys() const {
                vector<string> keys;
                const auto rows = filtered_networks();
                keys.reserve(rows.size());
                for (const auto row: rows) {
                    keys.push_back(interface_identity(snap_.networks_[row]));
                }
                return keys;
            }

            NEFORCE_NODISCARD bool list_view() const {
                return current_ == view::processes || current_ == view::overview || current_ == view::network ||
                       current_ == view::containers || current_ == view::services || current_ == view::vms;
            }

            NEFORCE_NODISCARD size_t current_list_count() const {
                switch (current_) {
                    case view::processes:
                    case view::overview:
                        return process_rows().size();
                    case view::network:
                        return network_section_ == 1 ? filtered_networks().size() : filtered_connections().size();
                    case view::containers:
                        return workload_rows(true).size();
                    case view::services:
                        return workload_rows(false).size();
                    case view::vms:
                        return inventory_.virtual_machines_.size();
                    default:
                        return 0;
                }
            }

            NEFORCE_NODISCARD size_t& current_list_index() {
                if (current_ == view::network && network_section_ == 0) {
                    return selected_connection_;
                }
                return selected_;
            }

            void remember_current_list_selection() {
                switch (current_) {
                    case view::processes:
                    case view::overview:
                        remember_selection(process_rows());
                        break;
                    case view::network:
                        if (network_section_ == 1) {
                            const auto rows = filtered_networks();
                            if (rows.empty()) {
                                interface_selection_ = list_selection{};
                            } else {
                                selected_ = neforce::min(selected_, rows.size() - 1);
                                interface_selection_.key_ = interface_identity(snap_.networks_[rows[selected_]]);
                                interface_selection_.row_ = selected_;
                                interface_selection_.valid_ = true;
                            }
                        } else {
                            const auto rows = filtered_connections();
                            if (rows.empty()) {
                                connection_selection_ = list_selection{};
                            } else {
                                selected_connection_ = neforce::min(selected_connection_, rows.size() - 1);
                                connection_selection_.key_ = connection_identity(
                                        snap_.connections_.connections_[rows[selected_connection_]]);
                                connection_selection_.row_ = selected_connection_;
                                connection_selection_.valid_ = true;
                            }
                        }
                        break;
                    case view::containers: {
                        const auto rows = workload_rows(true);
                        if (rows.empty()) {
                            container_selection_ = list_selection{};
                        } else {
                            selected_ = neforce::min(selected_, rows.size() - 1);
                            container_selection_.key_ = container_identity(inventory_.containers_[rows[selected_]]);
                            container_selection_.row_ = selected_;
                            container_selection_.valid_ = true;
                        }
                        break;
                    }
                    case view::services:
                        remember_service_selection();
                        break;
                    case view::vms:
                        if (inventory_.virtual_machines_.empty()) {
                            vm_selection_ = list_selection{};
                        } else {
                            selected_ = neforce::min(selected_, inventory_.virtual_machines_.size() - 1);
                            vm_selection_.key_ = vm_identity(inventory_.virtual_machines_[selected_]);
                            vm_selection_.row_ = selected_;
                            vm_selection_.valid_ = true;
                        }
                        break;
                    default:
                        break;
                }
            }

            void move_current_list(const int delta) {
                const size_t count = current_list_count();
                auto& index = current_list_index();
                if (count == 0) {
                    index = 0;
                    return;
                }
                const int64_t next = static_cast<int64_t>(index) + delta;
                index = static_cast<size_t>(
                        neforce::clamp(next, static_cast<int64_t>(0), static_cast<int64_t>(count) - 1));
                remember_current_list_selection();
            }

            void restore_process_selection() {
                if (current_ != view::processes && current_ != view::overview) {
                    return;
                }
                const auto rows = process_rows();
                if (rows.empty()) {
                    selected_ = 0;
                    selected_process_.reset();
                    return;
                }
                const size_t original = selected_;
                if (selected_process_.has_value()) {
                    for (size_t cursor = 0; cursor < rows.size(); ++cursor) {
                        const auto& item = snap_.processes_[rows[cursor].index_];
                        if (item.pid_ == selected_process_->first && item.start_time_ == selected_process_->second) {
                            selected_ = cursor;
                            if (selected_ != original) {
                                free_scroll_ = false;
                            }
                            return;
                        }
                    }
                }
                selected_ = neforce::min(selected_, rows.size() - 1);
                if (selected_ != original) {
                    free_scroll_ = false;
                }
                select_process(snap_.processes_[rows[selected_].index_]);
            }

            void reconcile_current_selection() {
                switch (current_) {
                    case view::processes:
                    case view::overview:
                        restore_process_selection();
                        break;
                    case view::network: {
                        if (network_section_ == 1) {
                            reconcile_selection(interface_selection_, selected_, interface_keys());
                        } else {
                            reconcile_selection(connection_selection_, selected_connection_, connection_keys());
                        }
                        break;
                    }
                    case view::containers: {
                        const auto rows = workload_rows(true);
                        vector<string> keys;
                        keys.reserve(rows.size());
                        for (const auto row: rows) {
                            keys.push_back(container_identity(inventory_.containers_[row]));
                        }
                        reconcile_selection(container_selection_, selected_, keys);
                        break;
                    }
                    case view::services: {
                        const auto rows = workload_rows(false);
                        vector<string> keys;
                        keys.reserve(rows.size());
                        for (const auto row: rows) {
                            keys.push_back(service_identity(inventory_.services_[row]));
                        }
                        reconcile_selection(service_selection_, selected_, keys);
                        break;
                    }
                    case view::vms: {
                        vector<string> keys;
                        keys.reserve(inventory_.virtual_machines_.size());
                        for (const auto& item: inventory_.virtual_machines_) {
                            keys.push_back(vm_identity(item));
                        }
                        reconcile_selection(vm_selection_, selected_, keys);
                        break;
                    }
                    default:
                        break;
                }
            }

            static uint64_t vm_mib(const uint64_t bytes) noexcept { return bytes / (1024ULL * 1024ULL); }

            static uint64_t vm_config_memory(const virtual_machine& item) noexcept {
                return item.memory_.has_value() && *item.memory_ > 0 ? *item.memory_ : item.max_memory_.value_or(0);
            }

            NEFORCE_NODISCARD static string process_identity(const process& item) {
                return format("{}:{}", static_cast<uint64_t>(item.pid_), static_cast<uint64_t>(item.start_time_));
            }

            NEFORCE_NODISCARD static string connection_identity(const connection& item) {
                if (!item.id_.empty()) {
                    return item.id_;
                }
                return format("{}|{}|{}|{}", item.protocol_, item.local_, item.remote_,
                              static_cast<uint64_t>(item.inode_));
            }

            NEFORCE_NODISCARD static string container_identity(const container& item) {
                return item.runtime_ + "|" + (item.id_.empty() ? item.name_ : item.id_);
            }

            NEFORCE_NODISCARD static string service_identity(const service& item) {
                return item.scope_ + "|" + item.name_;
            }

            NEFORCE_NODISCARD static string interface_identity(const network& item) { return item.name_; }

            NEFORCE_NODISCARD static string vm_identity(const virtual_machine& item) {
                return item.backend_ + "|" + (item.uuid_.empty() ? item.name_ : item.uuid_);
            }

            void sync_vm_edit() {
                if (current_ != view::vms || selected_ >= inventory_.virtual_machines_.size()) {
                    return;
                }
                const auto& item = inventory_.virtual_machines_[selected_];
                const string key = vm_identity(item);
                if (key == vm_edit_key_) {
                    return;
                }
                vm_edit_key_ = key;
                vm_edit_vcpus_ = item.vcpus_.has_value() && *item.vcpus_ > 0 ? *item.vcpus_ : 1;
                const uint64_t bytes = vm_config_memory(item);
                vm_edit_memory_mib_ = vm_mib(bytes);
                vm_edit_autostart_ = contains_ignore_case(item.autostart_.view(), "enable");
            }

            void adjust_vm_vcpus(const int delta) {
                sync_vm_edit();
                if (delta == 0) {
                    return;
                }
                const int64_t next = static_cast<int64_t>(vm_edit_vcpus_) + delta;
                vm_edit_vcpus_ = static_cast<uint32_t>(neforce::clamp<int64_t>(next, 1, 256));
                status_ = format("VM vCPUs pending: {}", static_cast<uint64_t>(vm_edit_vcpus_));
            }

            void adjust_vm_memory(const int64_t delta_mib) {
                sync_vm_edit();
                const int64_t next = static_cast<int64_t>(vm_edit_memory_mib_) + delta_mib;
                vm_edit_memory_mib_ = static_cast<uint64_t>(neforce::clamp<int64_t>(next, 128, 1024 * 1024));
                status_ = format("VM memory pending: {} MiB", vm_edit_memory_mib_);
            }

            void open_vm_side_panel() {
                if (current_ != view::vms || selected_ >= inventory_.virtual_machines_.size()) {
                    return;
                }
                close_item_detail_side_panel();
                vm_side_panel_ = true;
                item_detail_outside_ms_ = 0;
                sync_vm_edit();
            }

            void draw_vm_side_panel(painter& brush, const rect& area) {
                if (area.h_ < 8 || area.w_ < 28) {
                    return;
                }
                if (selected_ >= inventory_.virtual_machines_.size()) {
                    brush.box(area, " VM ", colors_.blue_);
                    brush.empty(area.inset(1), "No virtual machine selected", "Use ↑/↓ or click a VM row.");
                    return;
                }
                sync_vm_edit();
                const auto& item = inventory_.virtual_machines_[selected_];
                brush.box(area, format(" VM · {} ", item.name_).view(), colors_.blue_);
                int y = area.y_ + 1;
                const int bottom = area.bottom() - 1;
                const auto line = [&](const string& text, color tint) {
                    if (y >= bottom) {
                        return;
                    }
                    brush.text(area.x_ + 2, y, truncate_display(text.view(), area.w_ - 4).view(), tint);
                    ++y;
                };
                const auto control = [&](const size_t id, const int x, const int width, const string& text,
                                         color tint) {
                    if (y >= bottom) {
                        return;
                    }
                    brush.text(x, y, truncate_display(text.view(), width).view(), tint);
                    hits_.push_back({{x, y, width, 1}, hit_action::vm_control, id});
                };
                const bool managed = item.backend_ == "libvirt";
                line(format("{} · {} · {}", item.backend_, item.state_,
                            item.id_.empty() ? string("no ID") : format("ID {}", item.id_)),
                     state_color(item.state_, colors_));
                line(format("PID {} · {} vCPU", item.pid_.has_value() ? format("{}", *item.pid_) : string("—"),
                            item.vcpus_.has_value() ? format("{}", *item.vcpus_) : string("—")),
                     colors_.muted_);
                line(vm_memory_text(item), colors_.muted_);

                if (y + 3 < bottom) {
                    ++y;
                    const uint64_t current_mib = vm_mib(vm_config_memory(item));
                    const bool current_autostart = contains_ignore_case(item.autostart_.view(), "enable");
                    const bool vcpus_dirty = !item.vcpus_.has_value() || vm_edit_vcpus_ != *item.vcpus_;
                    const bool memory_dirty = vm_edit_memory_mib_ != current_mib;
                    const bool autostart_dirty = vm_edit_autostart_ != current_autostart;
                    const string vcpus_text = vcpus_dirty ? format("{}*", static_cast<uint64_t>(vm_edit_vcpus_))
                                                          : format("{}", static_cast<uint64_t>(vm_edit_vcpus_));
                    const string memory_text = memory_dirty ? format("{} MiB*", vm_edit_memory_mib_)
                                                            : format("{} MiB", vm_edit_memory_mib_);
                    brush.text(area.x_ + 2, y, "vCPUs", colors_.muted_);
                    brush.text(area.x_ + 11, y, vcpus_text.view(), vcpus_dirty ? colors_.amber_ : colors_.text_);
                    if (area.w_ >= 34) {
                        control(9, area.right() - 10, 3, "[-]", managed ? colors_.amber_ : colors_.muted_);
                        control(10, area.right() - 5, 3, "[+]", managed ? colors_.mint_ : colors_.muted_);
                    }
                    ++y;
                    brush.text(area.x_ + 2, y, "Memory", colors_.muted_);
                    brush.text(area.x_ + 11, y, memory_text.view(), memory_dirty ? colors_.amber_ : colors_.text_);
                    if (area.w_ >= 34) {
                        control(11, area.right() - 10, 3, "[-]", managed ? colors_.amber_ : colors_.muted_);
                        control(12, area.right() - 5, 3, "[+]", managed ? colors_.mint_ : colors_.muted_);
                    }
                    ++y;
                    const string auto_text =
                            managed ? format("{}{}", vm_edit_autostart_ ? "on" : "off", autostart_dirty ? "*" : "")
                                    : string("n/a");
                    brush.text(area.x_ + 2, y, "Autostart", colors_.muted_);
                    brush.text(area.x_ + 13, y, format("[{}]", auto_text).view(),
                               managed ? (vm_edit_autostart_ ? colors_.mint_ : colors_.amber_) : colors_.muted_);
                    hits_.push_back({{area.x_ + 13, y, area.w_ - 15, 1}, hit_action::vm_control, 7});
                    ++y;
                }

                if (y < bottom) {
                    ++y;
                }
                control(0, area.x_ + 2, area.w_ - 4, "[ Start ]", managed ? colors_.mint_ : colors_.muted_);
                ++y;
                control(1, area.x_ + 2, area.w_ - 4, "[ Shutdown ]", colors_.amber_);
                ++y;
                control(2, area.x_ + 2, area.w_ - 4, "[ Reboot ]", managed ? colors_.amber_ : colors_.muted_);
                ++y;
                control(3, area.x_ + 2, area.w_ - 4, "[ Force off ]", colors_.red_);
                ++y;
                control(4, area.x_ + 2, area.w_ - 4, "[ Pause ]", colors_.muted_);
                ++y;
                control(5, area.x_ + 2, area.w_ - 4, "[ Resume ]", colors_.muted_);
                ++y;
                if (y < bottom) {
                    ++y;
                }
                control(6, area.x_ + 2, area.w_ - 4, "[ Apply config ]", managed ? colors_.mint_ : colors_.muted_);
                ++y;
                control(8, area.x_ + 2, area.w_ - 4, "[ Open process ]", colors_.muted_);
                ++y;
                if (y < bottom - 2) {
                    brush.text(area.x_ + 2, bottom - 2, "w apply · +/- CPU · [] mem", colors_.muted_);
                    brush.text(area.x_ + 2, bottom - 1, "a auto · D close", colors_.muted_);
                }
            }

            void show_item_detail_side_panel() {
                if (detail_lines_.empty()) {
                    item_detail_panel_ = false;
                    return;
                }
                item_detail_panel_ = true;
                item_detail_refresh_ = false;
                item_detail_outside_ms_ = 0;
                item_detail_outside_x_ = -1;
                item_detail_outside_y_ = -1;
                item_detail_title_ = popup_title_.empty() ? string("DETAIL") : popup_title_;
                item_detail_lines_ = detail_lines_;
                popup_ = popup_kind::none;
                popup_parent_ = popup_kind::none;
                popup_scroll_ = 0;
            }

            void close_item_detail_side_panel() {
                item_detail_panel_ = false;
                item_detail_refresh_ = false;
                item_detail_title_.clear();
                item_detail_lines_.clear();
                item_detail_panel_rect_ = rect{};
                item_detail_anchor_rect_ = rect{};
                item_detail_outside_ms_ = 0;
                item_detail_outside_x_ = -1;
                item_detail_outside_y_ = -1;
            }

            bool consume_detail_panel_outside_click(const tui::mouse_event& event) {
                const bool generic_open = item_detail_panel_;
                const bool process_open =
                        process_side_panel_ && (current_ == view::processes || current_ == view::overview);
                const bool workload_open = workload_side_panel_ && current_ == view::services;
                const bool vm_open = vm_side_panel_ && current_ == view::vms;
                if ((!generic_open && !process_open && !workload_open && !vm_open) ||
                    event.action != tui::mouse_action::press ||
                    event.button != tui::mouse_button::left) {
                    return false;
                }
                const rect panel_rect = generic_open ? item_detail_panel_rect_
                                        : process_open ? process_panel_rect_
                                        : workload_open ? workload_side_panel_rect_
                                                        : vm_side_panel_rect_;
                const rect anchor_rect = generic_open ? item_detail_anchor_rect_
                                         : process_open ? process_table_rect_
                                         : workload_open ? workload_rect_
                                                         : workload_rect_;
                if (panel_rect.contains(event.x, event.y)) {
                    return false;
                }
                const auto close_panel = [&] {
                    if (generic_open) {
                        close_item_detail_side_panel();
                    } else if (process_open) {
                        process_side_panel_ = false;
                        process_panel_rect_ = rect{};
                    } else if (workload_open) {
                        workload_side_panel_ = false;
                        workload_side_panel_rect_ = rect{};
                    } else {
                        vm_side_panel_ = false;
                        vm_side_panel_rect_ = rect{};
                    }
                };
                const uint64_t now = monotonic_ms();
                const bool double_click = item_detail_outside_ms_ != 0 && now - item_detail_outside_ms_ < 400 &&
                                          item_detail_outside_x_ == event.x && item_detail_outside_y_ == event.y;
                item_detail_outside_ms_ = now;
                item_detail_outside_x_ = event.x;
                item_detail_outside_y_ = event.y;
                if (!anchor_rect.contains(event.x, event.y)) {
                    if (double_click) {
                        close_panel();
                        return true;
                    }
                    return false;
                }
                if (double_click) {
                    close_panel();
                    return true;
                }
                if (generic_open) {
                    item_detail_refresh_ = true;
                }
                return false;
            }

            void refresh_item_detail_side_panel() {
                if (!item_detail_refresh_) {
                    return;
                }
                item_detail_refresh_ = false;
                if (!item_detail_panel_) {
                    return;
                }
                switch (current_) {
                    case view::network:
                        if (network_section_ == 1) {
                            open_network_detail();
                        } else {
                            open_connection_detail();
                        }
                        break;
                    case view::storage:
                        open_storage_detail();
                        break;
                    case view::containers:
                        open_workload_detail(true);
                        break;
                    case view::services:
                        open_workload_detail(false);
                        break;
                    case view::gpu:
                        open_gpu_process_detail();
                        break;
                    case view::explorer:
                        open_extra_detail();
                        break;
                    default:
                        return;
                }
                if (popup_ == popup_kind::info || popup_ == popup_kind::detail) {
                    show_item_detail_side_panel();
                } else {
                    close_item_detail_side_panel();
                }
            }

            void draw_item_detail_side_panel(painter& brush, const rect& area) {
                if (!item_detail_panel_ || item_detail_lines_.empty() || area.h_ < 4 || area.w_ < 40) {
                    return;
                }
                rect anchor = area;
                switch (current_) {
                    case view::processes:
                    case view::overview:
                        anchor = process_table_rect_;
                        break;
                    case view::network:
                        anchor = network_section_ == 1 ? network_interfaces_rect_ : network_connections_rect_;
                        break;
                    case view::storage:
                        anchor = storage_section_ == 1 ? storage_io_rect_ : storage_filesystems_rect_;
                        break;
                    case view::containers:
                    case view::services:
                    case view::vms:
                        anchor = workload_rect_;
                        break;
                    case view::gpu:
                        anchor = gpu_process_rect_;
                        break;
                    case view::explorer:
                        anchor = explorer_rect_;
                        break;
                    default:
                        break;
                }
                if (anchor.w_ < 24 || anchor.h_ < 4) {
                    anchor = area;
                }
                const int width = neforce::max(24, neforce::min(54, anchor.w_ * 38 / 100));
                const rect panel{anchor.right() - width, anchor.y_, width, anchor.h_};
                item_detail_panel_rect_ = panel;
                item_detail_anchor_rect_ = anchor;
                hits_.push_back({panel, hit_action::none, 0});
                brush.box(panel, format(" {} ", item_detail_title_).view(), colors_.blue_);
                const rect inner = panel.inset(1);
                int y = inner.y_;
                for (const auto& line: item_detail_lines_) {
                    if (y >= inner.bottom() - 1) {
                        break;
                    }
                    if (line.empty()) {
                        ++y;
                        continue;
                    }
                    const auto wrapped = wrap_display(line.view(), neforce::max(8, inner.w_ - 2));
                    for (const auto& part: wrapped) {
                        if (y >= inner.bottom() - 1) {
                            break;
                        }
                        brush.text(inner.x_ + 1, y, part.view(),
                                   starts_with(part.view(), "#") ? colors_.text_ : colors_.muted_);
                        ++y;
                    }
                }
                if (y < inner.bottom()) {
                    brush.text(inner.x_ + 1, inner.bottom() - 1, "D close · double-click another row", colors_.muted_);
                }
            }

            void draw_virtual_machines(painter& brush, const rect& area) {
                reconcile_current_selection();
                const auto& vms = inventory_.virtual_machines_;
                size_t running = 0;
                size_t paused = 0;
                for (const auto& item: vms) {
                    if (contains_ignore_case(item.state_.view(), "running")) {
                        ++running;
                    } else if (contains_ignore_case(item.state_.view(), "paused")) {
                        ++paused;
                    }
                }
                int cursor = area.y_;
                if (area.h_ >= 10) {
                    const rect stats{area.x_, cursor, area.w_, 3};
                    brush.box(stats, " VIRTUAL MACHINES ", colors_.blue_);
                    const int width = neforce::max(14, stats.w_ / 4);
                    brush.text(stats.x_ + 2, stats.y_ + 1,
                               format("VMS {:<5}", static_cast<uint64_t>(vms.size())).view(), colors_.blue_);
                    brush.text(stats.x_ + width, stats.y_ + 1,
                               format("RUNNING {:<5}", static_cast<uint64_t>(running)).view(), colors_.mint_);
                    brush.text(stats.x_ + width * 2, stats.y_ + 1,
                               format("PAUSED {:<5}", static_cast<uint64_t>(paused)).view(),
                               paused > 0 ? colors_.amber_ : colors_.mint_);
                    brush.text(stats.x_ + width * 3, stats.y_ + 1,
                               format("{}", inventory_.vm_managers_.empty()
                                                    ? string("no VM manager")
                                                    : join_strings(inventory_.vm_managers_, " + "))
                                       .view(),
                               colors_.muted_);
                    cursor += 3;
                }
                const bool vm_panel = vm_side_panel_ && area.w_ >= 100;
                const int vm_panel_width = vm_panel ? neforce::max(34, area.w_ * 32 / 100) : 0;
                const rect table{area.x_, cursor, area.w_ - vm_panel_width - (vm_panel ? 1 : 0),
                                 area.h_ - (cursor - area.y_)};
                workload_rect_ = table;
                const string manager = inventory_.vm_managers_.empty() ? string("QEMU / libvirt not detected")
                                                                       : join_strings(inventory_.vm_managers_, " + ");
                brush.box(table,
                          format(" VIRTUAL MACHINES · {} · {} ", static_cast<uint64_t>(vms.size()), manager).view(),
                          colors_.blue_);
                if (vms.empty()) {
                    brush.empty(table.inset(1), "No libvirt domains or QEMU processes found",
                                "Install libvirt or start qemu-system-* to populate this page.");
                    return;
                }
                if (selected_ >= vms.size()) {
                    selected_ = vms.size() - 1;
                }
                const int visible = neforce::max(0, table.h_ - 3);
                if (!free_scroll_) {
                    if (selected_ >= table_offset_ + static_cast<size_t>(visible)) {
                        table_offset_ = selected_ + 1 - static_cast<size_t>(visible);
                    }
                    table_offset_ = neforce::min(selected_, table_offset_);
                } else {
                    table_offset_ = vms.size() > static_cast<size_t>(visible)
                                            ? neforce::min(table_offset_, vms.size() - static_cast<size_t>(visible))
                                            : 0;
                }
                const bool compact = vm_panel || table.w_ < 100;
                const bool show_pid = !compact || table.w_ >= 72;
                const int backend_width = compact ? 8 : 9;
                const int state_width = compact ? 10 : 11;
                const int vcpu_width = compact ? 5 : 6;
                const int memory_width = compact ? 18 : 20;
                const int pid_width = 7;
                const int fixed_width =
                        backend_width + state_width + vcpu_width + memory_width + (show_pid ? pid_width : 0);
                const int name_width = neforce::max(compact ? 16 : 28, table.w_ - fixed_width - 5);
                string header = align_cell("NAME", name_width) + align_cell("BACKEND", backend_width) +
                                align_cell("STATE", state_width) + align_cell("VCPUS", vcpu_width, true) +
                                align_cell("MEMORY", memory_width, true);
                if (show_pid) {
                    header += align_cell("PID", pid_width, true);
                }
                brush.text(table.x_ + 2, table.y_ + 1, truncate_display(header.view(), table.w_ - 4).view(),
                           colors_.muted_);
                int row = table.y_ + 2;
                for (size_t offset = table_offset_; offset < vms.size() && row < table.bottom() - 1; ++offset, ++row) {
                    const bool active = offset == selected_;
                    hits_.push_back({{table.x_ + 1, row, table.w_ - 2, 1}, hit_action::workload_row, offset});
                    if (active) {
                        brush.fill(rect{table.x_ + 1, row, table.w_ - 2, 1}, " ", colors_.text_, colors_.selection_);
                    }
                    const auto& item = vms[offset];
                    string line = align_cell(item.name_, name_width) + align_cell(item.backend_, backend_width) +
                                  align_cell(item.state_, state_width) +
                                  align_cell(item.vcpus_.has_value() ? format("{}", *item.vcpus_) : string("—"),
                                             vcpu_width, true) +
                                  align_cell(vm_memory_text(item), memory_width, true);
                    if (show_pid) {
                        line += align_cell(item.pid_.has_value() ? format("{}", *item.pid_) : string("—"), pid_width,
                                           true);
                    }
                    const color tint = active ? colors_.text_
                                              : (contains_ignore_case(item.state_.view(), "running") ? colors_.mint_
                                                                                                     : colors_.muted_);
                    brush.text(table.x_ + 2, row, truncate_display(line.view(), table.w_ - 4).view(), tint);
                }
                const rect scrollbar{table.right() - 1, table.y_ + 2, 1, neforce::max(0, table.h_ - 3)};
                hits_.push_back({scrollbar, hit_action::workload_scroll, 0});
                brush.scrollbar(scrollbar, vms.size(), static_cast<size_t>(visible), table_offset_);
                if (vm_panel) {
                    vm_side_panel_rect_ = rect{table.right() + 1, table.y_, area.right() - table.right() - 1, table.h_};
                    draw_vm_side_panel(brush, vm_side_panel_rect_);
                } else {
                    vm_side_panel_rect_ = rect{};
                }
            }

            void remember_service_selection() {
                if (current_ != view::services) {
                    return;
                }
                const auto rows = workload_rows(false);
                if (rows.empty()) {
                    service_selection_ = list_selection{};
                    return;
                }
                selected_ = neforce::min(selected_, rows.size() - 1);
                const auto& item = inventory_.services_[rows[selected_]];
                service_selection_.key_ = service_identity(item);
                service_selection_.row_ = selected_;
                service_selection_.valid_ = true;
            }

            void draw_workloads(painter& brush, const rect& area, bool containers) {
                reconcile_current_selection();
                const size_t total = containers ? inventory_.containers_.size() : inventory_.services_.size();
                const auto rows = workload_rows(containers);
                int cursor = area.y_;
                if (area.h_ >= 10) {
                    const rect stats{area.x_, cursor, area.w_, 3};
                    brush.box(stats, " WORKLOADS ", containers ? colors_.blue_ : colors_.purple_);
                    if (containers) {
                        size_t running = 0;
                        size_t unhealthy = 0;
                        double rx = 0.0;
                        double tx = 0.0;
                        double read = 0.0;
                        double write = 0.0;
                        for (const auto& item: inventory_.containers_) {
                            if (item.state_ == "running" || item.state_ == "Up") {
                                ++running;
                                rx += item.network_rx_per_sec_.value_or(0.0);
                                tx += item.network_tx_per_sec_.value_or(0.0);
                                read += item.block_read_per_sec_.value_or(0.0);
                                write += item.block_write_per_sec_.value_or(0.0);
                            }
                            if (contains_ignore_case(item.status_.view(), "unhealthy") || item.state_ == "restarting") {
                                ++unhealthy;
                            }
                        }
                        const int width = neforce::max(12, stats.w_ / (area.w_ >= 130 ? 7 : 3));
                        brush.text(stats.x_ + 2, stats.y_ + 1,
                                   format("CONTAINERS {:<4}", static_cast<uint64_t>(total)).view(), colors_.blue_);
                        brush.text(stats.x_ + width, stats.y_ + 1,
                                   format("RUNNING {:<4}", static_cast<uint64_t>(running)).view(), colors_.mint_);
                        brush.text(stats.x_ + width * 2, stats.y_ + 1,
                                   format("UNHEALTHY {:<4}", static_cast<uint64_t>(unhealthy)).view(),
                                   unhealthy > 0 ? colors_.red_ : colors_.mint_);
                        if (area.w_ >= 130) {
                            brush.text(stats.x_ + width * 3, stats.y_ + 1, format("NET {}/s", bytes(rx + tx)).view(),
                                       colors_.mint_);
                            brush.text(stats.x_ + width * 4, stats.y_ + 1, format("RX {}/s", bytes(rx)).view(),
                                       colors_.mint_);
                            brush.text(stats.x_ + width * 5, stats.y_ + 1, format("TX {}/s", bytes(tx)).view(),
                                       colors_.blue_);
                            brush.text(stats.x_ + width * 6, stats.y_ + 1,
                                       format("DISK {}/s", bytes(read + write)).view(), colors_.amber_);
                        }
                    } else {
                        size_t active = 0;
                        size_t failed = 0;
                        for (const auto& item: inventory_.services_) {
                            if (item.active_ == "active") {
                                ++active;
                            }
                            if (item.active_ == "failed") {
                                ++failed;
                            }
                        }
                        brush.text(stats.x_ + 2, stats.y_ + 1,
                                   format("SERVICES {:<5}", static_cast<uint64_t>(total)).view(), colors_.purple_);
                        brush.text(stats.x_ + stats.w_ / 3, stats.y_ + 1,
                                   format("ACTIVE {:<5}", static_cast<uint64_t>(active)).view(), colors_.mint_);
                        brush.text(stats.x_ + stats.w_ * 2 / 3, stats.y_ + 1,
                                   format("FAILED {:<5}", static_cast<uint64_t>(failed)).view(),
                                   failed > 0 ? colors_.red_ : colors_.mint_);
                    }
                    cursor += 3;
                }
                rect table{area.x_, cursor, area.w_, area.h_ - (cursor - area.y_)};
                const bool service_panel = !containers && workload_side_panel_ && area.w_ >= 100;
                const int panel_width = service_panel ? neforce::max(32, area.w_ * 32 / 100) : 0;
                if (service_panel) {
                    table.w_ = area.w_ - panel_width - 1;
                }
                workload_rect_ = table;
                const string manager =
                        containers ? (inventory_.runtimes_.empty() ? string("no runtime")
                                                                   : join_strings(inventory_.runtimes_, " + "))
                                   : (inventory_.service_managers_.empty()
                                              ? string("no manager")
                                              : join_strings(inventory_.service_managers_, " + "));
                brush.box(table,
                          format(" {} · {}/{} · {} ", containers ? "CONTAINERS" : "SERVICES",
                                 static_cast<uint64_t>(rows.size()), static_cast<uint64_t>(total), manager)
                                  .view(),
                          containers ? colors_.blue_ : colors_.purple_);
                if (rows.empty()) {
                    brush.empty(table.inset(1),
                                total == 0 ? (containers ? "No local containers" : "No local services")
                                           : "No matching workloads",
                                total == 0 ? (containers ? "Docker Desktop or Podman is not detected."
                                                         : "The service manager returned no services.")
                                           : "Clear or change the filter.");
                    return;
                }
                const int visible = neforce::max(0, table.h_ - 3);
                if (selected_ >= rows.size()) {
                    selected_ = rows.size() - 1;
                }
                if (!free_scroll_) {
                    if (selected_ >= table_offset_ + static_cast<size_t>(visible)) {
                        table_offset_ = selected_ + 1 - static_cast<size_t>(visible);
                    }
                    table_offset_ = neforce::min(selected_, table_offset_);
                } else {
                    table_offset_ = rows.size() > static_cast<size_t>(visible)
                                            ? neforce::min(table_offset_, rows.size() - static_cast<size_t>(visible))
                                            : 0;
                }
                const bool wide = table.w_ >= 140;
                const int name_width = wide ? neforce::max(16, table.w_ * 17 / 100) : 24;
                constexpr int runtime_width = 8;
                constexpr int state_width = 9;
                constexpr int cpu_width = 6;
                const int memory_width = wide ? neforce::max(14, table.w_ * 17 / 100) : 12;
                constexpr int pids_width = 5;
                constexpr int rate_width = 10;
                constexpr int scope_width = 7;
                constexpr int sub_width = 9;
                constexpr int pid_width = 6;
                constexpr int tasks_width = 5;
                string header;
                if (containers) {
                    header = align_cell("NAME", name_width) + align_cell("RUNTIME", runtime_width) +
                             align_cell("STATE", state_width) + align_cell("CPU", cpu_width, true) +
                             align_cell("MEMORY / LIMIT", memory_width, true);
                    if (wide) {
                        header.append(align_cell("PIDS", pids_width, true));
                        header.append(align_cell("NET RX/s", rate_width, true));
                        header.append(align_cell("NET TX/s", rate_width, true));
                    }
                    header.append("IMAGE");
                } else {
                    header = align_cell("NAME", name_width) + align_cell("SCOPE", scope_width) +
                             align_cell("ACTIVE", state_width) + align_cell("SUB", sub_width) +
                             align_cell("PID", pid_width, true);
                    if (wide) {
                        header.append(align_cell("CPU", cpu_width, true));
                    }
                    header.append(align_cell("MEMORY", memory_width, true));
                    if (wide) {
                        header.append(align_cell("TASKS", tasks_width, true));
                        header.append("DESCRIPTION");
                    }
                }
                brush.text(table.x_ + 2, table.y_ + 1, truncate_display(header.view(), table.w_ - 4).view(),
                           colors_.muted_);
                int row = table.y_ + 2;
                for (size_t offset = table_offset_; offset < rows.size() && row < table.bottom() - 1; ++offset, ++row) {
                    const bool active = offset == selected_;
                    hits_.push_back({{table.x_ + 1, row, table.w_ - 2, 1}, hit_action::workload_row, offset});
                    if (active) {
                        brush.fill(rect{table.x_ + 1, row, table.w_ - 2, 1}, " ", colors_.text_, colors_.selection_);
                    }
                    if (containers) {
                        const auto& item = inventory_.containers_[rows[offset]];
                        const string memory =
                                item.memory_.has_value()
                                        ? format("{}{}", bytes(static_cast<double>(*item.memory_)),
                                                 item.memory_limit_.has_value()
                                                         ? format(" / {}",
                                                                  bytes(static_cast<double>(*item.memory_limit_)))
                                                         : string())
                                        : string("—");
                        string line = align_cell(item.name_, name_width) + align_cell(item.runtime_, runtime_width) +
                                      align_cell(item.state_, state_width) +
                                      align_cell(item.cpu_.has_value() ? format("{:.1f}%", *item.cpu_) : string("—"),
                                                 cpu_width, true) +
                                      align_cell(memory, memory_width, true);
                        if (wide) {
                            line.append(align_cell(item.pids_.has_value() ? format("{}", *item.pids_) : string("—"),
                                                   pids_width, true));
                            line.append(align_cell(item.network_rx_per_sec_.has_value()
                                                           ? format("{}/s", bytes(*item.network_rx_per_sec_))
                                                           : string("—"),
                                                   rate_width, true));
                            line.append(align_cell(item.network_tx_per_sec_.has_value()
                                                           ? format("{}/s", bytes(*item.network_tx_per_sec_))
                                                           : string("—"),
                                                   rate_width, true));
                        }
                        line.append(item.image_);
                        brush.text(table.x_ + 2, row, truncate_display(line.view(), table.w_ - 4).view(),
                                   active ? colors_.text_ : state_color(item.state_, colors_));
                    } else {
                        const auto& item = inventory_.services_[rows[offset]];
                        const process* owner = nullptr;
                        if (item.pid_.has_value() && *item.pid_ != 0) {
                            for (const auto& proc: snap_.processes_) {
                                if (!proc.thread_ && proc.pid_ == *item.pid_) {
                                    owner = &proc;
                                    break;
                                }
                            }
                        }
                        const string cpu_text =
                                item.cpu_percent_.has_value()
                                        ? format("{:.1f}%", *item.cpu_percent_)
                                        : (owner != nullptr ? format("{:.1f}%", owner->cpu_) : string("—"));
                        const string memory_text =
                                item.memory_.has_value()
                                        ? bytes(static_cast<double>(*item.memory_))
                                        : (owner != nullptr ? bytes(static_cast<double>(owner->memory_)) : string("—"));
                        string tasks_text = item.tasks_.has_value() ? format("{}", *item.tasks_) : string("—");
                        if (tasks_text == "—" && owner != nullptr) {
                            if (const json* tasks = owner->extra_.get("threads"); tasks != nullptr) {
                                tasks_text = tasks->is_string() ? tasks->as_string() : tasks->dump();
                            }
                        }
                        string line = align_cell(item.name_, name_width) + align_cell(item.scope_, scope_width) +
                                      align_cell(item.active_, state_width) + align_cell(item.sub_, sub_width) +
                                      align_cell(item.pid_.has_value() ? format("{}", *item.pid_) : string("—"),
                                                 pid_width, true);
                        if (wide) {
                            line.append(align_cell(cpu_text, cpu_width, true));
                        }
                        line.append(align_cell(memory_text, memory_width, true));
                        if (wide) {
                            line.append(align_cell(tasks_text, tasks_width, true));
                            line.append(item.description_);
                        }
                        brush.text(table.x_ + 2, row, truncate_display(line.view(), table.w_ - 4).view(),
                                   active ? colors_.text_ : state_color(item.active_, colors_));
                    }
                }
                const rect workload_scrollbar{table.right() - 1, table.y_ + 2, 1, neforce::max(0, table.h_ - 3)};
                hits_.push_back({workload_scrollbar, hit_action::workload_scroll, 0});
                brush.scrollbar(workload_scrollbar, rows.size(), static_cast<size_t>(visible), table_offset_);
                if (service_panel) {
                    workload_side_panel_rect_ =
                            rect{table.right() + 1, table.y_, area.right() - table.right() - 1, table.h_};
                    draw_service_side_panel(brush, workload_side_panel_rect_);
                } else {
                    workload_side_panel_rect_ = rect{};
                }
            }

            // ---------------------------------------------------------------- actions

            void sync_sort_config() {
                cfg_.sort_index_ = sort_index_;
                cfg_.reverse_sort_ = reverse_;
            }

