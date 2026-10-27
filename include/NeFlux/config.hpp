#ifndef NEXUSFLUX_CONFIG_HPP__
#define NEXUSFLUX_CONFIG_HPP__

/**
 * @file config.hpp
 * @brief 配置模型
 *
 * 加载、校验与保存。
 */

#include <NeFlux/json.hpp>

#include <NeForce/core/container/vector.hpp>
#include <NeForce/core/string/string.hpp>
#include <NeForce/core/utility/optional.hpp>

namespace neforce::neflux {

    /**
     * @brief 显示选项
     */
    struct display_config {
        string cpu_graph_ = "total";           ///< 上方 CPU 图表指标
        string cpu_graph_lower_ = "total";     ///< 下方 CPU 图表指标
        string temperature_unit_ = "celsius";  ///< 温度单位
        string cpu_sensor_ = {};               ///< CPU 温度传感器
        string network_interface_ = {};        ///< 网络接口过滤
        bool network_bits_ = false;            ///< 使用 bit/s
        bool network_binary_ = true;           ///< 使用二进制单位
        bool network_logarithmic_ = false;     ///< 网络图表对数刻度
        bool network_start_zero_ = false;      ///< 网络累计从零开始
        bool network_sync_scale_ = false;      ///< 上下行共享刻度
        uint64_t network_scale_ = 0;           ///< 固定网络图表上限
        bool network_swap_ = false;            ///< 上传显示在上方
        bool memory_graphs_ = true;            ///< 启用内存图表
        bool show_swap_ = true;                ///< 显示交换区
        bool show_battery_ = true;             ///< 显示电池
        bool process_basename_ = false;        ///< 进程仅显示程序名
        bool group_processes_ = false;         ///< 按程序聚合进程
        string overview_panel_ = "all";        ///< 概览展开面板
        bool transparent_background_ = true;   ///< 终端背景透明
        string graph_symbol_ = "braille";      ///< 图表符号：braille、block 或 ascii
        bool cpu_lazy_sort_ = true;            ///< CPU 排序使用平滑值
        bool rounded_corners_ = true;          ///< 圆角边框
        bool process_colors_ = true;           ///< 进程行按 CPU 着色
        bool process_gradient_ = true;         ///< 进程 CPU 颜色渐变
        bool graph_gradient_ = true;           ///< 图表垂直渐变
        bool gpu_in_cpu_box_ = false;          ///< 在 CPU 面板内显示 GPU 摘要
        bool multi_gpu_overview_ = true;       ///< 总览 GPU 卡片显示多个设备
        string graph_symbol_cpu_ = "default";  ///< CPU 图表符号
        string graph_symbol_mem_ = "default";  ///< 内存图表符号
        string graph_symbol_net_ = "default";  ///< 网络图表符号
        string graph_symbol_proc_ = "default"; ///< 进程图表符号
        string graph_symbol_gpu_ = "default";  ///< GPU 图表符号

        /**
         * @brief 获取温度显示值
         * @param celsius 摄氏度
         * @return 目标单位数值
         */
        NEFORCE_NODISCARD double temperature_value(double celsius) const noexcept;

        /**
         * @brief 获取温度单位符号
         * @return 单位符号
         */
        NEFORCE_NODISCARD string temperature_symbol() const;

        /**
         * @brief 格式化温度
         * @param celsius 摄氏度
         * @param digits 小数位
         * @return 带单位文本
         */
        NEFORCE_NODISCARD string temperature(double celsius, size_t digits) const;

        /**
         * @brief 格式化速率
         * @param bytes_per_second 字节/秒
         * @return 带单位文本
         */
        NEFORCE_NODISCARD string rate(double bytes_per_second) const;

        /**
         * @brief 获取设置项列表
         * @return 名称与值列表
         */
        NEFORCE_NODISCARD vector<pair<string, string>> settings() const;

        /**
         * @brief 调整设置项
         * @param index 设置索引
         * @param forward 是否向前
         * @param networks 可选网络接口
         * @param sensors 可选传感器
         */
        void adjust(size_t index, bool forward, const vector<string>& networks, const vector<string>& sensors);

        /**
         * @brief 校验显示选项
         * @throws value_exception 选项非法时抛出
         */
        void validate() const;
    };

    /**
     * @brief 本地探针配置
     */
    struct probe_config {
        string name_;                  ///< 探针名称
        vector<string> command_;       ///< 命令参数
        uint64_t timeout_seconds_ = 3; ///< 超时秒数
    };

    /**
     * @brief 自定义布局节点
     */
    struct layout_node {
        /**
         * @brief 节点类型
         */
        enum class kind : unsigned char {
            panel, ///< 叶子面板
            split, ///< 分割容器
        };

