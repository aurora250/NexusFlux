#include <NeFlux/common.hpp>
#include <NeFlux/model.hpp>
#include <NeForce/core/string/format.hpp>
namespace neforce::neflux {

    json to_json_value(const memory& value);
    json to_json_value(const process& value);
    json to_json_value(const network& value);
    json to_json_value(const disk& value);
    json to_json_value(const disk_io& value);
    json to_json_value(const sensor& value);
    json to_json_value(const battery& value);
    json to_json_value(const pressure& value);
    json to_json_value(const gpu_process& value);
    json to_json_value(const gpu& value);
    json to_json_value(const connection& value);
    json to_json_value(const connection_sample& value);
    json to_json_value(const container& value);
    json to_json_value(const service& value);
    json to_json_value(const virtual_machine& value);
    json to_json_value(const listener& value);
    json to_json_value(const inventory& value);

    void from_json_value(const json& value, memory& out);
    void from_json_value(const json& value, process& out);
    void from_json_value(const json& value, network& out);
    void from_json_value(const json& value, disk& out);
    void from_json_value(const json& value, disk_io& out);
    void from_json_value(const json& value, sensor& out);
    void from_json_value(const json& value, battery& out);
    void from_json_value(const json& value, pressure& out);
    void from_json_value(const json& value, gpu_process& out);
    void from_json_value(const json& value, gpu& out);
    void from_json_value(const json& value, connection& out);
    void from_json_value(const json& value, connection_sample& out);
    void from_json_value(const json& value, container& out);
    void from_json_value(const json& value, service& out);
    void from_json_value(const json& value, virtual_machine& out);
    void from_json_value(const json& value, listener& out);

    string tag::display() const { return inferred_ ? label_ + "?" : label_; }

    json to_json_value(const tag& value) {
        json out = json::object();
        out.set("key", value.key_);
        out.set("label", value.label_);
        out.set("confidence", value.inferred_ ? "inferred" : "observed");
        out.set("evidence", to_json_value(value.evidence_));
        return out;
    }

    void from_json_value(const json& value, tag& out) {
        out.key_ = value.string_or("key");
        out.label_ = value.string_or("label");
        out.inferred_ = value.string_or("confidence", "observed") == "inferred";
        from_json_value(value.get("evidence") != nullptr ? *value.get("evidence") : json::array(), out.evidence_);
    }

    json to_json_value(const memory& value) {
        json o = json::object();
        o.set("total", value.total_);
        o.set("used", value.used_);
        o.set("available", value.available_);
        o.set("free", to_json_value(value.free_));
        o.set("cached", to_json_value(value.cached_));
        o.set("swap_total", value.swap_total_);
        o.set("swap_used", value.swap_used_);
        return o;
    }

    json to_json_value(const process& value) {
        json o = json::object();
        o.set("tags", to_json_value(value.tags_));
        o.set("pid", value.pid_);
        o.set("parent", to_json_value(value.parent_));
        o.set("start_time", value.start_time_);
        o.set("name", value.name_);
        o.set("command", value.command_);
        o.set("user", value.user_);
        o.set("state", value.state_);
        o.set("cpu", value.cpu_);
        o.set("memory", value.memory_);
        o.set("virtual_memory", value.virtual_memory_);
        o.set("cpu_time_ms", value.cpu_time_ms_);
        o.set("read_per_sec", value.read_per_sec_);
        o.set("write_per_sec", value.write_per_sec_);
        o.set("read_total", value.read_total_);
        o.set("write_total", value.write_total_);
        o.set("gpu_memory", to_json_value(value.gpu_memory_));
        o.set("gpu_percent", to_json_value(value.gpu_percent_));
        o.set("gpu_ids", to_json_value(value.gpu_ids_));
        o.set("runtime", value.runtime_);
        o.set("thread", value.thread_);
        o.set("kernel_thread", value.kernel_thread_);
        o.set("nice", to_json_value(value.nice_));
        o.set("rx_per_sec", to_json_value(value.rx_per_sec_));
        o.set("tx_per_sec", to_json_value(value.tx_per_sec_));
        o.set("extra", value.extra_);
        return o;
    }

    json to_json_value(const network& value) {
        json o = json::object();
        o.set("name", value.name_);
        o.set("mac", value.mac_);
        o.set("mtu", value.mtu_);
        o.set("speed_mbps", to_json_value(value.speed_mbps_));
        o.set("rx_packets", value.rx_packets_);
        o.set("tx_packets", value.tx_packets_);
        o.set("rx_packets_per_sec", to_json_value(value.rx_packets_per_sec_));
        o.set("tx_packets_per_sec", to_json_value(value.tx_packets_per_sec_));
        o.set("rx_errors", value.rx_errors_);
        o.set("tx_errors", value.tx_errors_);
        o.set("rx_per_sec", value.rx_per_sec_);
        o.set("tx_per_sec", value.tx_per_sec_);
        o.set("rx_total", value.rx_total_);
        o.set("tx_total", value.tx_total_);
        o.set("errors", value.errors_);
        o.set("drops", to_json_value(value.drops_));
        o.set("state", value.state_);
        o.set("address", value.address_);
        return o;
    }

