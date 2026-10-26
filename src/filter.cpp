#include <NeFlux/common.hpp>
#include <NeFlux/filter.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/memory/shared_ptr.hpp>
#include <NeForce/core/string/format.hpp>
#include <NeForce/core/string/regex.hpp>
namespace neforce::neflux {

    namespace {

        struct term {
            string field;
            string value;
            bool negated = false;
            shared_ptr<regex> pattern_;
            optional<pair<char, double>> compare;
        };

    } // namespace

    struct query::impl {
        vector<vector<term>> groups;
        optional<string> error;
    };

    query::query() :
    impl_(make_unique<impl>()) {}

    query::~query() = default;

    query::query(query&&) noexcept = default;

    query& query::operator=(query&&) noexcept = default;

    query query::parse(const string& text) {
        query result;
        size_t group_begin = 0;
        while (group_begin <= text.size()) {
            const size_t group_end = text.find(" | ", group_begin);
            const string group =
                    text.substr(group_begin, group_end == string::npos ? string::npos : group_end - group_begin);
            group_begin = group_end == string::npos ? text.size() + 1 : group_end + 3;

            vector<string> words;
            string word;
            bool quoted = false;
            for (const char c: group) {
                if (c == '"') {
                    quoted = !quoted;
                } else if ((c == ' ' || c == '\t') && !quoted) {
                    if (!word.empty()) {
                        words.push_back(word);
                        word.clear();
                    }
                } else {
                    word.push_back(c);
                }
            }
            if (!word.empty()) {
                words.push_back(word);
            }
            if (quoted) {
                result.impl_->error = "Unclosed quoted term";
            }
            vector<term> terms;
            for (auto& raw: words) {
                bool negated = false;
                string current = raw;
                if (starts_with(current.view(), "!")) {
                    negated = true;
                    current = current.substr(1);
                }
                const auto colon = current.find(':');
                string field;
                string value = current;
                if (colon != string::npos) {
                    field = lower_ascii(current.substr(0, colon));
                    value = current.substr(colon + 1);
                }
                const vector<string> allowed = {"",    "pid", "user",   "name", "state", "cmd", "command",
                                                "cpu", "mem", "memory", "gpu",  "vram",  "tag"};
                bool known = false;
                for (const auto& item: allowed) {
                    if (item == field) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    result.impl_->error = format("Unknown field: {}", field);
                }
                term parsed;
                parsed.field = field;
                parsed.value = lower_ascii(value);
                parsed.negated = negated;
                if (value.size() >= 2 && value[0] == '/' && value[value.size() - 1] == '/') {
                    try {
                        const string pattern = value.substr(1, value.size() - 2);
                        parsed.pattern_ = make_shared<regex>(pattern, regex_option::caseless);
                    } catch (const exception& error) {
                        result.impl_->error = error.what();
                    }
                } else if (!value.empty() && (value[0] == '>' || value[0] == '<' || value[0] == '=')) {
                    const auto number = parse_f64(value.view().substr(1));
                    if (number.has_value()) {
                        parsed.compare = pair<char, double>{value[0], *number};
                    }
                }
                terms.push_back(move(parsed));
            }
            result.impl_->groups.push_back(move(terms));
        }
        return result;
    }

    bool query::matches(const process& value) const {
        if (impl_->error.has_value()) {
            return false;
        }
        const auto field_value = [&value](const string& field) -> string {
            if (field == "pid") {
                return format("{}", value.pid_);
            }
            if (field == "user") {
                return value.user_;
            }
            if (field == "name") {
                return value.name_;
            }
            if (field == "state") {
                return value.state_;
            }
            if (field == "cmd" || field == "command") {
                return value.command_;
            }
            if (field == "cpu") {
                return format("{}", value.cpu_);
            }
            if (field == "mem" || field == "memory") {
                return format("{}", static_cast<double>(value.memory_) / 1024.0 / 1024.0);
            }
            if (field == "gpu") {
                return value.gpu_percent_.has_value() ? format("{}", *value.gpu_percent_) : "—";
            }
            if (field == "vram") {
                return value.gpu_memory_.has_value()
                               ? format("{}", static_cast<double>(*value.gpu_memory_) / 1024.0 / 1024.0)
                               : "—";
            }
            if (field == "tag") {
                string out;
                for (const auto& item: value.tags_) {
                    if (!out.empty()) {
                        out.push_back(' ');
                    }
                    out.append(item.key_);
                    out.push_back(' ');
                    out.append(item.display());
                }
                return out;
            }
            string plain = format("{} {} {} {} ", value.pid_, value.user_, value.name_, value.command_);
            for (const auto& item: value.tags_) {
                plain.append(item.display());
                plain.push_back(' ');
            }
            return plain;
        };
        for (const auto& group: impl_->groups) {
            bool all = true;
            for (const auto& item: group) {
                const string candidate = field_value(item.field);
                bool matched = false;
                if (item.compare.has_value()) {
                    const auto number = parse_f64(candidate.view());
                    if (number.has_value()) {
                        const char op = item.compare->first;
                        const double target = item.compare->second;
                        matched = op == '>' ? *number > target : (op == '<' ? *number < target : *number == target);
                    }
                } else if (item.pattern_ != nullptr) {
                    matched = item.pattern_->search(candidate).matched();
                } else {
                    matched = contains_ignore_case(candidate.view(), item.value.view());
                }
                if (matched == item.negated) {
                    all = false;
                    break;
                }
            }
            if (all) {
                return true;
            }
        }
        return false;
    }

