#include "DotReport.h"
#include <fmt/format.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace connect {

namespace {

// Strip the top module prefix from an instance path and replace dots with
// underscores to produce a valid DOT node identifier.
// "soc_top.u_cpu" -> "u_cpu", "soc_top.u_bus.sub" -> "u_bus_sub"
std::string nodeId(const std::string& instancePath, const std::string& topModule) {
    std::string stripped = instancePath;
    std::string prefix = topModule + ".";
    if (stripped.rfind(prefix, 0) == 0) {
        stripped = stripped.substr(prefix.size());
    }
    std::string id;
    id.reserve(stripped.size());
    for (char c : stripped) {
        id += (c == '.') ? '_' : c;
    }
    return id;
}

// Escape a string for use inside DOT double-quoted labels.
std::string escapeDot(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') {
            result += '\\';
        }
        result += c;
    }
    return result;
}

// Key for grouping connections between the same pair of instances.
struct InstancePair {
    std::string srcInstance;
    std::string dstInstance;

    bool operator<(const InstancePair& o) const {
        if (srcInstance != o.srcInstance) return srcInstance < o.srcInstance;
        return dstInstance < o.dstInstance;
    }
};

// Determine the worst severity among issues associated with a set of
// connections between an instance pair.
enum class EdgeColor { BLACK, ORANGE, RED };

struct EdgeInfo {
    std::vector<std::string> signalLabels;
    EdgeColor color = EdgeColor::BLACK;
};

} // anonymous namespace

void DotReportGenerator::generate(const ReportData& data, std::ostream& out) const {
    // Collect unique instance nodes (excluding the top module itself).
    std::set<std::string> instances;
    for (const auto& conn : data.graph.connections) {
        if (conn.source.instancePath != data.topModule)
            instances.insert(conn.source.instancePath);
        if (conn.dest.instancePath != data.topModule)
            instances.insert(conn.dest.instancePath);
    }

    // Build a lookup: connection (by full source/dest path) -> worst severity.
    // We map each connection to its worst issue severity.
    std::map<std::string, Issue::Severity> connSeverity;
    for (const auto& issue : data.active) {
        if (!issue.connection.has_value()) continue;
        const auto& c = issue.connection.value();
        std::string key = c.source.fullPath() + " -> " + c.dest.fullPath();
        auto it = connSeverity.find(key);
        if (it == connSeverity.end()) {
            connSeverity[key] = issue.severity;
        } else {
            // ERROR > WARN > INFO
            if (issue.severity < it->second) {
                it->second = issue.severity;
            }
        }
    }

    // Group connections by (source_instance, dest_instance).
    std::map<InstancePair, EdgeInfo> edgeGroups;
    for (const auto& conn : data.graph.connections) {
        InstancePair pair{conn.source.instancePath, conn.dest.instancePath};
        auto& info = edgeGroups[pair];

        // Build signal label: "portName [width]"
        std::string label = fmt::format("{} [{}]", conn.source.portName, conn.source.width);
        info.signalLabels.push_back(label);

        // Check if this connection has an associated issue.
        std::string key = conn.source.fullPath() + " -> " + conn.dest.fullPath();
        auto sevIt = connSeverity.find(key);
        if (sevIt != connSeverity.end()) {
            EdgeColor newColor = EdgeColor::BLACK;
            if (sevIt->second == Issue::Severity::ERROR) {
                newColor = EdgeColor::RED;
            } else if (sevIt->second == Issue::Severity::WARN) {
                newColor = EdgeColor::ORANGE;
            }
            // Promote to worst color: RED > ORANGE > BLACK
            if (newColor < info.color) {
                // enum ordering: BLACK=0, ORANGE=1, RED=2 -- we need RED to win
            }
            if (static_cast<int>(newColor) > static_cast<int>(info.color)) {
                info.color = newColor;
            }
        }
    }

    // Emit DOT output.
    out << fmt::format("digraph \"{}\" {{\n", escapeDot(data.topModule));
    out << "    rankdir=LR;\n";
    out << "    node [shape=record];\n";
    out << "\n";

    // Emit nodes.
    for (const auto& inst : instances) {
        std::string id = nodeId(inst, data.topModule);
        std::string label = id; // Use the stripped name as the display label
        out << fmt::format("    {} [label=\"{}\"];\n", id, escapeDot(label));
    }

    if (!instances.empty() && !edgeGroups.empty()) {
        out << "\n";
    }

    // Emit edges.
    for (const auto& [pair, info] : edgeGroups) {
        std::string srcId = nodeId(pair.srcInstance, data.topModule);
        std::string dstId = nodeId(pair.dstInstance, data.topModule);

        // Build combined label from all signals.
        std::string label;
        for (size_t i = 0; i < info.signalLabels.size(); ++i) {
            if (i > 0) label += "\\n";
            label += info.signalLabels[i];
        }

        std::string colorStr;
        switch (info.color) {
            case EdgeColor::RED:    colorStr = "red"; break;
            case EdgeColor::ORANGE: colorStr = "orange"; break;
            case EdgeColor::BLACK:  colorStr = "black"; break;
        }

        out << fmt::format("    {} -> {} [label=\"{}\" color=\"{}\"];\n",
                           srcId, dstId, escapeDot(label), colorStr);
    }

    out << "}\n";
}

} // namespace connect