    json to_json_value(const disk& value) {
        json o = json::object();
        o.set("name", value.name_);
        o.set("mount", value.mount_);
        o.set("filesystem", value.filesystem_);
        o.set("kind", value.kind_);
        o.set("removable", value.removable_);
        o.set("total", value.total_);
        o.set("used", value.used_);
        o.set("available", value.available_);
        o.set("extra", value.extra_);
        return o;
    }

    json to_json_value(const disk_io& value) {
        json o = json::object();
        o.set("name", value.name_);
        o.set("read_per_sec", value.read_per_sec_);
        o.set("write_per_sec", value.write_per_sec_);
        o.set("busy_percent", to_json_value(value.busy_percent_));
        o.set("aggregate", value.aggregate_);
        o.set("read_iops", to_json_value(value.read_iops_));
        o.set("write_iops", to_json_value(value.write_iops_));
        o.set("await_ms", to_json_value(value.await_ms_));
        o.set("queue_depth", to_json_value(value.queue_depth_));
        return o;
    }

    json to_json_value(const sensor& value) {
        json o = json::object();
        o.set("name", value.name_);
        o.set("temperature", value.temperature_);
        o.set("critical", to_json_value(value.critical_));
        return o;
    }

    json to_json_value(const battery& value) {
        json o = json::object();
        o.set("name", value.name_);
        o.set("extra", value.extra_);
        o.set("percent", to_json_value(value.percent_));
        o.set("status", value.status_);
        o.set("watts", to_json_value(value.watts_));
        return o;
    }

    json to_json_value(const pressure& value) {
        json o = json::object();
        o.set("resource", value.resource_);
        o.set("some_10", to_json_value(value.some_10_));
        o.set("some_60", to_json_value(value.some_60_));
        o.set("some_300", to_json_value(value.some_300_));
        o.set("some_total_us", to_json_value(value.some_total_us_));
        o.set("full_10", to_json_value(value.full_10_));
        o.set("full_60", to_json_value(value.full_60_));
        o.set("full_300", to_json_value(value.full_300_));
        o.set("full_total_us", to_json_value(value.full_total_us_));
        return o;
    }

    json to_json_value(const gpu_process& value) {
        json o = json::object();
        o.set("pid", value.pid_);
        o.set("memory", to_json_value(value.memory_));
        o.set("utilization", to_json_value(value.utilization_));
        o.set("kind", value.kind_);
        o.set("memory_utilization", to_json_value(value.memory_utilization_));
        o.set("encoder", to_json_value(value.encoder_));
        o.set("decoder", to_json_value(value.decoder_));
        return o;
    }

    json to_json_value(const gpu& value) {
        json o = json::object();
        o.set("id", value.id_);
        o.set("name", value.name_);
        o.set("vendor", value.vendor_);
        o.set("backend", value.backend_);
        o.set("uuid", value.uuid_);
        o.set("driver", value.driver_);
        o.set("cuda_driver_version", to_json_value(value.cuda_driver_version_));
        o.set("nvml_version", to_json_value(value.nvml_version_));
        o.set("utilization", to_json_value(value.utilization_));
        o.set("memory_utilization", to_json_value(value.memory_utilization_));
        o.set("memory_used", to_json_value(value.memory_used_));
        o.set("memory_total", to_json_value(value.memory_total_));
        o.set("temperature", to_json_value(value.temperature_));
        o.set("fan", to_json_value(value.fan_));
        o.set("power_watts", to_json_value(value.power_watts_));
        o.set("power_limit_watts", to_json_value(value.power_limit_watts_));
        o.set("clock_mhz", to_json_value(value.clock_mhz_));
        o.set("memory_clock_mhz", to_json_value(value.memory_clock_mhz_));
        o.set("encoder", to_json_value(value.encoder_));
        o.set("decoder", to_json_value(value.decoder_));
        o.set("pcie_rx_kib", to_json_value(value.pcie_rx_kib_));
        o.set("pcie_tx_kib", to_json_value(value.pcie_tx_kib_));
        o.set("performance_state", to_json_value(value.performance_state_));
        o.set("processes", to_json_value(value.processes_));
        o.set("process_status", value.process_status_);
        o.set("unified_memory", value.unified_memory_);
        o.set("memory_limit", to_json_value(value.memory_limit_));
        o.set("extra", value.extra_);
        return o;
    }

