#include <NeFlux/common.hpp>
#include <NeFlux/json.hpp>
#include <NeForce/core/file/json/json_parser.hpp>
#include <NeForce/core/file/json/json_value.hpp>
#include <NeForce/core/typeinfo/type_traits.hpp>
#include <NeForce/core/utility/pair.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/numeric/math.hpp>
namespace neforce::neflux {

    namespace {

        string_view view_of(const string& value) { return string_view(value.data(), value.size()); }

        json convert(const json_value& value) {
            switch (value.type()) {
                case json_value::types::Null:
                    return json();
                case json_value::types::Bool:
                    return json(value.as_bool()->get_value());
                case json_value::types::Number: {
                    const double number = value.as_number()->get_value();
                    if (neforce::is_finite(number) && number >= -9007199254740992.0 && number <= 9007199254740992.0 &&
                        neforce::floor(number) == number) {
                        return json(static_cast<int64_t>(number));
                    }
                    return json(number);
                }
                case json_value::types::String:
                    return json(value.as_string()->get_value());
                case json_value::types::Array: {
                    json out = json::array();
                    for (const auto& element: value.as_array()->get_elements()) {
                        out.push(convert(*element));
                    }
                    return out;
                }
                case json_value::types::Object: {
                    json out = json::object();
                    for (const auto& member: value.as_object()->get_members()) {
                        out.set(member.first, convert(*member.second));
                    }
                    return out;
                }
            }
            return json();
        }

    } // namespace

    json::json() = default;

    json::json(const json& other) :
    type_(other.type_),
    boolean_(other.boolean_),
    integer_(other.integer_),
    number_(other.number_),
    number_text_(other.number_text_),
    string_(other.string_) {
        if (other.array_ != nullptr) {
            array_ = make_unique<array_type>(*other.array_);
        }
        if (other.object_ != nullptr) {
            object_ = make_unique<object_type>(*other.object_);
        }
    }

    json::json(json&& other) noexcept = default;

    json& json::operator=(const json& other) {
        if (this == &other) {
            return *this;
        }
        type_ = other.type_;
        boolean_ = other.boolean_;
        integer_ = other.integer_;
        number_ = other.number_;
        number_text_ = other.number_text_;
        string_ = other.string_;
        array_.reset();
        object_.reset();
        if (other.array_ != nullptr) {
            array_ = make_unique<array_type>(*other.array_);
        }
        if (other.object_ != nullptr) {
            object_ = make_unique<object_type>(*other.object_);
        }
        return *this;
    }

    json& json::operator=(json&& other) noexcept = default;

    json::~json() = default;

    json::json(bool value) :
    type_(type::boolean),
    boolean_(value) {}

    json::json(nullptr_t value) { static_cast<void>(value); }

    json::json(double value) :
    type_(type::number),
    number_(value) {}

    namespace {

        string double_text(double value, int precision) { return format_number(value, precision); }

    } // namespace

    json::json(float value) :
    type_(type::number),
    number_(static_cast<double>(value)),
    number_text_(double_text(value, 7)) {}

    json::json(const char* value) :
    type_(type::string),
    string_(value) {}

    json::json(const string& value) :
    type_(type::string),
    string_(value) {}

    json::json(string_view value) :
    type_(type::string),
    string_(value.data(), value.size()) {}

    json json::array() {
        json out;
        out.type_ = type::array;
        return out;
    }

    json json::object() {
        json out;
        out.type_ = type::object;
        return out;
    }

    json json::null() { return json(); }

    bool json::as_bool() const noexcept { return type_ == type::boolean ? boolean_ : false; }

    int64_t json::as_integer() const noexcept {
        if (type_ == type::integer) {
            return integer_;
        }
        if (type_ == type::number) {
            return static_cast<int64_t>(number_);
        }
        return 0;
    }

    double json::as_number() const noexcept {
        if (type_ == type::number) {
            return number_;
        }
        if (type_ == type::integer) {
            return static_cast<double>(integer_);
        }
        return 0.0;
    }

