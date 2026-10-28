#ifndef NEXUSFLUX_DISCOVERY_HPP__
#define NEXUSFLUX_DISCOVERY_HPP__

/**
 * @file discovery.hpp
 * @brief 本地运行时、容器、服务与监听端口发现
 */

#include <NeForce/core/container/map.hpp>
#include <NeFlux/config.hpp>
#include <NeFlux/model.hpp>


namespace neforce::neflux {

    /**
     * @brief 容器计数器速率计算器
     */
    class inventory_rates {
    public:
        /**
         * @brief 计算并填充速率
         * @param data 发现清单
         */
        void apply(inventory& data);

    private:
        struct entry {
            double seconds = 0.0;
            uint64_t counters[4] = {};
            bool valid = false;
        };
        map<string, entry> previous_;
        double began_ = -1.0;
    };

    /**
     * @brief 运行外部命令
     * @param program 程序
     * @param args 参数
     * @param timeout_ms 超时毫秒
     * @param include_stderr 是否附带标准错误
     * @return 标准输出
     * @throws process_exception 启动失败或超时时抛出
     */
    string run_command(const string& program, const vector<string>& args, int timeout_ms, bool include_stderr = false);

    /**
     * @brief 执行本地发现
     * @param cfg 配置
     * @return 发现清单
     */
    NEFORCE_NODISCARD inventory discover_with_config(const config& cfg);

    /**
     * @brief 发现来源开关
     */
    struct discovery_sources {
        bool containers_ = true; ///< 是否查询容器运行时
        bool services_ = true;   ///< 是否查询服务管理器
        bool extras_ = true;     ///< 是否查询监听、硬件与本地探针
    };

    /**
     * @brief 按来源开关执行发现
     * @param cfg 配置
     * @param sources 来源开关
     * @return 发现清单
     * @throws exception 解析不可恢复错误时抛出
     */
    NEFORCE_NODISCARD inventory discover_with_sources(const config& cfg, const discovery_sources& sources);

    /**
     * @brief 执行可裁剪的本地发现
     * @param cfg 配置
     * @param containers 是否发现容器
     * @param services 是否发现服务
     * @param extras 是否发现扩展信息
     * @return 发现清单
     */
    NEFORCE_NODISCARD inventory discover_selected(const config& cfg, bool containers, bool services, bool extras);

    /**
     * @brief 解析容器列表
     * @param runtime 运行时
     * @param output NDJSON 输出
     * @return 容器列表
     */
    NEFORCE_NODISCARD vector<container> parse_containers(const string& runtime, const string& output);

    /**
     * @brief 应用容器统计
     * @param containers 容器列表
     * @param output NDJSON 输出
     */
    void apply_container_stats(vector<container>& containers, const string& output);

    /**
     * @brief 解析带单位的大小
     * @param text 文本
     * @return 字节数
     */
    NEFORCE_NODISCARD optional<uint64_t> parse_size(string_view text);

    /**
     * @brief 解析 /proc/net 监听项
     * @param text 文件内容
     * @param protocol 协议名
     * @return 监听列表
     */
    NEFORCE_NODISCARD vector<listener> parse_listeners(const string& text, const string& protocol);

    /**
     * @brief 发现监听端口
     * @return 监听列表
     */
    NEFORCE_NODISCARD vector<listener> discover_listeners();

} // namespace neforce::neflux
#endif // NEXUSFLUX_DISCOVERY_HPP__