    json to_json_value(const connection& value) {
        json o = json::object();
        o.set("tags", to_json_value(value.tags_));
        o.set("id", value.id_);
        o.set("protocol", value.protocol_);
        o.set("local", value.local_);
        o.set("remote", value.remote_);
        o.set("state", value.state_);
        o.set("inode", value.inode_);
        o.set("pids", to_json_value(value.pids_));
        o.set("process_names", to_json_value(value.process_names_));
        o.set("receive_queue", to_json_value(value.receive_queue_));
        o.set("send_queue", to_json_value(value.send_queue_));
        o.set("received", to_json_value(value.received_));
        o.set("sent", to_json_value(value.sent_));
        o.set("rx_per_sec", to_json_value(value.rx_per_sec_));
        o.set("tx_per_sec", to_json_value(value.tx_per_sec_));
        o.set("rtt_ms", to_json_value(value.rtt_ms_));
        o.set("retransmits", to_json_value(value.retransmits_));
        return o;
    }

    json to_json_value(const connection_sample& value) {
        json o = json::object();
        o.set("sampled_at", value.sampled_at_);
        o.set("owners_sampled_at", value.owners_sampled_at_);
        o.set("status", value.status_);
        o.set("notes", to_json_value(value.notes_));
        o.set("owner_scan_complete", value.owner_scan_complete_);
        o.set("unreadable_processes", value.unreadable_processes_);
        o.set("truncated", value.truncated_);
        o.set("connections", to_json_value(value.connections_));
        return o;
    }

    json to_json_value(const container& value) {
        json o = json::object();
        o.set("tags", to_json_value(value.tags_));
        o.set("id", value.id_);
        o.set("runtime", value.runtime_);
        o.set("name", value.name_);
        o.set("image", value.image_);
        o.set("state", value.state_);
        o.set("status", value.status_);
        o.set("cpu", to_json_value(value.cpu_));
        o.set("memory", to_json_value(value.memory_));
        o.set("memory_limit", to_json_value(value.memory_limit_));
        o.set("network_rx", to_json_value(value.network_rx_));
        o.set("network_tx", to_json_value(value.network_tx_));
        o.set("pids", to_json_value(value.pids_));
        o.set("ports", value.ports_);
        o.set("command", value.command_);
        o.set("created", value.created_);
        o.set("running_for", value.running_for_);
        o.set("memory_percent", to_json_value(value.memory_percent_));
        o.set("block_read", to_json_value(value.block_read_));
        o.set("block_write", to_json_value(value.block_write_));
        o.set("network_rx_per_sec", to_json_value(value.network_rx_per_sec_));
        o.set("network_tx_per_sec", to_json_value(value.network_tx_per_sec_));
        o.set("block_read_per_sec", to_json_value(value.block_read_per_sec_));
        o.set("block_write_per_sec", to_json_value(value.block_write_per_sec_));
        return o;
    }

    json to_json_value(const service& value) {
        json o = json::object();
        o.set("tags", to_json_value(value.tags_));
        o.set("name", value.name_);
        o.set("scope", value.scope_);
        o.set("load", value.load_);
        o.set("active", value.active_);
        o.set("sub", value.sub_);
        o.set("description", value.description_);
        o.set("pid", to_json_value(value.pid_));
        o.set("cpu_percent", to_json_value(value.cpu_percent_));
        o.set("memory", to_json_value(value.memory_));
        o.set("cpu_time_ns", to_json_value(value.cpu_time_ns_));
        o.set("tasks", to_json_value(value.tasks_));
        o.set("extra", value.extra_);
        return o;
    }

    json to_json_value(const virtual_machine& value) {
        json o = json::object();
        o.set("tags", to_json_value(value.tags_));
        o.set("name", value.name_);
        o.set("id", value.id_);
        o.set("uuid", value.uuid_);
        o.set("backend", value.backend_);
        o.set("state", value.state_);
        o.set("autostart", value.autostart_);
        o.set("description", value.description_);
        o.set("pid", to_json_value(value.pid_));
        o.set("vcpus", to_json_value(value.vcpus_));
        o.set("memory", to_json_value(value.memory_));
        o.set("max_memory", to_json_value(value.max_memory_));
        o.set("cpu_percent", to_json_value(value.cpu_percent_));
        o.set("extra", value.extra_);
        return o;
    }

    json to_json_value(const listener& value) {
        json o = json::object();
        o.set("protocol", value.protocol_);
        o.set("address", value.address_);
        o.set("port", value.port_);
        o.set("uid", value.uid_);
        return o;
    }

    json to_json_value(const inventory& value) {
        json o = json::object();
        o.set("updated_at", value.updated_at_);
        o.set("containers", to_json_value(value.containers_));
        o.set("runtimes", to_json_value(value.runtimes_));
        o.set("services", to_json_value(value.services_));
        o.set("service_managers", to_json_value(value.service_managers_));
        o.set("virtual_machines", to_json_value(value.virtual_machines_));
        o.set("vm_managers", to_json_value(value.vm_managers_));
        o.set("listeners", to_json_value(value.listeners_));
        o.set("notes", to_json_value(value.notes_));
        o.set("extra", value.extra_);
        return o;
    }

