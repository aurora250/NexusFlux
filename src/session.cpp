#include <NeFlux/machine.hpp>
#include <NeFlux/session.hpp>
#include <NeFlux/source.hpp>

namespace neforce::neflux {

    unique_ptr<telemetry_source> create_source(const cli_args& args, const config& cfg, string& error) {
        const string selector = args.machine_.value_or(string("local"));
        auto target = parse_machine_selector(selector.view(), error);
        if (!target.has_value()) {
            return nullptr;
        }
        if (args.machine_user_.has_value()) {
            target->user_ = *args.machine_user_;
        }
        if (args.agent_path_.has_value()) {
            target->agent_path_ = *args.agent_path_;
        }
        if (args.machine_start_command_.has_value()) {
            target->start_argv_.push_back(*args.machine_start_command_);
            target->start_argv_.insert(target->start_argv_.end(), args.machine_start_args_.begin(),
                                       args.machine_start_args_.end());
        }
        if (!resolve_machine_target(*target, cfg, args.machine_start_, error)) {
            return nullptr;
        }
        if (args.machine_port_.has_value()) {
            target->port_ = *args.machine_port_;
        }
        source_options options;
        options.cfg_ = cfg;
        options.replay_ = args.replay_;
        options.read_only_ = args.read_only_;
        options.full_sampling_ = args.full_sampling_ || args.record_.has_value() || args.snapshot_ ||
                                 args.samples_.has_value() || args.csv_.has_value() || args.export_svg_.has_value() ||
                                 args.command_ == cli_command::doctor ||
                                 args.command_ == cli_command::network_connections;
        options.allow_actions_ = args.allow_actions_;
        if (target->kind_ == machine_kind::local) {
            return make_local_source(move(options));
        }
        return make_remote_source(move(*target), move(options));
    }

} // namespace neforce::neflux
