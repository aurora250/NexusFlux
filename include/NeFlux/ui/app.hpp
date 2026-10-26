#ifndef NEXUSFLUX_UI_APP_HPP__
#define NEXUSFLUX_UI_APP_HPP__

/**
 * @file app.hpp
 * @brief 交互式界面入口
 */

#include <NeFlux/cli.hpp>
#include <NeFlux/config.hpp>
#include <NeFlux/model.hpp>
namespace neforce::neflux::ui {

    /**
     * @brief 运行交互式监控
     * @param args 命令行参数
     * @param cfg 配置
     * @param config_path 配置文件路径
     * @return 进程退出码
     */
    int run_interactive(const cli_args& args, config cfg, const string& config_path);

    /**
     * @brief 离屏渲染纯文本
     * @param value 快照
     * @param data 发现清单
     * @param cfg 配置
     * @param view_name 页面名
     * @param width 终端列数
     * @param height 终端行数
     * @return 纯文本帧
     */
    NEFORCE_NODISCARD string render_plaintext(const snapshot& value, const inventory& data, const config& cfg,
                                              const string& view_name, int width, int height);

    /**
     * @brief 使用多份历史快照离屏渲染 SVG
     * @param values 历史快照
     * @param data 发现清单
     * @param cfg 配置
     * @param view_name 页面名
     * @param gpu_panel GPU 子面板（summary/history/device）
     * @param width 终端列数
     * @param height 终端行数
     * @return SVG 文本
     */
    NEFORCE_NODISCARD string render_svg_history(const vector<snapshot>& values, const inventory& data,
                                                const config& cfg, const string& view_name, const string& gpu_panel,
                                                int width, int height);

    /**
     * @brief 离屏渲染 SVG
     * @param value 快照
     * @param data 发现清单
     * @param cfg 配置
     * @param view_name 页面名
     * @param gpu_panel GPU 子面板（summary/history/device）
     * @param width 终端列数
     * @param height 终端行数
     * @return SVG 文本
     */
    NEFORCE_NODISCARD string render_svg(const snapshot& value, const inventory& data, config cfg,
                                        const string& view_name, const string& gpu_panel, int width, int height);

} // namespace neforce::neflux::ui
#endif // NEXUSFLUX_UI_APP_HPP__
