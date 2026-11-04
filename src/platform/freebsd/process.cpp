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

} // namespace neforce::neflux::platform
