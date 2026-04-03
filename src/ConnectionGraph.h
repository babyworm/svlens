#pragma once

#include <slang/ast/SemanticFacts.h>
#include <slang/text/SourceLocation.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace connect {

struct PortInfo {
    std::string instancePath;
    std::string portName;
    slang::ast::ArgumentDirection direction;
    uint32_t width = 0;
    bool isSigned = false;
    slang::SourceLocation location;

    std::string fullPath() const {
        return instancePath + "." + portName;
    }
};

enum class ConnectionKind {
    Direct,
    Approximate
};

struct Connection {
    PortInfo source;
    PortInfo dest;
    ConnectionKind kind = ConnectionKind::Direct;
};

struct ConnectionGraph {
    std::vector<Connection> connections;
    std::vector<PortInfo> allPorts;
    std::unordered_set<std::string> connectedPorts; // ports with non-empty expressions
    std::unordered_set<std::string> tieOffPorts;    // ports connected only to compile-time constants
    std::string topModule;
};

} // namespace connect
