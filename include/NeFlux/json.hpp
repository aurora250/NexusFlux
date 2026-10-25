#ifndef NEXUSFLUX_JSON_HPP__
#define NEXUSFLUX_JSON_HPP__

/**
 * @file json.hpp
 * @brief 轻量 JSON 值模型
 *
 * 为快照、录制回放与配置提供有序、可复制的 JSON 表示。
 * 解析与严格语法校验委托给 NexusForce 的 json_parser。
 */

#include <NeForce/core/container/vector.hpp>
#include <NeForce/core/memory/unique_ptr.hpp>
#include <NeForce/core/string/string.hpp>
#include <NeForce/core/string/string_view.hpp>
#include <NeForce/core/typeinfo/types.hpp>
#include <NeForce/core/utility/optional.hpp>

namespace neforce::neflux {
    // TODO: you may could use neforce::json_value instead
    /**
     * @brief 有序 JSON 值
     *
     * 对象成员保持插入顺序，便于输出快照字段。
     *
     * @note 数值同时保留双精度值与可选的原始文本，用以精确输出 float。
     */
    class json {
    public:
        /**
         * @brief JSON 类型
         */
        enum class type : unsigned char {
            null,    ///< 空值
            boolean, ///< 布尔值
            integer, ///< 64 位有符号整数
            number,  ///< 浮点数
            string,  ///< 字符串
            array,   ///< 数组
            object,  ///< 对象
        };

        /// @brief 数组容器类型
        using array_type = vector<json>;
        /// @brief 对象容器类型
        using object_type = vector<pair<string, json>>;

        /**
         * @brief 复制构造
         * @param other 来源
         */
        json(const json& other);

        /**
         * @brief 移动构造
         * @param other 来源
         */
        json(json&& other) noexcept;

        /**
         * @brief 复制赋值
         * @param other 来源
         * @return 自身引用
         */
        json& operator=(const json& other);

        /**
         * @brief 移动赋值
         * @param other 来源
         * @return 自身引用
         */
        json& operator=(json&& other) noexcept;

        /**
         * @brief 析构
         */
        ~json();

        /**
         * @brief 构造空值
         */
        json();

        /**
         * @brief 构造布尔值
         * @param value 布尔值
         */
        json(bool value);

        /**
         * @brief 构造空值
         * @param value nullptr
         */
        json(nullptr_t value);

        /**
         * @brief 构造整数
         * @param value 整数
         */
        template <typename T, typename = enable_if_t<is_integral_v<T> && !is_same_v<T, bool>>>
        json(T value) :
        type_(type::integer),
        integer_(static_cast<int64_t>(value)) {}

        /**
         * @brief 构造浮点数
         * @param value 浮点数
         */
        json(double value);

        /**
         * @brief 构造浮点数并保留 float 精度文本
         * @param value 单精度浮点数
         */
        json(float value);

        /**
         * @brief 构造字符串
         * @param value 字符串
         */
        json(const char* value);

        /**
         * @brief 构造字符串
         * @param value 字符串
         */
        json(const string& value);

        /**
         * @brief 构造字符串
         * @param value 字符串视图
         */
        json(string_view value);

        /**
         * @brief 构造空数组
         * @return 空数组
         */
        static json array();

        /**
         * @brief 构造空对象
         * @return 空对象
         */
        static json object();

        /**
         * @brief 构造空值
         * @return 空值
         */
        static json null();

        /**
         * @brief 获取类型
         * @return 类型
         */
        NEFORCE_NODISCARD type kind() const noexcept { return type_; }

        /// @brief 是否为空值
        NEFORCE_NODISCARD bool is_null() const noexcept { return type_ == type::null; }
        /// @brief 是否为布尔值
        NEFORCE_NODISCARD bool is_bool() const noexcept { return type_ == type::boolean; }
        /// @brief 是否为整数
        NEFORCE_NODISCARD bool is_integer() const noexcept { return type_ == type::integer; }
        /// @brief 是否为数值
        NEFORCE_NODISCARD bool is_number() const noexcept { return type_ == type::number; }
        /// @brief 是否为字符串
        NEFORCE_NODISCARD bool is_string() const noexcept { return type_ == type::string; }
        /// @brief 是否为数组
        NEFORCE_NODISCARD bool is_array() const noexcept { return type_ == type::array; }
        /// @brief 是否为对象
        NEFORCE_NODISCARD bool is_object() const noexcept { return type_ == type::object; }

        /**
         * @brief 读取布尔值
         * @return 布尔值，类型不符时为 false
         */
        NEFORCE_NODISCARD bool as_bool() const noexcept;

        /**
         * @brief 读取整数
         * @return 整数，类型不符时为 0
         */
        NEFORCE_NODISCARD int64_t as_integer() const noexcept;

        /**
         * @brief 读取数值
         * @return 双精度值，类型不符时为 0
         */
        NEFORCE_NODISCARD double as_number() const noexcept;

        /**
         * @brief 读取字符串
         * @return 字符串引用，类型不符时为空字符串
         */
        NEFORCE_NODISCARD const string& as_string() const noexcept;

        /**
         * @brief 读取数组
         * @return 数组引用，类型不符时为空数组
         */
        NEFORCE_NODISCARD const array_type& as_array() const noexcept;

        /**
         * @brief 读取对象
         * @return 对象引用，类型不符时为空对象
         */
        NEFORCE_NODISCARD const object_type& as_object() const noexcept;

        /**
         * @brief 读取可修改数组
         * @return 数组引用
         */
        NEFORCE_NODISCARD array_type& elements() noexcept;

        /**
         * @brief 读取可修改对象
         * @return 对象引用
         */
        NEFORCE_NODISCARD object_type& members() noexcept;