    void from_json_value(const json& value, memory& out) {
        out = memory{};
        read_field(value, "total", out.total_);
        read_field(value, "used", out.used_);
        read_field(value, "available", out.available_);
        read_field(value, "free", out.free_);
        read_field(value, "cached", out.cached_);
        read_field(value, "swap_total", out.swap_total_);
        read_field(value, "swap_used", out.swap_used_);
    }

    void from_json_value(const json& value, process& out) {
        out = process{};
        read_field(value, "tags", out.tags_);
        read_field(value, "pid", out.pid_);
        read_field(value, "parent", out.parent_);
        read_field(value, "start_time", out.start_time_);
        read_field(value, "name", out.name_);
        read_field(value, "command", out.command_);
        read_field(value, "user", out.user_);
        read_field(value, "state", out.state_);
        read_field(value, "cpu", out.cpu_);
        read_field(value, "memory", out.memory_);
        read_field(value, "virtual_memory", out.virtual_memory_);
        read_field(value, "cpu_time_ms", out.cpu_time_ms_);
        read_field(value, "read_per_sec", out.read_per_sec_);
        read_field(value, "write_per_sec", out.write_per_sec_);
        read_field(value, "read_total", out.read_total_);
        read_field(value, "write_total", out.write_total_);
        read_field(value, "gpu_memory", out.gpu_memory_);
        read_field(value, "gpu_percent", out.gpu_percent_);
        read_field(value, "gpu_ids", out.gpu_ids_);
        read_field(value, "runtime", out.runtime_);
        read_field(value, "thread", out.thread_);
        read_field(value, "kernel_thread", out.kernel_thread_);
        read_field(value, "nice", out.nice_);
        read_field(value, "rx_per_sec", out.rx_per_sec_);
        read_field(value, "tx_per_sec", out.tx_per_sec_);
        read_field(value, "extra", out.extra_);
    }

    void from_json_value(const json& value, network& out) {
        out = network{};
        read_field(value, "name", out.name_);
        read_field(value, "mac", out.mac_);
        read_field(value, "mtu", out.mtu_);
        read_field(value, "speed_mbps", out.speed_mbps_);
        read_field(value, "rx_packets", out.rx_packets_);
        read_field(value, "tx_packets", out.tx_packets_);
        read_field(value, "rx_packets_per_sec", out.rx_packets_per_sec_);
        read_field(value, "tx_packets_per_sec", out.tx_packets_per_sec_);
        read_field(value, "rx_errors", out.rx_errors_);
        read_field(value, "tx_errors", out.tx_errors_);
        read_field(value, "rx_per_sec", out.rx_per_sec_);
        read_field(value, "tx_per_sec", out.tx_per_sec_);
        read_field(value, "rx_total", out.rx_total_);
        read_field(value, "tx_total", out.tx_total_);
        read_field(value, "errors", out.errors_);
        read_field(value, "drops", out.drops_);
        read_field(value, "state", out.state_);
        read_field(value, "address", out.address_);
    }

    void from_json_value(const json& value, disk& out) {
        out = disk{};
        read_field(value, "name", out.name_);
        read_field(value, "mount", out.mount_);
        read_field(value, "filesystem", out.filesystem_);
        read_field(value, "kind", out.kind_);
        read_field(value, "removable", out.removable_);
        read_field(value, "total", out.total_);
        read_field(value, "used", out.used_);
        read_field(value, "available", out.available_);
        read_field(value, "extra", out.extra_);
    }

    void from_json_value(const json& value, disk_io& out) {
        out = disk_io{};
        read_field(value, "name", out.name_);
        read_field(value, "read_per_sec", out.read_per_sec_);
        read_field(value, "write_per_sec", out.write_per_sec_);
        read_field(value, "busy_percent", out.busy_percent_);
        read_field(value, "aggregate", out.aggregate_);
        read_field(value, "read_iops", out.read_iops_);
        read_field(value, "write_iops", out.write_iops_);
        read_field(value, "await_ms", out.await_ms_);
        read_field(value, "queue_depth", out.queue_depth_);
    }

    void from_json_value(const json& value, sensor& out) {
        out = sensor{};
        read_field(value, "name", out.name_);
        read_field(value, "temperature", out.temperature_);
        read_field(value, "critical", out.critical_);
    }

    void from_json_value(const json& value, battery& out) {
        out = battery{};
        read_field(value, "name", out.name_);
        read_field(value, "extra", out.extra_);
        read_field(value, "percent", out.percent_);
        read_field(value, "status", out.status_);
        read_field(value, "watts", out.watts_);
    }

