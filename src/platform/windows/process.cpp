#include "../platform_backend.hpp"

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
        error = "Process output files are not exposed by Windows.";
        return {};
    }

    void service_action(const string& name, const string& action, string& error) {
        try {
            if (action == "restart") {
                run_command("sc", {"stop", name}, 5000);
                run_command("sc", {"start", name}, 5000);
            } else {
                run_command("sc", {action, name}, 5000);
            }
        } catch (const exception& failure) {
            error = failure.what();
        }
    }

    vector<string> service_logs(const string& name, size_t max_lines, string& error) {
        static_cast<void>(name);
        static_cast<void>(max_lines);
        error = "Windows SCM does not expose service logs portably.";
        return {};
    }

    vector<string> process_file_locks(uint32_t pid, string& error) {
        static_cast<void>(pid);
        error = "Advisory file locks are not exposed on Windows.";
        return {};
    }

    vector<string> process_trace(uint32_t pid, string& error) {
        static_cast<void>(pid);
        error = "System-call tracing is not implemented on Windows.";
        return {};
    }

    vector<string> process_backtrace(uint32_t pid, string& error) {
        static_cast<void>(pid);
        error = "Process backtraces are not implemented on Windows.";
        return {};
    }

    vector<string> kernel_messages(string& error) {
        error = "Kernel messages are not exposed on Windows.";
        return {};
    }

    void set_io_priority(uint32_t pid, int priority, string& error) {
        static_cast<void>(pid);
        static_cast<void>(priority);
        error = "I/O priority is not supported on Windows.";
    }

} // namespace neforce::neflux::platform
