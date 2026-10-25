#include <NeFlux/common.hpp>
#include <NeFlux/tags.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/string/format.hpp>
namespace neforce::neflux {

    namespace {

        using role_index = map<string, pair<string, string>>;

        const role_index& role_table() {
            static const role_index table = [] {
                role_index value;
                const auto add = [&value](const char* key, const char* label,
                                          std::initializer_list<const char*> names) {
                    for (const char* name: names) {
                        value[name] = {key, label};
                    }
                };
                add("web", "Web",
                    {"nginx", "apache2", "httpd", "caddy", "traefik", "envoy", "haproxy", "gunicorn", "uvicorn",
                     "uwsgi", "php-fpm", "lighttpd"});
                add("database", "Database",
                    {"postgres", "postgresql", "postmaster", "mysqld", "mariadbd", "mongod", "mongos",
                     "clickhouse-server", "sqlservr", "influxd", "neo4j", "cassandra", "duckdb", "sqlite3"});
                add("db-client", "DB client",
                    {"psql", "mysql", "mariadb", "mongosh", "redis-cli", "pgcli", "mycli", "dbeaver"});
                add("cache", "Cache",
                    {"redis-server", "redis", "valkey-server", "valkey", "memcached", "keydb-server", "varnishd"});
                add("search", "Search",
                    {"elasticsearch", "opensearch", "solr", "meilisearch", "typesense-server", "qdrant", "weaviate"});
                add("container-runtime", "Containers",
                    {"docker", "dockerd", "containerd", "containerd-shim", "containerd-shim-runc-v2", "podman",
                     "conmon", "crun", "runc"});
                add("dns", "DNS",
                    {"named", "bind9", "unbound", "dnsmasq", "coredns", "systemd-resolved", "systemd-resolve",
                     "dnscrypt-proxy"});
                add("desktop", "Desktop / audio",
                    {"gnome-shell", "kwin_wayland", "kwin_x11", "plasmashell", "sway", "hyprland", "weston", "pipewire",
                     "pipewire-pulse", "pulseaudio", "wireplumber", "xorg", "xwayland"});
                add("terminal", "Terminal",
                    {"alacritty", "kitty", "wezterm", "wezterm-gui", "gnome-terminal-server", "gnome-terminal-",
                     "konsole", "foot", "xterm"});
                add("editor", "Editor",
                    {"code", "codium", "code-insiders", "clion", "idea", "pycharm", "goland", "webstorm", "nvim",
                     "neovim", "vim", "emacs", "nano", "micro"});
                add("packages", "Packages",
                    {"apt", "apt-get", "dpkg", "dnf", "yum", "rpm", "pacman", "zypper", "snapd"});
                add("scheduler", "Scheduled jobs",
                    {"cron", "crond", "anacron", "atd", "systemd", "launchd", "taskeng"});
                add("indexer", "File indexing",
                    {"updatedb", "tracker-miner-fs", "tracker-miner-fs-3", "tracker-miner-f", "tracker-extract",
                     "baloo_file", "mds"});
                add("browser", "Browser",
                    {"firefox", "chrome", "google-chrome", "chromium", "chromium-browser", "brave", "brave-browser",
                     "msedge", "microsoft-edge", "webkitwebproces", "webkitwebprocess"});
                add("media", "Media", {"ffmpeg", "obs", "vlc", "mpv", "gst-launch-1.0"});
                add("remote", "Remote access",
                    {"ssh", "sshd", "scp", "sftp", "sftp-server", "mosh", "mosh-server", "xrdp", "xrdp-sesman",
                     "remmina", "xfreerdp", "wfreerdp", "x11vnc", "tigervnc", "xtigervnc", "wayvnc", "rustdesk"});
                add("proxy", "Proxy", {"squid", "tinyproxy", "3proxy"});
                add("vpn", "VPN / tunnel",
                    {"wg", "wireguard-go", "openvpn", "tailscaled", "zerotier-one", "cloudflared", "frpc", "frps"});
                add("observability", "Monitoring",
                    {"prometheus", "grafana", "node_exporter", "telegraf", "netdata", "victoria-metrics"});
                add("virtualization", "VM",
                    {"qemu-system-x86_64", "qemu-system-aarch64", "qemu-kvm", "kvm", "libvirtd", "virtqemud",
                     "virtualbox", "virtualboxvm", "vboxheadless", "vmware-vmx", "prl_vm_app"});
                add("kubernetes", "Kubernetes",
                    {"kubectl", "kubelet", "kube-apiserver", "kube-controller-manager", "kube-scheduler", "kube-proxy",
                     "k3s", "rke2", "helm", "minikube", "kind", "k9s"});
                add("sync", "Sync",
                    {"rsync", "rclone", "syncthing", "unison", "nextcloud", "owncloud", "dropbox", "onedrive",
                     "seafile-applet", "lsyncd"});
                add("backup", "Backup",
                    {"restic", "borg", "borgmatic", "kopia", "duplicity", "duplicati", "bup", "urbackupclientbackend",
                     "veeamagent"});
                add("security", "Security scan",
                    {"clamd", "clamscan", "clamdscan", "freshclam", "msmpeng", "mrt", "rkhunter", "aide", "osqueryd",
                     "wazuh-agentd", "falco"});
                add("model-server", "Model server",
                    {"llama-server", "tritonserver", "text-generation-launcher", "text-generation-server",
                     "sglang.launch_server", "vllm.entrypoints.openai.api_server"});
                add("compute", "AI compute",
                    {"ollama", "vllm", "torchrun", "llama-server", "tritonserver", "text-generation-launcher",
                     "text-generation-server"});
                add("distributed-compute", "Distributed",
                    {"torchrun", "mpirun", "mpiexec", "orted", "prterun", "hydra_pmi_proxy", "torch.distributed.run",
                     "accelerate.commands.launch", "dask-scheduler", "dask-worker", "raylet", "gcs_server",
                     "spark-submit", "org.apache.spark.deploy.sparksubmit"});
                add("simulation", "Simulation",
                    {"gzserver", "gzclient", "gazebo", "webots", "isaac-sim", "mujoco", "coppeliasim", "carlaue4"});
                add("messaging", "Messaging",
                    {"rabbitmq-server", "beam.smp", "kafka", "kafka-server-start", "mosquitto", "nats-server", "pulsar",
                     "activemq", "emqx"});
                add("zenoh", "Zenoh", {"zenohd", "zenoh-bridge", "zenohd-ros1", "zenohd-ros2"});
                add("ros", "ROS",
                    {"roscore", "rosmaster", "roslaunch", "ros2", "rviz", "rviz2", "gazebo", "gzserver", "gzclient"});
                add("robotics", "Robotics",
                    {"robot_state_publisher", "nav2", "moveit", "microros-agent", "micro-ros-agent", "rosbag",
                     "rosbag2"});
                add("scientific", "Scientific", {"julia", "matlab", "octave", "gnuplot", "root", "rscript"});
                add("notebook", "Notebook", {"jupyter", "jupyter-lab", "jupyter-notebook", "code-server"});
                add("testing", "Tests", {"pytest", "ctest", "tox", "jest", "vitest", "mocha"});
                add("lint", "Code check",
                    {"ruff", "flake8", "pylint", "mypy", "clang-tidy", "eslint", "prettier", "shellcheck"});
                add("version-control", "Version control",
                    {"git", "git-lfs", "svn", "hg", "fossil", "jj", "git-remote-https", "git-remote-http",
                     "git-upload-pack", "git-receive-pack"});
                add("debugger", "Debugger", {"gdb", "lldb", "rr", "valgrind", "delve", "dlv"});
                add("profiler", "Profiler", {"perf", "hotspot", "py-spy", "perfetto", "valgrind"});
                add("language-server", "Language server",
                    {"clangd", "pylsp", "rust-analyzer", "gopls", "typescript-language-server"});
                add("ci", "CI worker", {"github-runner", "gitlab-runner", "jenkins", "buildkite-agent"});
                add("network", "Network",
                    {"ping", "ping6", "traceroute", "mtr", "dig", "nslookup", "nmap", "socat", "nc", "ncat", "netcat",
                     "tcpdump"});
                add("file-sharing", "File sharing",
                    {"smbd", "nmbd", "nfsd", "rpc.nfsd", "rclone", "syncthing", "seafile-applet"});
                add("archive", "Archive", {"tar", "zip", "unzip", "7z", "7za", "zstd", "xz", "pigz"});
                add("media-convert", "Media convert", {"ffmpeg", "handbrakecli", "avconv"});
                add("media-player", "Playback", {"mpv", "vlc", "spotify", "rhythmbox", "clementine"});
                add("streaming", "Capture / stream", {"obs", "obs-studio", "gst-launch-1.0", "ffmpeg"});
                add("render", "3D / render", {"blender", "maya", "houdini", "cinema4d", "cycles"});
                add("communication", "Calls / chat", {"discord", "slack", "zoom", "teams", "telegram", "signal"});
                add("office", "Office", {"libreoffice", "soffice", "onlyoffice", "evince"});
                add("games", "Games / launcher", {"steam", "heroic", "lutris", "wine", "proton"});
                add("automation", "Automation", {"ansible", "ansible-playbook", "terraform", "pulumi", "vagrant"});
                add("cloud-cli", "Cloud CLI", {"aws", "az", "gcloud", "ibmcloud", "doctl"});
                add("job-worker", "Job worker", {"celery", "rqworker", "sidekiq", "dramatiq"});
                return value;
            }();
            return table;
        }

