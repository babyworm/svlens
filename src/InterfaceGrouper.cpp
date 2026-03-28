#include "InterfaceGrouper.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace connect {

InterfaceGrouper::InterfaceGrouper() {
    initProtocols();
}

void InterfaceGrouper::initProtocols() {
    // AXI4 — all required signals across all channels
    protocols_.push_back({"AXI4",
        {"AWVALID", "AWREADY", "AWADDR", "AWLEN", "AWSIZE", "AWBURST",
         "WVALID", "WREADY", "WDATA", "WSTRB", "WLAST",
         "BVALID", "BREADY", "BRESP",
         "ARVALID", "ARREADY", "ARADDR", "ARLEN", "ARSIZE", "ARBURST",
         "RVALID", "RREADY", "RDATA", "RRESP", "RLAST"},
        {"AWVALID", "WVALID", "TVALID"}});

    // AXI4-Lite — simplified (no LEN/SIZE/BURST/LAST)
    protocols_.push_back({"AXI4-Lite",
        {"AWVALID", "AWREADY", "AWADDR",
         "WVALID", "WREADY", "WDATA", "WSTRB",
         "BVALID", "BREADY", "BRESP",
         "ARVALID", "ARREADY", "ARADDR",
         "RVALID", "RREADY", "RDATA", "RRESP"},
        {"AWVALID", "WVALID"}});

    // AXI-Stream
    protocols_.push_back({"AXI-Stream",
        {"TVALID", "TREADY", "TDATA", "TLAST", "TKEEP"},
        {"TVALID"}});

    // AHB
    protocols_.push_back({"AHB",
        {"HSEL", "HADDR", "HTRANS", "HWRITE", "HSIZE", "HBURST",
         "HWDATA", "HRDATA", "HREADY", "HRESP"},
        {"HTRANS"}});

    // APB
    protocols_.push_back({"APB",
        {"PSEL", "PENABLE", "PWRITE", "PADDR", "PWDATA", "PRDATA", "PREADY"},
        {"PSEL", "PENABLE", "PWRITE"}});
}

bool InterfaceGrouper::matchesSuffix(const std::string& portName, const std::string& signal) {
    if (portName.size() < signal.size())
        return false;

    std::string upperPort = portName;
    std::string upperSignal = signal;
    for (auto& c : upperPort) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (auto& c : upperSignal) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    auto pos = upperPort.rfind(upperSignal);
    if (pos == std::string::npos) return false;
    if (pos + upperSignal.size() != upperPort.size()) return false;
    if (pos > 0 && upperPort[pos - 1] != '_') return false;
    return true;
}

std::string InterfaceGrouper::detectPrefix(const std::vector<PortInfo>& ports,
                                           const std::vector<std::string>& matchedSignals) {
    // For each matched port, extract the prefix (everything before the signal suffix)
    std::vector<std::string> prefixes;
    for (const auto& port : ports) {
        for (const auto& sig : matchedSignals) {
            if (matchesSuffix(port.portName, sig)) {
                // The prefix is portName minus the suffix
                std::string upperPort = port.portName;
                std::string upperSig = sig;
                for (auto& c : upperPort) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                for (auto& c : upperSig) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                auto pos = upperPort.rfind(upperSig);
                if (pos != std::string::npos) {
                    prefixes.push_back(port.portName.substr(0, pos));
                }
                break;
            }
        }
    }

    if (prefixes.empty()) return "";

    // Find the most common prefix
    std::map<std::string, int> prefixCounts;
    for (const auto& p : prefixes) {
        prefixCounts[p]++;
    }

    std::string bestPrefix;
    int bestCount = 0;
    for (const auto& [p, count] : prefixCounts) {
        if (count > bestCount) {
            bestCount = count;
            bestPrefix = p;
        }
    }
    return bestPrefix;
}

