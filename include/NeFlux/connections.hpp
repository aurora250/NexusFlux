#ifndef NEXUSFLUX_CONNECTIONS_HPP__
#define NEXUSFLUX_CONNECTIONS_HPP__

/**
 * @file connections.hpp
 * @brief 本机网络连接采样
 */

#include <NeFlux/model.hpp>
#include <NeForce/core/memory/unique_ptr.hpp>
namespace neforce::neflux {

    /**
     * @brief 解码 /proc/net 地址
     * @param address `HEXADDR:HEXPORT`
     * @return `IP:PORT` 文本
     */
    NEFORCE_NODISCARD string decode_address(const string& address);

    /**
     * @brief 解析 /proc/net 连接表
     * @param protocol 协议名
     * @param text 文件内容
     * @return 连接列表
     */
    NEFORCE_NODISCARD vector<connection> parse_proc_connections(const string& protocol, const string& text);

    /**
     * @brief 应用 `ss -i -e` 的 TCP 信息
     * @param rows 连接列表
     * @param text 输出
     */
    void apply_tcp_info(vector<connection>& rows, const string& text);

    /**
     * @brief 连接状态说明
     * @param state 状态
     * @return 说明文本
     */
    NEFORCE_NODISCARD string state_help(const string& state);

    /**
     * @brief 连接采样器
     */
    class connection_collector {
    public:
        connection_collector();
        ~connection_collector();

        /**
         * @brief 采集一次连接
         * @param sockets 是否采集套接字
         * @return 采样批次
         */
        NEFORCE_NODISCARD connection_sample collect(bool sockets = true);

        /**
         * @brief 获取最近一次 TCP 采样状态
         * @return 状态
         */
        NEFORCE_NODISCARD const string& status() const noexcept;

    private:
        struct impl;
        unique_ptr<impl> impl_;
    };

} // namespace neforce::neflux
#endif // NEXUSFLUX_CONNECTIONS_HPP__
