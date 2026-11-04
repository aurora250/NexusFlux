#ifndef NEXUSFLUX_SOURCE_HPP__
#define NEXUSFLUX_SOURCE_HPP__

/**
 * @file source.hpp
 * @brief 采集数据源抽象
 *
 * 本地采集与远端 agent 共用同一接口；UI/CLI 只依赖 telemetry_source。
 */

#include <NeFlux/config.hpp>
#include <NeFlux/machine.hpp>
#include <NeFlux/model.hpp>
#include <NeForce/core/memory/unique_ptr.hpp>
#include <NeForce/core/utility/optional.hpp>

namespace neforce::neflux {

    /**
     * @brief 平台类型
     */
    enum class platform_kind : unsigned char {
        windows, ///< Windows
        linux,   ///< Linux
        macos,   ///< macOS
        freebsd, ///< FreeBSD
        unknown, ///< 未知
    };

    /**
     * @brief 进程详细程度
     */
    enum class process_detail : unsigned char {
        summary, ///< 仅名称
        normal,  ///< 截断的命令行
        full,    ///< 完整命令行
    };

    /**
     * @brief 采集策略
     */
    struct collect_policy {
        uint64_t interval_ms_ = 1000;                    ///< 采样间隔
        bool processes_ = true;                          ///< 进程
        bool threads_ = false;                           ///< 线程
        bool process_network_ = false;                   ///< 进程网络
        bool gpu_ = true;                                ///< GPU
        bool connections_ = true;                        ///< 连接
        bool containers_ = true;                         ///< 容器
        bool services_ = true;                           ///< 服务
        bool extras_ = true;                             ///< 扩展
        bool full_sampling_ = false;                     ///< 全量采样
        bool config_exec_ = true;                        ///< 是否允许执行配置命令
        vector<string> monitored_folders_;               ///< 监控目录
        vector<probe_config> probes_;                    ///< 本地探针
        process_detail detail_ = process_detail::normal; ///< 进程详细程度
    };

    /**
     * @brief 从配置生成采集策略
     * @param cfg 配置
     * @param full_sampling 是否全量
     * @return 策略
     */
    NEFORCE_NODISCARD collect_policy make_collect_policy(const config& cfg, bool full_sampling = false);

    /**
     * @brief 将采集策略应用到配置副本
     * @param policy 策略
     * @return 配置
     */
    NEFORCE_NODISCARD config config_from_policy(const collect_policy& policy);

    /**
     * @brief 采集端能力
     */
    struct source_capabilities {
        platform_kind platform_ = platform_kind::unknown; ///< 平台
        string os_version_;                               ///< 系统版本
        string arch_;                                     ///< 架构
        bool processes_ = true;                           ///< 进程列表
        bool threads_ = false;                            ///< 线程
        bool connections_ = false;                        ///< 连接表
        bool gpu_ = false;                                ///< GPU
        bool containers_ = false;                         ///< 容器
        bool services_ = false;                           ///< 服务
        bool sensors_ = false;                            ///< 温度/电池
        bool pressure_ = false;                           ///< 压力指标
        bool actions_ = false;                            ///< 动作总开关
        bool signals_ = false;                            ///< 信号
        bool nice_ = false;                               ///< nice
        bool affinity_ = false;                           ///< 亲和性
        bool process_logs_ = false;                       ///< 进程日志
        bool process_files_ = false;                      ///< 进程文件
        bool environment_ = false;                        ///< 环境变量
        bool network_capture_ = false;                    ///< 抓包
        bool workload_actions_ = false;                   ///< 容器/服务动作
        bool vm_actions_ = false;                         ///< VM 动作
    };

    /**
     * @brief 数据源兴趣开关
     */
    struct source_interest {
        bool gpu_processes_ = false; ///< GPU 进程
        bool connections_ = false;   ///< 连接
        bool containers_ = false;    ///< 容器
        bool services_ = false;      ///< 服务
        bool extras_ = false;        ///< 扩展信息
    };

    /**
     * @brief 远端动作请求
     */
    struct source_action {
        string kind_;             ///< 动作类型
        uint32_t pid_ = 0;        ///< 进程 ID
        uint64_t start_time_ = 0; ///< 进程启动时间
        string value_;            ///< 动作参数
    };

    /**
     * @brief 采集数据源
     */
    class telemetry_source {
    public:
        virtual ~telemetry_source() = default;

        /**
         * @brief 启动数据源
         * @param error 失败原因
         * @return 成功返回 true
         */
        virtual bool start(string& error) = 0;

        /// @brief 停止数据源
        virtual void stop() noexcept = 0;

        /**
         * @brief 更新兴趣开关
         * @param interest 兴趣
         */
        virtual void set_interest(const source_interest& interest) = 0;

        /// @brief 请求立即采样
        virtual void request_sample() = 0;

        /**
         * @brief 取出一帧快照
         * @param out 快照
         * @return 有数据返回 true
         */
        virtual bool take_snapshot(snapshot& out) = 0;

        /**
         * @brief 取出发现清单
         * @param out 清单
         * @return 有数据返回 true
         */
        virtual bool take_inventory(inventory& out) = 0;

        /**
         * @brief 取出错误
         * @return 错误文本
         */
        virtual optional<string> take_error() = 0;

        /// @brief 是否回放模式
        NEFORCE_NODISCARD virtual bool replay_active() const noexcept = 0;
        /// @brief 当前回放帧
        NEFORCE_NODISCARD virtual size_t replay_index() const noexcept = 0;
        /// @brief 回放总帧数
        NEFORCE_NODISCARD virtual size_t replay_size() const noexcept = 0;
        /// @brief 跳转回放
        virtual void seek(int delta) = 0;

        /// @brief 数据源标签
        NEFORCE_NODISCARD virtual string label() const = 0;
        /// @brief 能力
        NEFORCE_NODISCARD virtual const source_capabilities& capabilities() const noexcept = 0;

        /**
         * @brief 执行动作
         * @param action 动作
         * @param error 失败原因
         * @return 成功返回 true
         */
        virtual bool invoke_action(const source_action& action, string& error) = 0;
    };

    /**
     * @brief 本地数据源选项
     */
    struct source_options {
        config cfg_;                 ///< 配置
        optional<string> replay_;    ///< 回放文件
        bool read_only_ = false;     ///< 只读
        bool full_sampling_ = false; ///< 全量采样
        bool allow_actions_ = false; ///< 允许远端动作
    };

    /**
     * @brief 创建本地数据源
     * @param options 选项
     * @return 数据源
     */
    NEFORCE_NODISCARD unique_ptr<telemetry_source> make_local_source(source_options options);

    /**
     * @brief 创建远端数据源
     * @param target 目标机器
     * @param options 选项
     * @return 数据源
     */
    NEFORCE_NODISCARD unique_ptr<telemetry_source> make_remote_source(machine_target target, source_options options);

} // namespace neforce::neflux
#endif // NEXUSFLUX_SOURCE_HPP__
