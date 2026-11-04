#ifndef NEXUSFLUX_PROTOCOL_HPP__
#define NEXUSFLUX_PROTOCOL_HPP__

/**
 * @file protocol.hpp
 * @brief 服务端与 agent 的 NDJSON 协议
 */

#include <NeFlux/json.hpp>
#include <NeForce/core/string/string.hpp>
#include <NeForce/core/utility/optional.hpp>

namespace neforce::neflux {

    /**
     * @brief 协议消息
     */
    struct protocol_message {
        int version_ = 1; ///< 协议版本
        string type_;     ///< 消息类型
        uint64_t id_ = 0; ///< 请求/响应 ID
        json payload_;    ///< 负载
    };

    /**
     * @brief 编码一行协议消息
     * @param message 消息
     * @return JSON 行（不含换行）
     */
    NEFORCE_NODISCARD string encode_message(const protocol_message& message);

    /**
     * @brief 解析一行协议消息
     * @param line 文本
     * @param error 失败原因
     * @return 消息
     */
    NEFORCE_NODISCARD optional<protocol_message> decode_message(string_view line, string& error);

    /**
     * @brief 构造错误负载
     * @param code 错误码
     * @param message 错误文本
     * @param fatal 是否致命
     * @return 负载
     */
    NEFORCE_NODISCARD json make_error_payload(string_view code, string_view message, bool fatal = false);

} // namespace neforce::neflux
#endif // NEXUSFLUX_PROTOCOL_HPP__
