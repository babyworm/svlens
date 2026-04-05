#pragma once

#include <fmt/core.h>
#include <string>

namespace svlens {

inline std::string escapeJson(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '<':  result += "\\u003c"; break;
            default:
                if (c < 0x20)
                    result += fmt::format("\\u{:04x}", c);
                else
                    result += static_cast<char>(c);
                break;
        }
    }
    return result;
}

inline std::string jsonStr(const std::string& s) {
    return "\"" + escapeJson(s) + "\"";
}

} // namespace svlens
