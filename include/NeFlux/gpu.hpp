#ifndef NEXUSFLUX_GPU_HPP__
#define NEXUSFLUX_GPU_HPP__

/**
 * @file gpu.hpp
 * @brief GPU 采集
 *
 * 通过 NVML 动态库读取 NVIDIA 设备状态，并在不可用时优雅降级。
 */

#include <NeFlux/model.hpp>
namespace neforce::neflux {

    /**
     * @brief GPU 采集结果
     */
    struct gpu_result {
        vector<gpu> devices_; ///< 设备列表
        string status_;       ///< 采集状态
    };

    /**
     * @brief 采集 GPU
     * @param enabled 是否启用
     * @param live_processes 是否查询进程
     * @return 采集结果
     */
    NEFORCE_NODISCARD gpu_result collect_gpus(bool enabled, bool live_processes);

    /**
     * @brief GPU 选择条件
     */
    struct gpu_selection {
        uint64_t min_free_bytes_ = 0;               ///< 最小空闲显存
        uint64_t min_total_bytes_ = 0;              ///< 最小总显存
        optional<uint32_t> max_gpu_utilization_;    ///< 最大利用率
        optional<uint32_t> max_memory_utilization_; ///< 最大显存利用率
    };

    /**
     * @brief 按最少忙碌优先选择 GPU
     * @param devices 设备列表
     * @param selection 条件
     * @return 符合条件的设备，按忙碌程度排序
     */
    NEFORCE_NODISCARD vector<gpu> select_gpus(const vector<gpu>& devices, const gpu_selection& selection);

} // namespace neforce::neflux
#endif // NEXUSFLUX_GPU_HPP__
