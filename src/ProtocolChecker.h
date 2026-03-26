#pragma once
#include "Checker.h"
#include <map>
#include <string>
#include <vector>

namespace connect {

struct ProtocolChannel {
    std::string channelName;
    std::vector<std::string> required;
    std::vector<std::string> optional;
};

struct ProtocolDef {
    std::string name;
    std::vector<ProtocolChannel> channels;
};

class ProtocolChecker : public IChecker {
public:
    ProtocolChecker();
    std::vector<Issue> check(const ConnectionGraph& graph) const override;
private:
    std::vector<ProtocolDef> protocols_;
    void initProtocols();
    static bool matchesSuffix(const std::string& portName, const std::string& signal);
};

} // namespace connect