    void from_json_value(const json& value, pressure& out) {
        out = pressure{};
        read_field(value, "resource", out.resource_);
        read_field(value, "some_10", out.some_10_);
        read_field(value, "some_60", out.some_60_);
        read_field(value, "some_300", out.some_300_);
        read_field(value, "some_total_us", out.some_total_us_);
        read_field(value, "full_10", out.full_10_);
        read_field(value, "full_60", out.full_60_);
        read_field(value, "full_300", out.full_300_);
        read_field(value, "full_total_us", out.full_total_us_);
    }

    void from_json_value(const json& value, gpu_process& out) {
        out = gpu_process{};
        read_field(value, "pid", out.pid_);
        read_field(value, "memory", out.memory_);
        read_field(value, "utilization", out.utilization_);
        read_field(value, "kind", out.kind_);
        read_field(value, "memory_utilization", out.memory_utilization_);
        read_field(value, "encoder", out.encoder_);
        read_field(value, "decoder", out.decoder_);
    }

    void from_json_value(const json& value, gpu& out) {
        out = gpu{};
        read_field(value, "id", out.id_);
        read_field(value, "name", out.name_);
        read_field(value, "vendor", out.vendor_);
        read_field(value, "backend", out.backend_);
        read_field(value, "uuid", out.uuid_);
        read_field(value, "driver", out.driver_);
        read_field(value, "cuda_driver_version", out.cuda_driver_version_);
        read_field(value, "nvml_version", out.nvml_version_);
        read_field(value, "utilization", out.utilization_);
        read_field(value, "memory_utilization", out.memory_utilization_);
        read_field(value, "memory_used", out.memory_used_);
        read_field(value, "memory_total", out.memory_total_);
        read_field(value, "temperature", out.temperature_);
        read_field(value, "fan", out.fan_);
        read_field(value, "power_watts", out.power_watts_);
        read_field(value, "power_limit_watts", out.power_limit_watts_);
        read_field(value, "clock_mhz", out.clock_mhz_);
        read_field(value, "memory_clock_mhz", out.memory_clock_mhz_);
        read_field(value, "encoder", out.encoder_);
        read_field(value, "decoder", out.decoder_);
        read_field(value, "pcie_rx_kib", out.pcie_rx_kib_);
        read_field(value, "pcie_tx_kib", out.pcie_tx_kib_);
        read_field(value, "performance_state", out.performance_state_);
        read_field(value, "processes", out.processes_);
        read_field(value, "process_status", out.process_status_);
        read_field(value, "unified_memory", out.unified_memory_);
        read_field(value, "memory_limit", out.memory_limit_);
        read_field(value, "extra", out.extra_);
    }

    void from_json_value(const json& value, connection& out) {
        out = connection{};
        read_field(value, "tags", out.tags_);
        read_field(value, "id", out.id_);
        read_field(value, "protocol", out.protocol_);
        read_field(value, "local", out.local_);
        read_field(value, "remote", out.remote_);
        read_field(value, "state", out.state_);
        read_field(value, "inode", out.inode_);
        read_field(value, "pids", out.pids_);
        read_field(value, "process_names", out.process_names_);
        read_field(value, "receive_queue", out.receive_queue_);
        read_field(value, "send_queue", out.send_queue_);
        read_field(value, "received", out.received_);
        read_field(value, "sent", out.sent_);
        read_field(value, "rx_per_sec", out.rx_per_sec_);
        read_field(value, "tx_per_sec", out.tx_per_sec_);
        read_field(value, "rtt_ms", out.rtt_ms_);
        read_field(value, "retransmits", out.retransmits_);
    }

    void from_json_value(const json& value, connection_sample& out) {
        out = connection_sample{};
        read_field(value, "sampled_at", out.sampled_at_);
        read_field(value, "owners_sampled_at", out.owners_sampled_at_);
        read_field(value, "status", out.status_);
        read_field(value, "notes", out.notes_);
        read_field(value, "owner_scan_complete", out.owner_scan_complete_);
        read_field(value, "unreadable_processes", out.unreadable_processes_);
        read_field(value, "truncated", out.truncated_);
        read_field(value, "connections", out.connections_);
    }

    void from_json_value(const json& value, container& out) {
        out = container{};
        read_field(value, "tags", out.tags_);
        read_field(value, "id", out.id_);
        read_field(value, "runtime", out.runtime_);
        read_field(value, "name", out.name_);
        read_field(value, "image", out.image_);
        read_field(value, "state", out.state_);
        read_field(value, "status", out.status_);
        read_field(value, "cpu", out.cpu_);
        read_field(value, "memory", out.memory_);
        read_field(value, "memory_limit", out.memory_limit_);
        read_field(value, "network_rx", out.network_rx_);
        read_field(value, "network_tx", out.network_tx_);
        read_field(value, "pids", out.pids_);
        read_field(value, "ports", out.ports_);
        read_field(value, "command", out.command_);
        read_field(value, "created", out.created_);
        read_field(value, "running_for", out.running_for_);
        read_field(value, "memory_percent", out.memory_percent_);
        read_field(value, "block_read", out.block_read_);
        read_field(value, "block_write", out.block_write_);
        read_field(value, "network_rx_per_sec", out.network_rx_per_sec_);
        read_field(value, "network_tx_per_sec", out.network_tx_per_sec_);
        read_field(value, "block_read_per_sec", out.block_read_per_sec_);
        read_field(value, "block_write_per_sec", out.block_write_per_sec_);
    }

