#pragma once

#include "GlobUtil.h"

#include <slang/text/SourceLocation.h>
#include <slang/text/SourceManager.h>

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace svlens {

// Matches a directive on a declaration line or immediately above it.
// A path is mandatory for findings that can occur in multiple instances.
class InlineWaiver {
public:
    bool matches(const slang::SourceManager* manager, slang::SourceLocation location,
                 std::initializer_list<std::string_view> ruleIds, const std::string& fullPath, bool requirePath) {
        if (!manager || !location.valid())
            return false;
        const std::string filename(manager->getFileName(location));
        if (filename.empty())
            return false;

        auto [it, inserted] = linesByFile_.try_emplace(filename);
        if (inserted) {
            std::ifstream file(filename);
            std::string line;
            while (std::getline(file, line))
                it->second.push_back(std::move(line));
        }

        auto matchesLine = [&](size_t lineNumber) {
            if (lineNumber == 0 || lineNumber > it->second.size())
                return false;
            const auto& text = it->second[lineNumber - 1];
            constexpr std::string_view marker = "// svlens: waive ";
            const auto pos = text.find(marker);
            if (pos == std::string::npos)
                return false;
            const auto body = text.substr(pos + marker.size());
            const auto reason = body.find(" reason: ");
            if (reason == std::string::npos || body.find_first_not_of(" \t", reason + 9) == std::string::npos)
                return false;

            std::istringstream fields(body.substr(0, reason));
            std::string rule;
            std::string scope;
            std::string extra;
            if (!(fields >> rule) || (fields >> scope && fields >> extra))
                return false;
            bool ruleMatches = false;
            for (const auto candidate : ruleIds)
                ruleMatches |= candidate == rule;
            if (!ruleMatches)
                return false;
            if (scope.empty())
                return !requirePath;
            if (!scope.starts_with("path="))
                return false;
            const auto path = scope.substr(5);
            return requirePath ? path == fullPath : connect::globMatch(path, fullPath);
        };

        const auto line = static_cast<size_t>(manager->getLineNumber(location));
        return matchesLine(line) || (line > 1 && matchesLine(line - 1));
    }

private:
    std::unordered_map<std::string, std::vector<std::string>> linesByFile_;
};

} // namespace svlens
