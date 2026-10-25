#include <NeFlux/common.hpp>
#include <NeFlux/recording.hpp>
#include <NeFlux/util.hpp>
#include <NeForce/core/exception/exception.hpp>
#include <NeForce/core/exception/system_exception.hpp>
#include <NeForce/core/file/filesystem.hpp>
namespace neforce::neflux {

    namespace {

        NEFORCE_NORETURN void fail(const string& message) {
            char buffer[512] = {};
            const size_t count = message.size() < sizeof(buffer) - 1 ? message.size() : sizeof(buffer) - 1;
            memory_copy(buffer, message.data(), count);
            buffer[count] = '\0';
            throw value_exception(buffer);
        }

        string quoted(const string& value) {
            string out = "\"";
            for (const char c: value) {
                if (c == '"') {
                    out.append("\"\"");
                } else {
                    out.push_back(c);
                }
            }
            out.push_back('"');
            return out;
        }

        void visit_csv(const json& value, const string& path, uint64_t time, string& out) {
            switch (value.kind()) {
                case json::type::object:
                    for (const auto& member: value.as_object()) {
                        visit_csv(member.second, path + "." + member.first, time, out);
                    }
                    break;
                case json::type::array: {
                    const auto& elements = value.as_array();
                    for (size_t i = 0; i < elements.size(); ++i) {
                        visit_csv(elements[i], format("{}[{}]", path, i), time, out);
                    }
                    break;
                }
                default:
                    out.append(format("{},", time));
                    out.append(quoted(path));
                    out.append(",");
                    out.append(quoted(value.kind() == json::type::string ? value.as_string() : value.dump()));
                    out.append("\n");
                    break;
            }
        }

    } // namespace

    recorder::recorder(const string& file_path, uint64_t max_mib) :
    path_(file_path),
    max_bytes_(max_mib * 1024ULL * 1024ULL) {
        if (::neforce::path(file_path).exists()) {
            size_ = filesystem::size(::neforce::path(file_path)).bytes();
        }
        if (!append_text_file(file_path, "")) {
            throw file_exception("Cannot open recording file");
        }
    }

    void recorder::write(const snapshot& value) {
        string data = to_json(value).dump();
        data.push_back('\n');
        if (size_ > 0 && size_ + data.size() > max_bytes_) {
            for (int index = 3; index >= 1; --index) {
                const string from = index == 1 ? path_ : format("{}.{}", path_, index - 1);
                const string to = format("{}.{}", path_, index);
                if (::neforce::path(from).exists()) {
                    if (::neforce::path(to).exists()) {
                        filesystem::remove(::neforce::path(to));
                    }
                    filesystem::rename(::neforce::path(from), ::neforce::path(to));
                }
            }
            if (!write_text_file(path_, "")) {
                throw file_exception("Cannot rotate recording file");
            }
            size_ = 0;
        }
        if (!append_text_file(path_, data)) {
            throw file_exception("Cannot write recording");
        }
        size_ += data.size();
    }

    vector<snapshot> replay(const string& file_path) {
        if (!::neforce::path(file_path).exists()) {
            fail(format("Cannot open recording: {}", file_path));
        }
        if (filesystem::size(::neforce::path(file_path)).bytes() > 512ULL * 1024ULL * 1024ULL) {
            fail("Replay exceeds 512 MiB; split the recording first");
        }
        const auto text = read_text_file(file_path);
        if (!text.has_value()) {
            fail(format("Cannot read recording: {}", file_path));
        }
        vector<snapshot> out;
        size_t line_number = 0;
        for (const auto& line: split_lines(*text)) {
            ++line_number;
            if (trim(line.view()).empty()) {
                continue;
            }
            try {
                out.push_back(snapshot_from_json(json::parse(line.view())));
            } catch (const exception&) {
                fail(format("Invalid recording line {}", line_number));
            }
        }
        if (out.empty()) {
            fail("Recording contains no snapshots");
        }
        return out;
    }

    string snapshot_csv(const snapshot& value, bool header) {
        string out;
        if (header) {
            out.append("timestamp,path,value\n");
        }
        visit_csv(to_json(value), "system", value.timestamp_, out);
        return out;
    }

} // namespace neforce::neflux
