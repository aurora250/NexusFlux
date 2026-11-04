#include <NeFlux/collect.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/recording.hpp>
#include <NeFlux/util.hpp>
#include <NeFlux/source.hpp>
#include "platform/platform_backend.hpp"
#include <NeForce/core/async/atomic.hpp>
#include <NeForce/core/async/mutex.hpp>
#include <NeForce/core/async/thread.hpp>
#include <NeForce/core/async/this_thread.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/core/system/signal.hpp>
#include <NeForce/core/utility/optional.hpp>

namespace neforce::neflux {

    namespace {

        optional<system_signal::signal> parse_signal(string_view value) {
            const string_view name = value.starts_with("SIG") ? value.substr(3) : value;
            if (name == "TERM" || name == "terminate") {
                return system_signal::signal::terminate;
            }
            if (name == "KILL" || name == "kill") {
                return system_signal::signal::kill;
            }
            if (name == "INT" || name == "interrupt") {
                return system_signal::signal::interrupt;
            }
            if (name == "STOP" || name == "stop") {
                return system_signal::signal::stop;
            }
            if (name == "CONT" || name == "continue") {
                return system_signal::signal::resume;
            }
            if (name == "HUP" || name == "hangup") {
                return system_signal::signal::hangup;
            }
            if (name == "QUIT" || name == "quit") {
                return system_signal::signal::quit;
            }
            if (name == "USR1" || name == "user1") {
                return system_signal::signal::user1;
            }
            if (name == "USR2" || name == "user2") {
                return system_signal::signal::user2;
            }
            return none;
        }

        class local_source final : public telemetry_source {
        public:
            explicit local_source(source_options options) :
            cfg_(move(options.cfg_)),
            replay_file_(move(options.replay_)),
            replay_(replay_file_.has_value()),
            read_only_(options.read_only_) {
                if (options.full_sampling_) {
                    interest_.gpu_processes_ = true;
                    interest_.connections_ = true;
                    interest_.containers_ = true;
                    interest_.services_ = true;
                    interest_.extras_ = true;
                }
                capabilities_ = platform::capabilities();
            }

            ~local_source() override { stop(); }

            bool start(string& error) override {
                if (started_) {
                    return true;
                }
                started_ = true;
                if (replay_) {
                    try {
                        replay_data_ = replay(*replay_file_);
                    } catch (const exception& failure) {
                        error = failure.what();
                        started_ = false;
                        return false;
                    }
                    if (!replay_data_.empty()) {
                        unique_lock<mutex> guard(mutex_);
                        pending_ = replay_data_[0];
                        has_pending_ = true;
                        inventory_ = replay_data_[0].inventory_;
                        has_inventory_ = true;
                    }
                    next_replay_ms_ = monotonic_ms() + static_cast<int64_t>(cfg_.collection_.interval_ms_);
                    return true;
                }
                worker_ = thread([this] { run_collector(); });
                discovery_worker_ = thread([this] { run_discovery(); });
                return true;
            }

            void stop() noexcept override {
                if (!started_) {
                    return;
                }
                stop_ = true;
                if (worker_.joinable()) {
                    worker_.join();
                }
                if (discovery_worker_.joinable()) {
                    discovery_worker_.join();
                }
                started_ = false;
            }

            void set_interest(const source_interest& interest) override {
                interest_.gpu_processes_ = interest.gpu_processes_;
                interest_.connections_ = interest.connections_;
                interest_.containers_ = interest.containers_;
                interest_.services_ = interest.services_;
                interest_.extras_ = interest.extras_;
                discovery_wake_ = true;
            }

            void request_sample() override { sample_wake_ = true; }

            bool take_snapshot(snapshot& out) override {
                if (replay_) {
                    if (replay_finished_) {
                        return false;
                    }
                    if (monotonic_ms() < next_replay_ms_) {
                        return false;
                    }
                    next_replay_ms_ = monotonic_ms() + static_cast<int64_t>(cfg_.collection_.interval_ms_);
                    if (replay_index_ + 1 < replay_data_.size()) {
                        ++replay_index_;
                    } else {
                        replay_finished_ = true;
                    }
                    out = replay_data_[replay_index_];
                    return true;
                }
                unique_lock<mutex> guard(mutex_);
                if (!has_pending_) {
                    return false;
                }
                out = move(pending_);
                has_pending_ = false;
                return true;
            }

