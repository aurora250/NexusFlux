#ifndef NEFLUX_ADVANCED_HPP__
#define NEFLUX_ADVANCED_HPP__

/**
 * @file advanced.hpp
 * @brief 平台高级遥测
 *
 * 补充 CPU 调度器、RAPL、磁盘延迟、DRM 进程计数与按列请求的进程字段。
 */

#include <NeFlux/config.hpp>
#include <NeFlux/model.hpp>
#include <NeForce/core/memory/unique_ptr.hpp>
namespace neforce::neflux {

    /**
     * @brief 高级遥测采集器
     *
     * 保留计数器缓存以计算速率。
     */
    class advanced {
    public:
        advanced();
        ~advanced();

        advanced(const advanced&) = delete;
        advanced& operator=(const advanced&) = delete;

        /**
         * @brief 采集并填充快照
         * @param value 快照
         * @param cfg 配置
         */
        void collect(snapshot& value, const config& cfg);

    private:
        struct impl;
        unique_ptr<impl> impl_;
    };

    /**
     * @brief 采集平台扩展发现项
     * @param cfg 配置
     * @return JSON 对象，写入 `inventory.extra`
     */
    NEFORCE_NODISCARD json discover_platform_extras(const config& cfg);

} // namespace neforce::neflux
#endif // NEFLUX_ADVANCED_HPP__
