#include "CdcRunnerUtils.h"
#include "InlineWaiver.h"

#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/sdc_parser.h"
#include "sv-cdccheck/ff_classifier.h"
#include "sv-cdccheck/connectivity.h"
#include "sv-cdccheck/crossing_detector.h"
#include "sv-cdccheck/sync_verifier.h"
#include "sv-cdccheck/report_generator.h"
#include "sv-cdccheck/waiver.h"
#include "sv-cdccheck/clock_yaml_parser.h"

#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/types/Type.h>
#include <slang/ast/Expression.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
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

std::optional<sv_cdccheck::ReqAckDataContract>
reqAckDataContract(const slang::ast::InstanceSymbol& child, const std::string& path,
                   const std::unordered_set<std::string>& connectedPorts) {
    auto parameterValue = [&](std::string_view name) -> std::optional<uint64_t> {
        for (const auto* parameter : child.body.getParameters()) {
            if (!parameter || parameter->symbol.name != name ||
                parameter->symbol.kind != slang::ast::SymbolKind::Parameter)
                continue;
            const auto& value = parameter->symbol.as<slang::ast::ParameterSymbol>().getValue();
            if (!value.isInteger() || value.hasUnknown())
                return std::nullopt;
            return value.integer().as<uint64_t>();
        }
        return std::nullopt;
    };
    const auto width = parameterValue("Width");
    const auto direction = parameterValue("DataSrc2Dst");
    const auto dataReg = parameterValue("DataReg");
    const auto reqStability = parameterValue("EnReqStabA");
    if (!width || *width == 0 || *width > std::numeric_limits<uint32_t>::max() || !direction || *direction > 1 ||
        !dataReg || *dataReg != 0 || !reqStability || *reqStability > 1)
        return std::nullopt;

    auto hasPort = [&](std::string_view name, slang::ast::ArgumentDirection expected, uint64_t expectedWidth) {
        const auto* symbol = child.body.findPort(name);
        if (!symbol || symbol->kind != slang::ast::SymbolKind::Port || !connectedPorts.contains(std::string(name)))
            return false;
        const auto& port = symbol->as<slang::ast::PortSymbol>();
        return port.direction == expected && port.getType().isIntegral() &&
               port.getType().getBitWidth() == expectedWidth;
    };
    using Direction = slang::ast::ArgumentDirection;
    if (!hasPort("clk_src_i", Direction::In, 1) || !hasPort("rst_src_ni", Direction::In, 1) ||
        !hasPort("clk_dst_i", Direction::In, 1) || !hasPort("rst_dst_ni", Direction::In, 1) ||
        !hasPort("src_req_i", Direction::In, 1) || !hasPort("src_ack_o", Direction::Out, 1) ||
        !hasPort("dst_req_o", Direction::Out, 1) || !hasPort("dst_ack_i", Direction::In, 1) ||
        !hasPort("data_i", Direction::In, *width) || !hasPort("data_o", Direction::Out, *width))
        return std::nullopt;
    return sv_cdccheck::ReqAckDataContract{path, *direction == 1, static_cast<uint32_t>(*width)};
}

