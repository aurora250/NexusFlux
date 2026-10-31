#include <NeFlux/crash.hpp>
#include <NeForce/core/exception/crash_dump.hpp>
#include <NeForce/core/exception/terminate.hpp>
#include <NeForce/core/file/filesystem.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/system/environment.hpp>
#include <NeForce/core/system/process.hpp>
#include <NeForce/logging/file_sink.hpp>
#include <NeForce/logging/logger.hpp>
namespace neforce::neflux {

    namespace {

        using logger_handle = decltype(logger_registry::instance().get_logger(string{}));

        logger_handle crash_logger;

        void log_crash(const crash_dump::report& report, void*) noexcept {
            if (!crash_logger) {
                return;
            }
            try {
                crash_logger->log(log_level::FATAL, report.message, source_location::current());
                crash_logger->flush();
            } catch (...) {
                // The logger may already be unusable during process teardown.
            }
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

    } // namespace

    void initialize_crash_logging(int argc, char** argv) {
        try {
            const string base = environment::config_directory();
            const path log_root = base.empty() ? path(".neflux") / "logs" : path(base) / "neflux" / "logs";
            static_cast<void>(filesystem::create_directories(log_root));

            auto& registry = logger_registry::instance();
            auto log = registry.get_logger("nexus-flux");
            log->set_level(log_level::INFO);
            auto sink =
                    make_shared<file_sink>(log_root / "nexus-flux.log", static_cast<size_t>(8 * 1024 * 1024), true, 5);
            log->add_sink(sink);
            crash_logger = log;

            string command;
            for (int index = 0; index < argc; ++index) {
                if (!command.empty()) {
                    command.push_back(' ');
                }
                command.append(argv[index] != nullptr ? argv[index] : "");
            }
            log->log(log_level::INFO, format("startup pid={} version=0.1.0 command={}", process::current_id(), command),
                     source_location::current());
            log->flush();

            static_cast<void>(set_exit(on_exit));

            crash_dump::options options;
            options.directory = log_root;
            options.file_prefix = "nexus-flux";
            options.include_memory = true;
            options.callback = log_crash;
            if (!crash_dump::install_handlers(options)) {
                log->log(log_level::WARN, "failed to install crash handlers", source_location::current());
            }
        } catch (const exception&) {
            // Initialization must never prevent the application from starting.
        } catch (...) {
            // Initialization must never prevent the application from starting.
        }
    }

} // namespace neforce::neflux
