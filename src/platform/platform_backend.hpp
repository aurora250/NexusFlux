#ifndef NEXUSFLUX_PLATFORM_BACKEND_HPP__
#define NEXUSFLUX_PLATFORM_BACKEND_HPP__

#include <NeFlux/source.hpp>
#include <NeFlux/cli.hpp>
#include <NeForce/core/system/process.hpp>

namespace neforce::neflux::platform {

    struct topology_entry {
        optional<uint32_t> package;
        optional<uint32_t> core;
    };

    struct proc_meminfo {
        optional<uint64_t> total;
        optional<uint64_t> available;
        optional<uint64_t> free;
        optional<uint64_t> cached;
        optional<uint64_t> swap_total;
        optional<uint64_t> swap_free;
    };

    NEFORCE_NODISCARD source_capabilities capabilities();

    void read_load(float (&load)[3]);

    NEFORCE_NODISCARD uint64_t cpu_frequency_mhz();

    NEFORCE_NODISCARD vector<uint64_t> core_frequencies_mhz(size_t count);

    NEFORCE_NODISCARD vector<topology_entry> read_topology(size_t count);

    NEFORCE_NODISCARD vector<optional<float>> core_temperatures(const vector<topology_entry>& topology);

    NEFORCE_NODISCARD proc_meminfo read_meminfo();

    NEFORCE_NODISCARD vector<disk> read_disks();

    NEFORCE_NODISCARD vector<sensor> read_sensors();

    NEFORCE_NODISCARD vector<battery> read_batteries();

    NEFORCE_NODISCARD vector<pressure> read_pressure();

    NEFORCE_NODISCARD string process_state_text(const ::neforce::process::snapshot_entry& entry);

    void discover_services(inventory& data);

    struct connection_owner_state {
        int64_t owners_at = -1;
        ::neforce::process::socket_owner_scan owner_data;
        uint64_t owners_sampled_at = 0;
    };

    void collect_connections(connection_sample& sample, connection_owner_state& state);

    NEFORCE_NODISCARD vector<connection> parse_proc_connections(const string& protocol, const string& text);

    void apply_tcp_info(vector<connection>& rows, const string& text);

    void discover_ownership(inventory& data);

    void discover_virtual_machines(inventory& data);

    NEFORCE_NODISCARD string docker_host();

    NEFORCE_NODISCARD vector<string> capture_packets(const cli_args& args);

    NEFORCE_NODISCARD string network_routes_dns();

    NEFORCE_NODISCARD vector<string> process_environment(uint32_t pid, string& error);

    NEFORCE_NODISCARD vector<string> process_open_files(uint32_t pid, string& error);

    NEFORCE_NODISCARD vector<string> process_logs(uint32_t pid, size_t max_lines, string& error);

    void service_action(const string& name, const string& action, string& error);

    NEFORCE_NODISCARD vector<string> service_logs(const string& name, size_t max_lines, string& error);

    NEFORCE_NODISCARD vector<string> process_file_locks(uint32_t pid, string& error);

    NEFORCE_NODISCARD vector<string> process_trace(uint32_t pid, string& error);

    NEFORCE_NODISCARD vector<string> process_backtrace(uint32_t pid, string& error);

    NEFORCE_NODISCARD vector<string> kernel_messages(string& error);

    void set_io_priority(uint32_t pid, int priority, string& error);

    NEFORCE_NODISCARD string nvml_library_name();

    NEFORCE_NODISCARD vector<gpu> read_platform_gpus(uint32_t first_id);

} // namespace neforce::neflux::platform
#endif // NEXUSFLUX_PLATFORM_BACKEND_HPP__
