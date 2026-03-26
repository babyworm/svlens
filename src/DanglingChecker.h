#pragma once
#include "Checker.h"
namespace connect {
class DanglingChecker : public IChecker {
public:
    std::vector<Issue> check(const ConnectionGraph& graph) const override;
};
} // namespace connect
