#include <NeFlux/common.hpp>
#include <NeFlux/history.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/numeric/numeric_traits.hpp>
namespace neforce::neflux {

    namespace {

        constexpr double nan_value = numeric_traits<double>::quiet_nan();

        void push_limited(deque<double>& queue, double value, size_t limit) {
            queue.push_back(value);
            while (queue.size() > limit) {
                queue.pop_front();
            }
        }

        void push_optional(deque<double>& queue, optional<uint32_t> value, size_t limit) {
            push_limited(queue, value.has_value() ? static_cast<double>(*value) : nan_value, limit);
        }

        void push_optional(deque<double>& queue, optional<uint64_t> value, size_t limit) {
            push_limited(queue, value.has_value() ? static_cast<double>(*value) : nan_value, limit);
        }

        void push_optional(deque<double>& queue, optional<float> value, size_t limit) {
            push_limited(queue, value.has_value() ? static_cast<double>(*value) : nan_value, limit);
        }

    } // namespace

    void process_history::push(const process& value, optional<uint64_t> gpu_memory, optional<uint32_t> gpu_utilization,
                               size_t limit) {
        push_limited(cpu_, value.cpu_, limit);
        push_limited(ram_, static_cast<double>(value.memory_), limit);
        push_limited(gpu_, gpu_utilization.has_value() ? static_cast<double>(*gpu_utilization) : nan_value, limit);
        push_limited(vram_, gpu_memory.has_value() ? static_cast<double>(*gpu_memory) : nan_value, limit);
    }

    void network_history::push(double rx, double tx, uint64_t received, uint64_t sent, size_t limit) {
        push_limited(rx_, rx, limit);
        push_limited(tx_, tx, limit);
        rx_peak_ = neforce::max(rx_peak_, rx);
        tx_peak_ = neforce::max(tx_peak_, tx);
        rx_session_ += received;
        tx_session_ += sent;
    }

    network_history* history::network(const config& cfg) {
        if (cfg.display_.network_interface_.empty()) {
            return &network_totals_[cfg.show_loopback_ ? 1 : 0];
        }
        const auto found = networks_.find(cfg.display_.network_interface_);
        return found == networks_.end() ? nullptr : &found->second;
    }

    const network_history* history::network(const config& cfg) const {
        if (cfg.display_.network_interface_.empty()) {
            return &network_totals_[cfg.show_loopback_ ? 1 : 0];
        }
        const auto found = networks_.find(cfg.display_.network_interface_);
        return found == networks_.end() ? nullptr : &found->second;
    }

    plot_window history::window(size_t samples) const {
        plot_window result;
        result.samples = samples;
        result.end = sequence_;
        return result;
    }

    plot_window history::time_window(uint64_t seconds) const {
        plot_window result;
        result.samples = timestamps_.size();
        result.end = sequence_;
        result.has_time = true;
        result.time.timestamps = &timestamps_;
        result.time.periods = &periods_;
        result.time.seconds = seconds;
        return result;
    }

