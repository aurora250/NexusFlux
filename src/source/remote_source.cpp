#include <NeFlux/machine.hpp>
#include <NeFlux/model.hpp>
#include <NeFlux/protocol.hpp>
#include <NeFlux/source.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/async/atomic.hpp>
#include <NeForce/core/async/mutex.hpp>
#include <NeForce/core/async/thread.hpp>
#include <NeForce/core/async/this_thread.hpp>
#include <NeForce/core/container/deque.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/system/pipe.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/core/utility/optional.hpp>

namespace neforce::neflux {

    namespace {

        class remote_source final : public telemetry_source {
        public:
            remote_source(machine_target target, source_options options) :
            target_(move(target)),
            cfg_(move(options.cfg_)),
            read_only_(options.read_only_),
            allow_actions_(options.allow_actions_),
            full_sampling_(options.full_sampling_) {
                capabilities_.platform_ = platform_kind::unknown;
                capabilities_.actions_ = allow_actions_;
                if (options.full_sampling_) {
                    interest_.gpu_processes_ = true;
                    interest_.connections_ = true;
                    interest_.containers_ = true;
                    interest_.services_ = true;
                    interest_.extras_ = true;
                }
            }

            ~remote_source() override { stop(); }

            bool start(string& error) override {
                if (started_) {
                    return true;
                }
                const string destination =
                        target_.user_.empty() ? target_.host_ : format("{}@{}", target_.user_, target_.host_);
                vector<string> args{"-T",
                                    "-o",
                                    "BatchMode=yes",
                                    "-o",
                                    "ConnectTimeout=5",
                                    "-p",
                                    format("{}", target_.port_),
                                    destination,
                                    "--",
                                    target_.agent_path_,
                                    "agent",
                                    "--stdio"};
                if (read_only_) {
                    args.push_back("--read-only");
                }
                if (allow_actions_) {
                    args.push_back("--allow-actions");
                }
                try {
                    to_child_ = pipe(true, false);
                    from_child_ = pipe(true, false);
                    child_ = make_unique<::neforce::process>();
                    child_->set_external_stdin(to_child_);
                    child_->set_external_stdout(from_child_);
                    child_->start("ssh", args);
                    to_child_.close_read();
                    from_child_.close_write();
                } catch (const exception& failure) {
                    error = failure.what();
                    return false;
                }
                started_ = true;
                reader_ = thread([this] { run(); });
                const int64_t deadline = monotonic_ms() + 10000;
                while (!hello_received_ && running_ && monotonic_ms() < deadline) {
                    neforce::this_thread::sleep_for_ms(20);
                }
                if (!hello_received_) {
                    unique_lock<mutex> guard(mutex_);
                    error = error_.has_value() ? *error_ : string("agent handshake timeout");
                    return false;
                }
                return true;
            }

            void stop() noexcept override {
                if (!started_) {
                    return;
                }
                running_ = false;
                if (child_ != nullptr) {
                    try {
                        child_->terminate();
                    } catch (const exception&) {
                    }
                }
                if (reader_.joinable()) {
                    reader_.join();
                }
                to_child_.close();
                from_child_.close();
                child_.reset();
                started_ = false;
            }

            void set_interest(const source_interest& interest) override {
                interest_ = interest;
                send_configure();
            }

            void request_sample() override {
                protocol_message message;
                message.type_ = "sample_now";
                send_line(encode_message(message));
            }

            bool take_snapshot(snapshot& out) override {
                unique_lock<mutex> guard(mutex_);
                if (pending_.empty()) {
                    return false;
                }
                out = move(pending_.front());
                pending_.pop_front();
                inventory_ = out.inventory_;
                has_inventory_ = true;
                return true;
            }

