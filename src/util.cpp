#include <NeFlux/common.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/file/file_info.hpp>
#include <NeForce/core/file/filesystem.hpp>
#include <NeForce/core/file/path.hpp>
#include <NeForce/core/string/codepoint.hpp>
#include <NeForce/core/string/to_numerics.hpp>
#include <NeForce/core/string/to_string.hpp>
#include <NeForce/core/async/this_thread.hpp>
namespace neforce::neflux {

    namespace {

        bool is_space_char(const char c) noexcept {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        }

        char lower_char(const char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

    } // namespace

    uint64_t unix_time() noexcept {
        return static_cast<uint64_t>(system_clock::to_seconds(system_clock::now()).count());
    }

    int64_t monotonic_ms() noexcept {
        return static_cast<int64_t>(steady_clock::now().since_epoch().to_milli().count());
    }

    void sleep_ms(uint64_t ms) { neforce::this_thread::sleep_for_ms(static_cast<uint32_t>(ms)); }

    optional<string> read_text_file(const string& file_path) {
        try {
            file handle;
            if (!handle.open(path(file_path), false, file_access::read)) {
                return none;
            }
            string content = handle.read();
            if (!content.empty()) {
                return content;
            }
            // Procfs and sysfs report a size of zero; read them incrementally instead.
            constexpr size_t chunk = 65536;
            for (;;) {
                const string part = handle.read(chunk);
                if (part.empty()) {
                    break;
                }
                content.append(part);
                if (content.size() > 16 * 1024 * 1024) {
                    break;
                }
                if (part.size() < chunk) {
                    break;
                }
            }
            return content;
        } catch (...) {
            return none;
        }
    }

    bool write_text_file(const string& file_path, const string& content) {
        try {
            const path target(file_path);
            if (const auto parent = target.parent_path(); !parent.empty()) {
                filesystem::create_directories(parent);
            }
            file handle;
            if (!handle.open(path(file_path), false, file_access::write, file_shared::share_read_write,
                             file_creation::create_force)) {
                return false;
            }
            handle.write(content);
            handle.flush();
            return true;
        } catch (...) {
            return false;
        }
    }

    bool append_text_file(const string& file_path, const string& content) {
        try {
            const path target(file_path);
            if (const auto parent = target.parent_path(); !parent.empty()) {
                filesystem::create_directories(parent);
            }
            file handle;
            if (!handle.open(path(file_path), false, file_access::read_write, file_shared::share_read_write,
                             file_creation::open_force)) {
                return false;
            }
            ignore = handle.seek(0, file_pointer::end);
            handle.write(content);
            handle.flush();
            return true;
        } catch (...) {
            return false;
        }
    }

    string trim(string_view text) { return text.trim(); }

    string trim(const string& text) { return trim(text.view()); }

    string trim(const char* text) { return trim(string_view(text)); }

    string lower_ascii(const string& text) { return lower_ascii(text.view()); }

    bool contains_ignore_case(const string& haystack, string_view needle) {
        return contains_ignore_case(haystack.view(), needle);
    }

    vector<string> split_whitespace(const string& text) { return split_whitespace(text.view()); }

    vector<string> split(const string& text, char delimiter, bool keep_empty) {
        return split(text.view(), delimiter, keep_empty);
    }

    vector<string> split_lines(const string& text) { return split_lines(text.view()); }

    bool starts_with(const string& text, string_view prefix) noexcept { return starts_with(text.view(), prefix); }

    bool ends_with(const string& text, string_view suffix) noexcept { return ends_with(text.view(), suffix); }

    string lower_ascii(string_view text) {
        string out;
        out.reserve(text.size());
        for (const char c: text) {
            out.push_back(lower_char(c));
        }
        return out;
    }

