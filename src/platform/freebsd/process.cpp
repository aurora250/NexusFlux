#include "../platform_backend.hpp"
#include <NeFlux/util.hpp>
#include <NeFlux/discovery.hpp>

namespace neforce::neflux::platform {

    vector<string> process_environment(uint32_t pid, string& error) {
        static_cast<void>(pid);
        error = "Process environments are not exposed by this platform.";
        return {};
    }

    vector<string> process_open_files(uint32_t pid, string& error) {
        static_cast<void>(pid);
        error = "Open file descriptors are not exposed by this platform.";
        return {};
    }

    vector<string> process_logs(uint32_t pid, size_t max_lines, string& error) {
        static_cast<void>(pid);
        static_cast<void>(max_lines);
        error = "Process output files are not exposed by this platform.";
        return {};
    }

    void service_action(const string& name, const string& action, string& error) {
        static_cast<void>(name);
        static_cast<void>(action);
        error = "Service actions are not supported on this platform.";
    }

    vector<string> service_logs(const string& name, size_t max_lines, string& error) {
        static_cast<void>(name);
        static_cast<void>(max_lines);
        error = "Service logs are not supported on this platform.";
        return {};
    }

    vector<string> process_file_locks(uint32_t pid, string& error) {
        try {
            return split_lines(run_command("lsof", {"-p", format("{}", pid)}, 3000, true));
        } catch (const exception& failure) {
            error = format("lsof unavailable: {}", failure.what());
            return {};
        }
    }

    vector<string> process_trace(uint32_t pid, string& error) {
        static_cast<void>(pid);
        error = "System-call tracing is not implemented on this platform.";
        return {};
    }

    vector<string> process_backtrace(uint32_t pid, string& error) {
        static_cast<void>(pid);
        error = "Process backtraces are not implemented on this platform.";
        return {};
    }

    vector<string> kernel_messages(string& error) {
        try {
            return split_lines(run_command("dmesg", {}, 3000, true));
        } catch (const exception& failure) {
            error = format("Kernel messages unavailable: {}", failure.what());
            return {};
        }
    }

    void set_io_priority(uint32_t pid, int priority, string& error) {
        static_cast<void>(pid);
        static_cast<void>(priority);
        error = "I/O priority is not supported on this platform.";
    }

} // namespace neforce::neflux::platform