        void push_tag(vector<tag>& tags, const char* key, const char* label, bool inferred, const string& evidence) {
            for (auto& item: tags) {
                if (item.key_ == key) {
                    if (!inferred) {
                        item.inferred_ = false;
                    }
                    if (item.evidence_.size() < 4) {
                        bool duplicate = false;
                        for (const auto& existing: item.evidence_) {
                            if (existing == evidence) {
                                duplicate = true;
                                break;
                            }
                        }
                        if (!duplicate) {
                            item.evidence_.push_back(evidence);
                        }
                    }
                    return;
                }
            }
            tag item;
            item.key_ = key;
            item.label_ = label;
            item.inferred_ = inferred;
            item.evidence_.push_back(evidence);
            tags.push_back(move(item));
        }

        void apply_roles(vector<tag>& tags, const string& executable, const string& source) {
            string base = lower_ascii(executable.view());
            if (ends_with(base.view(), ".exe")) {
                base.resize(base.size() - 4);
            }
            const auto& table = role_table();
            const auto found = table.find(base);
            if (found == table.end()) {
                return;
            }
            push_tag(tags, found->second.first.view().data(), found->second.second.view().data(), false,
                     format("{}: exact basename {}", source, base));
        }

        string base_name(const string& value) {
            const auto trimmed = trim(value.view());
            const auto slash = trimmed.rfind('/');
            string name = slash == string::npos ? trimmed : trimmed.substr(slash + 1);
            const auto space = name.find(' ');
            if (space != string::npos) {
                name = name.substr(0, space);
            }
            return name;
        }

    } // namespace

