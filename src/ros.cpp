#include <NeFlux/ros.hpp>

#include <NeFlux/common.hpp>
#include <NeFlux/platform.hpp>
#include <NeFlux/util.hpp>

#include <NeForce/core/system/process.hpp>

#if NEFLUX_PLATFORM_LINUX
#    include <unistd.h>
#endif

namespace neforce::neflux {

    namespace {

        bool communication_key(string_view key) {
            return starts_with(key, "ROS_") || starts_with(key, "RMW_") || starts_with(key, "ZENOH_") ||
                   starts_with(key, "CYCLONEDDS_") || starts_with(key, "FASTDDS_") || starts_with(key, "FASTRTPS_") ||
                   key == "RUST_LOG";
        }

        bool allowed_environment_key(string_view key) {
            if (communication_key(key)) {
                return true;
            }
            for (const char* name:
                 {"PATH", "HOME", "USER", "LOGNAME", "TMPDIR", "LANG", "PYTHONPATH", "PYTHONHOME", "LD_LIBRARY_PATH",
                  "AMENT_PREFIX_PATH", "COLCON_PREFIX_PATH", "CMAKE_PREFIX_PATH"}) {
                if (key == name) {
                    return true;
                }
            }
            return false;
        }

        map<string, string> parse_environment(const string& text) {
            map<string, string> environment;
            size_t begin = 0;
            for (size_t index = 0; index <= text.size(); ++index) {
                if (index != text.size() && text[index] != '\0') {
                    continue;
                }
                if (index > begin) {
                    const auto equals = text.find('=', begin);
                    if (equals != string::npos && equals < index) {
                        const string key = text.substr(begin, equals - begin);
                        if (allowed_environment_key(key.view())) {
                            environment[key] = text.substr(equals + 1, index - equals - 1);
                        }
                    }
                }
                begin = index + 1;
            }
            return environment;
        }

        string run_session(const ros_session& session, const string& program, const vector<string>& arguments,
                           int timeout_ms) {
            ::neforce::process handle;
            handle.set_capture_stdout(true).set_capture_stderr(true);
            handle.set_work_dir(session.cwd_);
            for (const auto& entry: session.environment_) {
                handle.set_env(entry.first, entry.second);
            }
            handle.start(program, arguments);
            const int code = handle.wait(timeout_ms);
            string output = handle.stdout_output();
            const auto& error = handle.stderr_output();
            if (code == -1) {
                handle.terminate();
                if (output.empty()) {
                    return format("{}\nCommand exceeded {} ms and was stopped.", output, timeout_ms);
                }
                output.append(format("\n[command still running after {} ms; partial output]", timeout_ms));
            } else if (code != 0 && output.empty()) {
                output = error;
            } else if (!error.empty() && code != 0) {
                output.append("\n[stderr]\n");
                output.append(error);
            }
            if (trim(output.view()).empty()) {
                return format("{} exited with code {} and no output", program, code);
            }
            return output;
        }

    } // namespace