    const string& json::as_string() const noexcept {
        static const string empty;
        return type_ == type::string ? string_ : empty;
    }

    const json::array_type& json::as_array() const noexcept {
        static const array_type empty;
        return array_ != nullptr ? *array_ : empty;
    }

    const json::object_type& json::as_object() const noexcept {
        static const object_type empty;
        return object_ != nullptr ? *object_ : empty;
    }

    json::array_type& json::elements() noexcept {
        if (type_ != type::array) {
            type_ = type::array;
            object_.reset();
        }
        if (array_ == nullptr) {
            array_ = make_unique<array_type>();
        }
        return *array_;
    }

    json::object_type& json::members() noexcept {
        if (type_ != type::object) {
            type_ = type::object;
            array_.reset();
        }
        if (object_ == nullptr) {
            object_ = make_unique<object_type>();
        }
        return *object_;
    }

    json& json::push(json value) {
        elements().push_back(move(value));
        return *this;
    }

    json& json::set(const string& key, json value) {
        auto& members_ref = members();
        for (auto& member: members_ref) {
            if (member.first == key) {
                member.second = move(value);
                return *this;
            }
        }
        members_ref.push_back({key, move(value)});
        return *this;
    }

    const json* json::get(const string& key) const noexcept {
        if (type_ != type::object || object_ == nullptr) {
            return nullptr;
        }
        for (const auto& member: *object_) {
            if (member.first == key) {
                return &member.second;
            }
        }
        return nullptr;
    }

    json& json::operator[](const string& key) {
        auto& members_ref = members();
        for (auto& member: members_ref) {
            if (member.first == key) {
                return member.second;
            }
        }
        members_ref.push_back({key, json()});
        return members_ref.back().second;
    }

    bool json::contains(const string& key) const noexcept { return get(key) != nullptr; }

    size_t json::size() const noexcept {
        if (type_ == type::array && array_ != nullptr) {
            return array_->size();
        }
        if (type_ == type::object && object_ != nullptr) {
            return object_->size();
        }
        return 0;
    }

    string json::dump(bool pretty) const {
        string out;
        out.reserve(256);
        dump_to(out, pretty, 0);
        return out;
    }

    namespace {
        size_t utf8_length(const unsigned char lead) {
            if (lead < 0x80) {
                return 1;
            }
            if (lead >= 0xC2 && lead <= 0xDF) {
                return 2;
            }
            if (lead >= 0xE0 && lead <= 0xEF) {
                return 3;
            }
            if (lead >= 0xF0 && lead <= 0xF4) {
                return 4;
            }
            return 0;
        }
    } // namespace