void appendNamedReqackReviews(const slang::ast::Scope& scope, const std::string& parentPath,
                              const sv_cdccheck::ClockDatabase& clocks,
                              std::vector<sv_cdccheck::CrossingReport>& crossings, int& sequence) {
    for (const auto& member : scope.members()) {
        if (member.kind == slang::ast::SymbolKind::Instance) {
            const auto& child = member.as<slang::ast::InstanceSymbol>();
            const std::string path = parentPath + "." + std::string(child.name);
            if (child.getDefinition().name == "prim_sync_reqack_data") {
                std::unordered_set<std::string> ports;
                std::unordered_set<std::string> connectedPorts;
                for (const auto* connection : child.getPortConnections()) {
                    if (connection) {
                        ports.insert(std::string(connection->port.name));
                        const auto* expression = connection->getExpression();
                        if (expression && expression->kind != slang::ast::ExpressionKind::EmptyArgument)
                            connectedPorts.insert(std::string(connection->port.name));
                    }
                }
                const bool signature = ports.contains("clk_src_i") && ports.contains("clk_dst_i") &&
                                       ports.contains("src_req_i") && ports.contains("src_ack_o") &&
                                       ports.contains("dst_req_o") && ports.contains("dst_ack_i");
                auto* srcDomain = clocks.domainForSignal(path + ".clk_src_i");
                auto* dstDomain = clocks.domainForSignal(path + ".clk_dst_i");
                if (signature && srcDomain && dstDomain && !srcDomain->isSameDomain(*dstDomain)) {
                    const auto dataContract = reqAckDataContract(child, path, connectedPorts);
                    auto append = [&](sv_cdccheck::ClockDomain* from, sv_cdccheck::ClockDomain* to,
                                      const char* sourcePort, const char* destPort) {
                        sv_cdccheck::CrossingReport report;
                        report.id = "CAUTION-REQACK-" + std::to_string(++sequence);
                        report.category = sv_cdccheck::ViolationCategory::Caution;
                        report.severity = sv_cdccheck::Severity::Medium;
                        report.source_signal = path + "." + sourcePort;
                        report.dest_signal = path + "." + destPort;
                        report.source_domain = from;
                        report.dest_domain = to;
                        report.sync_type = sv_cdccheck::SyncType::Handshake;
                        report.rule = "Ac_cdc01";
                        report.recommendation =
                            "Review named prim_sync_reqack_data clock boundary; "
                            "port signature is recognized but internal synchronization is not verified";
                        report.rationale = "A two-clock req/ack primitive has source and destination "
                                           "clock ports in different domains; this is an integration review item.";
                        if (dataContract &&
                            ((dataContract->src_to_dst && std::string_view(sourcePort) == "src_req_i") ||
                             (!dataContract->src_to_dst && std::string_view(sourcePort) == "dst_ack_i")))
                            report.reqack_data_contract = *dataContract;
                        crossings.push_back(std::move(report));
                    };
                    if (connectedPorts.contains("src_req_i") && connectedPorts.contains("dst_req_o"))
                        append(srcDomain, dstDomain, "src_req_i", "dst_req_o");
                    if (connectedPorts.contains("dst_ack_i") && connectedPorts.contains("src_ack_o"))
                        append(dstDomain, srcDomain, "dst_ack_i", "src_ack_o");
                }
            }
            appendNamedReqackReviews(child.body, path, clocks, crossings, sequence);
        } else if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
            const auto& block = member.as<slang::ast::GenerateBlockSymbol>();
            if (!block.isUninstantiated)
                appendNamedReqackReviews(block, parentPath + "." + block.getExternalName(), clocks, crossings,
                                         sequence);
        } else if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
            const auto& array = member.as<slang::ast::GenerateBlockArraySymbol>();
            for (const auto* entry : array.entries)
                if (entry && !entry->isUninstantiated)
                    appendNamedReqackReviews(*entry, parentPath + "." + entry->getExternalName(), clocks, crossings,
                                             sequence);
        }
    }
}

