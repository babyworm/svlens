#pragma once

#include "ConnectionGraph.h"

#include <string_view>

namespace connect {

struct GraphFilterOptions {
    bool ignoreTieOff = false;
    bool ignoreNc = false;
};

bool isLikelyNoConnectPort(std::string_view portName);

ConnectionGraph applyGraphFilters(const ConnectionGraph& graph,
                                  const GraphFilterOptions& options);

} // namespace connect
