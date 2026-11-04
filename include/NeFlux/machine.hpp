#ifndef NEXUSFLUX_MACHINE_HPP__
#define NEXUSFLUX_MACHINE_HPP__

/**
 * @file machine.hpp
 * @brief 启动目标机器解析
 */

#include <NeFlux/config.hpp>
#include <NeFlux/discovery.hpp>
#include <NeForce/core/utility/optional.hpp>

namespace neforce::neflux {

    /**
     * @brief 机器类型
     */
    enum class machine_kind : unsigned char {
        local, ///< 本机
        qemu,  ///< 本机 QEMU 进程
        ssh,   ///< 手工 SSH 目标
    };

    /**
     * @brief 目标机器
     */
    struct machine_target {
        machine_kind kind_ = machine_kind::local; ///< 类型
        string selector_;                         ///< 原始选择器
        string name_;                             ///< 显示名 / VM 名
        string host_ = "127.0.0.1";               ///< SSH 主机
        uint16_t port_ = 22;                      ///< SSH 端口
        string user_;                             ///< SSH 用户
        string agent_path_ = "nexus-flux";        ///< guest agent 路径
        vector<string> start_argv_;               ///< 显式 QEMU 启动命令
    };

    /**
     * @brief 解析 --machine 选择器
     * @param selector 选择器
     * @param error 失败原因
     * @return 目标
     */
    NEFORCE_NODISCARD optional<machine_target> parse_machine_selector(string_view selector, string& error);

    /**
     * @brief 读取 QEMU 虚拟机的 SSH hostfwd 端点
     * @param vm 虚拟机
     * @param error 失败原因
     * @return host/port
     */
    NEFORCE_NODISCARD optional<pair<string, uint16_t>> machine_ssh_endpoint(const virtual_machine& vm, string& error);

    /**
     * @brief 补全 QEMU 目标的 SSH 端点，必要时启动 VM
     * @param target 目标
     * @param cfg 配置
     * @param start_vm 是否尝试启动 VM
     * @param error 失败原因
     * @return 成功返回 true
     */
    bool resolve_machine_target(machine_target& target, const config& cfg, bool start_vm, string& error);

} // namespace neforce::neflux
#endif // NEXUSFLUX_MACHINE_HPP__
