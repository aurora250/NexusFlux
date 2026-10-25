#ifndef NEXUSFLUX_TAGS_HPP__
#define NEXUSFLUX_TAGS_HPP__

/**
 * @file tags.hpp
 * @brief 工作负载标签
 *
 * 依据采样到的程序名、命令行、镜像与服务描述给出标签；
 * 不读取进程环境，也不启动辅助命令。
 */

#include <NeFlux/model.hpp>
namespace neforce::neflux {

    /**
     * @brief 为快照添加标签
     * @param value 快照
     * @note 会同时标注进程、容器、服务与连接
     */
    void annotate(snapshot& value);

    /**
     * @brief 拼接标签展示文本
     * @param tags 标签
     * @return 以空格分隔的文本
     */
    NEFORCE_NODISCARD string tag_text(const vector<tag>& tags);

    /**
     * @brief 标签是否匹配查询
     * @param tags 标签
     * @param text 查询
     * @return 匹配返回 true
     */
    NEFORCE_NODISCARD bool tag_matches(const vector<tag>& tags, const string& text);

    /**
     * @brief 生成标签说明
     * @param tags 标签
     * @return 说明文本
     */
    NEFORCE_NODISCARD string tag_explain(const vector<tag>& tags);

    /**
     * @brief 组合标签与普通文本的查询匹配
     * @param tags 标签
     * @param text 查询
     * @param plain 普通文本
     * @return 匹配返回 true
     */
    NEFORCE_NODISCARD bool query_matches(const vector<tag>& tags, const string& text, const string& plain);

} // namespace neforce::neflux
#endif // NEXUSFLUX_TAGS_HPP__