            bool take_inventory(inventory& out) override {
                if (replay_) {
                    return false;
                }
                unique_lock<mutex> guard(mutex_);
                if (!has_inventory_) {
                    return false;
                }
                out = inventory_;
                has_inventory_ = false;
                return true;
            }

            optional<string> take_error() override {
                unique_lock<mutex> guard(mutex_);
                auto value = error_;
                error_.reset();
                return value;
            }

            bool replay_active() const noexcept override { return replay_; }
            size_t replay_index() const noexcept override { return replay_index_; }
            size_t replay_size() const noexcept override { return replay_data_.size(); }

            void seek(int delta) override {
                if (!replay_ || replay_data_.empty()) {
                    return;
                }
                int64_t target = static_cast<int64_t>(replay_index_) + delta;
                target = neforce::max(static_cast<int64_t>(0),
                                      neforce::min(static_cast<int64_t>(replay_data_.size()) - 1, target));
                replay_index_ = static_cast<size_t>(target);
                replay_finished_ = false;
                unique_lock<mutex> guard(mutex_);
                pending_ = replay_data_[replay_index_];
                has_pending_ = true;
                inventory_ = pending_.inventory_;
                has_inventory_ = true;
                next_replay_ms_ = monotonic_ms() + static_cast<int64_t>(cfg_.collection_.interval_ms_);
            }

            string label() const override {
                if (replay_) {
                    return replay_file_.has_value() ? format("replay:{}", *replay_file_) : string("replay");
                }
                return "local";
            }

            const source_capabilities& capabilities() const noexcept override { return capabilities_; }

            bool invoke_action(const source_action& action, string& error) override {
                if (read_only_) {
                    error = "read-only mode";
                    return false;
                }
                if (action.kind_ != "signal" && action.kind_ != "terminate" && action.kind_ != "kill" &&
                    action.kind_ != "stop" && action.kind_ != "continue" && action.kind_ != "nice" &&
                    action.kind_ != "affinity") {
                    error = "unsupported action";
                    return false;
                }
                if (action.start_time_ != 0) {
                    bool matched = false;
                    for (const auto& entry: ::neforce::process::snapshot()) {
                        if (entry.pid == action.pid_ && entry.start_time_ms / 1000 == action.start_time_) {
                            matched = true;
                            break;
                        }
                    }
                    if (!matched) {
                        error = "process identity mismatch";
                        return false;
                    }
                }
                if (action.kind_ == "nice") {
                    const auto value = parse_i64(action.value_.view());
                    if (!value.has_value() || *value < -20 || *value > 19) {
                        error = "nice must be -20..19";
                        return false;
                    }
                    if (!::neforce::process::set_nice(static_cast<::neforce::process::native_id_type>(action.pid_),
                                                      static_cast<int32_t>(*value))) {
                        error = "set_nice failed";
                        return false;
                    }
                    return true;
                }
                if (action.kind_ == "affinity") {
                    const auto value = parse_u64(action.value_.view());
                    if (!value.has_value()) {
                        error = "invalid affinity mask";
                        return false;
                    }
                    if (!::neforce::process::set_affinity(static_cast<::neforce::process::native_id_type>(action.pid_),
                                                          *value)) {
                        error = "set_affinity failed";
                        return false;
                    }
                    return true;
                }
                system_signal::signal signal = system_signal::signal::terminate;
                if (action.kind_ == "kill") {
                    signal = system_signal::signal::kill;
                } else if (action.kind_ == "stop") {
                    signal = system_signal::signal::stop;
                } else if (action.kind_ == "continue") {
                    signal = system_signal::signal::resume;
                } else if (action.kind_ == "signal") {
                    const auto named = parse_signal(action.value_.view());
                    if (!named.has_value()) {
                        error = "unknown signal";
                        return false;
                    }
                    signal = *named;
                }
                if (!system_signal::send_to_process(static_cast<int>(action.pid_), signal)) {
                    error = "signal failed";
                    return false;
                }
                return true;
            }

