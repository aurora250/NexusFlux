#include "extras.hpp"
#include <NeFlux/util.hpp>
#include <NeFlux/discovery.hpp>
#include <NeForce/core/file/filesystem.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/system/process.hpp>

namespace neforce::neflux {

    json discover_config_extras(const config& cfg) {
        json extra = json::object();
        if (!cfg.collection_.monitored_folders_.empty()) {
            json folders = json::array();
            for (const auto& folder: cfg.collection_.monitored_folders_) {
                json item = json::object();
                item.set("path", json(folder));
                uint64_t bytes_total = 0;
                uint64_t files = 0;
                uint64_t directories = 0;
                bool complete = true;
                vector<string> pending{folder};
                constexpr size_t limit = 20000;
                while (!pending.empty()) {
                    if (files + directories >= limit) {
                        complete = false;
                        break;
                    }
                    const string current = pending.back();
                    pending.pop_back();
                    try {
                        for (const auto& child: path(current).children(true)) {
                            if (child.is_directory()) {
                                ++directories;
                                pending.push_back(child.str());
                            } else if (child.is_file()) {
                                ++files;
                                bytes_total += filesystem::size(child).bytes();
                            }
                        }
                    } catch (const exception& error) {
                        complete = false;
                        item.set("error", json(error.what()));
                        break;
                    }
                }
                item.set("bytes", json(bytes_total));
                item.set("files", json(files));
                item.set("directories", json(directories));
                item.set("complete", json(complete));
                folders.push(move(item));
            }
            extra.set("Monitored folders", move(folders));
        }
        if (cfg.collection_.config_exec_ && !cfg.collection_.probes_.empty()) {
            json probes = json::object();
            for (const auto& probe: cfg.collection_.probes_) {
                try {
                    const string output = run_command(probe.command_[0],
                                                      vector<string>(probe.command_.begin() + 1, probe.command_.end()),
                                                      static_cast<int>(probe.timeout_seconds_ * 1000));
                    json item = json::object();
                    item.set("output", json(output));
                    item.set("timeout_seconds", json(probe.timeout_seconds_));
                    probes.set(probe.name_, move(item));
                } catch (const exception& error) {
                    json item = json::object();
                    item.set("error", json(error.what()));
                    probes.set(probe.name_, move(item));
                }
            }
            extra.set("Local probes", move(probes));
        }
        return extra;
    }

} // namespace neforce::neflux
