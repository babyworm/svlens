#include "ConventionChecker.h"
#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

#include <optional>
#include <stdexcept>
#include <set>

namespace connect {

namespace {

std::optional<std::string> getString(const YAML::Node& node, const char* key) {
    if (node && node[key])
        return node[key].as<std::string>();
    return std::nullopt;
}

} // namespace

ConventionRules loadConventionRules(const std::string& yamlPath) {
    YAML::Node root = YAML::LoadFile(yamlPath);
    if (!root || !root.IsMap()) {
        throw std::runtime_error(fmt::format(
            "invalid convention file '{}': expected a YAML mapping at the root", yamlPath));
    }

    ConventionRules rules;

    if (auto value = getString(root, "input_prefix"); value)
        rules.inputPrefix = *value;
    else if (auto value = getString(root, "inputPrefix"); value)
        rules.inputPrefix = *value;
    else if (root["input"] && root["input"]["prefix"])
        rules.inputPrefix = root["input"]["prefix"].as<std::string>();

    if (auto value = getString(root, "output_prefix"); value)
        rules.outputPrefix = *value;
    else if (auto value = getString(root, "outputPrefix"); value)
        rules.outputPrefix = *value;
    else if (root["output"] && root["output"]["prefix"])
        rules.outputPrefix = root["output"]["prefix"].as<std::string>();

    if (auto value = getString(root, "instance_prefix"); value)
        rules.instancePrefix = *value;
    else if (auto value = getString(root, "instancePrefix"); value)
        rules.instancePrefix = *value;
    else if (root["instance"] && root["instance"]["prefix"])
        rules.instancePrefix = root["instance"]["prefix"].as<std::string>();

    return rules;
}

ConventionChecker::ConventionChecker(const ConventionRules& rules) : rules_(rules) {}

std::vector<Issue> ConventionChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    // Check port naming conventions
    for (auto& port : graph.allPorts) {
        if (port.direction == slang::ast::ArgumentDirection::In) {
            if (!rules_.inputPrefix.empty() &&
                !port.portName.starts_with(rules_.inputPrefix)) {
                Issue issue;
                issue.type = Issue::Type::CONVENTION;
                issue.severity = Issue::Severity::INFO;
                issue.port = port;
                issue.detail = fmt::format(
                    "input port '{}' does not follow naming convention (expected prefix '{}')",
                    port.portName, rules_.inputPrefix);
                issues.push_back(std::move(issue));
            }
        } else if (port.direction == slang::ast::ArgumentDirection::Out) {
            if (!rules_.outputPrefix.empty() &&
                !port.portName.starts_with(rules_.outputPrefix)) {
                Issue issue;
                issue.type = Issue::Type::CONVENTION;
                issue.severity = Issue::Severity::INFO;
                issue.port = port;
                issue.detail = fmt::format(
                    "output port '{}' does not follow naming convention (expected prefix '{}')",
                    port.portName, rules_.outputPrefix);
                issues.push_back(std::move(issue));
            }
        }
    }

    // Check instance naming conventions
    std::set<std::string> checkedInstances;
    for (auto& port : graph.allPorts) {
        const auto& path = port.instancePath;
        if (checkedInstances.contains(path))
            continue;
        checkedInstances.insert(path);

        // Extract the last component of the instance path (after last '.')
        std::string instName = path;
        auto dotPos = path.rfind('.');
        if (dotPos != std::string::npos) {
            instName = path.substr(dotPos + 1);
        }

        if (!rules_.instancePrefix.empty() &&
            !instName.starts_with(rules_.instancePrefix)) {
            Issue issue;
            issue.type = Issue::Type::CONVENTION;
            issue.severity = Issue::Severity::INFO;
            issue.port = port; // Use first port of this instance as reference
            issue.detail = fmt::format(
                "instance '{}' does not follow naming convention (expected prefix '{}')",
                instName, rules_.instancePrefix);
            issues.push_back(std::move(issue));
        }
    }

    return issues;
}

} // namespace connect
