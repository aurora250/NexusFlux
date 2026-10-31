#ifndef NEXUSFLUX_COLLECT_HPP__
#define NEXUSFLUX_COLLECT_HPP__

/**
 * @file collect.hpp
 * @brief 系统采样器
 *
 * 负责按采样间隔读取 Linux 的 procfs/sysfs、NVML 与本地发现结果，组装快照模型。
 */

#include <NeForce/core/container/map.hpp>
#include <NeForce/core/system/sysinfo.hpp>
#include <NeForce/core/memory/unique_ptr.hpp>
#include <NeForce/core/utility/tuple.hpp>
#include <NeFlux/config.hpp>
#include <NeFlux/model.hpp>
namespace neforce::neflux {

    /**
     * @brief 计算计数器速率
     * @param previous 上次计数
     * @param current 本次计数
     * @param seconds 间隔秒数
     * @return 每秒速率，计数器回绕时为 0
     */
    NEFORCE_NODISCARD double rate(optional<uint64_t> previous, uint64_t current, double seconds);

    /**
     * @brief 解析 /proc/diskstats
     * @param text 文件内容
     * @return 设备到 (读取字节, 写入字节, 忙毫秒) 的映射
     */
    NEFORCE_NODISCARD map<string, tuple<uint64_t, uint64_t, uint64_t>> parse_diskstats(string_view text);

    /**
     * @brief 从 CPU 计数器计算时间构成
     * @param previous 上次计数
     * @param current 本次计数
     * @return 各分项百分比
     */
    NEFORCE_NODISCARD json cpu_breakdown(const vector<uint64_t>& previous, const vector<uint64_t>& current);

    /**
     * @brief 生成 CPU 硬件信息 JSON
     * @param cpu CPU 信息
     * @param architecture 系统架构
     * @return CPU 信息 JSON
     */
    NEFORCE_NODISCARD json cpu_info_json(const sysinfo::cpu_info& cpu, sysinfo::architecture architecture);

    /**
     * @brief 采集首帧轻量信息
     * @return 快照
     */
    NEFORCE_NODISCARD snapshot initial_snapshot();

    /**
     * @brief 系统采样器
     *
     * 保留上一次计数器与进程缓存，用于计算速率。
     */
    class collector {
    public:
        /**
         * @brief 构造采样器
         * @param gpu_enabled 是否采集 GPU
         */
        explicit collector(bool gpu_enabled);

        ~collector();

        collector(const collector&) = delete;
        collector& operator=(const collector&) = delete;

        /**
         * @brief 执行一次完整采样
         * @param cfg 配置
         * @return 快照
         * @throws exception 读取系统信息发生不可恢复错误时抛出
         */
        NEFORCE_NODISCARD snapshot collect(const config& cfg);

        /**
         * @brief 执行一次可选进程与 GPU 进程的采样
         * @param cfg 配置
         * @param processes_enabled 是否采集进程
         * @param live_gpu_processes 是否查询 GPU 进程
         * @param connections_enabled 是否采集连接表
         * @return 快照
         */
        NEFORCE_NODISCARD snapshot collect_with_processes(const config& cfg, bool processes_enabled,
                                                          bool live_gpu_processes, bool connections_enabled = true);

        /**
         * @brief 设置是否采集 GPU
         * @param enabled 是否采集
         */
        void set_gpu_enabled(bool enabled) noexcept;

        /**
         * @brief 查询是否采集 GPU
         * @return 是否采集
         */
        NEFORCE_NODISCARD bool gpu_enabled() const noexcept;

    private:
        struct impl;
        unique_ptr<impl> impl_;
    };

} // namespace neforce::neflux
#endif // NEXUSFLUX_COLLECT_HPP__