        kind kind_ = kind::panel;      ///< 节点类型
        string panel_;                 ///< 面板名
        string direction_;             ///< 分割方向
        vector<layout_node> children_; ///< 子节点
        vector<uint16_t> weights_;     ///< 权重

        /**
         * @brief 判断布局是否包含面板
         * @param name 面板名
         * @return 包含返回 true
         */
        NEFORCE_NODISCARD bool contains_panel(const string& name) const;
    };

    /**
     * @brief 应用配置
     */
    struct config {
        static constexpr size_t settings_count = 32; ///< 总设置项数量

        uint64_t interval_ms_ = 1000;             ///< 采样间隔
        bool collect_gpu_ = true;                 ///< 采集 GPU
        string theme_ = "midnight";               ///< 主题
        string theme_file_;                       ///< 外部 .theme 文件路径
        size_t view_ = 0;                         ///< 启动页面
        size_t sort_index_ = 0;                   ///< 排序索引
        bool reverse_sort_ = false;               ///< 反向排序
        bool tree_ = false;                       ///< 进程树
        size_t tree_auto_collapse_ = 0;           ///< 子进程数达到此值自动折叠，0 关闭
        bool tree_aggregate_ = false;             ///< 树模式累计子进程 CPU/内存
        size_t history_ = 180;                    ///< 历史样本数
        uint64_t time_window_seconds_ = 180;      ///< 图表时间窗口
        bool show_loopback_ = false;              ///< 显示环回接口
        float cpu_warning_ = 90.0F;               ///< CPU 告警阈值
        double memory_warning_ = 90.0;            ///< 内存告警阈值
        uint32_t gpu_temperature_warning_ = 85;   ///< GPU 温度告警阈值
        bool show_threads_ = false;               ///< 显示线程
        bool show_kernel_threads_ = true;         ///< 显示内核线程
        bool ascii_ = false;                      ///< ASCII 模式
        bool no_color_ = false;                   ///< 无颜色模式
        string layout_ = "balanced";              ///< 布局
        size_t overview_preset_ = 0;              ///< 当前总览 box preset
        vector<string> overview_presets_ = {      ///< 总览 box 组合 preset
                "cpu,memory,gpu,storage,network,processes",
                "cpu,gpu,processes",
                "cpu,memory,storage,network,processes",
                "cpu,memory,processes"};
        optional<layout_node> custom_layout_;     ///< 自定义布局
        vector<string> columns_;                  ///< 进程列
        json custom_colors_;                      ///< 自定义颜色
        vector<string> monitored_folders_;        ///< 监控目录
        bool process_network_ = false;            ///< 进程网络
        uint64_t alert_hold_seconds_ = 3;         ///< 告警保持时间
        size_t alert_history_ = 200;              ///< 告警历史容量
        vector<vector<string>> alert_commands_;   ///< 告警命令
        size_t log_lines_ = 500;                  ///< 日志行数
        uint64_t record_max_mib_ = 64;            ///< 录制分卷大小
        vector<probe_config> probes_;             ///< 本地探针
        display_config display_;                  ///< 显示选项
        bool check_updates_ = true;               ///< 检查更新
        optional<string> ignored_update_version_; ///< 忽略的版本
        bool config_exec_ = true;                 ///< 是否允许执行配置命令

        /**
         * @brief 获取主题名称列表
         * @return 主题列表
         */
        NEFORCE_NODISCARD static vector<string> themes();

        /**
         * @brief 切换主题
         * @param forward 是否向前
         */
        void cycle_theme(bool forward);

        /**
         * @brief 获取默认配置文件路径
         * @return 路径
         */
        NEFORCE_NODISCARD static string path();

        /**
         * @brief 加载配置
         * @param config_path 路径
         * @return 配置
         * @throws value_exception 配置非法或语法错误时抛出
         */
        NEFORCE_NODISCARD static config load(const string& config_path);

        /**
         * @brief 校验配置
         * @throws value_exception 配置非法时抛出
         */
        void validate() const;

        /**
         * @brief 保存配置
         * @param config_path 路径
         * @throws file_exception 写入失败时抛出
         */
        void save(const string& config_path) const;

        /**
         * @brief 输出 TOML 文本
         * @return TOML 文本
         */
        NEFORCE_NODISCARD string to_toml() const;

        /**
         * @brief 解析 TOML 文本
         * @param text TOML 文本
         * @return 配置
         * @throws value_exception 语法或字段错误时抛出
         */
        NEFORCE_NODISCARD static config from_toml(const string& text);
    };

} // namespace neforce::neflux
#endif // NEXUSFLUX_CONFIG_HPP__
