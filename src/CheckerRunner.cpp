#include "CheckerRunner.h"

namespace connect {
void CheckerRunner::addChecker(std::unique_ptr<IChecker> checker) {
    checkers_.push_back(std::move(checker));
}

std::vector<Issue> CheckerRunner::runAll(const ConnectionGraph& graph) const {
    std::vector<Issue> allIssues;
    for (auto& checker : checkers_) {
        auto issues = checker->check(graph);
        allIssues.insert(allIssues.end(),
                         std::make_move_iterator(issues.begin()),
                         std::make_move_iterator(issues.end()));
    }
    return allIssues;
}
} // namespace connect