    void annotate(snapshot& value) {
        for (auto& item: value.processes_) {
            item.tags_.clear();
            if (!item.kernel_thread_) {
                apply_roles(item.tags_, item.name_, "process");
            }
            if (item.memory_ >= 1024ULL * 1024ULL * 1024ULL) {
                push_tag(item.tags_, "memory-heavy", "RAM >=1 GiB", false,
                         format("resident memory {} bytes", static_cast<uint64_t>(item.memory_)));
            }
            if (item.cpu_ >= 100.0F) {
                push_tag(item.tags_, "cpu-cores", "CPU >=1 core", false,
                         format("CPU {:.1f}% (one core = 100%)", item.cpu_));
            }
            if (item.rx_per_sec_.value_or(0.0) + item.tx_per_sec_.value_or(0.0) >= 1024.0 * 1024.0) {
                push_tag(item.tags_, "network-io", "Network IO", false,
                         format("RX {:.0f} B/s + TX {:.0f} B/s", item.rx_per_sec_.value_or(0.0),
                                item.tx_per_sec_.value_or(0.0)));
            }
            if (item.read_per_sec_ + item.write_per_sec_ >= 10.0 * 1024.0 * 1024.0) {
                push_tag(item.tags_, "disk-io", "Disk IO", false,
                         format("read {:.0f} B/s + write {:.0f} B/s", item.read_per_sec_, item.write_per_sec_));
            }
            if (item.gpu_memory_.has_value() || !item.gpu_ids_.empty()) {
                push_tag(item.tags_, "gpu-memory", "GPU memory", false, "sampled GPU allocation");
            }
            if (item.gpu_percent_.has_value() && *item.gpu_percent_ > 0) {
                push_tag(item.tags_, "gpu-active", "GPU active", false,
                         format("sampled GPU utilization {}%", *item.gpu_percent_));
            }
            if (item.state_ == "Zombie") {
                push_tag(item.tags_, "zombie", "Zombie", false, "sampled process state Zombie");
            } else if (item.state_ == "Stopped" || item.state_ == "Stop") {
                push_tag(item.tags_, "stopped", "Stopped", false, "sampled process state Stopped");
            }
        }
        set<uint32_t> listening;
        set<uint32_t> connecting;
        set<uint32_t> queued;
        for (const auto& item: value.connections_.connections_) {
            const bool listen = item.state_ == "LISTEN" || item.state_ == "BOUND";
            const bool connecting_state = item.state_ == "SYN_SENT" || item.state_ == "SYN_RECV";
            const bool has_queue = item.receive_queue_.value_or(0) > 0 || item.send_queue_.value_or(0) > 0;
            for (const uint32_t pid: item.pids_) {
                if (listen) {
                    listening.insert(pid);
                }
                if (connecting_state) {
                    connecting.insert(pid);
                }
                if (has_queue) {
                    queued.insert(pid);
                }
            }
        }
        for (auto& item: value.processes_) {
            if (listening.count(item.pid_) != 0) {
                push_tag(item.tags_, "listener", "Listener", false, "sampled listening socket");
            }
            if (connecting.count(item.pid_) != 0) {
                push_tag(item.tags_, "connecting", "Connecting", false, "sampled connecting socket");
            }
            if (queued.count(item.pid_) != 0) {
                push_tag(item.tags_, "socket-queue", "Socket queue", false, "sampled non-empty socket queue");
            }
        }
        for (auto& item: value.inventory_.containers_) {
            item.tags_.clear();
            apply_roles(item.tags_, item.image_, "image");
            apply_roles(item.tags_, item.name_, "container");
            if (item.state_ == "restarting") {
                push_tag(item.tags_, "restarting", "Restarting", false, "container state restarting");
            }
            if (contains_ignore_case(item.status_.view(), "unhealthy")) {
                push_tag(item.tags_, "unhealthy", "Unhealthy", false, "container health status");
            }
        }
        for (auto& item: value.inventory_.services_) {
            item.tags_.clear();
            string unit = item.name_;
            if (ends_with(unit.view(), ".service")) {
                unit.resize(unit.size() - 8);
            }
            apply_roles(item.tags_, unit, "unit");
            if (item.active_ == "failed") {
                push_tag(item.tags_, "failed", "Failed", false, "unit active state failed");
            }
        }
        for (auto& item: value.connections_.connections_) {
            item.tags_.clear();
            for (const uint32_t pid: item.pids_) {
                for (const auto& proc: value.processes_) {
                    if (proc.pid_ != pid) {
                        continue;
                    }
                    for (const auto& source: proc.tags_) {
                        for (const auto& evidence: source.evidence_) {
                            push_tag(item.tags_, source.key_.view().data(), source.label_.view().data(),
                                     source.inferred_, format("owner {}: {}", pid, evidence));
                        }
                    }
                    break;
                }
            }
        }
    }

