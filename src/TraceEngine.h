#pragma once

#include "ConnectionGraph.h"

#include <string>
#include <vector>

namespace connect {

struct TraceHop {
    Connection connection;
    int depth;
};

class TraceEngine {
public:
    explicit TraceEngine(const ConnectionGraph& graph);

    /// Trace forward from output port (fan-out): follow source -> dest chains.
    std::vector<TraceHop> traceFanOut(const std::string& portPattern,
                                      int maxDepth = 16) const;

    /// Trace backward to input port (fan-in): follow dest <- source chains.
    std::vector<TraceHop> traceFanIn(const std::string& portPattern,
                                     int maxDepth = 16) const;

    /// Format trace results for display.
    /// @param fanOut true for fan-out (->), false for fan-in (<-)
    static std::string formatTrace(const std::vector<TraceHop>& hops,
                                   const std::string& pattern,
                                   bool fanOut);

private:
    const ConnectionGraph& graph_;

    static bool globMatch(const std::string& pattern, const std::string& text);
};

} // namespace connect
