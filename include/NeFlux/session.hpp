#ifndef NEXUSFLUX_SESSION_HPP__
#define NEXUSFLUX_SESSION_HPP__

/**
 * @file session.hpp
 * @brief 采集会话创建
 */

#include <NeFlux/cli.hpp>
#include <NeFlux/source.hpp>

namespace neforce::neflux {

    /**
     * @brief 按命令行创建数据源
     * @param args 命令行参数
     * @param cfg 配置
     * @param error 失败原因
     * @return 数据源
     */
    NEFORCE_NODISCARD unique_ptr<telemetry_source> create_source(const cli_args& args, const config& cfg,
                                                                 string& error);

} // namespace neforce::neflux
#endif // NEXUSFLUX_SESSION_HPP__
