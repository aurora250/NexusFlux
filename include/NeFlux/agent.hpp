#ifndef NEXUSFLUX_AGENT_HPP__
#define NEXUSFLUX_AGENT_HPP__

/**
 * @file agent.hpp
 * @brief 隐藏的采集端模式
 */

#include <NeFlux/cli.hpp>
#include <NeFlux/config.hpp>

namespace neforce::neflux {

    /**
     * @brief 运行采集端
     * @param args 命令行参数
     * @param cfg 配置
     * @return 退出码
     */
    int run_agent(const cli_args& args, config cfg);

} // namespace neforce::neflux
#endif // NEXUSFLUX_AGENT_HPP__
