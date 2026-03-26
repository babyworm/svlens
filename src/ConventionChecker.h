#pragma once
#include "Checker.h"
#include <string>

namespace connect {

struct ConventionRules {
    std::string inputPrefix = "i_";
    std::string outputPrefix = "o_";
    std::string instancePrefix = "u_";
};

class ConventionChecker : public IChecker {
public:
    ConventionChecker() = default;
    explicit ConventionChecker(const ConventionRules& rules);
    std::vector<Issue> check(const ConnectionGraph& graph) const override;
private:
    ConventionRules rules_;
};

} // namespace connect
