#pragma once

#include "sv-cdccheck/types.h"
#include <string>
#include <filesystem>
#include <ostream>

namespace sv_cdccheck {

/// Pass 6: Report generation
class ReportGenerator {
public:
    explicit ReportGenerator(const AnalysisResult& result, bool includeSvaIds = false);

    void generateMarkdown(const std::filesystem::path& output_path) const;
    void generateJSON(const std::filesystem::path& output_path) const;
    void generateHTML(const std::filesystem::path& output_path, const std::string& top_module = "") const;
    void generateDOT(const std::filesystem::path& output_path) const;
    void generateSDC(const std::filesystem::path& output_path) const;
    void generateWaiverTemplate(const std::filesystem::path& output_path) const;

    /// Emit SystemVerilog Assertions (SVA) for each crossing in the report.
    /// VIOLATION crossings produce a `cover property` when the source path is
    /// safe. Recognized 2FF/3FF chains with unambiguous stage paths produce
    /// assertions checking stage-to-stage sampled transfer. Verified
    /// prim_fifo_async Gray pointers can also emit a source-clock encoding
    /// assertion. A verified prim_sync_reqack request path can emit the
    /// primitive's destination ACK-requires-REQ integration assertion;
    /// generic handshake/FIFO protocol patterns remain documentation-only.
    /// The emitted assertion module uses AST-declared hierarchical paths and
    /// can be compiled alongside the analyzed design top. Users must still
    /// review simulation binding scope and temporal assumptions. See
    /// docs/schema/cdc_report.md for field semantics.
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
    bool includeSvaIds_ = false;
    void writeJSON(std::ostream& out) const;
};

} // namespace sv_cdccheck
