#ifndef NEXUSFLUX_CLI_HPP__
#define NEXUSFLUX_CLI_HPP__

/**
 * @file cli.hpp
 * @brief 命令行参数
 *
 * 解析全局选项与子命令。
 */

#include <NeForce/core/string/string.hpp>
#include <NeForce/core/utility/optional.hpp>
namespace neforce::neflux {

    /**
     * @brief 子命令类型
     */
    enum class cli_command : unsigned char {
        monitor,             ///< 交互式监控
        snapshot,            ///< 输出单次 JSON 快照
        sample,              ///< 输出多行 JSON 采样
        record,              ///< 录制采样
        replay,              ///< 回放录制
        export_csv,          ///< 导出 CSV
        export_svg,          ///< 导出 SVG
        gpu_select,          ///< 选择 CUDA 设备
        network_connections, ///< 列出连接
        network_check,       ///< 检查目标
        network_capture,     ///< 抓包
        network_read,        ///< 读取 PCAP
        doctor,              ///< 资源诊断
        config_path,         ///< 输出配置路径
        config_write,        ///< 写入配置
        completions,         ///< 输出补全脚本
        man,                 ///< 输出手册
        update,              ///< 更新
    };

    /**
     * @brief 命令行动作
     */
    struct cli_args {
        cli_command command_ = cli_command::monitor; ///< 子命令
        string file_;                                ///< 子命令文件参数
        string target_;                              ///< 网络检查目标
        string format_;                              ///< 导出格式
        string shell_;                               ///< 补全 shell
        vector<string> extra_;                       ///< 额外参数

        optional<uint64_t> interval_;                           ///< 采样间隔
        optional<string> theme_;                                ///< 主题
        optional<string> view_;                                 ///< 初始页面
        string gpu_panel_ = "summary";                          ///< GPU 页初始面板
        optional<string> config_path_;                          ///< 配置文件
        bool no_gpu_ = false;                                   ///< 禁用 GPU
        bool snapshot_ = false;                                 ///< 输出快照
        optional<uint32_t> samples_;                            ///< 采样次数
        optional<string> export_svg_;                           ///< SVG 输出
        uint16_t width_ = 160;                                  ///< 导出宽度
        uint16_t height_ = 48;                                  ///< 导出高度
        bool write_config_ = false;                             ///< 写入配置
        optional<string> record_;                               ///< 录制文件
        optional<string> csv_;                                  ///< CSV 输出
        optional<string> replay_;                               ///< 回放文件
        bool ascii_ = false;                                    ///< ASCII 模式
        bool no_color_ = false;                                 ///< 无颜色
        bool threads_ = false;                                  ///< 显示线程
        bool process_network_ = false;                          ///< 进程网络
        bool read_only_ = false;                                ///< 只读
        bool full_sampling_ = false;                            ///< 全量采样
        optional<string> layout_;                               ///< 布局
        optional<uint32_t> select_gpu_;                         ///< 选择 GPU 数量
        uint64_t min_free_mib_ = 0;                             ///< 最小空闲显存
        uint64_t min_total_mib_ = 0;                            ///< 最小总显存
        optional<uint32_t> max_gpu_utilization_;                ///< 最大 GPU 利用率
        optional<uint32_t> max_memory_utilization_;             ///< 最大显存利用率
        bool no_config_exec_ = false;                           ///< 禁用配置命令
        bool json_ = false;                                     ///< JSON 输出
        bool reverse_ = false;                                  ///< 反向排序
        string sort_ = "traffic";                               ///< 连接排序
        string filter_;                                         ///< 连接过滤
        string interface_ = "any";                              ///< 抓包接口
        string capture_filter_ = "tcp or udp or icmp or icmp6"; ///< 抓包过滤
        uint64_t seconds_ = 10;                                 ///< 抓包时长
        uint32_t packets_ = 2000;                               ///< 抓包数量
        bool update_check_ = false;                             ///< 仅检查更新
    };

    /**
     * @brief 解析命令行
     * @param argc 参数数量
     * @param argv 参数数组
     * @return 命令行参数
     * @throws value_exception 参数非法时抛出
     * @note `--help` 与 `--version` 由 `help_requested()` / `version_requested()` 标记，
     *       调用方可先检查再执行
     */
    NEFORCE_NODISCARD cli_args parse_cli(int argc, char** argv);

    /**
     * @brief 是否请求帮助
     * @return 请求返回 true
     */
    NEFORCE_NODISCARD bool help_requested() noexcept;

    /**
     * @brief 是否请求版本
     * @return 请求返回 true
     */
    NEFORCE_NODISCARD bool version_requested() noexcept;

    /**
     * @brief 获取帮助文本
     * @return 帮助文本
     */
    NEFORCE_NODISCARD string help_text();

    /**
     * @brief 获取版本文本
     * @return 版本文本
     */
    NEFORCE_NODISCARD string version_text();

} // namespace neforce::neflux
#endif // NEXUSFLUX_CLI_HPP__
