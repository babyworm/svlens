#include "ConventionChecker.h"
#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

#include <optional>
#include <regex>
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

    // Round 36 lowRISC-style fields. All optional; missing keys leave
    // the field empty/false, which disables the corresponding check.
    if (auto value = getString(root, "clock_pattern"); value)
        rules.clockPattern = *value;
    if (auto value = getString(root, "reset_pattern"); value)
        rules.resetPattern = *value;
    if (auto value = getString(root, "active_low_suffix"); value)
        rules.activeLowSuffix = *value;
    if (root["lowercase_port_names"] &&
        root["lowercase_port_names"].IsScalar())
        rules.lowercasePortNames =
            root["lowercase_port_names"].as<bool>();
    if (auto value = getString(root, "parameter_case_pattern"); value)
        rules.parameterCasePattern = *value;
    if (auto value = getString(root, "typedef_suffix_pattern"); value)
        rules.typedefSuffixPattern = *value;

    return rules;
}

ConventionChecker::ConventionChecker(const ConventionRules& rules) : rules_(rules) {}

std::vector<Issue> ConventionChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    // Round 36 lowRISC-style: pre-compile regexes once. Invalid
    // patterns are silently ignored (the check is skipped) so a
    // malformed YAML never aborts conn analysis. A separate
    // diagnostic is emitted as a single CONVENTION INFO entry.
    auto tryCompile = [&issues](const std::string& pattern,
                                const char* label) -> std::optional<std::regex> {
        if (pattern.empty())
            return std::nullopt;
        try {
            return std::regex(pattern);
        } catch (const std::regex_error& e) {
            Issue issue;
            issue.type = Issue::Type::CONVENTION;
            issue.severity = Issue::Severity::INFO;
            issue.detail = fmt::format(
                "convention rule '{}' has invalid regex '{}': {}",
                label, pattern, e.what());
            issues.push_back(std::move(issue));
            return std::nullopt;
        }
    };
    auto clockRe = tryCompile(rules_.clockPattern, "clock_pattern");
    auto resetRe = tryCompile(rules_.resetPattern, "reset_pattern");

    // Check port naming conventions
    for (auto& port : graph.allPorts) {
        // Round 36: lowRISC-style auxiliary checks fire BEFORE the
        // direction-prefix check so a name that matches a clock or
        // reset pattern is excused from the i_/o_ requirement.
        bool isClockLike = clockRe && std::regex_match(port.portName, *clockRe);
        bool isResetLike = resetRe && std::regex_match(port.portName, *resetRe);

        if (rules_.lowercasePortNames) {
            for (char c : port.portName) {
                if (c >= 'A' && c <= 'Z') {
                    Issue issue;
                    issue.type = Issue::Type::CONVENTION;
                    issue.severity = Issue::Severity::INFO;
                    issue.port = port;
                    issue.detail = fmt::format(
                        "port '{}' is not lowercase (lowRISC-style requires "
                        "snake_case)", port.portName);
                    issues.push_back(std::move(issue));
                    break;
                }
            }
        }

        // Active-low suffix: a port whose name ends with the suffix
        // should be a reset OR an explicit active-low signal.
        // Heuristic: just verify the suffix appears on at least the
        // declared reset; we cannot detect intent without semantic
        // info, so emit only when activeLowSuffix is configured AND
        // the port is in `In` direction yet does NOT match either
        // the clock or reset patterns -- a defensive informational
        // hint.
        if (!rules_.activeLowSuffix.empty() &&
            port.direction == slang::ast::ArgumentDirection::In &&
            port.portName.ends_with(rules_.activeLowSuffix) &&
            !isResetLike && !isClockLike) {
            Issue issue;
            issue.type = Issue::Type::CONVENTION;
            issue.severity = Issue::Severity::INFO;
            issue.port = port;
            issue.detail = fmt::format(
                "input port '{}' has active-low suffix '{}' but does not "
                "match the configured reset/clock pattern -- verify intent",
                port.portName, rules_.activeLowSuffix);
            issues.push_back(std::move(issue));
        }

        // Direction-prefix: skip when the port is a recognized
        // clock or reset (those have their own naming pattern).
        if (port.direction == slang::ast::ArgumentDirection::In) {
            if (isClockLike || isResetLike)
                continue;
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
