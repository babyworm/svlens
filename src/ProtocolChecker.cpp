#include "ProtocolChecker.h"
#include <algorithm>
#include <cctype>
#include <fmt/core.h>
#include <map>
#include <set>

namespace connect {

ProtocolChecker::ProtocolChecker() {
    initProtocols();
}

void ProtocolChecker::initProtocols() {
    // AXI4
    {
        ProtocolDef axi4;
        axi4.name = "AXI4";
        axi4.channels = {
            {"AW", {"AWVALID", "AWREADY", "AWADDR", "AWLEN", "AWSIZE", "AWBURST"},
                   {"AWID", "AWLOCK", "AWCACHE", "AWPROT", "AWQOS", "AWREGION", "AWUSER"}},
            {"W",  {"WVALID", "WREADY", "WDATA", "WSTRB", "WLAST"},
                   {"WUSER"}},
            {"B",  {"BVALID", "BREADY", "BRESP"},
                   {"BID", "BUSER"}},
            {"AR", {"ARVALID", "ARREADY", "ARADDR", "ARLEN", "ARSIZE", "ARBURST"},
                   {"ARID", "ARLOCK", "ARCACHE", "ARPROT", "ARQOS", "ARREGION", "ARUSER"}},
            {"R",  {"RVALID", "RREADY", "RDATA", "RRESP", "RLAST"},
                   {"RID", "RUSER"}}
        };
        protocols_.push_back(std::move(axi4));
    }

    // AXI4-Lite
    {
        ProtocolDef axilite;
        axilite.name = "AXI4-Lite";
        axilite.channels = {
            {"AW", {"AWVALID", "AWREADY", "AWADDR"}, {}},
            {"W",  {"WVALID", "WREADY", "WDATA", "WSTRB"}, {}},
            {"B",  {"BVALID", "BREADY", "BRESP"}, {}},
            {"AR", {"ARVALID", "ARREADY", "ARADDR"}, {}},
            {"R",  {"RVALID", "RREADY", "RDATA", "RRESP"}, {}}
        };
        protocols_.push_back(std::move(axilite));
    }

    // AXI-Stream
    {
        ProtocolDef axis;
        axis.name = "AXI-Stream";
        axis.channels = {
            {"data", {"TVALID", "TREADY", "TDATA"},
                     {"TLAST", "TKEEP", "TSTRB", "TID", "TDEST", "TUSER"}}
        };
        protocols_.push_back(std::move(axis));
    }

    // AHB
    {
        ProtocolDef ahb;
        ahb.name = "AHB";
        ahb.channels = {
            {"main", {"HSEL", "HADDR", "HTRANS", "HWRITE", "HSIZE", "HBURST",
                      "HWDATA", "HRDATA", "HREADY", "HRESP"},
                     {"HPROT", "HMASTLOCK", "HBUSREQ", "HGRANT"}}
        };
        protocols_.push_back(std::move(ahb));
    }

    // APB
    {
        ProtocolDef apb;
        apb.name = "APB";
        apb.channels = {
            {"main", {"PSEL", "PENABLE", "PWRITE", "PADDR", "PWDATA", "PRDATA", "PREADY"},
                     {"PSLVERR", "PPROT", "PSTRB"}}
        };
        protocols_.push_back(std::move(apb));
    }
}

bool ProtocolChecker::matchesSuffix(const std::string& portName, const std::string& signal) {
    if (portName.size() < signal.size())
        return false;
    // Case-insensitive suffix match
    auto portIt = portName.rbegin();
    auto sigIt = signal.rbegin();
    for (; sigIt != signal.rend(); ++portIt, ++sigIt) {
        if (std::toupper(static_cast<unsigned char>(*portIt)) !=
            std::toupper(static_cast<unsigned char>(*sigIt))) {
            return false;
        }
    }
    return true;
}

std::vector<Issue> ProtocolChecker::check(const ConnectionGraph& graph) const {
    std::vector<Issue> issues;

    // Group ports by instance path
    std::map<std::string, std::vector<const PortInfo*>> instancePorts;
    for (auto& port : graph.allPorts) {
        instancePorts[port.instancePath].push_back(&port);
    }

    // For each instance, check each protocol
    for (auto& [instPath, ports] : instancePorts) {
        for (auto& proto : protocols_) {
            for (auto& channel : proto.channels) {
                std::vector<std::string> foundRequired;
                std::vector<std::string> missingRequired;

                for (auto& sig : channel.required) {
                    bool found = false;
                    for (auto* port : ports) {
                        if (matchesSuffix(port->portName, sig)) {
                            found = true;
                            break;
                        }
                    }
                    if (found) {
                        foundRequired.push_back(sig);
                    } else {
                        missingRequired.push_back(sig);
                    }
                }

                // Partial presence: some found, some missing
                if (!foundRequired.empty() && !missingRequired.empty()) {
                    // Build missing list string
                    std::string missingStr;
                    for (size_t i = 0; i < missingRequired.size(); ++i) {
                        if (i > 0) missingStr += ", ";
                        missingStr += missingRequired[i];
                    }

                    // Use the first matching port for location reference
                    PortInfo refPort;
                    refPort.instancePath = instPath;
                    refPort.portName = foundRequired[0];
                    refPort.direction = slang::ast::ArgumentDirection::In;
                    refPort.width = 0;

                    // Find actual port for better info
                    for (auto* port : ports) {
                        if (matchesSuffix(port->portName, foundRequired[0])) {
                            refPort = *port;
                            break;
                        }
                    }

                    Issue issue;
                    issue.type = Issue::Type::PROTOCOL_INCOMPLETE;
                    issue.severity = Issue::Severity::WARN;
                    issue.port = refPort;
                    issue.detail = fmt::format(
                        "{} {} channel '{}': found {}/{} required signals, missing: {}",
                        proto.name, instPath, channel.channelName,
                        foundRequired.size(), channel.required.size(), missingStr);
                    issues.push_back(std::move(issue));
                }
            }
        }
    }

    return issues;
}

} // namespace connect