    void from_json_value(const json& value, service& out) {
        out = service{};
        read_field(value, "tags", out.tags_);
        read_field(value, "name", out.name_);
        read_field(value, "scope", out.scope_);
        read_field(value, "load", out.load_);
        read_field(value, "active", out.active_);
        read_field(value, "sub", out.sub_);
        read_field(value, "description", out.description_);
        read_field(value, "pid", out.pid_);
        read_field(value, "cpu_percent", out.cpu_percent_);
        read_field(value, "memory", out.memory_);
        read_field(value, "cpu_time_ns", out.cpu_time_ns_);
        read_field(value, "tasks", out.tasks_);
        read_field(value, "extra", out.extra_);
    }

    void from_json_value(const json& value, virtual_machine& out) {
        read_field(value, "tags", out.tags_);
        read_field(value, "name", out.name_);
        read_field(value, "id", out.id_);
        read_field(value, "uuid", out.uuid_);
        read_field(value, "backend", out.backend_);
        read_field(value, "state", out.state_);
        read_field(value, "autostart", out.autostart_);
        read_field(value, "description", out.description_);
        read_field(value, "pid", out.pid_);
        read_field(value, "vcpus", out.vcpus_);
        read_field(value, "memory", out.memory_);
        read_field(value, "max_memory", out.max_memory_);
        read_field(value, "cpu_percent", out.cpu_percent_);
        read_field(value, "extra", out.extra_);
    }

    void from_json_value(const json& value, listener& out) {
        out = listener{};
        read_field(value, "protocol", out.protocol_);
        read_field(value, "address", out.address_);
        read_field(value, "port", out.port_);
        read_field(value, "uid", out.uid_);
    }

    string battery::state_label() const {
        if (status_ == "Charging") {
            return "charging";
        }
        if (status_ == "Discharging") {
            return "on battery";
        }
        if (status_ == "Full") {
            return "full";
        }
        if (status_ == "Empty") {
            return "empty";
        }
        return {};
    }

    optional<string> battery::remaining() const {
        const char* key = nullptr;
        if (status_ == "Charging") {
            key = "time_to_full_seconds";
        } else if (status_ == "Discharging") {
            key = "time_to_empty_seconds";
        }
        if (key == nullptr) {
            return none;
        }
        const json* value = extra_.get(key);
        if (value == nullptr || !(value->is_number() || value->is_integer())) {
            return none;
        }
        const double raw = value->as_number();
        if (!(raw > 0.0)) {
            return none;
        }
        const auto seconds = static_cast<uint64_t>(raw);
        return format("{}h{:02}m", seconds / 3600, seconds % 3600 / 60);
    }

