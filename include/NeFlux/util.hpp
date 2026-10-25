#ifndef NEXUSFLUX_UTIL_HPP__
#define NEXUSFLUX_UTIL_HPP__

/**
 * @file util.hpp
 * @brief 通用工具
 *
 * 提供时间、字符串、数字解析和文件读取等跨模块辅助函数。
 */

#include <NeForce/core/container/vector.hpp>
#include <NeForce/core/file/file.hpp>
#include <NeForce/core/string/string.hpp>
#include <NeForce/core/string/string_view.hpp>
#include <NeForce/core/algorithm/compare.hpp>
#include <NeForce/core/utility/optional.hpp>
namespace neforce::neflux {

    /**
     * @brief 获取当前 UNIX 时间戳
     * @return 秒
     */
    NEFORCE_NODISCARD uint64_t unix_time() noexcept;

    /**
     * @brief 获取单调时钟毫秒值
     * @return 毫秒
     */
    NEFORCE_NODISCARD int64_t monotonic_ms() noexcept;

    /**
     * @brief 睡眠指定毫秒
     * @param ms 毫秒
     */
    void sleep_ms(uint64_t ms);

    /**
     * @brief 读取整个文本文件
     * @param file_path 路径
     * @return 文件内容，读取失败时为空
     */
    NEFORCE_NODISCARD optional<string> read_text_file(const string& file_path);

    /**
     * @brief 写入整个文本文件
     * @param file_path 路径
     * @param content 内容
     * @return 成功返回 true
     */
    bool write_text_file(const string& file_path, const string& content);

    /**
     * @brief 追加文本文件
     * @param file_path 路径
     * @param content 内容
     * @return 成功返回 true
     */
    bool append_text_file(const string& file_path, const string& content);

   // TODO: most of these functions down there have neforce API as its helper function or member function

    /**
     * @brief 去除首尾空白
     * @param text 文本
     * @return 去空白后的视图
     */
    NEFORCE_NODISCARD string_view trim_view(string_view text) noexcept;

    /**
     * @brief 去除首尾空白
     * @param text 文本
     * @return 去空白后的字符串
     */
    NEFORCE_NODISCARD string trim(string_view text);

    /**
     * @brief 去除首尾空白
     * @param text 文本
     * @return 去空白后的字符串
     */
    NEFORCE_NODISCARD string trim(const string& text);

    /**
     * @brief 去除首尾空白
     * @param text 文本
     * @return 去空白后的字符串
     */
    NEFORCE_NODISCARD string trim(const char* text);

    /**
     * @brief 转换为小写
     * @param text 文本
     * @return 小写字符串
     */
    NEFORCE_NODISCARD string lower_ascii(const string& text);

    /**
     * @brief 转换为小写
     * @param text 文本
     * @return 小写字符串
     */
    NEFORCE_NODISCARD string lower_ascii(string_view text);

    /**
     * @brief 判断是否包含子串（不区分大小写）
     * @param haystack 文本
     * @param needle 子串
     * @return 包含返回 true
     */
    NEFORCE_NODISCARD bool contains_ignore_case(string_view haystack, string_view needle);

    /**
 * @brief 判断是否包含子串（不区分大小写）
 * @param haystack 文本
 * @param needle 子串
 * @return 包含返回 true
 */
    NEFORCE_NODISCARD bool contains_ignore_case(const string& haystack, string_view needle);

    /**
 * @brief 解析无符号整数
 * @param text 文本
 * @return 数值，无效时为空
 */
    NEFORCE_NODISCARD optional<uint64_t> parse_u64(string_view text) noexcept;

    /**
 * @brief 解析有符号整数
 * @param text 文本
 * @return 数值，无效时为空
 */
    NEFORCE_NODISCARD optional<int64_t> parse_i64(string_view text) noexcept;

    /**
 * @brief 解析浮点数
 * @param text 文本
 * @return 数值，无效时为空
 */
    NEFORCE_NODISCARD optional<double> parse_f64(string_view text) noexcept;

    /**
 * @brief 按空白切分
 * @param text 文本
 * @return 片段列表
 */
    NEFORCE_NODISCARD vector<string> split_whitespace(string_view text);

    /**
 * @brief 按空白切分
 * @param text 文本
 * @return 片段列表
 */
    NEFORCE_NODISCARD vector<string> split_whitespace(const string& text);

    /**
 * @brief 按分隔符切分
 * @param text 文本
 * @param delimiter 分隔符
 * @param keep_empty 是否保留空片段
 * @return 片段列表
 */
    NEFORCE_NODISCARD vector<string> split(string_view text, char delimiter, bool keep_empty = false);

    /**
 * @brief 按分隔符切分
 * @param text 文本
 * @param delimiter 分隔符
 * @param keep_empty 是否保留空片段
 * @return 片段列表
 */
    NEFORCE_NODISCARD vector<string> split(const string& text, char delimiter, bool keep_empty = false);

