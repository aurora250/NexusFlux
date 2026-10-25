#ifndef NEXUSFLUX_DIAGNOSTICS_HPP__
#define NEXUSFLUX_DIAGNOSTICS_HPP__

/**
 * @file diagnostics.hpp
 * @brief 基于证据的资源诊断
 */

#include <NeFlux/config.hpp>
#include <NeFlux/model.hpp>
namespace neforce::neflux {

    /**
     * @brief 诊断发现
     */
    struct finding {
        string domain_;   ///< 资源域
        string severity_; ///< URGENT/CHECK/INFO/UNKNOWN
        string title_;    ///< 标题
        string evidence_; ///< 证据
        string meaning_;  ///< 含义
        string next_;     ///< 建议
    };

    /**
     * @brief 生成诊断发现
     * @param value 快照
     * @param cfg 配置
     * @return 按严重程度排序的发现
     */
    NEFORCE_NODISCARD vector<finding> findings(const snapshot& value, const config& cfg);

    /**
     * @brief 生成诊断报告
     * @param value 快照
     * @param cfg 配置
     * @return 纯文本报告
     */
    NEFORCE_NODISCARD string report(const snapshot& value, const config& cfg);

    /**
     * @brief 生成单行摘要
     * @param value 快照
     * @param cfg 配置
     * @return 摘要文本
     */
    NEFORCE_NODISCARD string summary(const snapshot& value, const config& cfg);

    /**
     * @brief 判断发现是否与页面相关
     * @param view 页面名
     * @param domain 资源域
     * @return 相关返回 true
     */
    NEFORCE_NODISCARD bool relevant(const string& view, const string& domain);

    /**
     * @brief 序列化发现
     * @param value 发现
     * @return JSON 值
     */
    NEFORCE_NODISCARD json to_json(const finding& value);

    /**
     * @brief 生成告警列表
     * @param value 快照
     * @param cfg 配置
     * @return 告警文本
     */
    NEFORCE_NODISCARD vector<string> resource_alerts(const snapshot& value, const config& cfg);

} // namespace neforce::neflux
#endif // NEXUSFLUX_DIAGNOSTICS_HPP__