        private:
            void run_collector() {
                try {
                    collector instance(cfg_.collection_.collect_gpu_);
                    int64_t deadline = monotonic_ms();
                    while (!stop_) {
                        auto sample = instance.collect_with_processes(cfg_, true, interest_.gpu_processes_,
                                                                      interest_.connections_);
                        {
                            unique_lock<mutex> guard(mutex_);
                            pending_ = move(sample);
                            has_pending_ = true;
                            sample_wake_ = false;
                        }
                        deadline = monotonic_ms() + static_cast<int64_t>(cfg_.collection_.interval_ms_);
                        while (!stop_ && !sample_wake_ && monotonic_ms() < deadline) {
                            neforce::this_thread::sleep_for_ms(20);
                        }
                    }
                } catch (const neforce::exception& failure) {
                    unique_lock<mutex> guard(mutex_);
                    error_ = failure.what();
                }
            }

            void run_discovery() {
                inventory_rates rates;
                while (!stop_) {
                    const discovery_sources sources{interest_.containers_, interest_.services_, interest_.extras_};
                    if (sources.containers_ || sources.services_ || sources.extras_) {
                        try {
                            auto found = discover_with_sources(cfg_, sources);
                            rates.apply(found);
                            unique_lock<mutex> guard(mutex_);
                            inventory_ = move(found);
                            has_inventory_ = true;
                        } catch (const neforce::exception& failure) {
                            unique_lock<mutex> guard(mutex_);
                            error_ = failure.what();
                        }
                    }
                    discovery_wake_ = false;
                    for (int step = 0; step < 50 && !stop_; ++step) {
                        if (discovery_wake_) {
                            break;
                        }
                        sleep_ms(100);
                    }
                }
            }

            config cfg_;
            optional<string> replay_file_;
            bool replay_ = false;
            bool read_only_ = false;
            bool started_ = false;
            bool stop_ = false;
            bool sample_wake_ = false;
            bool discovery_wake_ = false;
            source_interest interest_;
            source_capabilities capabilities_;
            thread worker_;
            thread discovery_worker_;
            mutex mutex_;
            snapshot pending_;
            bool has_pending_ = false;
            inventory inventory_;
            bool has_inventory_ = false;
            optional<string> error_;
            vector<snapshot> replay_data_;
            size_t replay_index_ = 0;
            int64_t next_replay_ms_ = 0;
            bool replay_finished_ = false;
        };

    } // namespace

    unique_ptr<telemetry_source> make_local_source(source_options options) {
        return make_unique<local_source>(move(options));
    }

} // namespace neforce::neflux

namespace neforce::neflux {

    collect_policy make_collect_policy(const config& cfg, const bool full_sampling) {
        collect_policy policy;
        policy.interval_ms_ = cfg.collection_.interval_ms_;
        policy.threads_ = cfg.collection_.show_threads_;
        policy.process_network_ = cfg.collection_.process_network_;
        policy.gpu_ = cfg.collection_.collect_gpu_;
        policy.full_sampling_ = full_sampling;
        policy.containers_ = true;
        policy.services_ = true;
        policy.extras_ = true;
        policy.connections_ = true;
        policy.processes_ = true;
        policy.config_exec_ = cfg.collection_.config_exec_;
        policy.monitored_folders_ = cfg.collection_.monitored_folders_;
        policy.probes_ = cfg.collection_.probes_;
        policy.detail_ = full_sampling ? process_detail::full : process_detail::normal;
        return policy;
    }

    config config_from_policy(const collect_policy& policy) {
        config cfg;
        cfg.collection_.interval_ms_ = policy.interval_ms_;
        cfg.collection_.show_threads_ = policy.threads_;
        cfg.collection_.process_network_ = policy.process_network_;
        cfg.collection_.collect_gpu_ = policy.gpu_;
        cfg.collection_.config_exec_ = policy.config_exec_;
        cfg.collection_.monitored_folders_ = policy.monitored_folders_;
        cfg.collection_.probes_ = policy.probes_;
        return cfg;
    }

} // namespace neforce::neflux