    bool contains_ignore_case(string_view haystack, string_view needle) {
        if (needle.empty()) {
            return true;
        }
        if (needle.size() > haystack.size()) {
            return false;
        }
        for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
            bool matched = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                if (lower_char(haystack[i + j]) != lower_char(needle[j])) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return true;
            }
        }
        return false;
    }

    optional<uint64_t> parse_u64(string_view text) noexcept {
        const auto value = trim_view(text);
        if (value.empty()) {
            return none;
        }
        uint64_t result = 0;
        for (const char c: value) {
            if (c < '0' || c > '9') {
                return none;
            }
            result = result * 10 + static_cast<uint64_t>(c - '0');
        }
        return result;
    }

    optional<int64_t> parse_i64(string_view text) noexcept {
        const auto value = trim_view(text);
        if (value.empty()) {
            return none;
        }
        bool negative = false;
        size_t start = 0;
        if (value[0] == '-' || value[0] == '+') {
            negative = value[0] == '-';
            start = 1;
        }
        if (start >= value.size()) {
            return none;
        }
        int64_t result = 0;
        for (size_t i = start; i < value.size(); ++i) {
            const char c = value[i];
            if (c < '0' || c > '9') {
                return none;
            }
            result = result * 10 + static_cast<int64_t>(c - '0');
        }
        return negative ? -result : result;
    }

    optional<double> parse_f64(string_view text) noexcept {
        const auto value = trim_view(text);
        if (value.empty()) {
            return none;
        }
        size_t consumed = 0;
        const double result = to_decimal(value, &consumed);
        if (consumed == 0) {
            return none;
        }
        return result;
    }

    vector<string> split_whitespace(string_view text) {
        vector<string> out;
        size_t i = 0;
        while (i < text.size()) {
            while (i < text.size() && is_space_char(text[i])) {
                ++i;
            }
            const size_t begin = i;
            while (i < text.size() && !is_space_char(text[i])) {
                ++i;
            }
            if (i > begin) {
                out.push_back(string(text.data() + begin, i - begin));
            }
        }
        return out;
    }

    vector<string> split(string_view text, char delimiter, bool keep_empty) {
        vector<string> out;
        size_t begin = 0;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == delimiter) {
                if (i > begin || keep_empty) {
                    out.push_back(string(text.data() + begin, i - begin));
                }
                begin = i + 1;
            }
        }
        return out;
    }

    vector<string> split_lines(string_view text) {
        vector<string> out;
        size_t begin = 0;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '\n') {
                size_t end = i;
                if (end > begin && text[end - 1] == '\r') {
                    --end;
                }
                out.push_back(string(text.data() + begin, end - begin));
                begin = i + 1;
            }
        }
        if (!out.empty() && out.back().empty()) {
            out.pop_back();
        }
        return out;
    }

    bool starts_with(string_view text, string_view prefix) noexcept {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    bool ends_with(string_view text, string_view suffix) noexcept {
        return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
    }

    int display_width(string_view text) noexcept {
        int width = 0;
        size_t i = 0;
        while (i < text.size()) {
            const auto cp = codepoint::decode_utf8(reinterpret_cast<const byte_t*>(text.data()), i, text.size());
            width += cp.display_width();
        }
        return width;
    }

    string truncate_display(string_view text, int width) {
        if (width <= 0) {
            return {};
        }
        int used = 0;
        size_t i = 0;
        while (i < text.size()) {
            const size_t begin = i;
            const auto cp = codepoint::decode_utf8(reinterpret_cast<const byte_t*>(text.data()), i, text.size());
            const int cell = cp.display_width();
            if (used + cell > width) {
                i = begin;
                break;
            }
            used += cell;
        }
        return string(text.data(), i);
    }

    string fit_display(string_view text, int width, bool right_align) {
        if (width <= 0) {
            return {};
        }
        const int current = display_width(text);
        if (current >= width) {
            return truncate_display(text, width);
        }
        string out;
        out.reserve(static_cast<size_t>(width));
        if (right_align) {
            out.append(static_cast<size_t>(width - current), ' ');
            out.append(text.data(), text.size());
        } else {
            out.append(text.data(), text.size());
            out.append(static_cast<size_t>(width - current), ' ');
        }
        return out;
    }

    string sanitize_text(string_view text) {
        string out;
        out.reserve(text.size());
        for (const unsigned char c: text) {
            if (c < 0x20 || c == 0x7F) {
                out.push_back(' ');
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        return out;
    }

    string format_number(double value, int precision) {
        if (!neforce::is_finite(value)) {
            return "0";
        }
        string text = to_string_general(value, precision);
        const auto exponent = text.find_first_of("eE");
        string mantissa = exponent == string::npos ? text : text.substr(0, exponent);
        string suffix = exponent == string::npos ? string() : text.substr(exponent);
        const auto dot = mantissa.find('.');
        if (dot != string::npos) {
            while (!mantissa.empty() && mantissa[mantissa.size() - 1] == '0') {
                mantissa.resize(mantissa.size() - 1);
            }
            if (!mantissa.empty() && mantissa[mantissa.size() - 1] == '.') {
                mantissa.resize(mantissa.size() - 1);
            }
        }
        if (mantissa.empty() || mantissa == "-0") {
            mantissa = "0";
        }
        return mantissa + suffix;
    }

    string format_fixed(double value, int digits) {
        if (digits < 0) {
            digits = 0;
        }
        switch (digits) {
            case 0:
                return to_string_fixed(value, 0);
            case 1:
                return to_string_fixed(value, 1);
            case 2:
                return to_string_fixed(value, 2);
            case 3:
                return to_string_fixed(value, 3);
            default:
                return to_string_fixed(value, 6);
        }
    }

    string basename_of(string_view command) {
        const auto trimmed = trim_view(command);
        const size_t slash = trimmed.rfind('/');
        if (slash == string_view::npos) {
            return string(trimmed.data(), trimmed.size());
        }
        return string(trimmed.data() + slash + 1, trimmed.size() - slash - 1);
    }

} // namespace neforce::neflux
