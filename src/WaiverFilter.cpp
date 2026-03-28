#include "WaiverFilter.h"
#include "GlobUtil.h"
#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

namespace connect {
WaiverFilter::WaiverFilter(const std::string& yamlPath) {
    try {
        YAML::Node root = YAML::LoadFile(yamlPath);
        if (!root["waivers"] || !root["waivers"].IsSequence()) return;
        for (const auto& node : root["waivers"]) {
            if (!node.IsMap()) continue;
            WaiverRule rule;
            rule.pattern = node["pattern"].as<std::string>("");
            rule.type = node["type"].as<std::string>("*");
            rule.reason = node["reason"].as<std::string>("");
            rule.source = node["source"].as<std::string>("");
            rules_.push_back(std::move(rule));
        }
    } catch (const YAML::Exception& e) {
        fmt::print(stderr, "Warning: failed to load waiver file '{}': {}\n",
                   yamlPath, e.what());
    }
}

WaiverFilter::WaiverResult WaiverFilter::apply(const std::vector<Issue>& issues) const {
    WaiverResult result;
    for (auto& issue : issues) {
        bool waived = false;
        std::string fullPath = issue.port.fullPath();
        for (auto& rule : rules_) {
            bool typeMatch = (rule.type == "*") || (rule.type == Issue::typeToString(issue.type));
            bool pathMatch;
            if (!rule.source.empty())
                pathMatch = (fullPath == rule.source);
            else
                pathMatch = globMatch(rule.pattern, fullPath);
            if (typeMatch && pathMatch) { waived = true; break; }
        }
        if (waived) result.waived.push_back(issue);
        else result.active.push_back(issue);
    }
    return result;
}
} // namespace connect
