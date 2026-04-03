#include "CdcRunnerUtils.h"

#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/sdc_parser.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"
#include "sv-cdccheck/report_generator.h"
#include "sv-cdccheck/waiver.h"
#include "sv-cdccheck/clock_yaml_parser.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace cdccli {

sv_cdccheck::AnalysisResult analyzeCdcCompilation(slang::ast::Compilation& compilation,
                                                  const CdcCliOptions& opts) {
    compilation.getRoot();
    compilation.getAllDiagnostics();

    if (!opts.quiet)
        std::cout << "sv-cdccheck: Design elaborated successfully.\n";

    sv_cdccheck::ClockDatabase clockDb;
    sv_cdccheck::ClockTreeAnalyzer clockAnalyzer(compilation, clockDb);

    if (!opts.sdcFile.empty()) {
        if (opts.verbose)
            std::cout << "  Loading SDC: " << opts.sdcFile << "\n";
        auto sdc = sv_cdccheck::SdcParser::parse(opts.sdcFile);
        clockAnalyzer.loadSdc(sdc);
    }

    sv_cdccheck::ClockYamlParser clockYamlParser;
    if (!opts.clockYamlFile.empty()) {
        if (opts.verbose)
            std::cout << "  Loading clock YAML: " << opts.clockYamlFile << "\n";
        if (!clockYamlParser.loadFile(opts.clockYamlFile)) {
            std::cerr << "sv-cdccheck: warning: could not load clock YAML file: "
                      << opts.clockYamlFile << "\n";
        } else {
            clockYamlParser.applyTo(clockDb);
            if (opts.verbose)
                std::cout << "  Clock sources from YAML: "
                          << clockYamlParser.getConfig().clock_sources.size() << "\n";
        }
    }

    clockAnalyzer.analyze();

    if (opts.verbose) {
        std::cout << "  Clock sources: " << clockDb.sources.size() << "\n";
        for (const auto& src : clockDb.sources)
            std::cout << "    " << src->name << " (" << src->origin_signal << ")\n";
    }

    auto classifier = std::make_unique<sv_cdccheck::FFClassifier>(compilation, clockDb);
    classifier->analyze();

    if (!opts.quiet)
        std::cout << "  FFs detected: " << classifier->getFFNodes().size() << "\n";

    for (const auto& err : classifier->getErrors())
        std::cerr << "sv-cdccheck: error: " << err.hier_path << ": " << err.message << "\n";

    sv_cdccheck::ConnectivityBuilder connectivity(compilation, classifier->getFFNodes());
    connectivity.analyze();

    if (opts.verbose)
        std::cout << "  FF-to-FF edges: " << connectivity.getEdges().size() << "\n";

    sv_cdccheck::CrossingDetector detector(connectivity.getEdges(), clockDb);
    detector.analyze();
    auto crossings = detector.getCrossings();

    sv_cdccheck::SyncVerifier verifier(crossings, classifier->getFFNodes(),
                                       connectivity.getEdges(), &clockDb);
    verifier.setRequiredStages(opts.syncStages);
    verifier.analyze();

    sv_cdccheck::WaiverManager waiverMgr;
    if (!opts.waiverFile.empty()) {
        if (opts.verbose)
            std::cout << "  Loading waivers: " << opts.waiverFile << "\n";
        if (!waiverMgr.loadFile(opts.waiverFile)) {
            std::cerr << "sv-cdccheck: warning: could not load waiver file: "
                      << opts.waiverFile << "\n";
        } else if (opts.verbose) {
            std::cout << "  Waivers loaded: " << waiverMgr.getWaivers().size() << "\n";
        }
    }

    for (auto& c : crossings) {
        if (waiverMgr.isWaived(c.source_signal, c.dest_signal))
            c.category = sv_cdccheck::ViolationCategory::Waived;
    }

    if (opts.ignoreGated) {
        crossings.erase(std::remove_if(crossings.begin(), crossings.end(),
                        [](const sv_cdccheck::CrossingReport& c) {
                            return c.severity == sv_cdccheck::Severity::Low;
                        }),
                        crossings.end());
    }

    sv_cdccheck::AnalysisResult result;
    result.clock_db = std::move(clockDb);
    result.crossings = std::move(crossings);
    result.ff_nodes = classifier->releaseFFNodes();
    result.edges = connectivity.getEdges();
    return result;
}

void emitCdcReports(const CdcCliOptions& opts,
                    const sv_cdccheck::AnalysisResult& result) {
    fs::create_directories(opts.outputDir);
    sv_cdccheck::ReportGenerator report(result);

    if (opts.format == "md" || opts.format == "all")
        report.generateMarkdown(fs::path(opts.outputDir) / "cdc_report.md");
    if (opts.format == "json" || opts.format == "all")
        report.generateJSON(fs::path(opts.outputDir) / "cdc_report.json");
    if (opts.format == "sdc" || opts.format == "all")
        report.generateSDC(fs::path(opts.outputDir) / "cdc_constraints.sdc");
    if (opts.format == "waiver" || opts.format == "all")
        report.generateWaiverTemplate(fs::path(opts.outputDir) / "cdc_waiver_template.yaml");
    if (!opts.dumpGraphFile.empty())
        report.generateDOT(opts.dumpGraphFile);
}

void printCdcSummary(const CdcCliOptions& opts,
                     const sv_cdccheck::AnalysisResult& result) {
    if (opts.quiet)
        return;

    std::cout << "\n  === CDC Summary ===\n";
    std::cout << "  VIOLATION:  " << result.violation_count() << "\n";
    std::cout << "  CAUTION:    " << result.caution_count() << "\n";
    std::cout << "  CONVENTION: " << result.convention_count() << "\n";
    std::cout << "  INFO:       " << result.info_count() << "\n";
    std::cout << "  WAIVED:     " << result.waived_count() << "\n";
    std::cout << "\n  Reports written to: " << opts.outputDir << "/\n";
}

} // namespace cdccli