            bool take_inventory(inventory& out) override {
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

            bool replay_active() const noexcept override { return false; }
            size_t replay_index() const noexcept override { return 0; }
            size_t replay_size() const noexcept override { return 0; }
            void seek(int) override {}

            string label() const override {
                return target_.kind_ == machine_kind::qemu ? format("qemu:{}", target_.name_)
                                                           : format("ssh:{}", target_.name_);
            }

            const source_capabilities& capabilities() const noexcept override { return capabilities_; }

            bool invoke_action(const source_action& action, string& error) override {
                if (!allow_actions_ || !capabilities_.actions_) {
                    error = "remote actions disabled";
                    return false;
                }
                protocol_message message;
                message.type_ = "action";
                message.id_ = ++next_action_id_;
                json payload = json::object();
                payload.set("kind", json(action.kind_));
                payload.set("pid", json(static_cast<int64_t>(action.pid_)));
                payload.set("start_time", json(static_cast<int64_t>(action.start_time_)));
                payload.set("value", json(action.value_));
                message.payload_ = move(payload);
                {
                    unique_lock<mutex> guard(action_mutex_);
                    action_id_ = message.id_;
                    action_done_ = false;
                    action_ok_ = false;
                    action_message_.clear();
                }
                send_line(encode_message(message));
                for (int attempt = 0; attempt < 150; ++attempt) {
                    {
                        unique_lock<mutex> guard(action_mutex_);
                        if (action_done_) {
                            if (!action_ok_) {
                                error = action_message_;
                            }
                            return action_ok_;
                        }
                    }
                    neforce::this_thread::sleep_for_ms(20);
                }
                error = "remote action timeout";
                return false;
            }

        private:
            void send_line(const string& line) {
                unique_lock<mutex> guard(write_mutex_);
                const string data = line + "\n";
                size_t offset = 0;
                while (offset < data.size() && running_) {
                    const int written = to_child_.write(data.data() + offset, data.size() - offset);
                    if (written <= 0) {
                        break;
                    }
                    offset += static_cast<size_t>(written);
                }
            }

            void send_configure() {
                protocol_message message;
                message.type_ = "configure";
                message.id_ = ++next_control_id_;
                json payload = json::object();
                payload.set("session", json("s-1"));
                json policy = json::object();
                policy.set("interval_ms", json(static_cast<int64_t>(cfg_.collection_.interval_ms_)));
                policy.set("processes", json(true));
                policy.set("threads", json(cfg_.collection_.show_threads_));
                policy.set("process_network", json(cfg_.collection_.process_network_));
                policy.set("gpu", json(cfg_.collection_.collect_gpu_));
                policy.set("connections", json(interest_.connections_));
                policy.set("containers", json(interest_.containers_));
                policy.set("services", json(interest_.services_));
                policy.set("extras", json(interest_.extras_));
                policy.set("process_detail", json(full_sampling_ ? "full" : "normal"));
                policy.set("full_sampling", json(full_sampling_));
                policy.set("config_exec", json(cfg_.collection_.config_exec_));
                json folders = json::array();
                for (const auto& folder: cfg_.collection_.monitored_folders_) {
                    folders.push(json(folder));
                }
                policy.set("monitored_folders", move(folders));
                json probes = json::array();
                for (const auto& probe: cfg_.collection_.probes_) {
                    json item = json::object();
                    item.set("name", json(probe.name_));
                    json command = json::array();
                    for (const auto& part: probe.command_) {
                        command.push(json(part));
                    }
                    item.set("command", move(command));
                    item.set("timeout_seconds", json(static_cast<int64_t>(probe.timeout_seconds_)));
                    probes.push(move(item));
                }
                policy.set("probes", move(probes));
                payload.set("policy", move(policy));
                message.payload_ = move(payload);
                send_line(encode_message(message));
            }

            void queue_error(string message) {
                unique_lock<mutex> guard(mutex_);
                error_ = move(message);
            }

            void handle_line(const string_view line) {
                string error;
                const auto decoded = decode_message(line, error);
                if (!decoded.has_value()) {
                    queue_error(error);
                    return;
                }
                const auto& message = *decoded;
                if (message.type_ == "hello") {
                    if (const json* os = message.payload_.get("os"); os != nullptr && os->is_string()) {
                        const auto& name = os->as_string();
                        capabilities_.platform_ = name == "windows"   ? platform_kind::windows
                                                  : name == "linux"   ? platform_kind::linux
                                                  : name == "macos"   ? platform_kind::macos
                                                  : name == "freebsd" ? platform_kind::freebsd
                                                                      : platform_kind::unknown;
                    }
                    if (const json* arch = message.payload_.get("arch"); arch != nullptr && arch->is_string()) {
                        capabilities_.arch_ = arch->as_string();
                    }
                    const json* caps = message.payload_.get("capabilities");
                    if (caps != nullptr && caps->is_array()) {
                        capabilities_.processes_ = true;
                        for (const auto& item: caps->as_array()) {
                            if (!item.is_string()) {
                                continue;
                            }
                            const auto& name = item.as_string();
                            if (name == "threads") {
                                capabilities_.threads_ = true;
                            } else if (name == "connections") {
                                capabilities_.connections_ = true;
                            } else if (name == "gpu") {
                                capabilities_.gpu_ = true;
                            } else if (name == "containers") {
                                capabilities_.containers_ = true;
                            } else if (name == "services") {
                                capabilities_.services_ = true;
                            } else if (name == "actions") {
                                capabilities_.actions_ = true;
                            } else if (name == "signal") {
                                capabilities_.signals_ = true;
                            } else if (name == "nice") {
                                capabilities_.nice_ = true;
                            } else if (name == "affinity") {
                                capabilities_.affinity_ = true;
                            }
                        }
                    }
                    hello_received_ = true;
                    send_configure();
                } else if (message.type_ == "snapshot") {
                    const json* data = message.payload_.get("data");
                    if (data == nullptr || !data->is_object()) {
                        queue_error("snapshot lacks data");
                        return;
                    }
                    try {
                        snapshot value = snapshot_from_json(*data);
                        unique_lock<mutex> guard(mutex_);
                        if (pending_.size() >= 4) {
                            pending_.pop_front();
                        }
                        pending_.push_back(move(value));
                    } catch (const exception& failure) {
                        queue_error(failure.what());
                    }
                } else if (message.type_ == "error") {
                    const json* text = message.payload_.get("message");
                    queue_error(text != nullptr && text->is_string() ? text->as_string() : string("agent error"));
                } else if (message.type_ == "action_result") {
                    unique_lock<mutex> guard(action_mutex_);
                    if (message.id_ == action_id_) {
                        const json* ok = message.payload_.get("ok");
                        const json* text = message.payload_.get("message");
                        action_ok_ = ok != nullptr && ok->is_bool() && ok->as_bool();
                        action_message_ = text != nullptr && text->is_string() ? text->as_string() : string();
                        action_done_ = true;
                    }
                } else if (message.type_ == "configured") {
                    const json* actions = message.payload_.get("actions");
                    if (actions != nullptr && actions->is_bool()) {
                        capabilities_.actions_ = actions->as_bool();
                    }
                }
            }

            void run() {
                string buffer;
                char chunk[65536];
                while (running_) {
                    const int bytes = from_child_.read(chunk, sizeof(chunk));
                    if (bytes <= 0) {
                        if (running_) {
                            queue_error("agent connection closed");
                            running_ = false;
                        }
                        break;
                    }
                    buffer.append(chunk, static_cast<size_t>(bytes));
                    parse_lines(buffer);
                }
            }

            void parse_lines(string& buffer) {
                size_t begin = 0;
                while (true) {
                    const size_t newline = buffer.find('\n', begin);
                    if (newline == string::npos) {
                        break;
                    }
                    const string_view line(buffer.data() + begin, newline - begin);
                    handle_line(line);
                    begin = newline + 1;
                }
                if (begin > 0) {
                    buffer.erase(0, begin);
                }
                if (buffer.size() > 32ULL * 1024ULL * 1024ULL) {
                    queue_error("agent protocol line too large");
                    running_ = false;
                    buffer.clear();
                }
            }

            machine_target target_;
            config cfg_;
            bool read_only_ = false;
            bool allow_actions_ = false;
            bool full_sampling_ = false;
            bool started_ = false;
            atomic<bool> running_{true};
            atomic<bool> hello_received_{false};
            source_interest interest_;
            source_capabilities capabilities_;
            unique_ptr<::neforce::process> child_;
            pipe to_child_;
            pipe from_child_;
            thread reader_;
            mutex mutex_;
            mutex write_mutex_;
            deque<snapshot> pending_;
            inventory inventory_;
            bool has_inventory_ = false;
            optional<string> error_;
            uint64_t next_control_id_ = 0;
            uint64_t next_action_id_ = 0;
            mutex action_mutex_;
            uint64_t action_id_ = 0;
            bool action_done_ = false;
            bool action_ok_ = false;
            string action_message_;
        };

    } // namespace

    unique_ptr<telemetry_source> make_remote_source(machine_target target, source_options options) {
        return make_unique<remote_source>(move(target), move(options));
    }

} // namespace neforce::neflux