    void json::escape_string(string& out, const string& value) {
        out.push_back('"');
        for (size_t index = 0; index < value.size();) {
            const auto c = static_cast<unsigned char>(value[index]);
            if (c >= 0x80) {
                const size_t length = utf8_length(c);
                bool valid = length != 0 && index + length <= value.size();
                if (valid) {
                    for (size_t offset = 1; offset < length; ++offset) {
                        const auto continuation = static_cast<unsigned char>(value[index + offset]);
                        if ((continuation & 0xC0U) != 0x80U) {
                            valid = false;
                            break;
                        }
                    }
                }
                if (valid && length == 3) {
                    const auto second = static_cast<unsigned char>(value[index + 1]);
                    if ((c == 0xE0 && second < 0xA0) || (c == 0xED && second >= 0xA0)) {
                        valid = false;
                    }
                }
                if (valid && length == 4) {
                    const auto second = static_cast<unsigned char>(value[index + 1]);
                    if ((c == 0xF0 && second < 0x90) || (c == 0xF4 && second >= 0x90)) {
                        valid = false;
                    }
                }
                if (!valid) {
                    out.append("\\ufffd");
                    ++index;
                    continue;
                }
                out.append(value.view().substr(index, length));
                index += length;
                continue;
            }
            switch (c) {
                case '"':
                    out.append("\\\"");
                    break;
                case '\\':
                    out.append("\\\\");
                    break;
                case '\b':
                    out.append("\\b");
                    break;
                case '\f':
                    out.append("\\f");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\r':
                    out.append("\\r");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                default:
                    if (c < 0x20) {
                        static constexpr char hex[] = "0123456789abcdef";
                        out.append("\\u00");
                        out.push_back(hex[(c >> 4) & 0xF]);
                        out.push_back(hex[c & 0xF]);
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
            }
            ++index;
        }
        out.push_back('"');
    }

    void json::append_number(string& out, double value) {
        if (!neforce::is_finite(value)) {
            out.append("null");
            return;
        }
        out.append(double_text(value, 15));
    }

    void json::append_float(string& out, float value) { out.append(double_text(value, 7)); }

    void json::dump_to(string& out, bool pretty, int indent) const {
        switch (type_) {
            case type::null:
                out.append("null");
                break;
            case type::boolean:
                out.append(boolean_ ? "true" : "false");
                break;
            case type::integer:
                out.append(format("{}", integer_));
                break;
            case type::number:
                if (!number_text_.empty()) {
                    out.append(number_text_);
                } else {
                    append_number(out, number_);
                }
                break;
            case type::string:
                escape_string(out, string_);
                break;
            case type::array: {
                out.push_back('[');
                const array_type& items = as_array();
                const bool multiline = pretty && !items.empty();
                for (size_t i = 0; i < items.size(); ++i) {
                    if (i != 0) {
                        out.push_back(',');
                    }
                    if (multiline) {
                        out.push_back('\n');
                        out.append(static_cast<size_t>((indent + 1) * 2), ' ');
                    }
                    items[i].dump_to(out, pretty, indent + 1);
                }
                if (multiline) {
                    out.push_back('\n');
                    out.append(static_cast<size_t>(indent * 2), ' ');
                }
                out.push_back(']');
                break;
            }
            case type::object: {
                out.push_back('{');
                const object_type& items = as_object();
                const bool multiline = pretty && !items.empty();
                for (size_t i = 0; i < items.size(); ++i) {
                    if (i != 0) {
                        out.push_back(',');
                    }
                    if (multiline) {
                        out.push_back('\n');
                        out.append(static_cast<size_t>((indent + 1) * 2), ' ');
                    }
                    escape_string(out, items[i].first);
                    out.push_back(':');
                    if (pretty) {
                        out.push_back(' ');
                    }
                    items[i].second.dump_to(out, pretty, indent + 1);
                }
                if (multiline) {
                    out.push_back('\n');
                    out.append(static_cast<size_t>(indent * 2), ' ');
                }
                out.push_back('}');
                break;
            }
        }
    }

    json json::parse(string_view text) {
        json_parser parser(string(text.data(), text.size()));
        auto parsed = parser.parse();
        if (parsed == nullptr) {
            return json();
        }
        return convert(*parsed);
    }

    string json::string_or(const string& key, const string& fallback) const {
        const json* value = get(key);
        return value != nullptr && value->is_string() ? value->as_string() : fallback;
    }

    int64_t json::integer_or(const string& key, int64_t fallback) const {
        const json* value = get(key);
        if (value == nullptr) {
            return fallback;
        }
        if (value->is_integer() || value->is_number()) {
            return value->as_integer();
        }
        return fallback;
    }

    double json::number_or(const string& key, double fallback) const {
        const json* value = get(key);
        if (value == nullptr) {
            return fallback;
        }
        if (value->is_integer() || value->is_number()) {
            return value->as_number();
        }
        return fallback;
    }

    bool json::bool_or(const string& key, bool fallback) const {
        const json* value = get(key);
        return value != nullptr && value->is_bool() ? value->as_bool() : fallback;
    }

    optional<string> json::optional_string(const string& key) const {
        const json* value = get(key);
        if (value == nullptr || value->is_null()) {
            return none;
        }
        return value->as_string();
    }

    optional<double> json::optional_number(const string& key) const {
        const json* value = get(key);
        if (value == nullptr || value->is_null() || !(value->is_number() || value->is_integer())) {
            return none;
        }
        return value->as_number();
    }

} // namespace neforce::neflux
