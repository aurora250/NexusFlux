#ifndef NEXFLUX_UI_TESTING_HPP__
#define NEXFLUX_UI_TESTING_HPP__

/**
 * @file testing.hpp
 * @brief TUI 无头测试入口
 */

#include <NeFlux/common.hpp>
#include <NeFlux/model.hpp>
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
     * @brief 在指定尺寸的无头 TUI 中注入输入并返回渲染文本
     * @param width 列数
     * @param height 行数
     * @param input 键盘/鼠标输入字节流，使用 `|` 分隔逐帧输入
     * @return 最后一帧的纯文本
     */
    NEFORCE_NODISCARD string headless_probe_sized(int width, int height, string_view input);

    /**
     * @brief 渲染后改变终端尺寸再渲染一帧，返回缩放后的文本
     * @param width 初始列数
     * @param height 初始行数
     * @param new_width 新列数
     * @param new_height 新行数
     * @return 缩放后最后一帧的纯文本
     * @note 用于验证内容/弹窗在缩放当帧就使用新尺寸
     */
    NEFORCE_NODISCARD string headless_probe_resized(int width, int height, int new_width, int new_height);

    /**
     * @brief 在给定快照上运行无头 TUI，并返回指定单元格背景色
     * @param value 快照
     * @param data 清单
     * @param view_name 视图名
     * @param input 键盘/鼠标输入字节流，使用 `|` 分隔逐帧输入
     * @param x 列坐标
     * @param y 行坐标
     * @return 0xAARRGGBB 背景色
     * @note 用于验证进程表等依赖真实数据的命中/选中效果
     */
    NEFORCE_NODISCARD uint32_t headless_probe_seeded_background(const snapshot& value, const inventory& data,
                                                                string_view view_name, string_view input, int x, int y);

    /**
     * @brief 获取无头 TUI 最后一次 ANSI 输出
     * @param input 键盘/鼠标输入字节流，使用 `|` 分隔逐帧输入
     * @return ANSI 转义序列
     */
    NEFORCE_NODISCARD string headless_probe_ansi(string_view input);

} // namespace neforce::neflux::ui
#endif // NEXFLUX_UI_TESTING_HPP__
