#include <NeFlux/agent.hpp>
#include <NeFlux/collect.hpp>
#include <NeFlux/discovery.hpp>
#include <NeFlux/protocol.hpp>
#include <NeFlux/source.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/async/atomic.hpp>
#include <NeForce/core/async/mutex.hpp>
#include <NeForce/core/async/thread.hpp>
#include <NeForce/core/async/this_thread.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/system/console.hpp>
#include <NeForce/core/system/signal.hpp>
#include <NeForce/core/utility/optional.hpp>
#ifdef NEFORCE_PLATFORM_WINDOWS
#    include <fcntl.h>
#    include <io.h>
#endif

namespace neforce::neflux {

    namespace {

        mutex g_write_mutex;

        void send(const protocol_message& message) {
            const string line = encode_message(message);
            unique_lock<mutex> guard(g_write_mutex);
            sys_console::instance().print_string(line);
            sys_console::instance().print_string("\n");
            sys_console::instance().flush();
        }

        void send_error(const uint64_t id, string_view code, string_view message, const bool fatal = false) {
            protocol_message reply;
            reply.type_ = "error";
            reply.id_ = id;
            reply.payload_ = make_error_payload(code, message, fatal);
            send(reply);
        }

        string architecture() {
#if defined(NEFORCE_ARCH_X86_64)
            return "x86_64";
#elif defined(NEFORCE_ARCH_AARCH64)
            return "aarch64";
#elif defined(NEFORCE_ARCH_X86)
            return "x86";
#else
            return "unknown";
#endif
        }

        json policy_json(const collect_policy& policy) {
            json value = json::object();
            value.set("interval_ms", json(static_cast<int64_t>(policy.interval_ms_)));
            value.set("processes", json(policy.processes_));
            value.set("threads", json(policy.threads_));
            value.set("process_network", json(policy.process_network_));
            value.set("gpu", json(policy.gpu_));
            value.set("connections", json(policy.connections_));
            value.set("containers", json(policy.containers_));
            value.set("services", json(policy.services_));
            value.set("extras", json(policy.extras_));
            value.set("full_sampling", json(policy.full_sampling_));
            value.set("process_detail",
                      json(policy.detail_ == process_detail::full
                                   ? "full"
                                   : (policy.detail_ == process_detail::summary ? "summary" : "normal")));
            value.set("config_exec", json(policy.config_exec_));
            json folders = json::array();
            for (const auto& folder: policy.monitored_folders_) {
                folders.push(json(folder));
            }
            value.set("monitored_folders", move(folders));
            json probes = json::array();
            for (const auto& probe: policy.probes_) {
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
            value.set("probes", move(probes));
            return value;
        }

        collect_policy parse_policy(const json& value) {
            collect_policy policy;
            if (const json* item = value.get("interval_ms"); item != nullptr && item->is_integer()) {
                policy.interval_ms_ = static_cast<uint64_t>(item->as_integer());
            }
            if (const json* item = value.get("processes"); item != nullptr && item->is_bool()) {
                policy.processes_ = item->as_bool();
            }
            if (const json* item = value.get("threads"); item != nullptr && item->is_bool()) {
                policy.threads_ = item->as_bool();
            }
            if (const json* item = value.get("process_network"); item != nullptr && item->is_bool()) {
                policy.process_network_ = item->as_bool();
            }
            if (const json* item = value.get("gpu"); item != nullptr && item->is_bool()) {
                policy.gpu_ = item->as_bool();
            }
            if (const json* item = value.get("connections"); item != nullptr && item->is_bool()) {
                policy.connections_ = item->as_bool();
            }
            if (const json* item = value.get("containers"); item != nullptr && item->is_bool()) {
                policy.containers_ = item->as_bool();
            }
            if (const json* item = value.get("services"); item != nullptr && item->is_bool()) {
                policy.services_ = item->as_bool();
            }
            if (const json* item = value.get("extras"); item != nullptr && item->is_bool()) {
                policy.extras_ = item->as_bool();
            }
            if (const json* item = value.get("full_sampling"); item != nullptr && item->is_bool()) {
                policy.full_sampling_ = item->as_bool();
            }
            if (const json* item = value.get("process_detail"); item != nullptr && item->is_string()) {
                const auto& text = item->as_string();
                policy.detail_ = text == "full"
                                         ? process_detail::full
                                         : (text == "summary" ? process_detail::summary : process_detail::normal);
            }
            if (policy.interval_ms_ < 250) {
                policy.interval_ms_ = 250;
            }
            if (policy.interval_ms_ > 10000) {
                policy.interval_ms_ = 10000;
            }
            if (const json* item = value.get("config_exec"); item != nullptr && item->is_bool()) {
                policy.config_exec_ = item->as_bool();
            }
            if (const json* folders = value.get("monitored_folders"); folders != nullptr && folders->is_array()) {
                for (const auto& folder: folders->as_array()) {
                    if (folder.is_string()) {
                        policy.monitored_folders_.push_back(folder.as_string());
                    }
                }
            }
            if (const json* probes = value.get("probes"); probes != nullptr && probes->is_array()) {
                for (const auto& item: probes->as_array()) {
                    const json* name = item.get("name");
                    const json* command = item.get("command");
                    if (name == nullptr || !name->is_string() || command == nullptr || !command->is_array()) {
                        continue;
                    }
                    probe_config probe;
                    probe.name_ = name->as_string();
                    for (const auto& part: command->as_array()) {
                        if (part.is_string()) {
                            probe.command_.push_back(part.as_string());
                        }
                    }
                    if (const json* timeout = item.get("timeout_seconds");
                        timeout != nullptr && timeout->is_integer() && timeout->as_integer() > 0) {
                        probe.timeout_seconds_ = static_cast<uint64_t>(timeout->as_integer());
                    }
                    if (!probe.command_.empty()) {
                        policy.probes_.push_back(move(probe));
                    }
                }
            }
            if (policy.full_sampling_) {
                policy.connections_ = true;
                policy.containers_ = true;
                policy.services_ = true;
                policy.extras_ = true;
                policy.detail_ = process_detail::full;
            }
            return policy;
        }

        void apply_detail(snapshot& value, const process_detail detail) {
            if (detail == process_detail::full) {
                return;
            }
            for (auto& item: value.processes_) {
                if (detail == process_detail::summary) {
                    item.command_ = item.name_;
                    continue;
                }
                if (item.command_.size() > 256) {
                    item.command_.resize(256);
                    item.command_.append("…");
                }
            }
        }

        collect_policy policy_from_config(const config& cfg, const bool full_sampling) {
            auto policy = make_collect_policy(cfg, full_sampling);
            policy.processes_ = true;
            return policy;
        }

    } // namespace

