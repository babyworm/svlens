#pragma once
#include "Issue.h"
#include <string>
#include <vector>

namespace slang {
class SourceManager;
}

namespace connect {
class WaiverFilter {
public:
    WaiverFilter() = default;
    explicit WaiverFilter(const std::string& yamlPath);

    struct WaiverResult {
        std::vector<Issue> active;
        std::vector<Issue> waived;
    };

    WaiverResult apply(const std::vector<Issue>& issues, const slang::SourceManager* sourceManager = nullptr) const;

private:
    struct WaiverRule {
        std::string pattern;
        std::string type;
        std::string reason;
        std::string source;
    };
    std::vector<WaiverRule> rules_;
};
} // namespace connect
