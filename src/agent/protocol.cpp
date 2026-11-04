#include <NeFlux/protocol.hpp>
#include <NeForce/core/string/format.hpp>

namespace neforce::neflux {

    string encode_message(const protocol_message& message) {
        json root = json::object();
        root.set("v", json(static_cast<int64_t>(message.version_)));
        root.set("type", json(message.type_));
        if (message.id_ != 0) {
            root.set("id", json(static_cast<int64_t>(message.id_)));
        }
        root.set("payload", message.payload_);
        return root.dump();
    }

    optional<protocol_message> decode_message(const string_view line, string& error) {
        if (line.empty() || line.size() > 32ULL * 1024ULL * 1024ULL) {
            error = "invalid protocol line length";
            return none;
        }
        try {
            const json root = json::parse(line);
            if (!root.is_object()) {
                error = "protocol message must be an object";
                return none;
            }
            const json* version = root.get("v");
            const json* type = root.get("type");
            if (version == nullptr || !version->is_integer() || type == nullptr || !type->is_string()) {
                error = "protocol message lacks v/type";
                return none;
            }
            protocol_message message;
            message.version_ = static_cast<int>(version->as_integer());
            message.type_ = type->as_string();
            if (const json* id = root.get("id"); id != nullptr && id->is_integer()) {
                message.id_ = static_cast<uint64_t>(id->as_integer());
            }
            if (const json* payload = root.get("payload"); payload != nullptr) {
                message.payload_ = *payload;
            } else {
                message.payload_ = json::object();
            }
            if (message.version_ != 1) {
                error = format("unsupported protocol version {}", static_cast<int64_t>(message.version_));
                return none;
            }
            return message;
        } catch (const exception& failure) {
            error = failure.what();
            return none;
        }
    }

    json make_error_payload(const string_view code, const string_view message, const bool fatal) {
        json payload = json::object();
        payload.set("code", json(code));
        payload.set("message", json(message));
        payload.set("fatal", json(fatal));
        return payload;
    }

} // namespace neforce::neflux
