#include "sv-cdccheck/waiver.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace sv_cdccheck {

bool WaiverManager::loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    parseYaml(content);
    return true;
}

bool WaiverManager::loadString(const std::string& yaml_content) {
    parseYaml(yaml_content);
    return !waivers_.empty();
}

void WaiverManager::parseYaml(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    WaiverEntry current;
    bool in_entry = false;

    auto trimmed = [](const std::string& s) -> std::string {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    };

    auto unquote = [](const std::string& s) -> std::string {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'')
            return s.substr(1, s.size() - 2);
        return s;
    };

    while (std::getline(stream, line)) {
        std::string trimline = trimmed(line);

        // Skip comments and empty lines
        if (trimline.empty() || trimline[0] == '#')
            continue;

        // New list entry marker
        if (trimline.starts_with("- ")) {
            // Save previous entry if valid
            if (in_entry && !current.id.empty()) {
                waivers_.push_back(std::move(current));
                current = WaiverEntry{};
            }
            in_entry = true;

            // Parse key-value on same line as dash
            std::string after_dash = trimmed(trimline.substr(2));
            auto colon = after_dash.find(':');
            if (colon != std::string::npos) {
                std::string key = trimmed(after_dash.substr(0, colon));
                std::string val = unquote(trimmed(after_dash.substr(colon + 1)));
                if (key == "id") current.id = val;
                else if (key == "crossing") current.crossing = val;
                else if (key == "pattern") current.pattern = val;
                else if (key == "reason") current.reason = val;
                else if (key == "owner") current.owner = val;
                else if (key == "date") current.date = val;
            }
            continue;
        }

        // Continuation key-value within an entry
        if (in_entry) {
            auto colon = trimline.find(':');
            if (colon != std::string::npos) {
                std::string key = trimmed(trimline.substr(0, colon));
                std::string val = unquote(trimmed(trimline.substr(colon + 1)));
                if (key == "id") current.id = val;
                else if (key == "crossing") current.crossing = val;
                else if (key == "pattern") current.pattern = val;
                else if (key == "reason") current.reason = val;
                else if (key == "owner") current.owner = val;
                else if (key == "date") current.date = val;
            }
        }
    }

    // Don't forget the last entry
    if (in_entry && !current.id.empty()) {
        waivers_.push_back(std::move(current));
    }
}

bool WaiverManager::isWaived(const std::string& source_signal,
                              const std::string& dest_signal) const {
    return findWaiver(source_signal, dest_signal).has_value();
}

std::optional<WaiverEntry> WaiverManager::findWaiver(
    const std::string& source_signal,
    const std::string& dest_signal) const
{
    for (auto& w : waivers_) {
        // Check exact crossing match
        if (!w.crossing.empty()) {
            if (matchCrossing(w.crossing, source_signal, dest_signal))
                return w;
        }
        // Check pattern match (matches either source or dest)
        if (!w.pattern.empty()) {
            if (matchPattern(w.pattern, source_signal) ||
                matchPattern(w.pattern, dest_signal))
                return w;
        }
    }
    return std::nullopt;
}

bool WaiverManager::matchCrossing(const std::string& crossing,
                                    const std::string& source,
                                    const std::string& dest) {
    // Format: "source -> dest"
    auto arrow = crossing.find("->");
    if (arrow == std::string::npos) return false;

    auto trimmed = [](const std::string& s) -> std::string {
        auto start = s.find_first_not_of(" \t");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t");
        return s.substr(start, end - start + 1);
    };

    std::string cross_src = trimmed(crossing.substr(0, arrow));
    std::string cross_dst = trimmed(crossing.substr(arrow + 2));

    return cross_src == source && cross_dst == dest;
}

bool WaiverManager::matchPattern(const std::string& pattern,
                                   const std::string& signal) {
    // Simple glob matching: only supports trailing '*'
    // e.g., "top.u_debug.*" matches "top.u_debug.foo"
    if (pattern.empty()) return false;

    // Check for trailing wildcard
    if (pattern.back() == '*') {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        return signal.starts_with(prefix);
    }

    // Exact match
    return pattern == signal;
}

} // namespace sv_cdccheck
