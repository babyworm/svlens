#pragma once

#include "ConnectionGraph.h"

#include <optional>
#include <string>
#include <vector>

namespace connect {

struct Issue {
    enum class Type {
        WIDTH_MISMATCH,
        TYPE_MISMATCH,
        DANGLING_OUTPUT,
        UNDRIVEN_INPUT
    };

    enum class Severity {
        ERROR,
        WARN,
        INFO
    };

    Type type;
    Severity severity;
    PortInfo port;
    std::optional<Connection> connection;
    std::string detail;

    static const char* typeToString(Type t) {
        switch (t) {
            case Type::WIDTH_MISMATCH:  return "WIDTH_MISMATCH";
            case Type::TYPE_MISMATCH:   return "TYPE_MISMATCH";
            case Type::DANGLING_OUTPUT: return "DANGLING_OUTPUT";
            case Type::UNDRIVEN_INPUT:  return "UNDRIVEN_INPUT";
        }
        return "UNKNOWN";
    }

    static const char* severityToString(Severity s) {
        switch (s) {
            case Severity::ERROR: return "ERROR";
            case Severity::WARN:  return "WARN";
            case Severity::INFO:  return "INFO";
        }
        return "UNKNOWN";
    }
};

} // namespace connect