    string battery::summary() const {
        vector<string> parts;
        parts.push_back(percent_.has_value() ? format("{:.0f}%", *percent_) : "—");
        if (!state_label().empty()) {
            parts.push_back(state_label());
        }
        if (const auto left = remaining(); left.has_value()) {
            parts.push_back(format("{} {}", *left, status_ == "Charging" ? "to full" : "left"));
        }
        if (watts_.has_value() && neforce::absolute(*watts_) >= 0.05F) {
            parts.push_back(format("{:.1f} W", neforce::absolute(*watts_)));
        }
        string out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                out.append(" · ");
            }
            out.append(parts[i]);
        }
        return out;
    }

    string gpu::memory_label() const { return unified_memory_ ? "UMEM" : "VRAM"; }

    optional<uint64_t> gpu::memory_capacity() const {
        optional<uint64_t> capacity = unified_memory_ ? memory_limit_ : memory_total_;
        if (!capacity.has_value()) {
            capacity = memory_total_;
        }
        if (capacity.has_value() && *capacity > 0) {
            return capacity;
        }
        return none;
    }

    optional<double> gpu::memory_percent() const {
        if (!memory_used_.has_value() || !memory_capacity().has_value()) {
            return none;
        }
        return percent(*memory_used_, *memory_capacity());
    }

    double percent(uint64_t used, uint64_t total) {
        if (total == 0) {
            return 0.0;
        }
        const double value = static_cast<double>(used) / static_cast<double>(total) * 100.0;
        return value < 0.0 ? 0.0 : (value > 100.0 ? 100.0 : value);
    }

    string bytes(float64_t value) {
        static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
        double number = value < 0 ? 0 : value;
        size_t index = 0;
        while (number >= 1024.0 && index < 5) {
            number /= 1024.0;
            ++index;
        }
        return index == 0 ? format("{:.0f} {}", number, units[index]) : format("{:.1f} {}", number, units[index]);
    }

    string duration(uint64_t seconds) {
        if (seconds >= 86400) {
            return format("{}d {:02}h", seconds / 86400, seconds % 86400 / 3600);
        }
        return format("{:02}:{:02}:{:02}", seconds / 3600, seconds % 3600 / 60, seconds % 60);
    }

    bool is_loopback(const string& name) {
        if (name == "lo" || name == "lo0") {
            return true;
        }
        string lower = name;
        for (auto& c: lower) {
            c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }
        return lower.find("loopback") != string::npos;
    }

    string gpu_key(const gpu& device) {
        if (device.uuid_.empty()) {
            return format("{}:{}:{}", device.vendor_, device.id_, device.name_);
        }
        return device.uuid_;
    }

    string container_key(const container& item) {
        return format("Container {}/{} ({})", item.runtime_, item.id_, item.name_);
    }

    json to_json(const connection_sample& value) { return to_json_value(value); }

    json to_json(const inventory& value) { return to_json_value(value); }

    inventory inventory_from_json(const json& value) {
        inventory out;
        read_field(value, "updated_at", out.updated_at_);
        read_field(value, "containers", out.containers_);
        read_field(value, "runtimes", out.runtimes_);
        read_field(value, "services", out.services_);
        read_field(value, "service_managers", out.service_managers_);
        read_field(value, "virtual_machines", out.virtual_machines_);
        read_field(value, "vm_managers", out.vm_managers_);
        read_field(value, "listeners", out.listeners_);
        read_field(value, "notes", out.notes_);
        read_field(value, "extra", out.extra_);
        return out;
    }

    json to_json(const snapshot& value) {
        json o = json::object();
        o.set("timestamp", value.timestamp_);
        o.set("elapsed", value.elapsed_);
        o.set("collection_ms", value.collection_ms_);
        o.set("hostname", value.hostname_);
        o.set("os", value.os_);
        o.set("kernel", value.kernel_);
        o.set("uptime", value.uptime_);
        o.set("cpu_name", value.cpu_name_);
        o.set("cpu_percent", value.cpu_percent_);
        o.set("cpu_mhz", value.cpu_mhz_);
        o.set("cpu_breakdown", value.cpu_breakdown_);
        o.set("cores", to_json_value(value.cores_));
        o.set("pcore_count", value.pcore_count_);
        o.set("ecore_count", value.ecore_count_);
        o.set("core_temperatures", to_json_value(value.core_temperatures_));
        json load = json::array();
        load.push(json(value.load_[0]));
        load.push(json(value.load_[1]));
        load.push(json(value.load_[2]));
        o.set("load", move(load));
        o.set("memory", to_json_value(value.memory_));
        o.set("processes", to_json_value(value.processes_));
        o.set("networks", to_json_value(value.networks_));
        o.set("connections", to_json_value(value.connections_));
        o.set("disks", to_json_value(value.disks_));
        o.set("disk_io", to_json_value(value.disk_io_));
        o.set("sensors", to_json_value(value.sensors_));
        o.set("batteries", to_json_value(value.batteries_));
        o.set("gpus", to_json_value(value.gpus_));
        o.set("gpu_status", value.gpu_status_);
        o.set("pressure", to_json_value(value.pressure_));
        o.set("alerts", to_json_value(value.alerts_));
        o.set("inventory", to_json_value(value.inventory_));
        o.set("extra", value.extra_);
        return o;
    }

    snapshot snapshot_from_json(const json& value) {
        snapshot s;
        read_field(value, "timestamp", s.timestamp_);
        read_field(value, "elapsed", s.elapsed_);
        read_field(value, "collection_ms", s.collection_ms_);
        read_field(value, "hostname", s.hostname_);
        read_field(value, "os", s.os_);
        read_field(value, "kernel", s.kernel_);
        read_field(value, "uptime", s.uptime_);
        read_field(value, "cpu_name", s.cpu_name_);
        read_field(value, "cpu_percent", s.cpu_percent_);
        read_field(value, "cpu_mhz", s.cpu_mhz_);
        read_field(value, "cpu_breakdown", s.cpu_breakdown_);
        read_field(value, "cores", s.cores_);
        read_field(value, "pcore_count", s.pcore_count_);
        read_field(value, "ecore_count", s.ecore_count_);
        read_field(value, "core_temperatures", s.core_temperatures_);
        if (const json* load = value.get("load"); load != nullptr && load->is_array()) {
            const auto& values = load->as_array();
            for (size_t i = 0; i < 3 && i < values.size(); ++i) {
                s.load_[i] = static_cast<float>(values[i].as_number());
            }
        }
        read_field(value, "memory", s.memory_);
        read_field(value, "processes", s.processes_);
        read_field(value, "networks", s.networks_);
        read_field(value, "connections", s.connections_);
        read_field(value, "disks", s.disks_);
        read_field(value, "disk_io", s.disk_io_);
        read_field(value, "sensors", s.sensors_);
        read_field(value, "batteries", s.batteries_);
        read_field(value, "gpus", s.gpus_);
        read_field(value, "gpu_status", s.gpu_status_);
        read_field(value, "pressure", s.pressure_);
        read_field(value, "alerts", s.alerts_);
        if (const json* inventory_value = value.get("inventory"); inventory_value != nullptr) {
            s.inventory_ = inventory_from_json(*inventory_value);
        }
        read_field(value, "extra", s.extra_);
        return s;
    }

    void snapshot::apply_inventory(inventory data) {
        for (const auto& item: data.services_) {
            if (item.active_ == "failed") {
                alerts_.push_back(format("Service {} failed", item.name_));
            }
        }
        for (const auto& item: data.containers_) {
            if (item.state_ == "restarting" || item.status_.find("unhealthy") != string::npos) {
                alerts_.push_back(format("Container {}: {}", item.name_, item.status_));
            }
        }
        for (auto& item: data.services_) {
            if (!item.pid_.has_value()) {
                continue;
            }
            for (const auto& proc: processes_) {
                if (proc.pid_ == *item.pid_) {
                    item.cpu_percent_ = proc.cpu_;
                    if (!item.memory_.has_value()) {
                        item.memory_ = proc.memory_;
                    }
                    break;
                }
            }
        }
        inventory_ = move(data);

        json groups = json::object();
        for (const auto& proc: processes_) {
            if (proc.thread_) {
                continue;
            }
            json* group = &groups[proc.name_];
            if (group->is_null()) {
                *group = json::object();
                (*group)["cpu_percent"] = json(0.0F);
                (*group)["rss_sum_bytes"] = json(static_cast<uint64_t>(0));
                (*group)["pids"] = json::array();
            }
            (*group)["cpu_percent"] = json(static_cast<float>((*group)["cpu_percent"].as_number()) + proc.cpu_);
            (*group)["rss_sum_bytes"] =
                    json(static_cast<uint64_t>((*group)["rss_sum_bytes"].as_integer()) + proc.memory_);
            (*group)["pids"].push(json(proc.pid_));
        }
        json applications = json::array();
        for (const auto& member: groups.as_object()) {
            json item = json::object();
            item.set("name", member.first);
            item.set("cpu_percent",
                     member.second.get("cpu_percent") != nullptr ? *member.second.get("cpu_percent") : json(0.0F));
            item.set("rss_sum_bytes", member.second.get("rss_sum_bytes") != nullptr
                                              ? *member.second.get("rss_sum_bytes")
                                              : json(static_cast<uint64_t>(0)));
            item.set("pids", member.second.get("pids") != nullptr ? *member.second.get("pids") : json::array());
            applications.push(move(item));
        }
        extra_.set("Applications", move(applications));

        if (const json* output = inventory_.extra_.get("Process TCP bytes / 1s (bpftrace)");
            output != nullptr && output->is_object()) {
            const json* text = output->get("output");
            if (text != nullptr && text->is_string()) {
                const string& body = text->as_string();
                size_t start = 0;
                while (start <= body.size()) {
                    const size_t end = body.find('\n', start);
                    const string line = body.substr(start, end == string::npos ? string::npos : end - start);
                    start = end == string::npos ? body.size() + 1 : end + 1;
                    if (line.empty()) {
                        continue;
                    }
                    json parsed;
                    try {
                        parsed = json::parse(line.view());
                    } catch (...) {
                        continue;
                    }
                    const json* data_node = parsed.get("data");
                    if (data_node == nullptr) {
                        continue;
                    }
                    for (const auto& direction: vector<pair<const char*, bool>>{{"@rx", true}, {"@tx", false}}) {
                        const json* values = data_node->get(direction.first);
                        if (values == nullptr || !values->is_object()) {
                            continue;
                        }
                        for (const auto& member: values->as_object()) {
                            uint32_t pid = 0;
                            for (const char c: member.first) {
                                if (c < '0' || c > '9') {
                                    break;
                                }
                                pid = pid * 10 + static_cast<uint32_t>(c - '0');
                            }
                            for (auto& proc: processes_) {
                                if (proc.pid_ != pid) {
                                    continue;
                                }
                                if (direction.second) {
                                    proc.rx_per_sec_ = member.second.as_number();
                                } else {
                                    proc.tx_per_sec_ = member.second.as_number();
                                }
                            }
                        }
                    }
                }
            }
        }
    }

} // namespace neforce::neflux
