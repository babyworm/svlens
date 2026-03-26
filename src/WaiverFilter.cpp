#include "WaiverFilter.h"
#include <yaml-cpp/yaml.h>

namespace connect {
WaiverFilter::WaiverFilter(const std::string& yamlPath) {
    YAML::Node root = YAML::LoadFile(yamlPath);
    if (!root["waivers"]) return;
    for (const auto& node : root["waivers"]) {
        WaiverRule rule;
        rule.pattern = node["pattern"].as<std::string>("");
        rule.type = node["type"].as<std::string>("*");
        rule.reason = node["reason"].as<std::string>("");
        rules_.push_back(std::move(rule));
    }
}

bool WaiverFilter::globMatch(const std::string& pattern, const std::string& text) {
    size_t pi = 0, ti = 0;
    size_t starP = std::string::npos, starT = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == text[ti] || pattern[pi] == '?')) {
            ++pi; ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starP = pi++; starT = ti;
        } else if (starP != std::string::npos) {
            pi = starP + 1; ti = ++starT;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

WaiverFilter::WaiverResult WaiverFilter::apply(const std::vector<Issue>& issues) const {
    WaiverResult result;
    for (auto& issue : issues) {
        bool waived = false;
        std::string fullPath = issue.port.fullPath();
        for (auto& rule : rules_) {
            bool typeMatch = (rule.type == "*") || (rule.type == Issue::typeToString(issue.type));
            bool pathMatch = globMatch(rule.pattern, fullPath);
            if (typeMatch && pathMatch) { waived = true; break; }
        }
        if (waived) result.waived.push_back(issue);
        else result.active.push_back(issue);
    }
    return result;
}
} // namespace connect
