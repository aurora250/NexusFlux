#ifndef NEXUSFLUX_FILTER_HPP__
#define NEXUSFLUX_FILTER_HPP__

/**
 * @file filter.hpp
 * @brief 交互式查询表达式
 *
 * 支持 `field:value`、`!` 取反、数值比较、`/regex/` 与空格 AND、` | ` 分隔 OR。
 */

#include <NeFlux/model.hpp>
#include <NeForce/core/string/string.hpp>
#include <NeForce/core/utility/optional.hpp>
namespace neforce::neflux {

    /**
     * @brief 告警事件
     */
    struct event {
        uint64_t timestamp_ = 0; ///< UNIX 秒
        string condition_;       ///< 条件文本
        bool active_ = false;    ///< 是否处于活动状态
    };

    /**
     * @brief 解析后的查询
     */
    class query {
    public:
        query();
        ~query();

        query(const query&) = delete;
        query& operator=(const query&) = delete;
        query(query&&) noexcept;
        query& operator=(query&&) noexcept;

        /**
         * @brief 解析查询文本
         * @param text 文本
         * @return 查询
         */
        NEFORCE_NODISCARD static query parse(const string& text);

        /**
         * @brief 匹配进程
         * @param value 进程
         * @return 匹配返回 true
         */
        NEFORCE_NODISCARD bool matches(const process& value) const;

        /**
         * @brief 获取解析错误
         * @return 错误文本，无错误时为空
         */
        NEFORCE_NODISCARD const optional<string>& error() const noexcept;

        /**
         * @brief 是否为空查询
         * @return 空返回 true
         */
        NEFORCE_NODISCARD bool empty() const noexcept;

    private:
        struct impl;
        unique_ptr<impl> impl_;
    };

    /**
     * @brief 匹配连接
     * @param value 连接
     * @param text 查询文本
     * @return 匹配返回 true
     */
    NEFORCE_NODISCARD bool connection_matches(const connection& value, const string& text);

    /**
     * @brief 匹配事件
     * @param event 事件
     * @param text 查询文本
     * @return 匹配返回 true
     */
    NEFORCE_NODISCARD bool event_matches(const event& event, const string& text);

} // namespace neforce::neflux
#endif // NEXUSFLUX_FILTER_HPP__
