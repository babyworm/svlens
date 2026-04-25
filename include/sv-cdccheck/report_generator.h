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

    /// Emit SystemVerilog Assertions (SVA) for each crossing in the report.
    /// VIOLATION crossings produce a `cover property` that fires when the
    /// source signal changes between dst_clk edges (a documentation aid for
    /// formal/simulation triage). INFO/Caution synced crossings produce a
    /// comment-only header documenting the verified synchronizer pattern.
    /// The emitted file is informational and is not bind-ready as-is; it is
    /// a starting point for downstream property files. See docs/schema/
    /// cdc_report.md for field semantics.
    ///
    /// Returns true on success, false when the output stream cannot be
    /// opened (e.g. parent directory missing, permission denied). Callers
    /// that surface this to the user should warn rather than abort —
    /// failure to write the SVA artifact does not invalidate the
    /// underlying analysis.
    [[nodiscard]] bool generateSVA(const std::filesystem::path& output_path,
                                   const std::string& top_module = "") const;

    /// RFC 8259 JSON string escaping
    static std::string jsonEscape(const std::string& s);

private:
    const AnalysisResult& result_;
};

} // namespace sv_cdccheck
