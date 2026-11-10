#include "../platform_backend.hpp"
#include <unistd.h>
#include <NeFlux/util.hpp>
#include <NeFlux/discovery.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/system/process.hpp>

namespace neforce::neflux::platform {

    vector<string> process_environment(uint32_t pid, string& error) {
        vector<string> lines;
        try {
            const auto text = read_text_file(format("/proc/{}/environ", pid));
            if (!text.has_value()) {
                error = "Cannot read the process environment; run as its owner.";
                return lines;
            }
            size_t begin = 0;
            for (size_t index = 0; index <= text->size(); ++index) {
                if (index != text->size() && (*text)[index] != '\0') {
                    continue;
                }
                if (index > begin) {
                    lines.push_back(text->substr(begin, index - begin));
                }
                begin = index + 1;
            }
        } catch (const exception& failure) {
            error = format("Cannot inspect process: {}", failure.what());
        }
        return lines;
    }

    vector<string> process_open_files(uint32_t pid, string& error) {
        vector<string> lines;
        try {
            for (const auto& entry: ::neforce::path(format("/proc/{}/fd", pid)).children()) {
                char target[4096] = {};
                const auto length = ::readlink(entry.str().data(), target, sizeof(target) - 1);
                string text = format("{:>5}  ", entry.filename());
                if (length > 0) {
                    text.append(string(target, static_cast<size_t>(length)));
                } else {
                    text.append("unreadable");
                }
                lines.push_back(move(text));
            }
            neforce::tim_sort(lines.begin(), lines.end(),
                              [](const string& left, const string& right) { return left < right; });
        } catch (const exception& failure) {
            error = format("Cannot inspect process: {}", failure.what());
        }
        return lines;
    }

    vector<string> process_logs(uint32_t pid, size_t max_lines, string& error) {
        vector<string> lines;
        const string base = format("/proc/{}/fd/", static_cast<uint64_t>(pid));
        string text;
        for (const int fd: {1, 2}) {
            char target[4096] = {};
            const string link = base + format("{}", fd);
            const ssize_t length = ::readlink(link.data(), target, sizeof(target) - 1);
            if (length <= 0) {
                continue;
            }
            target[length] = '\0';
            const string path(target, static_cast<size_t>(length));
            if (!starts_with(path.view(), "/") || starts_with(path.view(), "/dev/") ||
                starts_with(path.view(), "/proc/")) {
                continue;
            }
            const auto content = read_text_file(path);
            if (!content.has_value()) {
                continue;
            }
            const auto all = split_lines(*content);
            const size_t take = neforce::min(all.size(), max_lines);
            text.append(format("## fd {} · {}\n", fd, path));
            for (size_t index = all.size() - take; index < all.size(); ++index) {
                text.append(all[index]);
                text.push_back('\n');
            }
        }
        if (text.empty()) {
            error = "No readable stdout/stderr file for this process.";
            return lines;
        }
        lines = split_lines(text);
        return lines;
    }

    void service_action(const string& name, const string& action, string& error) {
        try {
            run_command("systemctl", {action, name}, 5000);
        } catch (const exception& failure) {
            error = failure.what();
        }
    }

    vector<string> service_logs(const string& name, size_t max_lines, string& error) {
        try {
            return split_lines(
                    run_command("journalctl", {"-u", name, "-n", format("{}", max_lines), "--no-pager"}, 5000));
        } catch (const exception& failure) {
            error = failure.what();
            return {};
        }
    }

    vector<string> process_file_locks(uint32_t pid, string& error) {
        const auto text = read_text_file("/proc/locks");
        if (!text.has_value()) {
            error = "Cannot read /proc/locks.";
            return {};
        }
        vector<string> lines;
        const string needle = format(" {}", pid);
        for (const auto& line: split_lines(*text)) {
            if (line.find(needle) != string::npos) {
                lines.push_back(line);
            }
        }
        if (lines.empty()) {
            lines.push_back("No advisory file locks for this process.");
        }
        return lines;
    }

    vector<string> process_trace(uint32_t pid, string& error) {
        try {
            return split_lines(run_command(
                    "timeout", {"3", "strace", "-p", format("{}", pid), "-f", "-e", "trace=file,network"}, 5000, true));
        } catch (const exception& failure) {
            error = format("strace unavailable or denied: {}", failure.what());
            return {};
        }
    }

    vector<string> process_backtrace(uint32_t pid, string& error) {
        try {
            return split_lines(run_command(
                    "timeout", {"5", "gdb", "-batch", "-p", format("{}", pid), "-ex", "thread apply all bt"}, 7000,
                    true));
        } catch (const exception& failure) {
            error = format("gdb unavailable or denied: {}", failure.what());
            return {};
        }
    }

    vector<string> kernel_messages(string& error) {
        try {
            return split_lines(run_command("journalctl", {"-k", "-n", "200", "--no-pager"}, 3000, true));
        } catch (const exception&) {
            try {
                return split_lines(run_command("dmesg", {}, 3000, true));
            } catch (const exception& failure) {
                error = format("Kernel messages unavailable: {}", failure.what());
                return {};
            }
        }
    }

    void set_io_priority(uint32_t pid, int priority, string& error) {
        if (priority < 0 || priority > 7) {
            error = "I/O priority must be 0..7.";
            return;
        }
        try {
            run_command("ionice", {"-c", "2", "-n", format("{}", priority), "-p", format("{}", pid)}, 3000);
        } catch (const exception& failure) {
            error = format("ionice failed: {}", failure.what());
        }
    }

} // namespace neforce::neflux::platform
