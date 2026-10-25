#ifndef NEFLUX_ROS_HPP__
#define NEFLUX_ROS_HPP__

/**
 * @file ros.hpp
 * @brief 显式 ROS 1/2 图查询
 *
 * 仅在用户请求时，使用选中进程的通信环境运行本机 ROS 命令行工具。
 */

#include <NeFlux/model.hpp>

#include <NeForce/core/container/map.hpp>
#include <NeForce/core/memory/unique_ptr.hpp>

namespace neforce::neflux {

    /**
 * @brief ROS 会话
 */
    struct ros_session {
        uint32_t pid_ = 0;                ///< 进程标识
        uint64_t start_time_ = 0;         ///< 进程启动时间
        int version_ = 0;                 ///< ROS 版本（1 或 2）
        map<string, string> environment_; ///< 通信相关环境变量
        string cwd_;                      ///< 工作目录
    };

    /**
 * @brief 打开进程的 ROS 会话
 * @param value 进程
 * @param error 失败原因
 * @return 会话，失败时为空
 * @note 仅允许同一用户、同一 net/mnt namespace 的进程
 */
    NEFORCE_NODISCARD optional<ros_session> open_ros_session(const process& value, string& error);

    /**
 * @brief ROS 查询类型
 */
    enum class ros_query : unsigned char {
        topics,       ///< 话题列表
        nodes,        ///< 节点列表
        services,     ///< 服务列表
        topic_info,   ///< 话题详情
        topic_echo,   ///< 单条消息
        topic_hz,     ///< 话题频率
        node_info,    ///< 节点详情
        service_info, ///< 服务详情
        parameters,   ///< 参数列表
        context,      ///< 通信上下文
        environment,  ///< 环境变量
    };

    /**
 * @brief 执行 ROS 查询
 * @param session 会话
 * @param type 查询类型
 * @param argument 可选参数（话题/节点/服务名）
 * @return 输出文本
 * @throws process_exception 命令启动失败时抛出
 */
    NEFORCE_NODISCARD string run_ros_query(const ros_session& session, ros_query type, const string& argument = {});

    /**
 * @brief 生成 ROS 会话摘要
 * @param session 会话
 * @return 摘要文本
 */
    NEFORCE_NODISCARD string ros_summary(const ros_session& session);

} // namespace neforce::neflux

#endif // NEFLUX_ROS_HPP__