std::string InterfaceGrouper::detectRole(const std::vector<PortInfo>& ports,
                                         const std::vector<std::string>& validSignals) {
    // Check direction of *VALID signals: Out → master, In → slave
    for (const auto& port : ports) {
        for (const auto& validSig : validSignals) {
            if (matchesSuffix(port.portName, validSig)) {
                if (port.direction == slang::ast::ArgumentDirection::Out) {
                    return "master";
                } else if (port.direction == slang::ast::ArgumentDirection::In) {
                    return "slave";
                }
            }
        }
    }
    return "unknown";
}

std::vector<InterfaceGroup> InterfaceGrouper::classify(const ConnectionGraph& graph) const {
    std::vector<InterfaceGroup> result;

    // Group ports by instance path
    std::map<std::string, std::vector<PortInfo>> instancePorts;
    for (const auto& port : graph.allPorts) {
        instancePorts[port.instancePath].push_back(port);
    }

    for (const auto& [instPath, ports] : instancePorts) {
        // Track AXI4 match ratio for superset disambiguation with AXI4-Lite
        bool axi4Matched = false;
        double axi4Ratio = 0.0;

        for (const auto& proto : protocols_) {
            // Count how many key signals match
            std::vector<std::string> matched;
            for (const auto& sig : proto.keySignals) {
                for (const auto& port : ports) {
                    if (matchesSuffix(port.portName, sig)) {
                        matched.push_back(sig);
                        break;
                    }
                }
            }

            // 60% threshold
            double ratio = static_cast<double>(matched.size()) /
                           static_cast<double>(proto.keySignals.size());
            if (ratio < 0.6) {
                continue;
            }

            if (proto.name == "AXI4") {
                axi4Matched = true;
                axi4Ratio = ratio;
            }

            // AXI4 superset rule: when both AXI4 and AXI4-Lite match the same
            // instance, keep only the one with the higher match ratio.
            if (proto.name == "AXI4-Lite" && axi4Matched) {
                if (axi4Ratio >= ratio) {
                    // AXI4 is a better or equal fit — suppress AXI4-Lite
                    continue;
                } else {
                    // AXI4-Lite is a better fit — remove the previously added AXI4 entry
                    result.erase(
                        std::remove_if(result.begin(), result.end(),
                            [&instPath](const InterfaceGroup& g) {
                                return g.instancePath == instPath && g.protocol == "AXI4";
                            }),
                        result.end());
                }
            }

            // Detect prefix from matched ports
            std::string prefix = detectPrefix(ports, matched);

            // Collect all ports that match any key signal OR share the prefix
            std::vector<PortInfo> groupPorts;
            std::set<std::string> addedPorts;

            // First: add ports matching protocol signals
            for (const auto& port : ports) {
                for (const auto& sig : proto.keySignals) {
                    if (matchesSuffix(port.portName, sig)) {
                        if (addedPorts.insert(port.portName).second) {
                            groupPorts.push_back(port);
                        }
                        break;
                    }
                }
            }

            // Second: add ports with the same prefix that weren't already matched
            if (!prefix.empty()) {
                for (const auto& port : ports) {
                    if (addedPorts.count(port.portName) == 0) {
                        // Case-insensitive prefix check
                        std::string upperName = port.portName;
                        std::string upperPrefix = prefix;
                        for (auto& c : upperName)
                            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        for (auto& c : upperPrefix)
                            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        if (upperName.size() >= upperPrefix.size() &&
                            upperName.substr(0, upperPrefix.size()) == upperPrefix) {
                            groupPorts.push_back(port);
                            addedPorts.insert(port.portName);
                        }
                    }
                }
            }

            // Detect role
            std::string role = detectRole(ports, proto.validSignals);

            InterfaceGroup group;
            group.instancePath = instPath;
            group.protocol = proto.name;
            group.role = role;
            group.prefix = prefix;
            group.matchedPorts = std::move(groupPorts);
            result.push_back(std::move(group));
        }
    }

    return result;
}

} // namespace connect
