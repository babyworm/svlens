#pragma once

#include "sv-cdccheck/types.h"
#include <string>
#include <filesystem>

namespace sv_cdccheck {

/// Pass 6: Report generation
class ReportGenerator {
public:
    explicit ReportGenerator(const AnalysisResult& result);

    void generateMarkdown(const std::filesystem::path& output_path) const;
    void generateJSON(const std::filesystem::path& output_path) const;
    void generateDOT(const std::filesystem::path& output_path) const;
    void generateSDC(const std::filesystem::path& output_path) const;
    void generateWaiverTemplate(const std::filesystem::path& output_path) const;

    /// RFC 8259 JSON string escaping
    static std::string jsonEscape(const std::string& s);

private:
    const AnalysisResult& result_;
};

} // namespace sv_cdccheck