    string tag_text(const vector<tag>& tags) {
        string out;
        for (const auto& item: tags) {
            if (!out.empty()) {
                out.push_back(' ');
            }
            out.append(item.display());
        }
        return out;
    }

    bool tag_matches(const vector<tag>& tags, const string& text) {
        string query = trim(text.view());
        if (!query.empty() && query[query.size() - 1] == '?') {
            query.resize(query.size() - 1);
        }
        query = lower_ascii(query.view());
        if (query.empty()) {
            return false;
        }
        for (const auto& item: tags) {
            if (contains_ignore_case(item.key_.view(), query.view()) ||
                contains_ignore_case(item.label_.view(), query.view())) {
                return true;
            }
        }
        return false;
    }

    string tag_explain(const vector<tag>& tags) {
        string out = "Labels describe sampled programs and resources. A ? marks an inferred purpose, not confirmed "
                     "activity. "
                     "No matching label means unrecognized, not inactive.\n";
        for (const auto& item: tags) {
            out.append("\n");
            out.append(item.display());
            out.append(" · ");
            out.append(item.inferred_ ? "purpose hint" : "sampled evidence");
            out.append("\n");
            for (size_t i = 0; i < item.evidence_.size(); ++i) {
                if (i != 0) {
                    out.push_back('\n');
                }
                out.append(item.evidence_[i]);
            }
            out.push_back('\n');
        }
        return out;
    }

    bool query_matches(const vector<tag>& tags, const string& text, const string& plain) {
        const string combined = format("{} {}", plain, tag_text(tags));
        for (const auto& word_raw: split_whitespace(text.view())) {
            bool negated = false;
            string word = word_raw;
            if (starts_with(word.view(), "!")) {
                negated = true;
                word = word.substr(1);
            }
            bool matched = false;
            if (starts_with(word.view(), "tag:")) {
                matched = tag_matches(tags, word.substr(4));
            } else {
                matched = contains_ignore_case(combined.view(), word.view());
            }
            if (matched == negated) {
                return false;
            }
        }
        return true;
    }

} // namespace neforce::neflux
