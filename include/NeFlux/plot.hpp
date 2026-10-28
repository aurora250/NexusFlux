#ifndef NEXUSFLUX_PLOT_HPP__
#define NEXUSFLUX_PLOT_HPP__

/**
 * @file plot.hpp
 * @brief 采样对齐的图表投影
 *
 * 将历史环形缓冲投影到终端列，保持样本相位、极值与缺失间隔。
 */

#include <NeForce/tui/chart/canvas_chart.hpp>
namespace neforce::neflux {
    using plot_range = tui::canvas_chart_range;
    using time_scale = tui::canvas_chart_time;
    using plot_window = tui::canvas_chart_window;
} // namespace neforce::neflux
#endif // NEXUSFLUX_PLOT_HPP__
