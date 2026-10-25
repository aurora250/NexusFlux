#ifndef NEXUSFLUX_PLOT_HPP__
#define NEXUSFLUX_PLOT_HPP__

/**
 * @file plot.hpp
 * @brief 采样对齐的图表投影
 *
 * 将历史环形缓冲投影到终端列，保持样本相位、极值与缺失间隔。
 */

#include <NeForce/core/container/deque.hpp>
#include <NeForce/core/container/vector.hpp>
namespace neforce::neflux {

    /**
     * @brief 单列投影区间
     */
    struct plot_range {
        double low = 0.0;  ///< 列内最小值
        double high = 0.0; ///< 列内最大值
        double last = 0.0; ///< 列内最新值
        bool gap = false;  ///< 是否跨越缺失样本
    };

    /**
     * @brief 时间刻度
     */
    struct time_scale {
        const deque<double>* timestamps = nullptr; ///< 样本时间
        const deque<double>* periods = nullptr;    ///< 采样周期
        uint64_t seconds = 0;                      ///< 时间窗口
    };

    /**
     * @brief 图表窗口
     */
    struct plot_window {
        size_t samples = 0;    ///< 可见样本数
        uint64_t end = 0;      ///< 独占序列号
        bool has_time = false; ///< 是否按时间投影
        time_scale time{};     ///< 时间刻度

        /**
         * @brief 获取投影步长
         * @param width 列数
         * @return 每列样本数
         */
        NEFORCE_NODISCARD size_t stride(size_t width) const;

        /**
         * @brief 获取可容纳的样本数
         * @param width 列数
         * @return 样本数
         */
        NEFORCE_NODISCARD size_t capacity(size_t width) const;

        /**
         * @brief 投影到列
         * @param values 采样值
         * @param width 列数
         * @param out 输出列
         * @note 非有限值形成缺口，不参与插值
         */
        void project(const deque<double>& values, size_t width, vector<plot_range>& out) const;
    };

    /**
     * @brief 计算量化后的图表上限
     * @param value 原始上限
     * @return 1/2/5×10^n 形式的量化上限
     */
    NEFORCE_NODISCARD double nice_max(double value);

} // namespace neforce::neflux
#endif // NEXUSFLUX_PLOT_HPP__
