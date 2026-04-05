#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace metrics {

// Identifies a signal or part of a signal in the design
struct ValueRef {
    std::string hier_path;   // e.g. "top.sub.sig"
    std::string base_name;   // e.g. "sig"
    std::string selector;    // e.g. "[7:0]", "[3]", "" if full
    enum Kind { Net, Port, FfQ, FfDSink, Const, Unknown } kind = Net;
    bool approximate = false;

    std::string canonical() const {
        if (selector.empty()) return hier_path;
        return hier_path + selector;
    }

    bool operator==(const ValueRef& o) const {
        return hier_path == o.hier_path && selector == o.selector;
    }
};

struct ValueRefHash {
    size_t operator()(const ValueRef& v) const {
        auto h1 = std::hash<std::string>{}(v.hier_path);
        auto h2 = std::hash<std::string>{}(v.selector);
        return h1 ^ (h2 << 1);
    }
};

// Represents a single transformation operation
struct TransformNode {
    uint32_t id = 0;

    enum OpKind {
        Alias,      // direct passthrough / assignment
        Slice,      // bit/range select
        Concat,     // concatenation
        Replicate,  // replication
        Cast,       // type conversion
        Unary,      // unary operator (not, negate, etc.)
        Binary,     // binary operator (add, xor, etc.)
        Mux,        // conditional/ternary
        Compare,    // comparison operators
        UnknownOp   // unsupported construct
    };
    OpKind op_kind = UnknownOp;
    std::string op_detail;    // e.g. "add", "xor", "eq"

    std::vector<ValueRef> inputs;
    ValueRef output;

    std::string source_loc;   // debug only, excluded from signature
    bool approximate = false;
    uint32_t bit_width = 0;

    // Canonical signature for normalization (excludes source_loc)
    std::string signature() const {
        std::string sig = opKindStr();
        if (!op_detail.empty())
            sig += ":" + op_detail;
        sig += ":" + std::to_string(bit_width);
        sig += ":" + std::to_string(inputs.size());
        return sig;
    }

    // Stride-normalizable key: strips specific range values from Slice ops
    std::string strideKey() const {
        if (op_kind == Slice) {
            return std::string("slice:") + std::to_string(bit_width) +
                   ":" + std::to_string(inputs.size());
        }
        return signature();
    }

    std::string opKindStr() const {
        switch (op_kind) {
            case Alias:     return "alias";
            case Slice:     return "slice";
            case Concat:    return "concat";
            case Replicate: return "replicate";
            case Cast:      return "cast";
            case Unary:     return "unary";
            case Binary:    return "binary";
            case Mux:       return "mux";
            case Compare:   return "compare";
            case UnknownOp: return "unknown";
        }
        return "unknown";
    }
};

// Records an unsupported or approximate construct encountered
struct UnsupportedEvent {
    std::string kind;        // e.g. "always_ff", "function_call"
    std::string source_loc;
    std::string detail;
};

// Describes a detected flip-flop
struct FFInfo {
    std::string name;       // FF variable name
    ValueRef q_ref;         // Q output reference
    ValueRef d_ref;         // D-side input reference
};

// The provenance-preserving transformation graph
struct TransformGraph {
    std::vector<TransformNode> nodes;

    // Maps a signal canonical name -> indices into nodes[] that drive it
    std::unordered_map<std::string, std::vector<uint32_t>> drivers_by_value;

    // Root signals (output ports, FF D-side sinks)
    std::vector<ValueRef> roots;

    // Detected flip-flops
    std::vector<FFInfo> flip_flops;

    // Unsupported constructs encountered during extraction
    std::vector<UnsupportedEvent> unsupported_events;

    uint32_t addNode(TransformNode node) {
        node.id = static_cast<uint32_t>(nodes.size());
        auto key = node.output.canonical();
        drivers_by_value[key].push_back(node.id);
        nodes.push_back(std::move(node));
        return nodes.back().id;
    }
};

} // namespace metrics
