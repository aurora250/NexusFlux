#ifndef NEFLUX_TASKSTATS_HPP__
#define NEFLUX_TASKSTATS_HPP__

/**
 * @file taskstats.hpp
 * @brief Linux TASKSTATS 延迟计数
 *
 * 通过 generic netlink 查询线程/进程组延迟；不修改任何内核设置。
 */

#include <NeFlux/common.hpp>
#include <NeFlux/json.hpp>
namespace neforce::neflux {

    /**
     * @brief TASKSTATS 延迟采集器
     *
     * 维护上一轮计数以计算每秒延迟占比。
     */
    class taskstats_collector {
    public:
        taskstats_collector();
        ~taskstats_collector();

        taskstats_collector(const taskstats_collector&) = delete;
        taskstats_collector& operator=(const taskstats_collector&) = delete;

        /**
         * @brief 查询一个进程的延迟统计
         * @param pid 进程或线程标识
         * @param start_time 启动时间，用于缓存匹配
         * @param thread 是否查询单线程
         * @return 延迟 JSON
         * @throws exception 内核未启用、权限不足或记录缺失时抛出
         */
        NEFORCE_NODISCARD json collect(uint32_t pid, uint64_t start_time, bool thread);

        /**
         * @brief 清理已消失进程的缓存
         * @param identities 仍存在的进程身份
         */
        void retain(const set<pair<uint32_t, uint64_t>>& identities);

    private:
        struct impl;
        unique_ptr<impl> impl_;
    };

} // namespace neforce::neflux
#endif // NEFLUX_TASKSTATS_HPP__
