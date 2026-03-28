#include "GraphDiff.h"

#include <map>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace connect {

namespace {

using Key = std::pair<std::string, std::string>;

// Strip width annotations like [31:0] from a port path.
std::string stripWidth(const std::string& s) {
    auto pos = s.find('[');
    if (pos == std::string::npos)
        return s;
    return s.substr(0, pos);
}

} // anonymous namespace

DiffResult computeDiff(const DiffInput& baseline, const DiffInput& current) {
    DiffResult result;

    // Index baseline connections by (source, dest) -> status
    std::map<Key, std::string> baseMap;
    for (const auto& c : baseline.connections) {
        baseMap[{c.source, c.dest}] = c.status;
    }

    // Index current connections by (source, dest) -> status
    std::map<Key, std::string> currMap;
    for (const auto& c : current.connections) {
        currMap[{c.source, c.dest}] = c.status;
    }

    // Find added and changed
    for (const auto& c : current.connections) {
        Key key{c.source, c.dest};
        auto it = baseMap.find(key);
        if (it == baseMap.end()) {
            result.added.push_back(c);
        } else if (it->second != c.status) {
            result.changed.push_back({c.source, c.dest, it->second, c.status});
        }
    }

    // Find removed
    for (const auto& c : baseline.connections) {
        Key key{c.source, c.dest};
        if (currMap.find(key) == currMap.end()) {
            result.removed.push_back(c);
        }
    }

    return result;
}

DiffInput loadDiffInputFromJson(const std::string& jsonPath) {
    // yaml-cpp can parse JSON natively (JSON is a subset of YAML).
    // YAML::LoadFile throws YAML::BadFile on missing files; wrap it to
    // preserve the std::runtime_error contract expected by callers.
    YAML::Node root;
    try {
        root = YAML::LoadFile(jsonPath);
    } catch (const YAML::BadFile&) {
        throw std::runtime_error("Cannot open file: " + jsonPath);
    }

    DiffInput input;

    if (!root["connections"] || !root["connections"].IsSequence())
        return input;

    for (const auto& conn : root["connections"]) {
        std::string source = conn["source"].as<std::string>("");
        std::string dest   = conn["dest"].as<std::string>("");
        std::string status = conn["status"].as<std::string>("OK");

        if (source.empty() || dest.empty())
            continue;

        input.connections.push_back({stripWidth(source), stripWidth(dest), status});
    }

    return input;
}

} // namespace connect
