#include <NeFlux/taskstats.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/memory/memory.hpp>
#if NEFLUX_PLATFORM_LINUX
#    include <linux/genetlink.h>
#    include <linux/netlink.h>
#    include <linux/taskstats.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif
namespace neforce::neflux {

    namespace {

#if NEFLUX_PLATFORM_LINUX
        vector<pair<uint16_t, string_view>> attributes(string_view bytes) {
            vector<pair<uint16_t, string_view>> values;
            while (bytes.size() >= 4) {
                uint16_t size = 0;
                uint16_t kind = 0;
                memory_copy(&size, bytes.data(), 2);
                memory_copy(&kind, bytes.data() + 2, 2);
                if (size < 4 || size > bytes.size()) {
                    break;
                }
                values.push_back({static_cast<uint16_t>(kind & 0x3FFFU), bytes.substr(4, size - 4)});
                const size_t skip = (static_cast<size_t>(size) + 3U) & ~static_cast<size_t>(3U);
                if (skip > bytes.size()) {
                    break;
                }
                bytes = bytes.substr(skip);
            }
            return values;
        }

        array<uint64_t, 3> parse_delays(string_view bytes) {
            array<uint64_t, 3> values{};
            if (bytes.size() < 64) {
                throw value_exception("Truncated TASKSTATS record");
            }
            uint16_t version = 0;
            memory_copy(&version, bytes.data(), 2);
            if (version < 1) {
                throw value_exception("Invalid TASKSTATS version");
            }
            for (size_t index = 0; index < 3; ++index) {
                memory_copy(&values[index], bytes.data() + 24 + index * 16, 8);
            }
            return values;
        }
#endif

    } // namespace

    struct taskstats_collector::impl {
#if NEFLUX_PLATFORM_LINUX
        int descriptor = -1;
        uint16_t family = 0;
        uint32_t sequence = 0;
        bool ready = false;
        string failure;
        int64_t failed_at = 0;
        map<string, pair<array<uint64_t, 3>, int64_t>> previous;

        vector<uint8_t> request(uint16_t target, uint8_t command, uint16_t attribute, string_view payload) {
            ++sequence;
            const size_t length = 4 + payload.size();
            const size_t size = 20 + ((length + 3) & ~static_cast<size_t>(3));
            vector<uint8_t> message(size, 0);
            const auto header_length = static_cast<uint32_t>(size);
            const uint16_t type = target;
            constexpr uint16_t flags = 1;
            memory_copy(message.data(), &header_length, 4);
            memory_copy(message.data() + 4, &type, 2);
            memory_copy(message.data() + 6, &flags, 2);
            memory_copy(message.data() + 8, &sequence, 4);
            message[16] = command;
            message[17] = 1;
            const auto attribute_length = static_cast<uint16_t>(length);
            memory_copy(message.data() + 20, &attribute_length, 2);
            memory_copy(message.data() + 22, &attribute, 2);
            memory_copy(message.data() + 24, payload.data(), payload.size());
            sockaddr_nl address{};
            address.nl_family = AF_NETLINK;
            if (::sendto(descriptor, message.data(), message.size(), 0, reinterpret_cast<sockaddr*>(&address),
                         sizeof(address)) != static_cast<ssize_t>(message.size())) {
                throw value_exception("TASKSTATS send failed");
            }
            vector<uint8_t> reply(16384, 0);
            const auto received = ::recv(descriptor, reply.data(), reply.size(), 0);
            if (received < 20) {
                throw value_exception("TASKSTATS unavailable: kernel support or permissions missing");
            }
            reply.resize(static_cast<size_t>(received));
            uint16_t kind = 0;
            memory_copy(&kind, reply.data() + 4, 2);
            if (kind == 2) {
                int32_t error = 0;
                memory_copy(&error, reply.data() + 16, 4);
                throw value_exception("TASKSTATS requires kernel support/CAP_NET_ADMIN");
            }
            uint32_t reply_sequence = 0;
            memory_copy(&reply_sequence, reply.data() + 8, 4);
            if (reply_sequence != sequence) {
                throw value_exception("TASKSTATS sequence mismatch");
            }
            return vector<uint8_t>(reply.begin() + 20, reply.end());
        }

