#pragma once
#include "Checker.h"
#include <string>
#include <vector>

namespace connect {

struct ExpectRule {
    std::string from;
    std::string to;
};

class ExpectChecker : public IChecker {
public:
    explicit ExpectChecker(const std::string& yamlPath);
    std::vector<Issue> check(const ConnectionGraph& graph) const override;

private:
    std::vector<ExpectRule> expected_;
    std::vector<ExpectRule> forbidden_;
};

} // namespace connect
