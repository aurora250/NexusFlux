#include <NeFlux/common.hpp>
#include <NeFlux/crash.hpp>
#include <NeFlux/platform.hpp>
#include <NeForce/core/async/atomic.hpp>
#include <NeForce/core/exception/exception.hpp>
#include <NeForce/core/exception/exception_ptr.hpp>
#include <NeForce/core/exception/terminate.hpp>
#include <NeForce/core/file/filesystem.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/string/utf.hpp>
#include <NeForce/core/system/environment.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/core/system/signal.hpp>
#include <NeForce/core/time/datetime.hpp>
#include <NeForce/logging/file_sink.hpp>
#include <NeForce/logging/logger.hpp>
#ifdef NEFLUX_PLATFORM_WINDOWS
#    include <windows.h>
#    include <dbghelp.h>
#endif
namespace neforce::neflux {

    namespace {

        using logger_handle = decltype(logger_registry::instance().get_logger(string{}));

        logger_handle crash_logger;
        path crash_directory;
        atomic<bool> in_crash{false};
#if !NEFLUX_PLATFORM_WINDOWS
        optional<signal_guard> crash_signal_guard;
#endif

        string timestamp_text() {
            const auto now = datetime::now();
            return format("{:04d}{:02d}{:02d}-{:02d}{:02d}{:02d}", now.date().year(), now.date().month(),
                          now.date().day(), now.time().hours(), now.time().minutes(), now.time().seconds());
        }

        void write_fatal(const string& detail) {
            const string message = format("fatal: {}", detail);
            if (crash_logger) {
                try {
                    crash_logger->log(log_level::FATAL, message, source_location::current());
                    crash_logger->flush();
                } catch (...) {
                    // The logger may already be unusable during process teardown.
                }
            }
            if (!crash_directory.empty()) {
                try {
                    static_cast<void>(filesystem::create_and_write(
                            crash_directory / "crash-fallback.log", timestamp_text() + " " + message + "\n", true));
                } catch (...) {
                }
            }
        }

        void crash_terminate_handler() {
            if (in_crash.exchange(true)) {
                immediate_exit(1);
            }
            string detail = "std::terminate";
            try {
                const auto current = current_exception();
                if (current) {
                    rethrow_exception(current);
                }
            } catch (const exception& error) {
                detail.append(": ");
                detail.append(error.what());
            } catch (...) {
                detail.append(": unknown exception");
            }
            write_fatal(detail);
            immediate_exit(1);
        }

        void on_exit() {
            if (!crash_logger) {
                return;
            }
            try {
                crash_logger->log(log_level::INFO, "shutdown", source_location::current());
                crash_logger->flush();
            } catch (...) {
            }
        }

#if NEFLUX_PLATFORM_WINDOWS
        ::LONG WINAPI unhandled_exception_filter(::EXCEPTION_POINTERS* info) {
            if (in_crash.exchange(true)) {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            const ::DWORD code =
                    info != nullptr && info->ExceptionRecord != nullptr ? info->ExceptionRecord->ExceptionCode : 0;
            const void* address = info != nullptr && info->ExceptionRecord != nullptr
                                          ? info->ExceptionRecord->ExceptionAddress
                                          : nullptr;
            string detail = format("Unhandled exception code=0x{:08X} address={}", code, address);
            if (!crash_directory.empty()) {
                const path dump_path =
                        crash_directory /
                        (format("nexus-flux-{}-{}.dmp", timestamp_text(), process::current_id()).view());
                const auto wide_path = character::to_wstring(dump_path.str().view());
                const ::HANDLE file = ::CreateFileW(wide_path.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                                  FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file != INVALID_HANDLE_VALUE) {
                    ::MINIDUMP_EXCEPTION_INFORMATION exception_info{};
                    exception_info.ThreadId = ::GetCurrentThreadId();
                    exception_info.ExceptionPointers = info;
                    exception_info.ClientPointers = FALSE;
                    const ::BOOL written = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file,
                                                               ::MiniDumpNormal, &exception_info, nullptr, nullptr);
                    ::CloseHandle(file);
                    detail.append(written == TRUE ? " dump=" : " dump-failed=");
                    detail.append(dump_path.str().view());
                }
            }
            write_fatal(detail);
            return EXCEPTION_CONTINUE_SEARCH;
        }
#else
        bool fatal_signal_handler(const system_signal::event event, void*) {
            write_fatal(format("fatal signal {}", static_cast<int>(event)));
            immediate_exit(1);
            return false;
        }
#endif

    } // namespace

    void initialize_crash_logging(int argc, char** argv) {
        try {
            const string base = environment::config_directory();
            const path log_root = base.empty() ? path(".neflux") / "logs" : path(base) / "neflux" / "logs";
            static_cast<void>(filesystem::create_directories(log_root));
            crash_directory = log_root;
            auto& registry = logger_registry::instance();
            auto log = registry.get_logger("nexus-flux");
            log->set_level(log_level::INFO);
            auto sink = make_shared<file_sink>(log_root / "nexus-flux.log",
                                                                     static_cast<size_t>(8 * 1024 * 1024), true, 5);
            log->add_sink(sink);
            crash_logger = log;
            string command;
            for (int index = 0; index < argc; ++index) {
                if (!command.empty()) {
                    command.push_back(' ');
                }
                command.append(argv[index] != nullptr ? argv[index] : "");
            }
            log->log(log_level::INFO,
                     format("startup pid={} version=0.1.0 command={}", process::current_id(), command),
                     source_location::current());
            log->flush();
            static_cast<void>(set_exit(on_exit));
            set_terminate(crash_terminate_handler);
#if NEFLUX_PLATFORM_WINDOWS
            ::SetUnhandledExceptionFilter(unhandled_exception_filter);
#else
            crash_signal_guard.emplace();
            auto& signals = system_signal::instance();
            signals.register_handler(system_signal::event::ABORT, fatal_signal_handler);
            signals.register_handler(system_signal::event::SEGMENT_FAULT, fatal_signal_handler);
            signals.register_handler(system_signal::event::ILLEGAL_INSTR, fatal_signal_handler);
            signals.register_handler(system_signal::event::FLOATING_POINT, fatal_signal_handler);
            signals.register_handler(system_signal::event::BUS_ERROR, fatal_signal_handler);
#endif
        } catch (const exception&) {
            // Initialization must never prevent the application from starting.
        } catch (...) {
            // Initialization must never prevent the application from starting.
        }
    }

} // namespace neforce::neflux
