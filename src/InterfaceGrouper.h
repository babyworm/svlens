#pragma once
#include "ConnectionGraph.h"
#include <string>
#include <vector>

namespace connect {

struct InterfaceGroup {
    std::string instancePath;   // "top.u_master"
    std::string protocol;       // "AXI4", "AXI4-Lite", "AXI-Stream", "AHB", "APB"
    std::string role;           // "master" or "slave" or "unknown"
    std::string prefix;         // common prefix e.g. "m_axi_"
    std::vector<PortInfo> matchedPorts;
};

class InterfaceGrouper {
public:
    InterfaceGrouper();
    std::vector<InterfaceGroup> classify(const ConnectionGraph& graph) const;

private:
    struct ProtocolSpec {
        std::string name;
        std::vector<std::string> keySignals;       // all key signals across channels
        std::vector<std::string> validSignals;      // *VALID signals for role detection
    };

    std::vector<ProtocolSpec> protocols_;
    void initProtocols();
    static bool matchesSuffix(const std::string& portName, const std::string& signal);
    static std::string detectPrefix(const std::vector<PortInfo>& ports,
                                    const std::vector<std::string>& matchedSignals);
    static std::string detectRole(const std::vector<PortInfo>& ports,
                                  const std::vector<std::string>& validSignals);
};

} // namespace connect
