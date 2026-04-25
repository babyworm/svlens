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
#include <fstream>
#include <iostream>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace cdccli {

namespace {

// Built-in defaults for the safe-cell registry. Extended with the
// user-supplied --sync-cell / --glitch-free-mux-cell entries and any
// names from --cdc-config <yaml>.
const std::unordered_set<std::string>& defaultSafeMuxCells() {
    static const std::unordered_set<std::string> kCells = {
        "BUFGCTRL",            // Xilinx 7-series glitch-free clock mux
        "BUFGMUX",             // Xilinx Spartan-style mux
        "ICG_MUX",             // generic ASIC ICG-based mux primitive
        "prim_clock_mux2",     // OpenTitan prim
        "pulp_clock_mux2",     // pulp-platform prim
        "pulp_clock_inverter"
    };
    return kCells;
}

const std::unordered_set<std::string>& defaultSafeSyncCells() {
    static const std::unordered_set<std::string> kCells = {
        "prim_flop_2sync",     // OpenTitan prim
        "prim_pulse_sync",
        "sync",                // pulp-platform common_cells
        "sync_2ff",            // generic 2-flop synchroniser
        "sync_3ff"
    };
    return kCells;
}

// Load a CDC YAML config: top-level `sync_cells: [...]` and
// `glitch_free_mux_cells: [...]` lists. Missing keys are tolerated.
// Errors are reported once and otherwise ignored so a bad config does
// not block the run.
void loadCdcConfigFile(const std::string& path,
                      std::unordered_set<std::string>& mux_out,
                      std::unordered_set<std::string>& sync_out)
{
    if (path.empty()) return;
    try {
        YAML::Node root = YAML::LoadFile(path);
        if (root["glitch_free_mux_cells"]) {
            for (const auto& v : root["glitch_free_mux_cells"])
                mux_out.insert(v.as<std::string>());
        }
        if (root["sync_cells"]) {
            for (const auto& v : root["sync_cells"])
                sync_out.insert(v.as<std::string>());
        }
    } catch (const std::exception& ex) {
        std::cerr << "svlens cdc: warning: could not parse --cdc-config: "
                  << ex.what() << "\n";
    }
}

} // namespace

sv_cdccheck::AnalysisResult analyzeCdcCompilation(slang::ast::Compilation& compilation,
                                                  const CdcCliOptions& opts) {
    compilation.getRoot();
    compilation.getAllDiagnostics();

    if (!opts.quiet)
        std::cout << "svlens cdc: Design elaborated successfully.\n";

    sv_cdccheck::ClockDatabase clockDb;
    sv_cdccheck::ClockTreeAnalyzer clockAnalyzer(compilation, clockDb);

    // Build the safe-cell registry: built-in defaults ∪ --sync-cell /
    // --glitch-free-mux-cell flags ∪ contents of --cdc-config YAML.
    {
        std::unordered_set<std::string> safe_mux = defaultSafeMuxCells();
        std::unordered_set<std::string> safe_sync = defaultSafeSyncCells();
        for (auto& n : opts.userGlitchFreeMuxCells) safe_mux.insert(n);
        for (auto& n : opts.userSyncCells) safe_sync.insert(n);
        loadCdcConfigFile(opts.cdcConfigFile, safe_mux, safe_sync);
        clockAnalyzer.setSafeMuxCells(safe_mux);
        clockAnalyzer.setSafeSyncCells(safe_sync);
        if (opts.verbose) {
            std::cout << "  Safe glitch-free mux cells: " << safe_mux.size() << "\n";
            std::cout << "  Safe sync cells: " << safe_sync.size() << "\n";
        }
    }

    std::vector<sv_cdccheck::SdcFalsePath> sdcFalsePaths;
    if (!opts.sdcFile.empty()) {
        if (opts.verbose)
            std::cout << "  Loading SDC: " << opts.sdcFile << "\n";
        auto sdc = sv_cdccheck::SdcParser::parse(opts.sdcFile);
        sdcFalsePaths = sdc.false_paths;
        clockAnalyzer.loadSdc(sdc);
    }

    sv_cdccheck::ClockYamlParser clockYamlParser;
    if (!opts.clockYamlFile.empty()) {
        if (opts.verbose)
            std::cout << "  Loading clock YAML: " << opts.clockYamlFile << "\n";
        if (!clockYamlParser.loadFile(opts.clockYamlFile)) {
            std::cerr << "svlens cdc: warning: could not load clock YAML file: "
                      << opts.clockYamlFile << "\n";
        } else {
            clockYamlParser.applyTo(clockDb);
            if (opts.verbose)
                std::cout << "  Clock sources from YAML: "
                          << clockYamlParser.getConfig().clock_sources.size() << "\n";
        }
    }

    clockAnalyzer.analyze();
    if (opts.checkClockMux)
        clockAnalyzer.detectUnsafeCombClocks();

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
        std::cerr << "svlens cdc: error: " << err.hier_path << ": " << err.message << "\n";

    sv_cdccheck::ConnectivityBuilder connectivity(compilation, classifier->getFFNodes());
    connectivity.analyze();

    if (opts.verbose)
        std::cout << "  FF-to-FF edges: " << connectivity.getEdges().size() << "\n";

    sv_cdccheck::CrossingDetector detector(connectivity.getEdges(), clockDb);
    if (!sdcFalsePaths.empty())
        detector.setFalsePaths(sdcFalsePaths);
    detector.analyze();
    auto crossings = detector.getCrossings();

    // Emit Ac_cdc05 violations for any flop using a comb-driven (unsafe)
    // clock. Each flop in such a domain gets a structural violation
    // independent of any FF-to-FF crossing. The corresponding clock
    // source has already been marked by detectUnsafeCombClocks() above.
    if (opts.checkClockMux) {
        int ac05_counter = 0;
        for (const auto& ff : classifier->getFFNodes()) {
            if (!ff || !ff->domain || !ff->domain->source) continue;
            if (!ff->domain->source->is_unsafe_comb_clock) continue;
            sv_cdccheck::CrossingReport r;
            r.id = "VIOLATION-CLKMUX-" + std::to_string(++ac05_counter);
            r.category = sv_cdccheck::ViolationCategory::Violation;
            r.severity = sv_cdccheck::Severity::High;
            r.source_signal = ff->domain->source->origin_signal;
            r.dest_signal = ff->hier_path;
            r.dest_domain = ff->domain;
            r.sync_type = sv_cdccheck::SyncType::None;
            r.rule = "Ac_cdc05";
            r.recommendation = "[Ac_cdc05] Flop clock is driven by a "
                "combinational expression. Use a glitch-free clock mux "
                "primitive (e.g. BUFGCTRL / ICG_MUX / prim_clock_mux2) "
                "or declare the mux output as an SDC generated clock. "
                "User-configurable safe-cell list: --glitch-free-mux-cell "
                "<name> or --cdc-config <yaml>.";
            crossings.push_back(std::move(r));
        }
    }

    sv_cdccheck::SyncVerifier verifier(crossings, classifier->getFFNodes(),
                                       connectivity.getEdges(), &clockDb);
    verifier.setRequiredStages(opts.syncStages);
    verifier.analyze();

    sv_cdccheck::WaiverManager waiverMgr;
    if (!opts.waiverFile.empty()) {
        if (opts.verbose)
            std::cout << "  Loading waivers: " << opts.waiverFile << "\n";
        if (!waiverMgr.loadFile(opts.waiverFile)) {
            std::cerr << "svlens cdc: warning: could not load waiver file: "
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
    if (!opts.svaOutputFile.empty())
        report.generateSVA(opts.svaOutputFile, opts.topModule);
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