        /**
         * @brief 追加数组元素
         * @param value 元素
         * @return 自身引用
         */
        json& push(json value);

        /**
         * @brief 设置对象成员
         * @param key 键
         * @param value 值
         * @return 自身引用
         */
        json& set(const string& key, json value);

        /**
         * @brief 获取对象成员
         * @param key 键
         * @return 成员指针，不存在时为 nullptr
         */
        NEFORCE_NODISCARD const json* get(const string& key) const noexcept;

        /**
         * @brief 获取或创建对象成员
         * @param key 键
         * @return 成员引用
         */
        json& operator[](const string& key);

        /**
         * @brief 是否包含成员
         * @param key 键
         * @return 包含返回 true
         */
        NEFORCE_NODISCARD bool contains(const string& key) const noexcept;

        /**
         * @brief 获取数组或对象元素数量
         * @return 元素数量
         */
        NEFORCE_NODISCARD size_t size() const noexcept;

        /**
         * @brief 是否为空数组或空对象
         * @return 为空返回 true
         */
        NEFORCE_NODISCARD bool empty() const noexcept { return size() == 0; }

        /**
         * @brief 输出 JSON 文本
         * @param pretty 是否缩进
         * @return JSON 文本
         */
        NEFORCE_NODISCARD string dump(bool pretty = false) const;

        /**
         * @brief 解析 JSON 文本
         * @param text JSON 文本
         * @return JSON 值
         * @throws json_exception 语法错误时抛出
         */
        NEFORCE_NODISCARD static json parse(string_view text);

        /**
         * @brief 读取字符串成员
         * @param key 键
         * @param fallback 缺省值
         * @return 字符串
         */
        NEFORCE_NODISCARD string string_or(const string& key, const string& fallback = {}) const;

        /**
         * @brief 读取整数成员
         * @param key 键
         * @param fallback 缺省值
         * @return 整数
         */
        NEFORCE_NODISCARD int64_t integer_or(const string& key, int64_t fallback = 0) const;

        /**
         * @brief 读取数值成员
         * @param key 键
         * @param fallback 缺省值
         * @return 双精度值
         */
        NEFORCE_NODISCARD double number_or(const string& key, double fallback = 0.0) const;

        /**
         * @brief 读取布尔成员
         * @param key 键
         * @param fallback 缺省值
         * @return 布尔值
         */
        NEFORCE_NODISCARD bool bool_or(const string& key, bool fallback = false) const;

        /**
         * @brief 读取可选字符串成员
         * @param key 键
         * @return 字符串，成员缺失或为 null 时为空
         */
        NEFORCE_NODISCARD optional<string> optional_string(const string& key) const;

        /**
         * @brief 读取可选数值成员
         * @param key 键
         * @return 数值，成员缺失或为 null 时为空
         */
        NEFORCE_NODISCARD optional<double> optional_number(const string& key) const;

    private:
        type type_ = type::null;
        bool boolean_ = false;
        int64_t integer_ = 0;
        double number_ = 0.0;
        string number_text_;
        string string_;
        unique_ptr<array_type> array_;
        unique_ptr<object_type> object_;

        void dump_to(string& out, bool pretty, int indent) const;
        static void escape_string(string& out, const string& value);
        static void append_number(string& out, double value);
        static void append_float(string& out, float value);
    };

    /**
     * @brief 将值转换为 JSON
     * @tparam T 值类型
     * @param value 值
     * @return JSON 值
     */
    template <typename T>
    json to_json_value(const T& value);

    template <typename T>
    json to_json_value(const optional<T>& value) {
        return value.has_value() ? to_json_value(*value) : json();
    }

    template <typename T>
    json to_json_value(const vector<T>& value) {
        json out = json::array();
        for (const auto& item: value) {
            out.push(to_json_value(item));
        }
        return out;
    }

    template <typename T>
    json to_json_value(const T& value) {
        return json(value);
    }

    inline json to_json_value(const json& value) { return value; }

    inline void from_json_value(const json& value, json& out) { out = value; }

    /**
     * @brief 从 JSON 读取值
     * @tparam T 值类型
     * @param value JSON 值
     * @param out 输出值
     */
    template <typename T>
    void from_json_value(const json& value, T& out);

    template <typename T>
    void from_json_value(const json& value, optional<T>& out) {
        if (value.is_null()) {
            out.reset();
            return;
        }
        T parsed{};
        from_json_value(value, parsed);
        out = move(parsed);
    }

    template <typename T>
    void from_json_value(const json& value, vector<T>& out) {
        out.clear();
        for (const auto& item: value.as_array()) {
            T parsed{};
            from_json_value(item, parsed);
            out.push_back(move(parsed));
        }
    }

    template <typename T>
    void from_json_value(const json& value, T& out) {
        if constexpr (is_same_v<T, string>) {
            out = value.as_string();
        } else if constexpr (is_same_v<T, bool>) {
            out = value.as_bool();
        } else if constexpr (is_integral_v<T>) {
            out = static_cast<T>(value.as_integer());
        } else if constexpr (is_floating_point_v<T>) {
            out = static_cast<T>(value.as_number());
        } else {
            out = T{};
        }
    }

    /**
     * @brief 读取对象成员到字段
     * @tparam T 字段类型
     * @param object 对象
     * @param key 键
     * @param field 字段
     */
    template <typename T>
    void read_field(const json& object, const string& key, T& field) {
        const json* value = object.get(key);
        if (value != nullptr) {
            from_json_value(*value, field);
        }
    }

} // namespace neforce::neflux

#endif // NEXUSFLUX_JSON_HPP__
