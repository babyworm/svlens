#pragma once
#include "Checker.h"
#include <memory>
#include <vector>

namespace connect {
class CheckerRunner {
public:
    void addChecker(std::unique_ptr<IChecker> checker);
    std::vector<Issue> runAll(const ConnectionGraph& graph) const;
private:
    std::vector<std::unique_ptr<IChecker>> checkers_;
};
} // namespace connect
