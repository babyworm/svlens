#pragma once

#include "Checker.h"

#include <regex>
#include <string>
#include <vector>

namespace connect {

struct UserRule {
    std::string id;
    std::string description;
    std::string target;
    std::regex expression;
    bool forbidden = false;
    Issue::Severity severity = Issue::Severity::WARN;
};

std::vector<UserRule> loadUserRules(const std::string& yamlPath);

class UserChecker : public IChecker {
public:
    explicit UserChecker(std::vector<UserRule> rules) : rules_(std::move(rules)) {}
    std::vector<Issue> check(const ConnectionGraph& graph) const override;

private:
    std::vector<UserRule> rules_;
};

} // namespace connect
