#pragma once

#include <string>
#include <vector>

namespace connect {

struct DiffConnection {
    std::string source;
    std::string dest;
    std::string status;
};

struct DiffInput {
    std::vector<DiffConnection> connections;
};

struct ChangedConnection {
    std::string source;
    std::string dest;
    std::string oldStatus;
    std::string newStatus;
};

struct DiffResult {
    std::vector<DiffConnection> added;
    std::vector<DiffConnection> removed;
    std::vector<ChangedConnection> changed;
    bool empty() const { return added.empty() && removed.empty() && changed.empty(); }
};

DiffResult computeDiff(const DiffInput& baseline, const DiffInput& current);
DiffInput loadDiffInputFromJson(const std::string& jsonPath);

} // namespace connect