    optional<ros_session> open_ros_session(const process& value, string& error) {
#if NEFLUX_PLATFORM_LINUX
        const string root = format("/proc/{}", value.pid_);
        for (const char* space: {"net", "mnt"}) {
            char process_target[256] = {};
            char self_target[256] = {};
            const string process_link = format("{}/ns/{}", root, space);
            const string self_link = format("/proc/self/ns/{}", space);
            const auto process_length = ::readlink(process_link.data(), process_target, sizeof(process_target) - 1);
            const auto self_length = ::readlink(self_link.data(), self_target, sizeof(self_target) - 1);
            if (process_length <= 0 || self_length <= 0 ||
                string_view(process_target, static_cast<size_t>(process_length)) !=
                        string_view(self_target, static_cast<size_t>(self_length))) {
                error = format("ROS process has a different {} namespace. Run nexus-flux inside its namespace.", space);
                return none;
            }
        }
        const auto environ = read_text_file(root + "/environ");
        if (!environ.has_value()) {
            error = "Cannot read the ROS process environment; run as the process owner.";
            return none;
        }
        if (environ->size() > 256 * 1024) {
            error = "Process environment exceeds the 256 KiB limit";
            return none;
        }
        ros_session session;
        session.pid_ = value.pid_;
        session.start_time_ = value.start_time_;
        session.environment_ = parse_environment(*environ);
        char cwd[4096] = {};
        const auto cwd_length = ::readlink((root + "/cwd").data(), cwd, sizeof(cwd) - 1);
        if (cwd_length > 0) {
            session.cwd_ = string(cwd, static_cast<size_t>(cwd_length));
        } else {
            session.cwd_ = "/";
        }
        const auto version = session.environment_.find("ROS_VERSION");
        if (version != session.environment_.end() && version->second == "1") {
            session.version_ = 1;
        } else if (version != session.environment_.end() && version->second == "2") {
            session.version_ = 2;
        } else if (value.command_.find("--ros-args") != string::npos || value.name_ == "ros2" ||
                   value.name_ == "rviz2" || starts_with(value.name_.view(), "component_container")) {
            session.version_ = 2;
        } else if (value.name_ == "roscore" || value.name_ == "rosmaster" || value.name_ == "roslaunch" ||
                   value.name_ == "rosout" || value.name_ == "rostopic" || value.name_ == "rosnode") {
            session.version_ = 1;
        }
        if (session.version_ == 0) {
            error = "Cannot determine ROS 1 / ROS 2 from this process. Launch it from a sourced ROS environment.";
            return none;
        }
        return session;
#else
        static_cast<void>(value);
        error = "ROS session inspection is currently Linux-only";
        return none;
#endif
    }

    string run_ros_query(const ros_session& session, ros_query type, const string& argument) {
        if (type == ros_query::environment) {
            string out;
            for (const auto& entry: session.environment_) {
                out.append(format("{}={}\n", entry.first, entry.second));
            }
            return out;
        }
        if (type == ros_query::context) {
            return format("ROS {} · PID {}\nWorking directory: {}\n\n{}", session.version_,
                          static_cast<uint64_t>(session.pid_), session.cwd_,
                          run_ros_query(session, ros_query::environment));
        }
        if (session.version_ == 2) {
            if (type == ros_query::topics) {
                return run_session(session, "ros2", {"topic", "list", "-t"}, 4000);
            }
            if (type == ros_query::nodes) {
                return run_session(session, "ros2", {"node", "list"}, 4000);
            }
            if (type == ros_query::services) {
                return run_session(session, "ros2", {"service", "list", "-t"}, 4000);
            }
            if (argument.empty()) {
                return "Select a topic, node or service first.";
            }
            if (type == ros_query::topic_info) {
                return run_session(session, "ros2", {"topic", "info", argument, "-v"}, 4000);
            }
            if (type == ros_query::topic_echo) {
                return run_session(session, "ros2", {"topic", "echo", "--once", argument}, 5000);
            }
            if (type == ros_query::topic_hz) {
                return run_session(session, "ros2", {"topic", "hz", argument}, 3000);
            }
            return run_session(session, "ros2", {"node", "info", argument}, 4000);
        }
        if (type == ros_query::topics) {
            return run_session(session, "rostopic", {"list", "-v"}, 4000);
        }
        if (type == ros_query::nodes) {
            return run_session(session, "rosnode", {"list"}, 4000);
        }
        if (type == ros_query::services) {
            return run_session(session, "rosservice", {"list"}, 4000);
        }
        if (argument.empty()) {
            return "Select a topic, node or service first.";
        }
        if (type == ros_query::topic_info) {
            return run_session(session, "rostopic", {"info", argument}, 4000);
        }
        if (type == ros_query::topic_echo) {
            return run_session(session, "rostopic", {"echo", "-n", "1", argument}, 5000);
        }
        if (type == ros_query::topic_hz) {
            return run_session(session, "rostopic", {"hz", argument}, 3000);
        }
        return run_session(session, "rosnode", {"info", argument}, 4000);
    }

    string ros_summary(const ros_session& session) {
        return format("ROS {} · PID {} · {} variables · {}", session.version_, static_cast<uint64_t>(session.pid_),
                      static_cast<uint64_t>(session.environment_.size()), session.cwd_);
    }

} // namespace neforce::neflux
