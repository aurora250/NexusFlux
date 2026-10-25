#ifndef NEXUSFLUX_HISTORY_HPP__
#define NEXUSFLUX_HISTORY_HPP__

/**
 * @file history.hpp
 * @brief 有界遥测历史
 *
 * 只做数据聚合，不访问终端或操作系统。
 */

#include <NeForce/core/container/deque.hpp>
#include <NeForce/core/container/map.hpp>
#include <NeFlux/config.hpp>
#include <NeFlux/model.hpp>
#include <NeFlux/plot.hpp>
namespace neforce::neflux {

    /**
     * @brief 进程历史
     */
    struct process_history {
        deque<double> cpu_;  ///< CPU 百分比
        deque<double> ram_;  ///< 常驻内存
        deque<double> gpu_;  ///< GPU 利用率
        deque<double> vram_; ///< 显存占用

        /**
         * @brief 追加一次采样
         * @param value 进程
         * @param gpu 显存与利用率
         * @param limit 容量
         */
        void push(const process& value, optional<uint64_t> gpu_memory, optional<uint32_t> gpu_utilization,
                  size_t limit);
    };

    /**
     * @brief GPU 历史
     */
    struct gpu_history {
        deque<double> utilization_;  ///< 计算利用率
        deque<double> vram_;         ///< 显存百分比
        deque<double> memory_busy_;  ///< 显存带宽利用率
        deque<double> encoder_;      ///< 编码利用率
        deque<double> decoder_;      ///< 解码利用率
        deque<double> power_;        ///< 功耗
        deque<double> temperature_;  ///< 温度
        deque<double> fan_;          ///< 风扇
        deque<double> clock_;        ///< 核心频率
        deque<double> memory_clock_; ///< 显存频率
        deque<double> pcie_rx_;      ///< PCIe 接收
        deque<double> pcie_tx_;      ///< PCIe 发送
    };

    /**
     * @brief 网络峰值
     */
    struct network_peak {
        double rx_ = 0.0; ///< 接收峰值
        double tx_ = 0.0; ///< 发送峰值
    };

    /**
     * @brief 网络接口历史
     */
    struct network_history {
        deque<double> rx_;                            ///< 接收速率
        deque<double> tx_;                            ///< 发送速率
        double rx_peak_ = 0.0;                        ///< 接收峰值
        double tx_peak_ = 0.0;                        ///< 发送峰值
        uint64_t rx_session_ = 0;                     ///< 会话接收字节
        uint64_t tx_session_ = 0;                     ///< 会话发送字节
        optional<pair<uint64_t, uint64_t>> previous_; ///< 上一累计计数

        /**
         * @brief 追加一次采样
         * @param rx 接收速率
         * @param tx 发送速率
         * @param received 接收增量
         * @param sent 发送增量
         * @param limit 容量
         */
        void push(double rx, double tx, uint64_t received, uint64_t sent, size_t limit);
    };

    /**
     * @brief 遥测历史集合
     */
    struct history {
        uint64_t sequence_ = 0;                 ///< 采样序列号
        deque<double> timestamps_;              ///< 样本时间
        deque<double> periods_;                 ///< 采样周期
        deque<double> cpu_;                     ///< CPU 百分比
        deque<double> memory_;                  ///< 内存百分比
        deque<double> rx_;                      ///< 网络接收
        deque<double> tx_;                      ///< 网络发送
        network_peak network_peaks_[2];         ///< 会话峰值（不含/含环回）
        map<string, network_history> networks_; ///< 接口历史
        network_history network_totals_[2];     ///< 聚合历史
        deque<double> disk_read_;               ///< 磁盘读取
        deque<double> disk_write_;              ///< 磁盘写入
        map<string, deque<double>> gpu_;        ///< GPU 利用率
        map<string, gpu_history> gpu_details_;  ///< GPU 详细历史
        map<string, deque<double>> series_;     ///< 通用序列

        /**
         * @brief 获取网络历史
         * @param cfg 配置
         * @return 网络历史，未知接口时为空
         */
        NEFORCE_NODISCARD network_history* network(const config& cfg);

        /**
         * @brief 获取网络历史
         * @param cfg 配置
         * @return 网络历史，未知接口时为空
         */
        NEFORCE_NODISCARD const network_history* network(const config& cfg) const;

        /**
         * @brief 按样本数创建窗口
         * @param samples 样本数
         * @return 窗口
         */
        NEFORCE_NODISCARD plot_window window(size_t samples) const;

        /**
         * @brief 按时间创建窗口
         * @param seconds 秒
         * @return 窗口
         */
        NEFORCE_NODISCARD plot_window time_window(uint64_t seconds) const;

        /**
         * @brief 合并一份快照
         * @param value 快照
         * @param cfg 配置
         */
        void ingest(const snapshot& value, const config& cfg);
    };

} // namespace neforce::neflux
#endif // NEXUSFLUX_HISTORY_HPP__