void annotateFifoTransferContracts(const slang::ast::Scope& scope, const std::string& parentPath,
                                   std::vector<sv_cdccheck::CrossingReport>& crossings) {
    for (const auto& member : scope.members()) {
        if (member.kind == slang::ast::SymbolKind::Instance) {
            const auto& child = member.as<slang::ast::InstanceSymbol>();
            const std::string path = parentPath + "." + std::string(child.name);
            if (child.getDefinition().name == "prim_fifo_async") {
                std::unordered_set<std::string> connectedPorts;
                for (const auto* connection : child.getPortConnections()) {
                    if (!connection)
                        continue;
                    const auto* expression = connection->getExpression();
                    if (expression && expression->kind != slang::ast::ExpressionKind::EmptyArgument)
                        connectedPorts.insert(std::string(connection->port.name));
                }
                auto hasPort = [&](std::string_view name, slang::ast::ArgumentDirection direction) {
                    const auto* symbol = child.body.findPort(name);
                    if (!symbol || symbol->kind != slang::ast::SymbolKind::Port ||
                        !connectedPorts.contains(std::string(name)))
                        return false;
                    const auto& port = symbol->as<slang::ast::PortSymbol>();
                    return port.direction == direction && port.getType().isIntegral() &&
                           port.getType().getBitWidth() == 1;
                };
                using Direction = slang::ast::ArgumentDirection;
                const bool writePort = hasPort("clk_wr_i", Direction::In) && hasPort("rst_wr_ni", Direction::In) &&
                                       hasPort("wvalid_i", Direction::In) && hasPort("wready_o", Direction::Out);
                const bool readPort = hasPort("clk_rd_i", Direction::In) && hasPort("rst_rd_ni", Direction::In) &&
                                      hasPort("rvalid_o", Direction::Out) && hasPort("rready_i", Direction::In);
                for (auto& crossing : crossings) {
                    if (writePort && crossing.source_signal == path + ".fifo_wptr_gray_q")
                        crossing.fifo_transfer_contract = sv_cdccheck::FifoTransferContract{path, true};
                    else if (readPort && crossing.source_signal == path + ".fifo_rptr_gray_q")
                        crossing.fifo_transfer_contract = sv_cdccheck::FifoTransferContract{path, false};
                }
            }
            annotateFifoTransferContracts(child.body, path, crossings);
        } else if (member.kind == slang::ast::SymbolKind::GenerateBlock) {
            const auto& block = member.as<slang::ast::GenerateBlockSymbol>();
            if (!block.isUninstantiated)
                annotateFifoTransferContracts(block, parentPath + "." + block.getExternalName(), crossings);
        } else if (member.kind == slang::ast::SymbolKind::GenerateBlockArray) {
            const auto& array = member.as<slang::ast::GenerateBlockArraySymbol>();
            for (const auto* entry : array.entries)
                if (entry && !entry->isUninstantiated)
                    annotateFifoTransferContracts(*entry, parentPath + "." + entry->getExternalName(), crossings);
        }
    }
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
    std::vector<sv_cdccheck::SdcMaxDelay> sdcMaxDelays;
    if (!opts.sdcFile.empty()) {
        if (opts.verbose)
            std::cout << "  Loading SDC: " << opts.sdcFile << "\n";
        auto sdc = sv_cdccheck::SdcParser::parse(opts.sdcFile);
        if (sdc.skipped_clock_groups)
            std::cerr << "svlens cdc: warning: skipped " << sdc.skipped_clock_groups
                      << " unsupported set_clock_groups command(s); CDC relationships may be incomplete\n";
        if (sdc.skipped_max_delays)
            std::cerr << "svlens cdc: warning: skipped " << sdc.skipped_max_delays
                      << " unsupported or invalid set_max_delay command(s); timing context may be incomplete\n";
        sdcFalsePaths = sdc.false_paths;
        sdcMaxDelays = sdc.max_delays;
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
    if (clockAnalyzer.skippedSdcRelationshipGroups())
        std::cerr << "svlens cdc: warning: skipped " << clockAnalyzer.skippedSdcRelationshipGroups()
                  << " SDC clock relationship group(s) with unresolved, ambiguous, or overlapping clocks\n";
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
    if (!sdcMaxDelays.empty())
        detector.setMaxDelays(sdcMaxDelays);
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

    // Record the clock boundary of a named req/ack primitive even when its
    // internal FF graph is opaque to the generic crossing detector. A name
    // and port signature merit CAUTION, not a claim of safe synchronization.
    int reqackSequence = 0;
    for (const auto* top : compilation.getRoot().topInstances) {
        if (top && top->name == opts.topModule) {
            appendNamedReqackReviews(top->body, std::string(top->name), clockDb, crossings, reqackSequence);
            annotateFifoTransferContracts(top->body, std::string(top->name), crossings);
        }
    }

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

    svlens::InlineWaiver inlineWaiver;
    std::unordered_map<std::string, slang::SourceLocation> ffLocations;
    for (const auto& ff : classifier->getFFNodes())
        ffLocations.emplace(ff->hier_path, ff->location);
    for (auto& c : crossings) {
        bool waived = waiverMgr.isWaived(c.source_signal, c.dest_signal);
        if (!waived) {
            if (const auto it = ffLocations.find(c.dest_signal); it != ffLocations.end())
                waived =
                    inlineWaiver.matches(compilation.getSourceManager(), it->second, {c.rule}, c.dest_signal, true);
        }
        if (waived)
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
    result.sourceManager = compilation.getSourceManager();
    result.clock_db = std::move(clockDb);
    result.crossings = std::move(crossings);
    result.ff_nodes = classifier->releaseFFNodes();
    result.edges = connectivity.getEdges();
    return result;
}

void emitCdcReports(const CdcCliOptions& opts,
                    const sv_cdccheck::AnalysisResult& result) {
    fs::create_directories(opts.outputDir);
    bool svaWritten = false;
    if (!opts.svaOutputFile.empty()) {
        sv_cdccheck::ReportGenerator svaReport(result);
        svaWritten = svaReport.generateSVA(opts.svaOutputFile, opts.topModule);
        if (!svaWritten) {
            std::cerr << "svlens cdc: warning: failed to write --emit-sva file '" << opts.svaOutputFile
                      << "' (parent directory missing or " << "permission denied); analysis result is unaffected.\n";
        }
    }
    sv_cdccheck::ReportGenerator report(result, svaWritten);

    if (opts.format == "md" || opts.format == "all")
        report.generateMarkdown(fs::path(opts.outputDir) / "cdc_report.md");
    if (opts.format == "json" || opts.format == "all")
        report.generateJSON(fs::path(opts.outputDir) / "cdc_report.json");
    if (opts.format == "html" || opts.format == "all")
        report.generateHTML(fs::path(opts.outputDir) / "cdc_report.html", opts.topModule);
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