        void connect_client() {
            if (ready) {
                return;
            }
            descriptor = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
            if (descriptor < 0) {
                throw value_exception("TASKSTATS socket unavailable");
            }
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000;
            setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            const string payload("TASKSTATS\0", 10);
            const auto reply = request(16, 3, 2, string_view(payload.data(), payload.size()));
            for (const auto& item: attributes(string_view(reinterpret_cast<const char*>(reply.data()), reply.size()))) {
                if (item.first == 1 && item.second.size() >= 2) {
                    memory_copy(&family, item.second.data(), 2);
                }
            }
            if (family == 0) {
                throw value_exception("TASKSTATS family missing");
            }
            ready = true;
        }

        array<uint64_t, 3> delays(uint32_t pid, bool thread) {
            const string payload(reinterpret_cast<const char*>(&pid), sizeof(pid));
            const auto reply = request(family, 1, thread ? 1 : 2, string_view(payload.data(), payload.size()));
            for (const auto& outer:
                 attributes(string_view(reinterpret_cast<const char*>(reply.data()), reply.size()))) {
                if (outer.first != 4 && outer.first != 5) {
                    continue;
                }
                for (const auto& inner: attributes(outer.second)) {
                    if (inner.first == 3) {
                        return parse_delays(inner.second);
                    }
                }
            }
            throw value_exception("TASKSTATS record missing");
        }
#endif
    };

    taskstats_collector::taskstats_collector() :
    impl_(make_unique<impl>()) {}

    taskstats_collector::~taskstats_collector() {
#if NEFLUX_PLATFORM_LINUX
        if (impl_->descriptor >= 0) {
            ::close(impl_->descriptor);
        }
#endif
    }

    json taskstats_collector::collect(uint32_t pid, uint64_t start_time, bool thread) {
#if NEFLUX_PLATFORM_LINUX
        if (!impl_->failure.empty() && monotonic_ms() - impl_->failed_at < 30000) {
            throw value_exception(impl_->failure.data());
        }
        try {
            const auto delayacct = read_text_file("/proc/sys/kernel/task_delayacct");
            if (delayacct.has_value() && trim(delayacct->view()) == "0") {
                throw value_exception("kernel.task_delayacct is disabled; counters are unavailable");
            }
            impl_->connect_client();
            const auto now_values = impl_->delays(pid, thread);
            const int64_t now = monotonic_ms();
            const string key = format("{}:{}", pid, start_time);
            json result = json::object();
            const auto found = impl_->previous.find(key);
            optional<array<double, 3>> rates;
            if (found != impl_->previous.end() && now > found->second.second) {
                array<double, 3> values{};
                const double seconds = static_cast<double>(now - found->second.second) / 1000.0;
                for (size_t index = 0; index < 3; ++index) {
                    if (now_values[index] >= found->second.first[index]) {
                        values[index] = static_cast<double>(now_values[index] - found->second.first[index]) / 1e9 /
                                        seconds * 100.0;
                    }
                }
                rates = values;
            }
            impl_->previous[key] = {now_values, now};
            result.set("cpu_delay", rates.has_value() ? json((*rates)[0]) : json());
            result.set("io_delay", rates.has_value() ? json((*rates)[1]) : json());
            result.set("swap_delay", rates.has_value() ? json((*rates)[2]) : json());
            json totals = json::array();
            for (const auto value: now_values) {
                totals.push(json(value));
            }
            result.set("taskstats_delay_total_ns", move(totals));
            result.set("delay_scope", json(thread ? "thread" : "thread group"));
            impl_->failure.clear();
            return result;
        } catch (const exception& error) {
            impl_->failure = error.what();
            impl_->failed_at = monotonic_ms();
            if (impl_->descriptor >= 0) {
                ::close(impl_->descriptor);
                impl_->descriptor = -1;
            }
            impl_->ready = false;
            throw;
        }
#else
        static_cast<void>(pid);
        static_cast<void>(start_time);
        static_cast<void>(thread);
        throw value_exception("TASKSTATS is Linux-only");
#endif
    }

    void taskstats_collector::retain(const set<pair<uint32_t, uint64_t>>& identities) {
#if NEFLUX_PLATFORM_LINUX
        vector<string> stale;
        for (const auto& entry: impl_->previous) {
            bool present = false;
            for (const auto& identity: identities) {
                if (entry.first == format("{}:{}", identity.first, identity.second)) {
                    present = true;
                    break;
                }
            }
            if (!present) {
                stale.push_back(entry.first);
            }
        }
        for (const auto& key: stale) {
            impl_->previous.erase(key);
        }
#else
        static_cast<void>(identities);
#endif
    }

} // namespace neforce::neflux
