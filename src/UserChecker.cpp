#include "UserChecker.h"

#include <yaml-cpp/yaml.h>

#include <set>
#include <stdexcept>

namespace connect {

std::vector<UserRule> loadUserRules(const std::string& yamlPath) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(yamlPath);
    } catch (const YAML::Exception& error) {
        throw std::runtime_error("invalid user rules file '" + yamlPath + "': " + error.what());
    }
    if (!root.IsMap() || !root["checkers"].IsSequence())
        throw std::runtime_error("user rules file needs a checkers sequence");

    std::vector<UserRule> rules;
    std::set<std::string> ids;
    for (const auto& entry : root["checkers"]) {
        if (!entry.IsMap() || !entry["id"] || !entry["target"] || !entry["description"] || !entry["severity"])
            throw std::runtime_error("each user checker needs id, target, description, and severity");
        UserRule rule;
        try {
            rule.id = entry["id"].as<std::string>();
            rule.target = entry["target"].as<std::string>();
            rule.description = entry["description"].as<std::string>();
            const auto severity = entry["severity"].as<std::string>();
            if (severity == "error")
                rule.severity = Issue::Severity::ERROR;
            else if (severity == "warning")
                rule.severity = Issue::Severity::WARN;
            else if (severity == "info")
                rule.severity = Issue::Severity::INFO;
            else
                throw std::runtime_error("invalid severity for user checker " + rule.id);
            if (rule.target != "module" && rule.target != "instance" && rule.target != "signal" &&
                rule.target != "port")
                throw std::runtime_error("invalid target for user checker " + rule.id);
            if (rule.id.empty() || !ids.insert(rule.id).second)
                throw std::runtime_error("empty or duplicate user checker id: " + rule.id);
            if (static_cast<bool>(entry["pattern"]) == static_cast<bool>(entry["forbidden"]))
                throw std::runtime_error("user checker needs exactly one of pattern or forbidden: " + rule.id);
            rule.forbidden = static_cast<bool>(entry["forbidden"]);
            rule.expression = std::regex(entry[rule.forbidden ? "forbidden" : "pattern"].as<std::string>());
        } catch (const YAML::Exception& error) {
            throw std::runtime_error("invalid user checker YAML: " + std::string(error.what()));
        } catch (const std::regex_error& error) {
            throw std::runtime_error("invalid user checker regex: " + std::string(error.what()));
        }
        rules.push_back(std::move(rule));
    }
    return rules;
}

std::vector<Issue> UserChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;
    auto inspect = [&](const UserRule& rule, const std::string& name, const PortInfo& port) {
        const bool matches = std::regex_match(name, rule.expression);
        if (rule.forbidden != matches)
            return;
        Issue issue;
        issue.type = Issue::Type::CONVENTION;
        issue.severity = rule.severity;
        issue.port = port;
        issue.detail = rule.description + ": " + name;
        issue.ruleId = rule.id;
        issues.push_back(std::move(issue));
    };

    for (const auto& rule : rules_) {
        if (rule.target == "port") {
            for (const auto& port : graph.allPorts)
                inspect(rule, port.portName, port);
            continue;
        }
        const auto& declarations = rule.target == "module"     ? graph.modules
                                   : rule.target == "instance" ? graph.instances
                                                               : graph.signals;
        std::set<std::string> seenModules;
        for (const auto& declaration : declarations) {
            if (rule.target == "module" && !seenModules.insert(declaration.name).second)
                continue;
            PortInfo port;
            port.instancePath = declaration.scopePath;
            port.portName = declaration.name;
            port.direction = slang::ast::ArgumentDirection::InOut;
            port.location = declaration.location;
            inspect(rule, declaration.name, port);
        }
    }
    return issues;
}

} // namespace connect
