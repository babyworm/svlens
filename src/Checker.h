#pragma once
#include "ConnectionGraph.h"
#include "Issue.h"
#include <vector>

namespace connect {
class IChecker {
public:
    virtual ~IChecker() = default;
    virtual std::vector<Issue> check(const ConnectionGraph& graph) const = 0;
};
} // namespace connect
