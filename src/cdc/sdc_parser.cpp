#include "sv-cdccheck/sdc_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <regex>
#include <string_view>
#include <utility>

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
            if (auto group = parseClockGroups(tokens))
                result.clock_groups.push_back(*group);
            else
                ++result.skipped_clock_groups;
        } else if (tokens[0] == "set_false_path") {
            result.false_paths.push_back(parseSetFalsePath(tokens));
        } else if (tokens[0] == "set_max_delay") {
            if (auto maxDelay = parseSetMaxDelay(tokens))
                result.max_delays.push_back(*maxDelay);
            else
                ++result.skipped_max_delays;
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

std::optional<SdcClockGroup> SdcParser::parseClockGroups(const std::vector<std::string>& tokens) {
    SdcClockGroup group;
    group.type = SdcClockGroup::Type::Asynchronous; // default

    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == "-asynchronous") {
            group.type = SdcClockGroup::Type::Asynchronous;
        } else if (tokens[i] == "-physically_exclusive") {
            group.type = SdcClockGroup::Type::Exclusive;
        } else if (tokens[i] == "-logically_exclusive") {
            group.type = SdcClockGroup::Type::LogicallyExclusive;
        } else if (tokens[i] == "-group") {
            if (i + 1 >= tokens.size())
                return std::nullopt;
            bool includeGenerated = false;
            auto names = parseClockGroupNames(tokens[++i], includeGenerated);
            if (!names)
                return std::nullopt;
            group.groups.push_back(std::move(*names));
            group.include_generated.push_back(includeGenerated);
        } else if (tokens[i] == "-name" && i + 1 < tokens.size()) {
            ++i;
        } else {
            return std::nullopt;
        }
    }
    if (group.groups.empty())
        return std::nullopt;
    return group;
}

std::string SdcParser::extractTarget(const std::string& tcl_expr) {
    // [get_ports sys_clk] → "sys_clk"
    // [get_clocks {sys_clk}] → "sys_clk" (one braced name only)
    // [get_pins u_div/clk_out] → "u_div/clk_out"
    static std::regex re(R"(\[get_(?:ports|pins|clocks)\s+(\S+)\])");
    std::smatch match;
    if (std::regex_search(tcl_expr, match, re)) {
        std::string target = match[1].str();
        if (target.size() > 2 && target.front() == '{' && target.back() == '}' &&
            target.find_first_of(" \t\r\n", 1) == std::string::npos)
            return target.substr(1, target.size() - 2);
        return target;
    }
    return tcl_expr;
}

std::optional<std::vector<std::string>> SdcParser::parseClockGroupNames(const std::string& selector,
                                                                        bool& includeGenerated) {
    std::string inner = selector;
    includeGenerated = false;
    constexpr std::string_view getClocks = "[get_clocks ";
    if (inner.starts_with(getClocks) && inner.ends_with(']')) {
        inner = inner.substr(getClocks.size(), inner.size() - getClocks.size() - 1);
        const auto first = inner.find_first_not_of(" \t");
        if (first == std::string::npos)
            return std::nullopt;
        const auto last = inner.find_last_not_of(" \t");
        inner = inner.substr(first, last - first + 1);
        constexpr std::string_view includeOption = "-include_generated_clocks";
        if (inner.starts_with(includeOption)) {
            if (inner.size() <= includeOption.size() || inner[includeOption.size()] != ' ')
                return std::nullopt;
            inner = inner.substr(includeOption.size() + 1);
            const auto nameStart = inner.find_first_not_of(" \t");
            if (nameStart == std::string::npos)
                return std::nullopt;
            inner.erase(0, nameStart);
            includeGenerated = true;
        }
    } else if (inner.find_first_of("[]") != std::string::npos) {
        return std::nullopt;
    }

    const bool braced = inner.size() >= 2 && inner.front() == '{' && inner.back() == '}';
    if (braced) {
        inner = inner.substr(1, inner.size() - 2);
    } else if (inner.empty() || inner.front() == '{' || inner.back() == '}' ||
               inner.find_first_of(" \t") != std::string::npos) {
        return std::nullopt;
    }
    std::vector<std::string> result;
    std::istringstream iss(inner);
    std::string token;
    while (iss >> token) {
        if (token.find_first_of("*?$[]{};\\") != std::string::npos || token.starts_with('-'))
            return std::nullopt;
        result.push_back(token);
    }
    if (result.empty())
        return std::nullopt;
    return result;
}

SdcFalsePath SdcParser::parseSetFalsePath(const std::vector<std::string>& tokens) {
    SdcFalsePath fp;
    bool fromClock = false;
    bool toClock = false;
    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == "-from" && i + 1 < tokens.size()) {
            const auto& target = tokens[++i];
            fp.from = extractTarget(target);
            fromClock = target.starts_with("[get_clocks ");
        } else if (tokens[i] == "-to" && i + 1 < tokens.size()) {
            const auto& target = tokens[++i];
            fp.to = extractTarget(target);
            toClock = target.starts_with("[get_clocks ");
        }
    }
    fp.clock_to_clock = fromClock && toClock && !fp.from.empty() && !fp.to.empty() &&
                        fp.from.find('[') == std::string::npos && fp.to.find('[') == std::string::npos;
    return fp;
}

std::optional<SdcMaxDelay> SdcParser::parseSetMaxDelay(const std::vector<std::string>& tokens) {
    SdcMaxDelay md;
    bool haveDelay = false;
    bool haveFrom = false;
    bool haveTo = false;
    auto clockName = [&](const std::string& selector) -> std::optional<std::string> {
        if (!selector.starts_with("[get_clocks "))
            return std::nullopt;
        auto name = extractTarget(selector);
        if (name.empty() || name.find_first_of("*?$[]{}") != std::string::npos)
            return std::nullopt;
        return name;
    };
    for (size_t i = 1; i < tokens.size(); i++) {
        if (tokens[i] == "-from") {
            if (haveFrom || i + 1 >= tokens.size())
                return std::nullopt;
            auto name = clockName(tokens[++i]);
            if (!name)
                return std::nullopt;
            md.from = std::move(*name);
            haveFrom = true;
        } else if (tokens[i] == "-to") {
            if (haveTo || i + 1 >= tokens.size())
                return std::nullopt;
            auto name = clockName(tokens[++i]);
            if (!name)
                return std::nullopt;
            md.to = std::move(*name);
            haveTo = true;
        } else if (tokens[i] == "-datapath_only") {
            if (md.datapath_only)
                return std::nullopt;
            md.datapath_only = true;
        } else if (!tokens[i].empty() && tokens[i].front() != '-') {
            if (haveDelay)
                return std::nullopt;
            try {
                size_t parsed = 0;
                md.delay = std::stod(tokens[i], &parsed);
                if (parsed != tokens[i].size() || !std::isfinite(md.delay) || md.delay <= 0)
                    return std::nullopt;
            } catch (const std::exception&) {
                return std::nullopt;
            }
            haveDelay = true;
        } else {
            return std::nullopt;
        }
    }
    if (!haveDelay || !haveFrom || !haveTo)
        return std::nullopt;
    return md;
}

} // namespace sv_cdccheck
