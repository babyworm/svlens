#include "GraphDiff.h"

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

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

// Trim leading/trailing whitespace.
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Extract the string value following a JSON key like "source": "value".
// Looks for the pattern `"key": "value"` and returns value.
std::string extractJsonString(const std::string& line, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = line.find(needle);
    if (pos == std::string::npos)
        return "";

    // Find the colon after the key
    pos = line.find(':', pos + needle.size());
    if (pos == std::string::npos)
        return "";

    // Find the opening quote of the value
    auto qStart = line.find('"', pos + 1);
    if (qStart == std::string::npos)
        return "";

    // Find the closing quote
    auto qEnd = line.find('"', qStart + 1);
    if (qEnd == std::string::npos)
        return "";

    return line.substr(qStart + 1, qEnd - qStart - 1);
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
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + jsonPath);
    }

    DiffInput input;
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Find the "connections" array
    auto connPos = content.find("\"connections\"");
    if (connPos == std::string::npos)
        return input;

    // Find the opening bracket of the array
    auto arrStart = content.find('[', connPos);
    if (arrStart == std::string::npos)
        return input;

    // Parse each object in the array by finding { ... } blocks
    size_t pos = arrStart + 1;
    while (pos < content.size()) {
        auto objStart = content.find('{', pos);
        if (objStart == std::string::npos)
            break;

        auto objEnd = content.find('}', objStart);
        if (objEnd == std::string::npos)
            break;

        std::string obj = content.substr(objStart, objEnd - objStart + 1);

        std::string source = extractJsonString(obj, "source");
        std::string dest = extractJsonString(obj, "dest");
        std::string status = extractJsonString(obj, "status");

        if (!source.empty() && !dest.empty()) {
            input.connections.push_back({
                stripWidth(source),
                stripWidth(dest),
                status
            });
        }

        pos = objEnd + 1;
    }

    return input;
}

} // namespace connect
