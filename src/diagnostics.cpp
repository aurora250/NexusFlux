#include <NeFlux/common.hpp>
#include <NeFlux/diagnostics.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/system/process.hpp>
namespace neforce::neflux {

    namespace {

        finding make_finding(const char* domain, const char* severity, string title, string evidence,
                             const char* meaning, const char* next) {
            finding value;
            value.domain_ = domain;
            value.severity_ = severity;
            value.title_ = move(title);
            value.evidence_ = move(evidence);
            value.meaning_ = meaning;
            value.next_ = next;
            return value;
        }

        int severity_rank(const string& severity) {
            if (severity == "URGENT") {
                return 0;
            }
            if (severity == "CHECK") {
                return 1;
            }
            if (severity == "UNKNOWN") {
                return 2;
            }
            return 3;
        }

        optional<double> breakdown(const snapshot& value, const char* key) {
            const json* item = value.cpu_breakdown_.get(key);
            if (item == nullptr || !(item->is_number() || item->is_integer())) {
                return none;
            }
            return item->as_number();
        }

    } // namespace

    vector<finding> findings(const snapshot& value, const config& cfg) {
        vector<finding> out;
        if (value.timestamp_ == 0) {
            out.push_back(make_finding("system", "UNKNOWN", "Waiting for a sample", "No host sample has arrived",
                                       "There is no evidence to diagnose yet.", "Wait for collection to finish."));
            return out;
        }
        if (value.processes_.empty()) {
            const auto fallback = neforce::process::snapshot();
            if (!fallback.empty()) {
                out.push_back(make_finding(
                        "process", "CHECK", "Process inventory is empty",
                        format("procfs exposes {} processes, but the sampler returned none", fallback.size()),
                        "The process list may have been filtered or the sampler failed.",
                        "Open Processes, clear the filter, and check sampling diagnostics."));
            }
        }
        if (value.cpu_percent_ >= cfg.cpu_warning_) {
            const process* top = nullptr;
            for (const auto& item: value.processes_) {
                if (item.thread_) {
                    continue;
                }
                if (top == nullptr || item.cpu_ > top->cpu_) {
                    top = &item;
                }
            }
            out.push_back(make_finding("cpu", "CHECK", "CPU is busy",
                                       format("Host {:.1f}% across {} logical cores; largest process: {}",
                                              value.cpu_percent_, value.cores_.size(),
                                              top != nullptr ? format("{} (PID {}) {:.1f}% of one core", top->name_,
                                                                      static_cast<uint64_t>(top->pid_), top->cpu_)
                                                             : string("unavailable")),
                                       "High CPU can be useful work. A single sample does not prove a runaway process.",
                                       "Processes: sort by CPU; open the owner and command; compare its trend before "
                                       "adjusting priority."));
        }
        size_t hot_cores = 0;
        for (const float core: value.cores_) {
            if (core >= 95.0F) {
                ++hot_cores;
            }
        }
        if (hot_cores > 0 && value.cpu_percent_ < cfg.cpu_warning_) {
            out.push_back(make_finding("cpu", "CHECK", "Some cores are saturated",
                                       format("{}/{} cores >=95%; host {:.1f}%", static_cast<uint64_t>(hot_cores),
                                              value.cores_.size(), value.cpu_percent_),
                                       "Single-threaded work can be CPU-limited even when total CPU looks low.",
                                       "Processes: inspect the busy process and its threads."));
        }
        if (const auto wait = breakdown(value, "iowait"); wait.has_value() && *wait >= 10.0) {
            out.push_back(make_finding(
                    "storage", "CHECK", "Tasks are waiting for I/O", format("CPU I/O wait {:.1f}%", *wait),
                    "The CPU is waiting for data. Device latency, a remote filesystem or memory pressure may be "
                    "involved.",
                    "Storage: compare latency and queue depth; Processes: inspect disk read/write rates."));
        }
        if (const auto steal = breakdown(value, "steal"); steal.has_value() && *steal >= 5.0) {
            out.push_back(make_finding("cpu", "CHECK", "Virtual machine CPU contention",
                                       format("CPU steal {:.1f}%", *steal),
                                       "The hypervisor withheld CPU time from this guest.",
                                       "Check host/VM CPU allocation with the machine operator."));
        }
        if (value.memory_.total_ > 0 && percent(value.memory_.available_, value.memory_.total_) < 10.0) {
            const process* top = nullptr;
            for (const auto& item: value.processes_) {
                if (item.thread_) {
                    continue;
                }
                if (top == nullptr || item.memory_ > top->memory_) {
                    top = &item;
                }
            }
            out.push_back(make_finding(
                    "memory", "CHECK", "Little available memory",
                    format("{} available of {}; swap {} / {}; largest RSS: {}",
                           bytes(static_cast<double>(value.memory_.available_)),
                           bytes(static_cast<double>(value.memory_.total_)),
                           bytes(static_cast<double>(value.memory_.swap_used_)),
                           bytes(static_cast<double>(value.memory_.swap_total_)),
                           top != nullptr ? format("{} PID {} {}", top->name_, static_cast<uint64_t>(top->pid_),
                                                   bytes(static_cast<double>(top->memory_)))
                                          : string("unknown")),
                    "Low available memory can trigger reclaim, swapping or OOM. Used/cache memory alone is not proof "
                    "of a leak.",
                    "Processes: sort by memory; compare the selected process trend and check OOM messages."));
        }
        for (const auto& item: value.pressure_) {
            const bool some = item.some_10_.has_value() && *item.some_10_ >= 10.0;
            const bool full = item.full_10_.has_value() && *item.full_10_ >= 1.0;
            if (!some && !full) {
                continue;
            }
            const string domain = item.resource_ == "io" ? "storage" : item.resource_;
            out.push_back(make_finding(
                    domain.view().data(), "CHECK", format("{} resource stalls", item.resource_),
                    format("PSI some/10s {}%, full/10s {}%",
                           item.some_10_.has_value() ? format("{:.1f}", *item.some_10_) : string("—"),
                           item.full_10_.has_value() ? format("{:.1f}", *item.full_10_) : string("—")),
                    "PSI measures time when tasks could not make progress. It is different from resource utilization.",
                    "Compare this resource's trend with its busiest processes and the incident time."));
        }
        for (const auto& item: value.disks_) {
            if (item.total_ == 0) {
                continue;
            }
            const double used = percent(item.used_, item.total_);
            if (used < 90.0) {
                continue;
            }
            out.push_back(make_finding(
                    "storage", used >= 95.0 ? "URGENT" : "CHECK", format("{} is nearly full", item.mount_),
                    format("{:.1f}% used; {} available, {} total", used, bytes(static_cast<double>(item.available_)),
                           bytes(static_cast<double>(item.total_))),
                    "Writes, logs, builds and databases can fail when this filesystem runs out of space.",
                    "Storage: find large files and check inodes before removing anything."));
        }
        for (const auto& item: value.disk_io_) {
            if (item.await_ms_.has_value() && *item.await_ms_ >= 50.0) {
                out.push_back(make_finding(
                        "storage", "CHECK", format("{} has slow I/O", item.name_),
                        format("AWAIT {:.1f} ms; queue {}; busy {}", *item.await_ms_,
                               item.queue_depth_.has_value() ? format("{:.1f}", *item.queue_depth_) : string("—"),
                               item.busy_percent_.has_value() ? format("{:.1f}%", *item.busy_percent_) : string("—")),
                        "I/O requests are taking time to finish; acceptable latency depends on the device and "
                        "workload.",
                        "Compare queue depth and throughput over time."));
            }
        }
        for (const auto& item: value.gpus_) {
            if (item.memory_percent().has_value() && *item.memory_percent() >= 90.0) {
                out.push_back(make_finding(
                        "gpu", "CHECK", format("GPU {} memory is nearly full", static_cast<uint64_t>(item.id_)),
                        format("{}: {} / {}", item.name_,
                               item.memory_used_.has_value() ? bytes(static_cast<double>(*item.memory_used_))
                                                             : string("—"),
                               item.memory_capacity().has_value() ? bytes(static_cast<double>(*item.memory_capacity()))
                                                                  : string("—")),
                        "New GPU allocations may fail. High VRAM use alone does not show a memory leak.",
                        "GPU: inspect VRAM owners and lower batch/context size."));
            }
            if (item.temperature_.has_value() && *item.temperature_ >= cfg.gpu_temperature_warning_) {
                out.push_back(make_finding("gpu", "CHECK", format("GPU {} is hot", static_cast<uint64_t>(item.id_)),
                                           format("{}°C; warning {}°C", *item.temperature_,
                                                  static_cast<uint64_t>(cfg.gpu_temperature_warning_)),
                                           "High temperature can reduce clock speeds. The threshold is a warning, not "
                                           "proof of throttling.",
                                           "GPU: inspect clocks and power; check cooling and workload."));
            }
        }
        for (const auto& item: value.networks_) {
            if (item.errors_ > 0 || (item.drops_.has_value() && *item.drops_ > 0)) {
                out.push_back(make_finding(
                        "network", "INFO", format("{} has recorded errors/drops", item.name_),
                        format("Lifetime errors {}; drops {}", static_cast<uint64_t>(item.errors_),
                               item.drops_.has_value() ? format("{}", *item.drops_) : string("not reported")),
                        "These are cumulative counters; they may predate the current incident.",
                        "Compare successive samples; check link quality only if counters keep increasing."));
            }
        }
        size_t syn_sent = 0;
        size_t close_wait = 0;
        for (const auto& item: value.connections_.connections_) {
            if (item.state_ == "SYN_SENT") {
                ++syn_sent;
            } else if (item.state_ == "CLOSE_WAIT") {
                ++close_wait;
            }
        }
        if (syn_sent > 0) {
            out.push_back(make_finding(
                    "network", "CHECK", "Connections waiting for a reply",
                    format("{} sockets in SYN_SENT", static_cast<uint64_t>(syn_sent)),
                    "A destination has not yet accepted the TCP connection. A short transition can be normal.",
                    "Network: filter state:SYN_SENT and inspect the owner and destination."));
        }
        if (close_wait >= 10) {
            out.push_back(
                    make_finding("network", "CHECK", "Many peer-closed connections",
                                 format("{} sockets in CLOSE_WAIT", static_cast<uint64_t>(close_wait)),
                                 "Peers closed these sockets, while local applications still hold them.",
                                 "Network: filter state:CLOSE_WAIT and inspect that application's socket lifecycle."));
        }
        for (const auto& item: value.processes_) {
            if (item.thread_ || item.state_ != "Zombie") {
                continue;
            }
            out.push_back(
                    make_finding("process", "CHECK", format("{} is a zombie", item.name_),
                                 format("PID {}, parent {}", static_cast<uint64_t>(item.pid_),
                                        item.parent_.has_value() ? format("{}", *item.parent_) : string("—")),
                                 "The process exited; its parent has not collected its exit status. Killing the zombie "
                                 "will not reclaim it.",
                                 "Processes: inspect the parent and fix child reaping in the parent application."));
            if (out.size() >= 100) {
                break;
            }
        }
        for (const auto& item: value.inventory_.containers_) {
            if (item.state_ == "restarting" || item.state_ == "dead" ||
                contains_ignore_case(item.status_.view(), "unhealthy")) {
                out.push_back(
                        make_finding("container", "CHECK", format("Container {} needs attention", item.name_),
                                     format("{} · {} · {}", item.runtime_, item.state_, item.status_),
                                     "A restart loop or failing health check may make the application unavailable.",
                                     "Containers: inspect the cause before restarting."));
            }
        }
        for (const auto& item: value.inventory_.services_) {
            if (item.active_ != "failed") {
                continue;
            }
            out.push_back(make_finding(
                    "service", "CHECK", format("Service {} failed", item.name_),
                    format("scope {} · {} / {}", item.scope_, item.active_, item.sub_),
                    "The service manager reports a failed unit; this does not identify the underlying error.",
                    "Services: inspect logs and exit status before choosing restart."));
        }
        neforce::tim_sort(out.begin(), out.end(), [](const finding& left, const finding& right) {
            return severity_rank(left.severity_) < severity_rank(right.severity_);
        });
        if (out.size() > 100) {
            out.resize(100);
        }
        return out;
    }

