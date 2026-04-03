#include "sv-cdccheck/sdc_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

namespace sv_cdccheck {

SdcConstraints SdcParser::parse(const std::filesystem::path& sdc_path) {
    SdcConstraints result;
    auto lines = preprocessLines(sdc_path);

    for (auto& line : lines) {
        auto tokens = tokenize(line);
        if (tokens.empty()) continue;

        if (tokens[0] == "create_clock") {
            result.clocks.push_back(parseCreateClock(tokens));
        } else if (tokens[0] == "create_generated_clock") {
            result.generated_clocks.push_back(parseGeneratedClock(tokens));
        } else if (tokens[0] == "set_clock_groups") {
            result.clock_groups.push_back(parseClockGroups(tokens));
        }
        // Other SDC commands are silently ignored
    }
    return result;
}

std::vector<std::string> SdcParser::preprocessLines(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::vector<std::string> result;
    std::string accumulated;

    std::string raw;
    while (std::getline(file, raw)) {
        // Strip comments (# to end of line, but not inside brackets)
        {
            size_t comment_pos = std::string::npos;
            int brackets = 0;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '[') brackets++;
                else if (raw[i] == ']' && brackets > 0) brackets--;
                else if (raw[i] == '#' && brackets == 0) {
                    comment_pos = i;
                    break;
                }
            }
            if (comment_pos != std::string::npos)
                raw = raw.substr(0, comment_pos);
        }

        // Trim trailing whitespace
        while (!raw.empty() && std::isspace(raw.back()))
            raw.pop_back();

        // Handle backslash continuation
        if (!raw.empty() && raw.back() == '\\') {
            raw.pop_back();
            accumulated += raw + " ";
            continue;
        }

        accumulated += raw;
        if (!accumulated.empty()) {
            // Trim leading whitespace
            auto start = accumulated.find_first_not_of(" \t");
            if (start != std::string::npos)
                result.push_back(accumulated.substr(start));
        }
        accumulated.clear();
    }

    if (!accumulated.empty()) {
        auto start = accumulated.find_first_not_of(" \t");
        if (start != std::string::npos)
            result.push_back(accumulated.substr(start));
    }

    return result;
}

std::vector<std::string> SdcParser::tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    int bracket_depth = 0;
    int brace_depth = 0;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '[') bracket_depth++;
        if (c == ']' && bracket_depth > 0) bracket_depth--;
        if (c == '{') brace_depth++;
        if (c == '}' && brace_depth > 0) brace_depth--;

        if (std::isspace(c) && bracket_depth == 0 && brace_depth == 0) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty())
        tokens.push_back(current);

    return tokens;
}

SdcClockDef SdcParser::parseCreateClock(const std::vector<std::string>& tokens) {
    SdcClockDef def;
    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == "-name" && i + 1 < tokens.size()) {
            def.name = tokens[++i];
        } else if (tokens[i] == "-period" && i + 1 < tokens.size()) {
            ++i;
            try {
                def.period = std::stod(tokens[i]);
            } catch (const std::exception&) {
                // Skip malformed period value
            }
        } else if (tokens[i].starts_with("[get_ports") || tokens[i].starts_with("[get_pins")) {
            def.target = extractTarget(tokens[i]);
        }
    }
    return def;
}

SdcGeneratedClockDef SdcParser::parseGeneratedClock(const std::vector<std::string>& tokens) {
    SdcGeneratedClockDef def;
    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == "-name" && i + 1 < tokens.size()) {
            def.name = tokens[++i];
        } else if (tokens[i] == "-source" && i + 1 < tokens.size()) {
            def.source_clock = extractTarget(tokens[++i]);
        } else if (tokens[i] == "-divide_by" && i + 1 < tokens.size()) {
            ++i;
            try {
                def.divide_by = std::stoi(tokens[i]);
            } catch (const std::exception&) {
                // Skip malformed divide_by value
            }
        } else if (tokens[i] == "-multiply_by" && i + 1 < tokens.size()) {
            ++i;
            try {
                def.multiply_by = std::stoi(tokens[i]);
            } catch (const std::exception&) {
                // Skip malformed multiply_by value
            }
        } else if (tokens[i] == "-invert") {
            def.invert = true;
        } else if (tokens[i].starts_with("[get_")) {
            def.target = extractTarget(tokens[i]);
        }
    }
    return def;
}

SdcClockGroup SdcParser::parseClockGroups(const std::vector<std::string>& tokens) {
    SdcClockGroup group;
    group.type = SdcClockGroup::Type::Asynchronous; // default

    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == "-asynchronous") {
            group.type = SdcClockGroup::Type::Asynchronous;
        } else if (tokens[i] == "-physically_exclusive") {
            group.type = SdcClockGroup::Type::Exclusive;
        } else if (tokens[i] == "-logically_exclusive") {
            group.type = SdcClockGroup::Type::LogicallyExclusive;
        } else if (tokens[i] == "-group" && i + 1 < tokens.size()) {
            group.groups.push_back(parseBraceList(tokens[++i]));
        }
    }
    return group;
}

std::string SdcParser::extractTarget(const std::string& tcl_expr) {
    // [get_ports sys_clk] → "sys_clk"
    // [get_pins u_div/clk_out] → "u_div/clk_out"
    static std::regex re(R"(\[get_(?:ports|pins)\s+(\S+)\])");
    std::smatch match;
    if (std::regex_search(tcl_expr, match, re))
        return match[1].str();
    return tcl_expr;
}

std::vector<std::string> SdcParser::parseBraceList(const std::string& s) {
    std::vector<std::string> result;
    std::string inner = s;

    // Strip outer braces
    if (!inner.empty() && inner.front() == '{') inner = inner.substr(1);
    if (!inner.empty() && inner.back() == '}') inner.pop_back();

    std::istringstream iss(inner);
    std::string token;
    while (iss >> token)
        result.push_back(token);

    return result;
}

} // namespace sv_cdccheck
