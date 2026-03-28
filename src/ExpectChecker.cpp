#include "ExpectChecker.h"
#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

namespace connect {

ExpectChecker::ExpectChecker(const std::string& yamlPath) {
    YAML::Node root = YAML::LoadFile(yamlPath);

    if (root["expected"]) {
        for (const auto& node : root["expected"]) {
            ExpectRule rule;
            rule.from = node["from"].as<std::string>();
            rule.to = node["to"].as<std::string>();
            expected_.push_back(std::move(rule));
        }
    }

    if (root["forbidden"]) {
        for (const auto& node : root["forbidden"]) {
            ExpectRule rule;
            rule.from = node["from"].as<std::string>();
            rule.to = node["to"].as<std::string>();
            forbidden_.push_back(std::move(rule));
        }
    }
}

bool ExpectChecker::globMatch(const std::string& pattern, const std::string& text) {
    // Simple glob: '*' matches any sequence of characters (including dots)
    size_t pi = 0, ti = 0;
    size_t starP = std::string::npos, starT = 0;

    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == text[ti] || pattern[pi] == '?')) {
            ++pi;
            ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starP = pi;
            starT = ti;
            ++pi;
        } else if (starP != std::string::npos) {
            pi = starP + 1;
            ++starT;
            ti = starT;
        } else {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
    }

    return pi == pattern.size();
}

std::vector<Issue> ExpectChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    // Check expected connections: each rule must match at least one connection
    for (const auto& rule : expected_) {
        bool found = false;
        for (const auto& conn : graph.connections) {
            if (globMatch(rule.from, conn.source.fullPath()) &&
                globMatch(rule.to, conn.dest.fullPath())) {
                found = true;
                break;
            }
        }
        if (!found) {
            Issue issue;
            issue.type = Issue::Type::EXPECT_MISSING;
            issue.severity = Issue::Severity::ERROR;
            issue.port = {};
            issue.port.instancePath = "expect";
            issue.port.portName = rule.from;
            issue.port.direction = slang::ast::ArgumentDirection::Out;
            issue.detail = fmt::format(
                "Expected connection not found: {} -> {}", rule.from, rule.to);
            issues.push_back(std::move(issue));
        }
    }

    // Check forbidden connections: no connection may match any forbidden rule
    for (const auto& conn : graph.connections) {
        for (const auto& rule : forbidden_) {
            if (globMatch(rule.from, conn.source.fullPath()) &&
                globMatch(rule.to, conn.dest.fullPath())) {
                Issue issue;
                issue.type = Issue::Type::EXPECT_FORBIDDEN;
                issue.severity = Issue::Severity::ERROR;
                issue.port = conn.source;
                issue.connection = conn;
                issue.detail = fmt::format(
                    "Forbidden connection found: {} -> {} (matches rule {} -> {})",
                    conn.source.fullPath(), conn.dest.fullPath(),
                    rule.from, rule.to);
                issues.push_back(std::move(issue));
                break; // one issue per connection is enough
            }
        }
    }

    return issues;
}

} // namespace connect