    string report(const snapshot& value, const config& cfg) {
        const auto items = findings(value, cfg);
        string out = format("Diagnosis · {} · {}\n", value.hostname_, value.os_);
        out.append(format("CPU {:.1f}% · RAM {:.1f}% · {} processes · {} alerts\n\n", value.cpu_percent_,
                          percent(value.memory_.used_, value.memory_.total_), value.processes_.size(),
                          value.alerts_.size()));
        if (items.empty()) {
            out.append("No resource problems detected in this sample.\n");
            return out;
        }
        for (const auto& item: items) {
            out.append(format("[{}] {} · {}\n", item.severity_, item.domain_, item.title_));
            out.append(format("  Evidence  {}\n", item.evidence_));
            out.append(format("  Meaning   {}\n", item.meaning_));
            out.append(format("  Next      {}\n\n", item.next_));
        }
        return out;
    }

    string summary(const snapshot& value, const config& cfg) {
        if (value.timestamp_ == 0) {
            return "Discovering host…";
        }
        const auto items = findings(value, cfg);
        if (items.empty()) {
            return "No resource alerts";
        }
        string out = format("{} finding{} · ", items.size(), items.size() == 1 ? "" : "s");
        for (size_t index = 0; index < items.size() && index < 3; ++index) {
            if (index != 0) {
                out.append(" · ");
            }
            out.append(items[index].title_);
        }
        if (items.size() > 3) {
            out.append(" …");
        }
        return out;
    }

    bool relevant(const string& view, const string& domain) {
        if (view == "overview" || view == "system" || view == "explorer" || view == "history" || view == "events") {
            return true;
        }
        if (view == "processes") {
            return domain == "cpu" || domain == "memory" || domain == "process" || domain == "storage";
        }
        if (view == "gpu") {
            return domain == "gpu";
        }
        if (view == "storage") {
            return domain == "storage";
        }
        if (view == "network") {
            return domain == "network";
        }
        if (view == "containers") {
            return domain == "container";
        }
        if (view == "services") {
            return domain == "service";
        }
        return false;
    }

    json to_json(const finding& value) {
        json out = json::object();
        out.set("domain", value.domain_);
        out.set("severity", value.severity_);
        out.set("title", value.title_);
        out.set("evidence", value.evidence_);
        out.set("meaning", value.meaning_);
        out.set("next", value.next_);
        return out;
    }

    vector<string> resource_alerts(const snapshot& value, const config& cfg) {
        vector<string> out;
        for (const auto& item: findings(value, cfg)) {
            if (item.severity_ == "URGENT" || item.severity_ == "CHECK") {
                out.push_back(item.title_);
            }
        }
        return out;
    }

} // namespace neforce::neflux
