            void draw_storage_page(painter& brush, const rect& area) {
                if (!snap_.disk_io_.empty() && selected_io_ >= snap_.disk_io_.size()) {
                    selected_io_ = 0;
                }
                const disk_io* selected_device = !storage_device_auto_ && selected_io_ < snap_.disk_io_.size()
                                                         ? &snap_.disk_io_[selected_io_]
                                                         : nullptr;
                const disk_history* selected_history =
                        selected_device != nullptr ? history_.disk(selected_device->name_) : nullptr;
                const bool per_device = selected_device != nullptr && selected_history != nullptr;
                const deque<double>& chart_read = per_device ? selected_history->read_ : history_.disk_read_;
                const deque<double>& chart_write = per_device ? selected_history->write_ : history_.disk_write_;
                double read_total = 0.0;
                double write_total = 0.0;
                for (const auto& item: snap_.disk_io_) {
                    if (item.aggregate_) {
                        read_total += item.read_per_sec_;
                        write_total += item.write_per_sec_;
                    }
                }
                constexpr int stats_height = 3;
                const rect stats{area.x_, area.y_, area.w_, neforce::min(stats_height, area.h_)};
                brush.box(stats, " STORAGE ", colors_.amber_);
                const int stat_width = neforce::max(10, stats.w_ / 3);
                brush.text(stats.x_ + 2, stats.y_ + 1, format("FILESYSTEMS {:>3}", snap_.disks_.size()).view(),
                           colors_.amber_);
                brush.text(stats.x_ + stat_width, stats.y_ + 1, format("READ  {}/s", bytes(read_total)).view(),
                           colors_.mint_);
                brush.text(stats.x_ + stat_width * 2, stats.y_ + 1, format("WRITE {}/s", bytes(write_total)).view(),
                           colors_.blue_);
                int cursor = stats.bottom();
                const int chart_height = area.h_ >= 23 ? area.h_ / 3 : 0;
                if (chart_height > 0) {
                    const int left_width = area.w_ / 2;
                    const rect read_chart{area.x_, cursor, left_width - 1, chart_height};
                    const rect write_chart{area.x_ + left_width, cursor, area.w_ - left_width, chart_height};
                    double read_peak = 1024.0;
                    double write_peak = 1024.0;
                    for (const double value: chart_read) {
                        read_peak = neforce::max(read_peak, value);
                    }
                    for (const double value: chart_write) {
                        write_peak = neforce::max(write_peak, value);
                    }
                    const string read_title = per_device ? format(" DISK READ · {} ", selected_device->name_)
                                                         : string(" DISK READ · physical devices ");
                    const string write_title = per_device ? format(" DISK WRITE · {} ", selected_device->name_)
                                                          : string(" DISK WRITE · physical devices ");
                    brush.box(read_chart, read_title.view(), colors_.mint_);
                    graph_painter(brush, cfg_.display_.graph_symbol_proc_)
                            .chart(read_chart.inset(1), "read", {chart_series{"Read", &chart_read, colors_.mint_}},
                                   tui::canvas_chart_nice_max(read_peak * 1.1), "B/s", chart_window());
                    brush.box(write_chart, write_title.view(), colors_.blue_);
                    graph_painter(brush, cfg_.display_.graph_symbol_proc_)
                            .chart(write_chart.inset(1), "write", {chart_series{"Write", &chart_write, colors_.blue_}},
                                   tui::canvas_chart_nice_max(write_peak * 1.1), "B/s", chart_window());
                    const auto has_values = [](const deque<double>& values) {
                        for (const double value: values) {
                            if (value > 0.0) {
                                return true;
                            }
                        }
                        return false;
                    };
                    if (!has_values(chart_read)) {
                        brush.text(read_chart.x_ + 2, read_chart.y_ + 2, "No disk I/O in window", colors_.muted_);
                    }
                    if (!has_values(chart_write)) {
                        brush.text(write_chart.x_ + 2, write_chart.y_ + 2, "No disk I/O in window", colors_.muted_);
                    }
                    cursor += chart_height;
                }
                const int remaining = area.h_ - (cursor - area.y_);
                const int filesystem_height = neforce::max(6, remaining * 42 / 100);
                const rect filesystems{area.x_, cursor, area.w_, filesystem_height};
                storage_filesystems_rect_ = filesystems;
                brush.box(filesystems, " FILESYSTEMS ", colors_.amber_);
                const bool compact = filesystems.w_ < 100;
                const int mount_width = neforce::max(10, filesystems.w_ * 24 / 100);
                const int device_width = compact ? 0 : neforce::max(10, filesystems.w_ * 15 / 100);
                const int fs_width = compact ? 0 : 8;
                constexpr int used_width = 9;
                constexpr int total_width = 9;
                const int inode_width = compact ? 0 : 12;
                constexpr int cap_width = 7;
                string header = align_cell("MOUNT", mount_width);
                if (!compact) {
                    header.append(align_cell("DEVICE", device_width));
                    header.append(align_cell("FS", fs_width));
                }
                header.append(align_cell("USED", used_width, true));
                header.append(align_cell("TOTAL", total_width, true));
                if (!compact) {
                    header.append(align_cell("INODES FREE", inode_width, true));
                }
                header.append(align_cell("CAP", cap_width, true));
                brush.text(filesystems.x_ + 2, filesystems.y_ + 1,
                           truncate_display(header.view(), filesystems.w_ - 4).view(), colors_.muted_);
                if (!snap_.disks_.empty() && selected_ >= snap_.disks_.size()) {
                    selected_ = snap_.disks_.size() - 1;
                }
                int y = filesystems.y_ + 2;
                for (size_t index = 0; index < snap_.disks_.size() && y < filesystems.bottom() - 1; ++index, ++y) {
                    const auto& disk = snap_.disks_[index];
                    const double usage = percent(disk.used_, disk.total_);
                    const color tint = usage >= 90 ? colors_.red_ : device_tint(index);
                    const bool active = storage_section_ == 0 && selected_ == index;
                    hits_.push_back(
                            {{filesystems.x_ + 1, y, filesystems.w_ - 2, 1}, hit_action::storage_fs_row, index});
                    if (active) {
                        brush.fill(rect{filesystems.x_ + 1, y, filesystems.w_ - 2, 1}, " ", colors_.text_,
                                   colors_.selection_);
                    }
                    string line = align_cell(disk.mount_, mount_width);
                    if (!compact) {
                        line.append(align_cell(disk.name_, device_width));
                        line.append(align_cell(disk.filesystem_, fs_width));
                    }
                    line.append(align_cell(bytes(static_cast<double>(disk.used_)), used_width, true));
                    line.append(align_cell(bytes(static_cast<double>(disk.total_)), total_width, true));
                    if (!compact) {
                        const string inodes = disk.extra_.get("inodes_free") != nullptr
                                                      ? format("{}", disk.extra_.get("inodes_free")->as_integer())
                                                      : string("—");
                        line.append(align_cell(inodes, inode_width, true));
                    }
                    line.append(align_cell(format("{:.1f}%", usage), cap_width, true));
                    brush.text(filesystems.x_ + 2, y, truncate_display(line.view(), filesystems.w_ - 4).view(),
                               active ? colors_.text_ : tint);
                }
                if (snap_.disks_.empty()) {
                    brush.text(filesystems.x_ + 2, filesystems.y_ + 2, "No readable mounts", colors_.muted_);
                }
                const rect io{area.x_, filesystems.bottom(), area.w_, area.h_ - (filesystems.bottom() - area.y_)};
                storage_io_rect_ = io;
                brush.box(io, format(" BLOCK DEVICES · {} ", static_cast<uint64_t>(snap_.disk_io_.size())).view(),
                          colors_.amber_);
                if (io.h_ <= 2) {
                    return;
                }
                const bool wide = io.w_ >= 100;
                const int io_device_width = neforce::max(12, io.w_ * 18 / 100);
                constexpr int read_width = 11;
                constexpr int write_width = 11;
                constexpr int busy_width = 7;
                constexpr int iops_width = 7;
                constexpr int await_width = 8;
                constexpr int queue_width = 6;
                string io_header = align_cell("DEVICE", io_device_width);
                io_header.append(align_cell("READ/s", read_width, true));
                io_header.append(align_cell("WRITE/s", write_width, true));
                io_header.append(align_cell("BUSY", busy_width, true));
                if (wide) {
                    io_header.append(align_cell("R IOPS", iops_width, true));
                    io_header.append(align_cell("W IOPS", iops_width, true));
                }
                io_header.append(align_cell("AWAIT", await_width, true));
                io_header.append(align_cell("QUEUE", queue_width, true));
                brush.text(io.x_ + 2, io.y_ + 1, truncate_display(io_header.view(), io.w_ - 4).view(), colors_.muted_);
                int row = io.y_ + 2;
                for (size_t index = 0; index < snap_.disk_io_.size() && row < io.bottom() - 1; ++index, ++row) {
                    const auto& item = snap_.disk_io_[index];
                    hits_.push_back({{io.x_ + 1, row, io.w_ - 2, 1}, hit_action::storage_io_row, index});
                    const bool active = !storage_device_auto_ && storage_section_ == 1 && index == selected_io_ &&
                                        current_ == view::storage;
                    if (active) {
                        brush.fill(rect{io.x_ + 1, row, io.w_ - 2, 1}, " ", colors_.text_, colors_.selection_);
                    }
                    string line = align_cell(item.name_, io_device_width);
                    line.append(align_cell(cfg_.display_.rate(item.read_per_sec_), read_width, true));
                    line.append(align_cell(cfg_.display_.rate(item.write_per_sec_), write_width, true));
                    line.append(align_cell(item.busy_percent_.has_value() ? format("{:.1f}%", *item.busy_percent_)
                                                                          : string("—"),
                                           busy_width, true));
                    if (wide) {
                        line.append(align_cell(item.read_iops_.has_value() ? format("{:.1f}", *item.read_iops_)
                                                                           : string("—"),
                                               iops_width, true));
                        line.append(align_cell(item.write_iops_.has_value() ? format("{:.1f}", *item.write_iops_)
                                                                            : string("—"),
                                               iops_width, true));
                    }
                    line.append(align_cell(item.await_ms_.has_value() ? format("{:.2f}", *item.await_ms_) : string("—"),
                                           await_width, true));
                    line.append(align_cell(item.queue_depth_.has_value() ? format("{:.2f}", *item.queue_depth_)
                                                                         : string("—"),
                                           queue_width, true));
                    brush.text(io.x_ + 2, row, truncate_display(line.view(), io.w_ - 4).view(),
                               active ? colors_.text_ : (item.aggregate_ ? colors_.mint_ : colors_.muted_));
                }
                if (snap_.disk_io_.empty()) {
                    brush.text(io.x_ + 2, io.y_ + 2, "No readable block devices", colors_.muted_);
                }
            }

            void open_storage_detail() {
                detail_lines_.clear();
                if (storage_section_ == 0 && !snap_.disks_.empty() && selected_ < snap_.disks_.size()) {
                    const auto& disk = snap_.disks_[selected_];
                    detail_lines_.push_back(format("# {} · {}", disk.mount_, disk.name_));
                    detail_lines_.push_back(format("Filesystem    {}", disk.filesystem_));
                    detail_lines_.push_back(format("Kind          {}", disk.kind_));
                    detail_lines_.push_back(format("Removable     {}", disk.removable_ ? "yes" : "no"));
                    detail_lines_.push_back(format("Total         {}", bytes(static_cast<double>(disk.total_))));
                    detail_lines_.push_back(format("Used          {}", bytes(static_cast<double>(disk.used_))));
                    detail_lines_.push_back(format("Available     {}", bytes(static_cast<double>(disk.available_))));
                    detail_lines_.push_back(format("Capacity      {:.1f}%", percent(disk.used_, disk.total_)));
                    for (const auto& field: disk.extra_.as_object()) {
                        detail_lines_.push_back(
                                format("{:<13} {}", field.first,
                                       field.second.is_string() ? field.second.as_string() : field.second.dump()));
                    }
                }
                if (storage_section_ == 1 && selected_io_ < snap_.disk_io_.size()) {
                    storage_device_auto_ = false;
                    const auto& io = snap_.disk_io_[selected_io_];
                    if (!detail_lines_.empty()) {
                        detail_lines_.push_back("");
                    }
                    detail_lines_.push_back(format("# block device · {}", io.name_));
                    detail_lines_.push_back(format("Read          {}/s", bytes(io.read_per_sec_)));
                    detail_lines_.push_back(format("Write         {}/s", bytes(io.write_per_sec_)));
                    detail_lines_.push_back(format("Read IOPS     {}", io.read_iops_.has_value()
                                                                               ? format("{:.1f}", *io.read_iops_)
                                                                               : string("—")));
                    detail_lines_.push_back(format("Write IOPS    {}", io.write_iops_.has_value()
                                                                               ? format("{:.1f}", *io.write_iops_)
                                                                               : string("—")));
                    detail_lines_.push_back(format("Await         {}", io.await_ms_.has_value()
                                                                               ? format("{:.2f} ms", *io.await_ms_)
                                                                               : string("—")));
                    detail_lines_.push_back(format("Queue depth   {}", io.queue_depth_.has_value()
                                                                               ? format("{:.2f}", *io.queue_depth_)
                                                                               : string("—")));
                    detail_lines_.push_back(format("Busy          {}", io.busy_percent_.has_value()
                                                                               ? format("{:.1f}%", *io.busy_percent_)
                                                                               : string("—")));
                }
                if (detail_lines_.empty()) {
                    status_ = "No storage device selected.";
                    return;
                }
                popup_title_ = "STORAGE";
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            NEFORCE_NODISCARD vector<size_t> filtered_networks() const {
                vector<size_t> rows;
                for (size_t index = 0; index < snap_.networks_.size(); ++index) {
                    const auto& item = snap_.networks_[index];
                    if (!cfg_.show_loopback_ && is_loopback(item.name_)) {
                        continue;
                    }
                    if (!filter_.empty()) {
                        const string haystack = format("{} {} {}", item.name_, item.address_, item.state_);
                        if (!contains_ignore_case(haystack.view(), lower_ascii(filter_).view())) {
                            continue;
                        }
                    }
                    rows.push_back(index);
                }
                const auto before = [&](size_t left, size_t right) {
                    const auto& a = snap_.networks_[left];
                    const auto& b = snap_.networks_[right];
                    switch (interface_sort_ % 5) {
                        case 1:
                            return a.tx_per_sec_ > b.tx_per_sec_;
                        case 2:
                            return a.name_ < b.name_;
                        case 3:
                            return a.state_ < b.state_;
                        case 4:
                            return a.speed_mbps_.value_or(0) > b.speed_mbps_.value_or(0);
                        default:
                            return a.rx_per_sec_ > b.rx_per_sec_;
                    }
                };
                neforce::tim_sort(rows.begin(), rows.end(), [&](size_t left, size_t right) {
                    return reverse_ ? before(right, left) : before(left, right);
                });
                return rows;
            }

            void open_network_detail() {
                reconcile_current_selection();
                const auto rows = filtered_networks();
                if (rows.empty()) {
                    status_ = "No interface selected.";
                    return;
                }
                const auto& item = snap_.networks_[rows[neforce::min(selected_, rows.size() - 1)]];
                detail_lines_.clear();
                detail_lines_.push_back(format("Interface     {}", item.name_));
                detail_lines_.push_back(format("State         {}", item.state_));
                detail_lines_.push_back(format("Address       {}", item.address_));
                detail_lines_.push_back(format("MAC           {}", item.mac_.empty() ? string("—") : item.mac_));
                detail_lines_.push_back(format("MTU           {}", static_cast<uint64_t>(item.mtu_)));
                detail_lines_.push_back(format("Speed         {}", item.speed_mbps_.has_value()
                                                                           ? format("{} Mbit/s", *item.speed_mbps_)
                                                                           : string("—")));
                detail_lines_.push_back(format("Received      {}", bytes(static_cast<double>(item.rx_total_))));
                detail_lines_.push_back(format("Sent          {}", bytes(static_cast<double>(item.tx_total_))));
                detail_lines_.push_back(format("RX rate       {}", cfg_.display_.rate(item.rx_per_sec_)));
                detail_lines_.push_back(format("TX rate       {}", cfg_.display_.rate(item.tx_per_sec_)));
                detail_lines_.push_back(format("Packets       RX {} · TX {}", static_cast<uint64_t>(item.rx_packets_),
                                               static_cast<uint64_t>(item.tx_packets_)));
                detail_lines_.push_back(format("Errors        RX {} · TX {}", static_cast<uint64_t>(item.rx_errors_),
                                               static_cast<uint64_t>(item.tx_errors_)));
                detail_lines_.push_back(
                        format("Dropped       {}", item.drops_.has_value() ? format("{}", *item.drops_) : string("—")));
                popup_title_ = format("INTERFACE · {}", item.name_);
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            NEFORCE_NODISCARD vector<size_t> filtered_connections() const {
                vector<size_t> rows;
                for (size_t index = 0; index < snap_.connections_.connections_.size(); ++index) {
                    if (connection_matches(snap_.connections_.connections_[index], filter_)) {
                        rows.push_back(index);
                    }
                }
                const auto order = [&](size_t left, size_t right) {
                    const auto& a = snap_.connections_.connections_[left];
                    const auto& b = snap_.connections_.connections_[right];
                    switch (network_sort_ % 5) {
                        case 1:
                            return a.tx_per_sec_.value_or(0.0) > b.tx_per_sec_.value_or(0.0);
                        case 2:
                            return a.state_ < b.state_;
                        case 3:
                            return a.local_ < b.local_;
                        case 4:
                            return (a.pids_.empty() ? 0U : a.pids_[0]) < (b.pids_.empty() ? 0U : b.pids_[0]);
                        default:
                            return a.rx_per_sec_.value_or(0.0) > b.rx_per_sec_.value_or(0.0);
                    }
                };
                neforce::tim_sort(rows.begin(), rows.end(), [&](size_t left, size_t right) {
                    return reverse_ ? order(right, left) : order(left, right);
                });
                return rows;
            }

            void open_connection_detail() {
                reconcile_current_selection();
                const auto rows = filtered_connections();
                if (rows.empty()) {
                    status_ = "No connection selected.";
                    return;
                }
                const auto& item =
                        snap_.connections_.connections_[rows[neforce::min(selected_connection_, rows.size() - 1)]];
                detail_lines_.clear();
                detail_lines_.push_back(format("Protocol      {}", item.protocol_));
                detail_lines_.push_back(format("Local         {}", item.local_));
                detail_lines_.push_back(format("Remote        {}", item.remote_));
                detail_lines_.push_back(format("State         {}", item.state_));
                detail_lines_.push_back(
                        format("PID           {}", item.pids_.empty() ? string("—") : format("{}", item.pids_[0])));
                string names;
                for (const auto& name: item.process_names_) {
                    if (!names.empty()) {
                        names.append(", ");
                    }
                    names.append(name);
                }
                detail_lines_.push_back(format("Processes     {}", names.empty() ? string("—") : names));
                detail_lines_.push_back(format("Received      {}", item.received_.has_value()
                                                                           ? bytes(static_cast<double>(*item.received_))
                                                                           : string("—")));
                detail_lines_.push_back(format("Sent          {}", item.sent_.has_value()
                                                                           ? bytes(static_cast<double>(*item.sent_))
                                                                           : string("—")));
                detail_lines_.push_back(format("RX rate       {}", item.rx_per_sec_.has_value()
                                                                           ? cfg_.display_.rate(*item.rx_per_sec_)
                                                                           : string("—")));
                detail_lines_.push_back(format("TX rate       {}", item.tx_per_sec_.has_value()
                                                                           ? cfg_.display_.rate(*item.tx_per_sec_)
                                                                           : string("—")));
                detail_lines_.push_back(format("Recv queue    {}", item.receive_queue_.has_value()
                                                                           ? format("{} B", *item.receive_queue_)
                                                                           : string("—")));
                detail_lines_.push_back(format("Send queue    {}", item.send_queue_.has_value()
                                                                           ? format("{} B", *item.send_queue_)
                                                                           : string("—")));
                detail_lines_.push_back(format("RTT           {}", item.rtt_ms_.has_value()
                                                                           ? format("{:.2f} ms", *item.rtt_ms_)
                                                                           : string("—")));
                detail_lines_.push_back(format("Retransmits   {}", item.retransmits_.has_value()
                                                                           ? format("{}", *item.retransmits_)
                                                                           : string("—")));
                detail_lines_.push_back(format("Inode         {}", static_cast<uint64_t>(item.inode_)));
                string tags;
                for (const auto& tag: item.tags_) {
                    if (!tags.empty()) {
                        tags.append(", ");
                    }
                    tags.append(tag.display());
                }
                detail_lines_.push_back(format("Tags          {}", tags.empty() ? string("—") : tags));
                popup_title_ = format("CONNECTION · {}:{}", item.protocol_, item.state_);
                popup_ = popup_kind::info;
                popup_scroll_ = 0;
            }

            void draw_network_page(painter& brush, const rect& area) {
                reconcile_current_selection();
                const auto* net = history_.network(cfg_);
                const int top = neforce::min(12, area.h_ / 3);
                if (net != nullptr) {
                    const bool sync_scale = cfg_.display_.network_sync_scale_;
                    const auto fixed = static_cast<double>(cfg_.display_.network_scale_);
                    const double shared = neforce::max(net->rx_peak_, net->tx_peak_);
                    const double rx_max = fixed > 0.0 ? fixed : (sync_scale ? shared : net->rx_peak_);
                    const double tx_max = fixed > 0.0 ? fixed : (sync_scale ? shared : net->tx_peak_);
                    graph_painter(brush, cfg_.display_.graph_symbol_net_)
                            .chart(rect{area.x_, area.y_, area.w_ / 2 - 1, top}, "Download",
                                   {chart_series{"RX", &net->rx_, colors_.mint_}},
                                   tui::canvas_chart_nice_max(neforce::max(1.0, rx_max)), "B/s", chart_window(),
                                   cfg_.display_.network_logarithmic_);
                    graph_painter(brush, cfg_.display_.graph_symbol_net_)
                            .chart(rect{area.x_ + area.w_ / 2, area.y_, area.w_ / 2, top}, "Upload",
                                   {chart_series{"TX", &net->tx_, colors_.blue_}},
                                   tui::canvas_chart_nice_max(neforce::max(1.0, tx_max)), "B/s", chart_window(),
                                   cfg_.display_.network_logarithmic_);
                }
                const rect interfaces{area.x_, area.y_ + top, area.w_, neforce::max(7, (area.h_ - top) / 2)};
                network_interfaces_rect_ = interfaces;
                const auto interface_rows = filtered_networks();
                brush.box(interfaces,
                          format(" INTERFACES · {}/{} ", interface_rows.size(), snap_.networks_.size()).view(),
                          network_section_ == 1 ? colors_.blue_ : colors_.mint_);
                const int iface_width = neforce::max(10, interfaces.w_ * 14 / 100);
                constexpr int state_width = 9;
                const int address_width = neforce::max(18, interfaces.w_ * 32 / 100);
                constexpr int rate_width = 11;
                string iface_header = align_cell("INTERFACE", iface_width) + align_cell("STATE", state_width) +
                                      align_cell("ADDRESS", address_width) + align_cell("RX/s", rate_width, true) +
                                      align_cell("TX/s", rate_width, true) + align_cell("TOTAL", rate_width, true);
                brush.text(interfaces.x_ + 2, interfaces.y_ + 1,
                           truncate_display(iface_header.view(), interfaces.w_ - 4).view(), colors_.muted_);
                {
                    int hx = interfaces.x_ + 2;
                    const int widths[] = {iface_width, state_width, address_width, rate_width, rate_width, rate_width};
                    const size_t action_indices[] = {2, 3, 99, 0, 1, 99};
                    for (size_t i = 0; i < 6; ++i) {
                        if (widths[i] > 0) {
                            hits_.push_back({{hx, interfaces.y_ + 1, widths[i], 1},
                                             hit_action::interface_sort,
                                             action_indices[i]});
                        }
                        hx += widths[i];
                    }
                }
                if (network_section_ == 1 && !interface_rows.empty() && selected_ >= interface_rows.size()) {
                    selected_ = interface_rows.size() - 1;
                }
                int row = interfaces.y_ + 2;
                const auto visible_interfaces = static_cast<size_t>(neforce::max(0, interfaces.h_ - 3));
                for (size_t offset = 0; offset < interface_rows.size() && offset < visible_interfaces;
                     ++offset, ++row) {
                    const auto& item = snap_.networks_[interface_rows[offset]];
                    hits_.push_back({{interfaces.x_ + 1, row, interfaces.w_ - 2, 1},
                                     hit_action::network_interface_row,
                                     offset});
                    if (network_section_ == 1 && offset == selected_) {
                        brush.fill(rect{interfaces.x_ + 1, row, interfaces.w_ - 2, 1}, " ", colors_.text_,
                                   colors_.selection_);
                    }
                    const string line =
                            align_cell(item.name_, iface_width) + align_cell(item.state_, state_width) +
                            align_cell(item.address_, address_width) +
                            align_cell(cfg_.display_.rate(item.rx_per_sec_), rate_width, true) +
                            align_cell(cfg_.display_.rate(item.tx_per_sec_), rate_width, true) +
                            align_cell(bytes(static_cast<double>(item.rx_total_ + item.tx_total_)), rate_width, true);
                    brush.text(interfaces.x_ + 2, row, truncate_display(line.view(), interfaces.w_ - 4).view(),
                               network_section_ == 1 && offset == selected_ ? colors_.text_
                                                                            : state_color(item.state_, colors_));
                }
                const rect connections{area.x_, interfaces.bottom(), area.w_, area.h_ - interfaces.h_ - top};
                const auto rows = filtered_connections();
                network_connections_rect_ = connections;
                const string status = snap_.connections_.status_.empty() ? "sampling" : snap_.connections_.status_;
                brush.box(connections,
                          format(" CONNECTIONS · {}/{} · {} ", static_cast<uint64_t>(rows.size()),
                                 static_cast<uint64_t>(snap_.connections_.connections_.size()), status)
                                  .view(),
                          network_section_ == 0 ? colors_.blue_ : colors_.muted_);
                if (connections.h_ > 2) {
                    constexpr int proto_width = 6;
                    const int endpoint_width = neforce::max(14, connections.w_ * 22 / 100);
                    constexpr int conn_state_width = 11;
                    constexpr int conn_rate_width = 10;
                    const string conn_header =
                            align_cell("PROTO", proto_width) + align_cell("LOCAL", endpoint_width) +
                            align_cell("REMOTE", endpoint_width) + align_cell("STATE", conn_state_width) +
                            align_cell("RX/s", conn_rate_width, true) + align_cell("TX/s", conn_rate_width, true);
                    brush.text(connections.x_ + 2, connections.y_ + 1,
                               truncate_display(conn_header.view(), connections.w_ - 4).view(), colors_.muted_);
                    {
                        int hx = connections.x_ + 2;
                        const int widths[] = {proto_width,      endpoint_width,  endpoint_width,
                                              conn_state_width, conn_rate_width, conn_rate_width};
                        const size_t action_indices[] = {99, 3, 99, 2, 0, 1};
                        for (size_t i = 0; i < 6; ++i) {
                            if (widths[i] > 0) {
                                hits_.push_back({{hx, connections.y_ + 1, widths[i], 1},
                                                 hit_action::connection_sort,
                                                 action_indices[i]});
                            }
                            hx += widths[i];
                        }
                    }
                    if (!rows.empty() && selected_connection_ >= rows.size()) {
                        selected_connection_ = rows.size() - 1;
                    }
                    const int visible = neforce::max(0, connections.h_ - 3);
                    if (selected_connection_ >= connection_offset_ + static_cast<size_t>(visible)) {
                        connection_offset_ = selected_connection_ + 1 - static_cast<size_t>(visible);
                    }
                    connection_offset_ = neforce::min(selected_connection_, connection_offset_);
                    int y = connections.y_ + 2;
                    for (size_t irow = 0; compare_less(irow, visible) && connection_offset_ + irow < rows.size();
                         ++irow) {
                        const size_t index = connection_offset_ + irow;
                        const auto& item = snap_.connections_.connections_[rows[index]];
                        hits_.push_back(
                                {{connections.x_ + 1, y, connections.w_ - 2, 1}, hit_action::connection_row, index});
                        const bool active = network_section_ == 0 && index == selected_connection_;
                        if (active) {
                            brush.fill(rect{connections.x_ + 1, y, connections.w_ - 2, 1}, " ", colors_.text_,
                                       colors_.selection_);
                        }
                        const string line =
                                align_cell(item.protocol_, proto_width) + align_cell(item.local_, endpoint_width) +
                                align_cell(item.remote_, endpoint_width) + align_cell(item.state_, conn_state_width) +
                                align_cell(item.rx_per_sec_.has_value() ? format("{}/s", bytes(*item.rx_per_sec_))
                                                                        : string("—"),
                                           conn_rate_width, true) +
                                align_cell(item.tx_per_sec_.has_value() ? format("{}/s", bytes(*item.tx_per_sec_))
                                                                        : string("—"),
                                           conn_rate_width, true);
                        brush.text(connections.x_ + 2, y, truncate_display(line.view(), connections.w_ - 4).view(),
                                   active ? colors_.text_ : colors_.muted_);
                        ++y;
                    }
                    const rect connection_scrollbar{connections.right() - 2, connections.y_ + 2, 2, visible};
                    hits_.push_back({connection_scrollbar, hit_action::connections_scroll, 0});
                    brush.scrollbar(connection_scrollbar, rows.size(), static_cast<size_t>(visible),
                                    connection_offset_);
                }
            }