    int run_agent(const cli_args& args, config cfg) {
#ifdef NEFORCE_PLATFORM_WINDOWS
        ::_setmode(::_fileno(stdin), _O_BINARY);
        ::_setmode(::_fileno(stdout), _O_BINARY);
#endif
        atomic<bool> running{true};
        atomic<bool> paused{false};
        atomic<bool> sample_wake{false};
        atomic<bool> configured{false};
        atomic<uint64_t> sequence{0};
        mutex state_mutex;
        collect_policy policy = policy_from_config(cfg, args.full_sampling_);
        thread worker;

        source_capabilities capabilities;
        {
            source_options options;
            options.cfg_ = cfg;
            auto local = make_local_source(move(options));
            capabilities = local->capabilities();
        }

        protocol_message hello;
        hello.type_ = "hello";
        json hello_payload = json::object();
        hello_payload.set("agent_version", json("0.2.0"));
        hello_payload.set("protocol", json(static_cast<int64_t>(1)));
        hello_payload.set("os", json(capabilities.platform_ == platform_kind::windows   ? "windows"
                                     : capabilities.platform_ == platform_kind::linux   ? "linux"
                                     : capabilities.platform_ == platform_kind::macos   ? "macos"
                                     : capabilities.platform_ == platform_kind::freebsd ? "freebsd"
                                                                                        : "unknown"));
        hello_payload.set("arch", json(architecture()));
        json caps = json::array();
        if (capabilities.processes_) {
            caps.push(json("processes"));
        }
        if (capabilities.threads_) {
            caps.push(json("threads"));
        }
        if (capabilities.connections_) {
            caps.push(json("connections"));
        }
        if (capabilities.gpu_) {
            caps.push(json("gpu"));
        }
        if (capabilities.containers_) {
            caps.push(json("containers"));
        }
        if (capabilities.services_) {
            caps.push(json("services"));
        }
        if (capabilities.actions_) {
            caps.push(json("actions"));
        }
        if (capabilities.signals_) {
            caps.push(json("signal"));
        }
        if (capabilities.nice_) {
            caps.push(json("nice"));
        }
        if (capabilities.affinity_) {
            caps.push(json("affinity"));
        }
        hello_payload.set("capabilities", move(caps));
        hello.payload_ = move(hello_payload);
        send(hello);

        const auto stop_worker = [&] {
            running = false;
            sample_wake = true;
            if (worker.joinable()) {
                worker.join();
            }
        };

        auto start_worker = [&] {
            if (worker.joinable()) {
                return;
            }
            worker = thread([&] {
                collector instance(policy.gpu_);
                inventory current_inventory;
                int64_t next_discovery = 0;
                while (running) {
                    if (paused) {
                        neforce::this_thread::sleep_for_ms(50);
                        continue;
                    }
                    const auto began = monotonic_ms();
                    collect_policy current_policy;
                    {
                        unique_lock<mutex> guard(state_mutex);
                        current_policy = policy;
                    }
                    auto sample = instance.collect_with_processes(config_from_policy(current_policy),
                                                                  current_policy.processes_, current_policy.gpu_,
                                                                  current_policy.connections_);
                    if (began >= next_discovery) {
                        try {
                            const discovery_sources sources{current_policy.containers_, current_policy.services_,
                                                            current_policy.extras_};
                            current_inventory = discover_with_sources(config_from_policy(current_policy), sources);
                            next_discovery = began + 5000;
                        } catch (const exception& failure) {
                            send_error(0, "discovery", failure.what());
                        }
                    }
                    if (current_policy.containers_ || current_policy.services_ || current_policy.extras_) {
                        sample.apply_inventory(current_inventory);
                    }
                    apply_detail(sample, current_policy.detail_);
                    protocol_message message;
                    message.type_ = "snapshot";
                    json payload = json::object();
                    payload.set("seq", json(static_cast<int64_t>(++sequence)));
                    payload.set("collected_at", json(static_cast<int64_t>(sample.timestamp_)));
                    payload.set("collection_ms", json(sample.collection_ms_));
                    payload.set("data", to_json(sample));
                    message.payload_ = move(payload);
                    send(message);
                    const int64_t deadline = monotonic_ms() + static_cast<int64_t>(current_policy.interval_ms_);
                    while (running && !sample_wake && monotonic_ms() < deadline) {
                        neforce::this_thread::sleep_for_ms(20);
                    }
                    sample_wake = false;
                }
            });
        };

        vector<char> input_buffer(1024 * 1024);
        while (running && std::fgets(input_buffer.data(), static_cast<int>(input_buffer.size()), stdin) != nullptr) {
            string line(input_buffer.data());
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            string error;
            const auto decoded = decode_message(line.view(), error);
            if (!decoded.has_value()) {
                send_error(0, "protocol", error.view(), true);
                continue;
            }
            const auto& message = *decoded;
            if (message.type_ == "configure") {
                const json* policy_json_value = message.payload_.get("policy");
                if (policy_json_value == nullptr || !policy_json_value->is_object()) {
                    send_error(message.id_, "configure", "missing policy");
                    continue;
                }
                {
                    unique_lock<mutex> guard(state_mutex);
                    policy = parse_policy(*policy_json_value);
                    if (policy.full_sampling_) {
                        policy.connections_ = true;
                        policy.containers_ = true;
                        policy.services_ = true;
                        policy.extras_ = true;
                    }
                    configured = true;
                }
                start_worker();
                protocol_message reply;
                reply.type_ = "configured";
                reply.id_ = message.id_;
                json payload = json::object();
                payload.set("policy", policy_json(policy));
                payload.set("actions", json(args.allow_actions_ && !args.read_only_ && capabilities.actions_));
                reply.payload_ = move(payload);
                send(reply);
            } else if (message.type_ == "pause") {
                paused = true;
            } else if (message.type_ == "resume") {
                paused = false;
            } else if (message.type_ == "sample_now") {
                sample_wake = true;
            } else if (message.type_ == "action") {
                if (!args.allow_actions_ || args.read_only_) {
                    send_error(message.id_, "action", "actions disabled");
                    continue;
                }
                const json* kind = message.payload_.get("kind");
                const json* pid = message.payload_.get("pid");
                if (kind == nullptr || !kind->is_string() || pid == nullptr || !pid->is_integer()) {
                    send_error(message.id_, "action", "invalid action");
                    continue;
                }
                source_action action;
                action.kind_ = kind->as_string();
                action.pid_ = static_cast<uint32_t>(pid->as_integer());
                if (const json* start_time = message.payload_.get("start_time");
                    start_time != nullptr && start_time->is_integer()) {
                    action.start_time_ = static_cast<uint64_t>(start_time->as_integer());
                }
                if (const json* value = message.payload_.get("value"); value != nullptr && value->is_string()) {
                    action.value_ = value->as_string();
                } else if (const json* value = message.payload_.get("signal"); value != nullptr && value->is_string()) {
                    action.value_ = value->as_string();
                }
                source_options options;
                options.cfg_ = cfg;
                auto local = make_local_source(move(options));
                string action_error;
                const bool ok = local->invoke_action(action, action_error);
                protocol_message reply;
                reply.type_ = "action_result";
                reply.id_ = message.id_;
                json payload = json::object();
                payload.set("ok", json(ok));
                payload.set("message", json(ok ? "ok" : action_error));
                reply.payload_ = move(payload);
                send(reply);
            } else if (message.type_ == "bye") {
                break;
            } else {
                send_error(message.id_, "protocol", "unknown message type");
            }
        }
        stop_worker();
        protocol_message bye;
        bye.type_ = "bye";
        send(bye);
        static_cast<void>(configured);
        return 0;
    }

} // namespace neforce::neflux
