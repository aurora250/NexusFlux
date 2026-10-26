#ifndef NEXFLUX_UI_TESTING_HPP__
#define NEXFLUX_UI_TESTING_HPP__

/**
 * @file testing.hpp
 * @brief TUI 无头测试入口
 */

#include <NeFlux/common.hpp>
namespace neforce::neflux::ui {

    /**
     * @brief 在无头 TUI 中注入输入并返回渲染文本
     * @param input 键盘/鼠标输入字节流
     * @return 渲染后的纯文本
     */
    NEFORCE_NODISCARD string headless_probe(string_view input);

    /**
     * @brief 获取无头 TUI 指定单元格背景色
     * @param input 键盘/鼠标输入字节流，使用 `|` 分隔逐帧输入
     * @param x 列坐标
     * @param y 行坐标
     * @return 0xAARRGGBB 背景色
     */
    NEFORCE_NODISCARD uint32_t headless_probe_background(string_view input, int x, int y);

    /**
     * @brief 获取无头 TUI 矩形区域背景色
     * @param input 键盘/鼠标输入字节流，使用 `|` 分隔逐帧输入
     * @param x 起始列
     * @param y 起始行
     * @param width 宽度
     * @param height 高度
     * @return 按行优先排列的 0xAARRGGBB 背景色
     */
    NEFORCE_NODISCARD vector<uint32_t> headless_probe_backgrounds(string_view input, int x, int y, int width,
                                                                  int height);

    /**
     * @brief 获取无头 TUI 最后一次 ANSI 输出
     * @param input 键盘/鼠标输入字节流，使用 `|` 分隔逐帧输入
     * @return ANSI 转义序列
     */
    NEFORCE_NODISCARD string headless_probe_ansi(string_view input);

} // namespace neforce::neflux::ui
#endif // NEXFLUX_UI_TESTING_HPP__