    const optional<string>& query::error() const noexcept { return impl_->error; }

    bool query::empty() const noexcept {
        for (const auto& group: impl_->groups) {
            if (!group.empty()) {
                return false;
            }
        }
        return true;
    }

    namespace {
        bool connection_token_matches(const connection& value, const string& raw) {
            string token = raw;
            bool negated = false;
            if (starts_with(token.view(), "!")) {
                negated = true;
                token = token.substr(1);
            }
            if (token.empty()) {
                return true;
            }
            const auto colon = token.find(':');
            const string field = colon == string::npos ? string() : lower_ascii(token.substr(0, colon));
            const string needle = lower_ascii(colon == string::npos ? token : token.substr(colon + 1));
            if (field == "port") {
                const auto port_of = [](const string& endpoint) {
                    const auto icolon = endpoint.rfind(':');
                    return icolon == string::npos ? string() : endpoint.substr(icolon + 1);
                };
                const bool found = port_of(value.local_) == needle || port_of(value.remote_) == needle;
                return negated ? !found : found;
            }
            if (field == "pid") {
                const auto wanted = parse_u64(needle.view());
                bool found = false;
                if (wanted.has_value()) {
                    for (const uint32_t pid: value.pids_) {
                        if (pid == *wanted) {
                            found = true;
                            break;
                        }
                    }
                }
                return negated ? !found : found;
            }
            string haystack;
            if (field == "local" || field == "address") {
                haystack = value.local_;
            } else if (field == "remote" || field == "peer") {
                haystack = value.remote_;
            } else if (field == "host") {
                haystack = value.local_ + " " + value.remote_;
            } else if (field == "state") {
                haystack = value.state_;
            } else if (field == "protocol" || field == "proto") {
                haystack = value.protocol_;
            } else if (field == "process" || field == "name") {
                for (const auto& name: value.process_names_) {
                    haystack.append(name);
                    haystack.push_back(' ');
                }
            } else if (field == "inode") {
                haystack = format("{}", value.inode_);
            } else if (field == "tag") {
                for (const auto& tag: value.tags_) {
                    haystack.append(tag.display());
                    haystack.push_back(' ');
                }
            } else if (field.empty()) {
                haystack = value.protocol_ + " " + value.local_ + " " + value.remote_ + " " + value.state_;
                for (const auto& name: value.process_names_) {
                    haystack.append(" ");
                    haystack.append(name);
                }
            }
            bool matched = false;
            if (needle.size() >= 2 && needle[0] == '/' && needle[needle.size() - 1] == '/') {
                try {
                    const regex pattern(needle.substr(1, needle.size() - 2), regex_option::caseless);
                    matched = pattern.search(haystack).matched();
                } catch (const exception&) {
                    matched = false;
                }
            } else {
                matched = contains_ignore_case(haystack.view(), needle.view());
            }
            return negated ? !matched : matched;
        }
    } // namespace

    bool connection_matches(const connection& value, const string& text) {
        if (trim(text.view()).empty()) {
            return true;
        }
        size_t begin = 0;
        while (begin <= text.size()) {
            const size_t end = text.find(" | ", begin);
            const string group = text.substr(begin, end == string::npos ? string::npos : end - begin);
            begin = end == string::npos ? text.size() + 1 : end + 3;
            bool group_matches = true;
            for (const auto& token: split_whitespace(group.view())) {
                if (!connection_token_matches(value, token)) {
                    group_matches = false;
                    break;
                }
            }
            if (group_matches) {
                return true;
            }
        }
        return false;
    }

    bool event_matches(const event& event, const string& text) {
        const string line =
                format("{} {} {}", event.timestamp_, event.active_ ? "active" : "recovered", event.condition_);
        return contains_ignore_case(line.view(), text.view());
    }

} // namespace neforce::neflux
