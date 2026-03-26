#include "ConventionChecker.h"
#include <fmt/core.h>
#include <set>

namespace connect {

ConventionChecker::ConventionChecker(const ConventionRules& rules) : rules_(rules) {}

std::vector<Issue> ConventionChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    // Check port naming conventions
    for (auto& port : graph.allPorts) {
        if (port.direction == slang::ast::ArgumentDirection::In) {
            if (!rules_.inputPrefix.empty() &&
                port.portName.substr(0, rules_.inputPrefix.size()) != rules_.inputPrefix) {
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
                port.portName.substr(0, rules_.outputPrefix.size()) != rules_.outputPrefix) {
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
        if (checkedInstances.count(path))
            continue;
        checkedInstances.insert(path);

        // Extract the last component of the instance path (after last '.')
        std::string instName = path;
        auto dotPos = path.rfind('.');
        if (dotPos != std::string::npos) {
            instName = path.substr(dotPos + 1);
        }

        if (!rules_.instancePrefix.empty() &&
            instName.substr(0, rules_.instancePrefix.size()) != rules_.instancePrefix) {
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