    void history::ingest(const snapshot& value, const config& cfg) {
        const size_t limit = cfg.history_;
        ++sequence_;
        const double period = static_cast<double>(cfg.interval_ms_) / 1000.0;
        const optional<double> previous = timestamps_.empty() ? none : optional<double>(timestamps_.back());
        double time = 0.0;
        if (neforce::is_finite(value.elapsed_) && (!previous.has_value() || value.elapsed_ > *previous)) {
            time = neforce::max(0.0, value.elapsed_);
        } else {
            time = (previous.has_value() ? *previous : 0.0) + period;
        }
        timestamps_.push_back(time);
        periods_.push_back(period);
        while (timestamps_.size() > limit) {
            timestamps_.pop_front();
            periods_.pop_front();
        }
        push_limited(cpu_, value.cpu_percent_, limit);
        push_limited(memory_, percent(value.memory_.used_, value.memory_.total_), limit);

        vector<const ::neforce::neflux::network*> networks;
        for (const auto& item: value.networks_) {
            if (cfg.show_loopback_ || !is_loopback(item.name_)) {
                networks.push_back(&item);
            }
        }
        double rx_total = 0.0;
        double tx_total = 0.0;
        for (const auto* item: networks) {
            rx_total += item->rx_per_sec_;
            tx_total += item->tx_per_sec_;
        }
        push_limited(rx_, rx_total, limit);
        push_limited(tx_, tx_total, limit);
        for (size_t include_loopback = 0; include_loopback < 2; ++include_loopback) {
            double rx = 0.0;
            double tx = 0.0;
            for (const auto& item: value.networks_) {
                if (include_loopback == 1 || !is_loopback(item.name_)) {
                    rx += item.rx_per_sec_;
                    tx += item.tx_per_sec_;
                }
            }
            network_peaks_[include_loopback].rx_ = neforce::max(network_peaks_[include_loopback].rx_, rx);
            network_peaks_[include_loopback].tx_ = neforce::max(network_peaks_[include_loopback].tx_, tx);
        }
        vector<string> stale_networks;
        for (const auto& entry: networks_) {
            bool present = false;
            for (const auto& item: value.networks_) {
                if (item.name_ == entry.first) {
                    present = true;
                    break;
                }
            }
            if (!present) {
                stale_networks.push_back(entry.first);
            }
        }
        for (const auto& key: stale_networks) {
            networks_.erase(key);
        }
        double aggregate_rx[2] = {0.0, 0.0};
        double aggregate_tx[2] = {0.0, 0.0};
        uint64_t aggregate_received[2] = {0, 0};
        uint64_t aggregate_sent[2] = {0, 0};
        for (const auto& item: value.networks_) {
            auto& item_history = networks_[item.name_];
            uint64_t received = 0;
            uint64_t sent = 0;
            if (item_history.previous_.has_value()) {
                received = item.rx_total_ >= item_history.previous_->first
                                   ? item.rx_total_ - item_history.previous_->first
                                   : 0;
                sent = item.tx_total_ >= item_history.previous_->second
                               ? item.tx_total_ - item_history.previous_->second
                               : 0;
            }
            item_history.previous_ = pair<uint64_t, uint64_t>{item.rx_total_, item.tx_total_};
            item_history.push(item.rx_per_sec_, item.tx_per_sec_, received, sent, limit);
            for (size_t include_loopback = 0; include_loopback < 2; ++include_loopback) {
                if (include_loopback == 1 || !is_loopback(item.name_)) {
                    aggregate_rx[include_loopback] += item.rx_per_sec_;
                    aggregate_tx[include_loopback] += item.tx_per_sec_;
                    aggregate_received[include_loopback] += received;
                    aggregate_sent[include_loopback] += sent;
                }
            }
        }
        for (size_t index = 0; index < 2; ++index) {
            network_totals_[index].push(aggregate_rx[index], aggregate_tx[index], aggregate_received[index],
                                        aggregate_sent[index], limit);
        }
        double disk_read = 0.0;
        double disk_write = 0.0;
        for (const auto& item: value.disk_io_) {
            if (item.aggregate_) {
                disk_read += item.read_per_sec_;
                disk_write += item.write_per_sec_;
            }
        }
        push_limited(disk_read_, disk_read, limit);
        push_limited(disk_write_, disk_write, limit);

        for (const auto& item: value.gpus_) {
            push_optional(gpu_[gpu_key(item)], item.utilization_, limit);
        }
        set<string> present_gpu;
        for (const auto& item: value.gpus_) {
            present_gpu.insert(gpu_key(item));
        }
        vector<string> stale_gpu;
        for (const auto& entry: gpu_) {
            if (present_gpu.count(entry.first) == 0) {
                stale_gpu.push_back(entry.first);
            }
        }
        for (const auto& key: stale_gpu) {
            gpu_.erase(key);
        }
        set<string> present_uuids;
        for (const auto& item: value.gpus_) {
            present_uuids.insert(item.uuid_);
        }
        vector<string> stale_details;
        for (const auto& entry: gpu_details_) {
            if (present_uuids.count(entry.first) == 0) {
                stale_details.push_back(entry.first);
            }
        }
        for (const auto& key: stale_details) {
            gpu_details_.erase(key);
        }
        for (const auto& item: value.gpus_) {
            auto& details = gpu_details_[item.uuid_];
            push_optional(details.utilization_, item.utilization_, limit);
            push_limited(details.vram_, item.memory_percent().has_value() ? *item.memory_percent() : nan_value, limit);
            push_optional(details.memory_busy_, item.memory_utilization_, limit);
            push_optional(details.encoder_, item.encoder_, limit);
            push_optional(details.decoder_, item.decoder_, limit);
            push_optional(details.power_, item.power_watts_, limit);
            push_optional(details.temperature_, item.temperature_, limit);
            push_optional(details.fan_, item.fan_, limit);
            push_optional(details.clock_, item.clock_mhz_, limit);
            push_optional(details.memory_clock_, item.memory_clock_mhz_, limit);
            push_optional(details.pcie_rx_, item.pcie_rx_kib_, limit);
            push_optional(details.pcie_tx_, item.pcie_tx_kib_, limit);
        }

        map<string, double> values;
        values["CPU total (%)"] = value.cpu_percent_;
        for (const auto& item: value.cpu_breakdown_.as_object()) {
            values[format("CPU {} (%)", item.first)] = item.second.as_number();
        }
        values["RAM (%)"] = percent(value.memory_.used_, value.memory_.total_);
        values["Swap (%)"] = percent(value.memory_.swap_used_, value.memory_.swap_total_);
        const auto option_percent = [](optional<uint64_t> part, uint64_t total) {
            return part.has_value() && total > 0 ? percent(*part, total) : nan_value;
        };
        values["RAM available (%)"] = option_percent(value.memory_.available_, value.memory_.total_);
        values["RAM cached (%)"] = option_percent(value.memory_.cached_, value.memory_.total_);
        values["RAM free (%)"] = option_percent(value.memory_.free_, value.memory_.total_);
        for (size_t index = 0; index < value.cores_.size(); ++index) {
            values[format("CPU core {:03} (%)", static_cast<uint64_t>(index))] = value.cores_[index];
        }
        for (const auto& item: value.sensors_) {
            values[format("Temperature {} (°C)", item.name_)] = item.temperature_;
        }
        for (const auto& item: value.gpus_) {
            for (const auto& pair_value:
                 vector<pair<const char*, optional<uint32_t>>>{{"GPU", item.utilization_},
                                                               {"Encoder", item.encoder_},
                                                               {"Decoder", item.decoder_},
                                                               {"Memory", item.memory_utilization_}}) {
                if (pair_value.second.has_value()) {
                    values[format("{} {} (%)", item.name_, pair_value.first)] = *pair_value.second;
                }
            }
        }
        for (const auto& item: value.networks_) {
            values[format("Network {} RX (B/s)", item.name_)] = item.rx_per_sec_;
            values[format("Network {} TX (B/s)", item.name_)] = item.tx_per_sec_;
        }
        for (const auto& item: value.disk_io_) {
            values[format("Disk {} read (B/s)", item.name_)] = item.read_per_sec_;
            values[format("Disk {} write (B/s)", item.name_)] = item.write_per_sec_;
            if (item.await_ms_.has_value()) {
                values[format("Disk {} latency (ms)", item.name_)] = *item.await_ms_;
            }
        }
        for (const auto& item: value.inventory_.containers_) {
            const string key = container_key(item);
            values[format("{} network RX (B/s)", key)] =
                    item.network_rx_per_sec_.has_value() ? *item.network_rx_per_sec_ : nan_value;
            values[format("{} network TX (B/s)", key)] =
                    item.network_tx_per_sec_.has_value() ? *item.network_tx_per_sec_ : nan_value;
            values[format("{} disk read (B/s)", key)] =
                    item.block_read_per_sec_.has_value() ? *item.block_read_per_sec_ : nan_value;
            values[format("{} disk write (B/s)", key)] =
                    item.block_write_per_sec_.has_value() ? *item.block_write_per_sec_ : nan_value;
            values[format("{} CPU (%)", key)] = item.cpu_.has_value() ? *item.cpu_ : nan_value;
            values[format("{} memory (B)", key)] =
                    item.memory_.has_value() ? static_cast<double>(*item.memory_) : nan_value;
        }
        for (const auto& item: value.inventory_.services_) {
            if (item.memory_.has_value()) {
                values[format("Service {}/{} memory (B)", item.scope_, item.name_)] =
                        static_cast<double>(*item.memory_);
            }
            if (item.cpu_percent_.has_value()) {
                values[format("Service {}/{} main CPU (%)", item.scope_, item.name_)] = *item.cpu_percent_;
            }
        }
        vector<string> stale_series;
        for (const auto& entry: series_) {
            if (values.find(entry.first) == values.end()) {
                stale_series.push_back(entry.first);
            }
        }
        for (const auto& key: stale_series) {
            series_.erase(key);
        }
        for (const auto& item: values) {
            push_limited(series_[item.first], item.second, limit);
        }
    }

} // namespace neforce::neflux