    /**
 * @brief 按行切分
 * @param text 文本
 * @return 行列表
 */
    NEFORCE_NODISCARD vector<string> split_lines(string_view text);

    /**
 * @brief 按行切分
 * @param text 文本
 * @return 行列表
 */
    NEFORCE_NODISCARD vector<string> split_lines(const string& text);

    /**
 * @brief 判断文本是否以指定前缀开头
 * @param text 文本
 * @param prefix 前缀
 * @return 匹配返回 true
 */
    NEFORCE_NODISCARD bool starts_with(string_view text, string_view prefix) noexcept;

    /**
 * @brief 判断文本是否以指定前缀开头
 * @param text 文本
 * @param prefix 前缀
 * @return 匹配返回 true
 */
    NEFORCE_NODISCARD bool starts_with(const string& text, string_view prefix) noexcept;

    /**
 * @brief 判断文本是否以指定后缀结尾
 * @param text 文本
 * @param suffix 后缀
 * @return 匹配返回 true
 */
    NEFORCE_NODISCARD bool ends_with(string_view text, string_view suffix) noexcept;

    /**
 * @brief 判断文本是否以指定后缀结尾
 * @param text 文本
 * @param suffix 后缀
 * @return 匹配返回 true
 */
    NEFORCE_NODISCARD bool ends_with(const string& text, string_view suffix) noexcept;

    /**
     * @brief 按显示宽度截断字符串
     * @param text 文本
     * @param width 最大显示宽度
     * @return 截断后的文本
     */
    NEFORCE_NODISCARD string truncate_display(string_view text, int width);

    /**
     * @brief 计算字符串显示宽度
     * @param text 文本
     * @return 终端列数
     */
    NEFORCE_NODISCARD int display_width(string_view text) noexcept;

    /**
     * @brief 将文本填充或截断到指定显示宽度
     * @param text 文本
     * @param width 目标宽度
     * @param right_align 是否右对齐
     * @return 填充后的文本
     */
    NEFORCE_NODISCARD string fit_display(string_view text, int width, bool right_align = false);

    /**
     * @brief 转义控制字符
     * @param text 文本
     * @return 可打印文本
     */
    NEFORCE_NODISCARD string sanitize_text(string_view text);

    /**
     * @brief 按下标稳定排序，避免对 NexusForce 容器元素做 swap
     * @tparam T 元素类型
     * @tparam Compare 比较函数类型
     * @param items 待排序容器
     * @param compare 比较函数
     * @note NexusForce 的 `sort` 在逆序输入时会越界；此实现先排序下标再重排元素
     */
    template <typename T, typename Compare>
    void stable_sort_by_index(vector<T>& items, Compare compare) {
        if (items.size() < 2) {
            return;
        }
        vector<size_t> order;
        order.reserve(items.size());
        for (size_t index = 0; index < items.size(); ++index) {
            order.push_back(index);
        }
        vector<size_t> buffer;
        buffer.resize(order.size());
        for (size_t width = 1; width < order.size(); width *= 2) {
            for (size_t begin = 0; begin < order.size(); begin += width * 2) {
                const size_t middle = min(begin + width, order.size());
                const size_t end = min(begin + width * 2, order.size());
                size_t left = begin;
                size_t right = middle;
                size_t out = begin;
                while (left < middle && right < end) {
                    if (compare(items[order[right]], items[order[left]])) {
                        buffer[out++] = order[right++];
                    } else {
                        buffer[out++] = order[left++];
                    }
                }
                while (left < middle) {
                    buffer[out++] = order[left++];
                }
                while (right < end) {
                    buffer[out++] = order[right++];
                }
            }
            for (size_t index = 0; index < order.size(); ++index) {
                order[index] = buffer[index];
            }
        }
        vector<T> sorted;
        sorted.reserve(items.size());
        for (const size_t index: order) {
            sorted.push_back(_NEFORCE move(items[index]));
        }
        items = _NEFORCE move(sorted);
    }

    /**
     * @brief 格式化 JSON/TOML 数值
     * @param value 数值
     * @param precision 有效数字
     * @return 精简文本
     */
    NEFORCE_NODISCARD string format_number(double value, int precision = 15);

    /**
     * @brief 格式化浮点数
     * @param value 数值
     * @param digits 小数位
     * @return 定点文本
     */
    NEFORCE_NODISCARD string format_fixed(double value, int digits);

    /**
     * @brief 获取进程可执行文件基名
     * @param command 命令行
     * @return 基名
     */
    NEFORCE_NODISCARD string basename_of(string_view command);

} // namespace neforce::neflux
#endif // NEXUSFLUX_UTIL_HPP__
