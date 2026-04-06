#include "MetricsRunner.h"
#include "CompilationSession.h"
#include "JsonUtils.h"
#include "metrics/TransformExtractor.h"
#include "metrics/TransformGraph.h"
#include "metrics/BaselineDiff.h"
#include "metrics/ConeAnalyzer.h"
#include "metrics/Normalization.h"

#include <slang/ast/Compilation.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>

#include <algorithm>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace metrics {

namespace {

struct RootResult {
    std::string root_id;
    std::string root_kind;
    ConeSummary cone;
    NormalizationResult norm;
};

struct FfPathResult {
    std::string source_ff;
    std::string dest_ff;
    bool has_comb_logic = false;
    uint32_t comb_signal_count = 0;
    uint32_t normalized_comb_count = 0;
    std::string sync_type = "unknown";
    std::vector<std::string> path;
    bool approximate = false;
    std::string provenance_level = "hint_only";
};

std::string jsonBool(bool v) { return v ? "true" : "false"; }

using svlens::jsonStr;

void writeJsonReport(const std::string& outputDir,
                     const std::string& topModule,
                     const std::vector<RootResult>& results,
                     const std::vector<FfPathResult>& ffPaths,
                     const TransformGraph& graph,
                     const BaselineDiffResult& diff,
                     const MetricsCliOptions& opts) {
    fs::create_directories(outputDir);
    std::ofstream ofs(fs::path(outputDir) / "metrics_report.json");
    if (!ofs) {
        fmt::print(stderr, "Error: cannot write to {}/metrics_report.json\n", outputDir);
        return;
    }

    int outputCount = 0;
    int ffdCount = 0;
    uint32_t totalRaw = 0;
    uint32_t totalNorm = 0;
    uint32_t totalLaneGroups = 0;
    uint32_t approxCones = 0;
    uint32_t maxOutputCone = 0;
    uint32_t maxFfdCone = 0;

    // Merge normalization groups across all cones
    std::map<std::string, NormalizationGroup> mergedGroups;

    for (auto& r : results) {
        if (r.root_kind == "output") {
            ++outputCount;
            if (r.cone.raw_node_count > maxOutputCone)
                maxOutputCone = r.cone.raw_node_count;
        } else {
            ++ffdCount;
            if (r.cone.raw_node_count > maxFfdCone)
                maxFfdCone = r.cone.raw_node_count;
        }
        totalRaw += r.cone.raw_node_count;
        totalNorm += r.norm.normalized_count;
        if (r.cone.approximate) ++approxCones;

        for (auto& g : r.norm.groups) {
            auto& mg = mergedGroups[g.signature];
            mg.signature = g.signature;
            mg.multiplicity += g.multiplicity;
            mg.collapsed_from += g.collapsed_from;
            if (mg.representative_width == 0)
                mg.representative_width = g.representative_width;
        }
    }
    totalLaneGroups = static_cast<uint32_t>(mergedGroups.size());

    // Aggregate unsupported events by kind, keeping up to 3 examples
    struct UnsupAgg { uint32_t count = 0; std::vector<std::string> examples; };
    std::map<std::string, UnsupAgg> unsupportedAgg;
    for (auto& evt : graph.unsupported_events) {
        auto& agg = unsupportedAgg[evt.kind];
        agg.count++;
        if (agg.examples.size() < 3 && !evt.detail.empty())
            agg.examples.push_back(evt.detail);
    }

    ofs << "{\n";
    ofs << "  \"version\": \"1.1\",\n";
    ofs << "  \"tool_version\": \"" << SVLENS_VERSION << "\",\n";
    ofs << "  \"top\": " << jsonStr(topModule) << ",\n";

    // summary
    ofs << "  \"summary\": {\n";
    ofs << "    \"outputs_analyzed\": " << outputCount << ",\n";
    ofs << "    \"ff_d_roots_analyzed\": " << ffdCount << ",\n";
    ofs << "    \"cones_analyzed\": " << results.size() << ",\n";
    ofs << "    \"approximate_cones\": " << approxCones << ",\n";
    ofs << "    \"unsupported_count\": " << graph.unsupported_events.size() << "\n";
    ofs << "  },\n";

    // analysis
    ofs << "  \"analysis\": {\n";
    ofs << "    \"raw_transform_count\": " << totalRaw << ",\n";
    ofs << "    \"normalized_transform_count\": " << totalNorm << ",\n";
    ofs << "    \"repeated_lane_groups\": " << totalLaneGroups << ",\n";
    ofs << "    \"ff_to_ff_paths\": " << ffPaths.size() << ",\n";
    ofs << "    \"max_output_cone\": " << maxOutputCone << ",\n";
    ofs << "    \"max_ffd_cone\": " << maxFfdCone << "\n";
    ofs << "  },\n";

    // roots
    ofs << "  \"roots\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        auto& r = results[i];
        ofs << "    {\n";
        ofs << "      \"root_id\": " << jsonStr(r.root_id) << ",\n";
        ofs << "      \"root_kind\": " << jsonStr(r.root_kind) << ",\n";
        ofs << "      \"raw_node_count\": " << r.cone.raw_node_count << ",\n";
        ofs << "      \"logic_depth_est\": " << r.cone.logic_depth_est << ",\n";
        ofs << "      \"normalized_transform_count\": " << r.norm.normalized_count << ",\n";
        ofs << "      \"repeated_lane_group_count\": "
            << static_cast<uint32_t>(r.norm.groups.size()) << ",\n";
        ofs << "      \"source_inputs\": " << r.cone.source_input_count << ",\n";
        ofs << "      \"source_ffs\": " << r.cone.source_ff_count << ",\n";
        ofs << "      \"approximate\": " << jsonBool(r.cone.approximate) << "\n";
        ofs << "    }";
        if (i + 1 < results.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "  ],\n";

    // ff_paths
    ofs << "  \"ff_paths\": [\n";
    for (size_t i = 0; i < ffPaths.size(); ++i) {
        auto& fp = ffPaths[i];
        ofs << "    {\n";
        ofs << "      \"source_ff\": " << jsonStr(fp.source_ff) << ",\n";
        ofs << "      \"dest_ff\": " << jsonStr(fp.dest_ff) << ",\n";
        ofs << "      \"has_comb_logic\": " << jsonBool(fp.has_comb_logic) << ",\n";
        ofs << "      \"comb_signal_count\": " << fp.comb_signal_count << ",\n";
        ofs << "      \"normalized_comb_count\": " << fp.normalized_comb_count << ",\n";
        ofs << "      \"sync_type\": " << jsonStr(fp.sync_type) << ",\n";
        ofs << "      \"path\": [";
        for (size_t j = 0; j < fp.path.size(); ++j) {
            ofs << jsonStr(fp.path[j]);
            if (j + 1 < fp.path.size()) ofs << ", ";
        }
        ofs << "],\n";
        ofs << "      \"approximate\": " << jsonBool(fp.approximate) << ",\n";
        ofs << "      \"provenance_level\": " << jsonStr(fp.provenance_level) << "\n";
        ofs << "    }";
        if (i + 1 < ffPaths.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "  ],\n";

    // normalization
    ofs << "  \"normalization\": {\n";
    ofs << "    \"enabled\": " << jsonBool(opts.normalizeBitLanes) << ",\n";
    ofs << "    \"lane_min_width\": " << opts.laneMinWidth << ",\n";
    ofs << "    \"groups\": [\n";
    {
        size_t gi = 0;
        for (auto& [sig, g] : mergedGroups) {
            ofs << "      {\n";
            ofs << "        \"signature\": " << jsonStr(g.signature) << ",\n";
            ofs << "        \"multiplicity\": " << g.multiplicity << ",\n";
            ofs << "        \"representative_width\": " << g.representative_width << ",\n";
            ofs << "        \"collapsed_from\": " << g.collapsed_from << "\n";
            ofs << "      }";
            if (gi + 1 < mergedGroups.size()) ofs << ",";
            ofs << "\n";
            ++gi;
        }
    }
    ofs << "    ]\n";
    ofs << "  },\n";

    // unsupported
    ofs << "  \"unsupported\": [\n";
    {
        size_t ui = 0;
        for (auto& [kind, agg] : unsupportedAgg) {
            ofs << "    {\n";
            ofs << "      \"kind\": " << jsonStr(kind) << ",\n";
            ofs << "      \"count\": " << agg.count;
            if (!agg.examples.empty()) {
                ofs << ",\n      \"examples\": [";
                for (size_t ei = 0; ei < agg.examples.size(); ++ei) {
                    ofs << jsonStr(agg.examples[ei]);
                    if (ei + 1 < agg.examples.size()) ofs << ", ";
                }
                ofs << "]";
            }
            ofs << "\n    }";
            if (ui + 1 < unsupportedAgg.size()) ofs << ",";
            ofs << "\n";
            ++ui;
        }
    }
    ofs << "  ]";

    // Baseline diff (if available)
    if (diff.has_baseline) {
        ofs << ",\n  \"baseline_diff\": {\n";
        ofs << "    \"total_raw_delta\": " << diff.total_raw_delta << ",\n";
        ofs << "    \"total_norm_delta\": " << diff.total_norm_delta << ",\n";
        ofs << "    \"regressions\": " << diff.regressions << ",\n";
        ofs << "    \"improvements\": " << diff.improvements << ",\n";
        ofs << "    \"new_roots\": " << diff.new_roots << ",\n";
        ofs << "    \"removed_roots\": " << diff.removed_roots << ",\n";
        ofs << "    \"root_diffs\": [\n";
        for (size_t i = 0; i < diff.root_diffs.size(); ++i) {
            auto& rd = diff.root_diffs[i];
            ofs << "      {\"root_id\": " << jsonStr(rd.root_id)
                << ", \"root_kind\": " << jsonStr(rd.root_kind)
                << ", \"raw_delta\": " << rd.raw_delta
                << ", \"depth_delta\": " << rd.depth_delta
                << ", \"norm_delta\": " << rd.norm_delta
                << ", \"is_new\": " << jsonBool(rd.is_new)
                << ", \"is_removed\": " << jsonBool(rd.is_removed) << "}";
            if (i + 1 < diff.root_diffs.size()) ofs << ",";
            ofs << "\n";
        }
        ofs << "    ]\n";
        ofs << "  }";
    }

    // --emit-cones: per-root node detail
    if (opts.emitCones) {
        ofs << ",\n  \"cone_detail\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            auto& r = results[i];
            ofs << "    {\n";
            ofs << "      \"root_id\": " << jsonStr(r.root_id) << ",\n";
            ofs << "      \"nodes\": [\n";
            for (size_t j = 0; j < r.cone.cone_nodes.size(); ++j) {
                auto& node = graph.nodes[r.cone.cone_nodes[j]];
                ofs << "        {\"id\": " << node.id
                    << ", \"op\": " << jsonStr(node.opKindStr())
                    << ", \"detail\": " << jsonStr(node.op_detail)
                    << ", \"width\": " << node.bit_width
                    << ", \"approx\": " << jsonBool(node.approximate) << "}";
                if (j + 1 < r.cone.cone_nodes.size()) ofs << ",";
                ofs << "\n";
            }
            ofs << "      ]\n";
            ofs << "    }";
            if (i + 1 < results.size()) ofs << ",";
            ofs << "\n";
        }
        ofs << "  ]";
    }

    // --emit-raw-graph: full transform graph
    if (opts.emitRawGraph) {
        ofs << ",\n  \"raw_graph\": {\n";
        ofs << "    \"node_count\": " << graph.nodes.size() << ",\n";
        ofs << "    \"nodes\": [\n";
        for (size_t i = 0; i < graph.nodes.size(); ++i) {
            auto& node = graph.nodes[i];
            ofs << "      {\"id\": " << node.id
                << ", \"op\": " << jsonStr(node.opKindStr())
                << ", \"detail\": " << jsonStr(node.op_detail)
                << ", \"output\": " << jsonStr(node.output.canonical())
                << ", \"inputs\": [";
            for (size_t k = 0; k < node.inputs.size(); ++k) {
                ofs << jsonStr(node.inputs[k].canonical());
                if (k + 1 < node.inputs.size()) ofs << ", ";
            }
            ofs << "], \"width\": " << node.bit_width
                << ", \"approx\": " << jsonBool(node.approximate)
                << ", \"source_loc\": " << jsonStr(node.source_loc) << "}";
            if (i + 1 < graph.nodes.size()) ofs << ",";
            ofs << "\n";
        }
        ofs << "    ]\n";
        ofs << "  }";
    }

    ofs << "\n}\n";
}

void writeMarkdownReport(const std::string& outputDir,
                         const std::string& topModule,
                         const std::vector<RootResult>& results,
                         const std::vector<FfPathResult>& ffPaths,
                         const TransformGraph& graph,
                         const MetricsCliOptions& opts) {
    fs::create_directories(outputDir);
    std::ofstream ofs(fs::path(outputDir) / "metrics_report.md");
    if (!ofs) return;

    ofs << "# Metrics Report: " << topModule << "\n\n";
    ofs << "Schema version: 1.1 | Tool: svlens " << SVLENS_VERSION << "\n\n";

    // Summary
    uint32_t totalRaw = 0, totalNorm = 0, approxCones = 0;
    for (auto& r : results) {
        totalRaw += r.cone.raw_node_count;
        totalNorm += r.norm.normalized_count;
        if (r.cone.approximate) ++approxCones;
    }
    ofs << "## Summary\n\n";
    ofs << "| Metric | Value |\n|---|---|\n";
    ofs << "| Roots analyzed | " << results.size() << " |\n";
    ofs << "| Raw transforms | " << totalRaw << " |\n";
    ofs << "| Normalized transforms | " << totalNorm << " |\n";
    ofs << "| Approximate cones | " << approxCones << " |\n";
    ofs << "| FF-to-FF paths | " << ffPaths.size() << " |\n";
    ofs << "| Unsupported events | " << graph.unsupported_events.size() << " |\n\n";

    // Roots table
    ofs << "## Roots\n\n";
    ofs << "| Root | Kind | Raw | Depth | Normalized | Inputs | FFs | Approx |\n";
    ofs << "|---|---|---|---|---|---|---|---|\n";
    for (auto& r : results) {
        ofs << "| " << r.root_id << " | " << r.root_kind
            << " | " << r.cone.raw_node_count
            << " | " << r.cone.logic_depth_est
            << " | " << r.norm.normalized_count
            << " | " << r.cone.source_input_count
            << " | " << r.cone.source_ff_count
            << " | " << (r.cone.approximate ? "yes" : "no") << " |\n";
    }
    ofs << "\n";

    // FF paths
    if (!ffPaths.empty()) {
        ofs << "## FF-to-FF Paths\n\n";
        ofs << "| Source | Dest | Comb | Signals | Sync | Provenance |\n";
        ofs << "|---|---|---|---|---|---|\n";
        for (auto& fp : ffPaths) {
            ofs << "| " << fp.source_ff << " | " << fp.dest_ff
                << " | " << (fp.has_comb_logic ? "yes" : "no")
                << " | " << fp.comb_signal_count
                << " | " << fp.sync_type
                << " | " << fp.provenance_level << " |\n";
        }
        ofs << "\n";
    }

    // Unsupported
    if (!graph.unsupported_events.empty()) {
        ofs << "## Unsupported Constructs\n\n";
        std::map<std::string, uint32_t> counts;
        for (auto& evt : graph.unsupported_events) counts[evt.kind]++;
        for (auto& [kind, count] : counts)
            ofs << "- **" << kind << "**: " << count << " occurrences\n";
        ofs << "\n";
    }
}

} // anonymous namespace

int runMetricsWithCompilation(slang::ast::Compilation& compilation,
                              const MetricsCliOptions& opts) {
    auto* topInst = compilation.getRoot().topInstances.empty()
                        ? nullptr
                        : compilation.getRoot().topInstances[0];

    for (auto* inst : compilation.getRoot().topInstances) {
        if (inst->name == opts.topModule) {
            topInst = inst;
            break;
        }
    }

    if (!topInst || topInst->name != opts.topModule) {
        fmt::print(stderr, "Error: top module '{}' not found\n", opts.topModule);
        return 1;
    }

    // Step 1: Extract transformation graph
    TransformExtractor extractor(compilation, opts.topModule, opts.maxForUnroll);
    TransformGraph graph = extractor.extract();

    // Step 2: Analyze cones for each root
    ConeAnalyzer coneAnalyzer(graph, opts.maxDepth);

    std::vector<RootResult> results;
    for (auto& root : graph.roots) {
        // Filter based on --roots option
        bool isOutput = (root.kind == ValueRef::Port);
        bool isFfD = (root.kind == ValueRef::FfDSink);

        if (opts.roots == "outputs" && !isOutput) continue;
        if (opts.roots == "ffd" && !isFfD) continue;

        ConeSummary cone = coneAnalyzer.analyzeCone(root);

        // Step 3: Normalize
        NormalizationResult norm = normalizeCone(
            graph, cone.cone_nodes, opts.normalizeBitLanes, opts.laneMinWidth);

        RootResult rr;
        rr.root_id = root.base_name;
        rr.root_kind = isOutput ? "output" : "ff_d";
        rr.cone = std::move(cone);
        rr.norm = std::move(norm);
        results.push_back(std::move(rr));
    }

    // Sort results for deterministic output
    std::sort(results.begin(), results.end(),
              [](const RootResult& a, const RootResult& b) {
                  if (a.root_kind != b.root_kind) return a.root_kind < b.root_kind;
                  return a.root_id < b.root_id;
              });

    // Step 4: Build FF-to-FF paths
    // For each pair of FFs, check if dest FF's D-side cone reaches source FF's Q
    std::vector<FfPathResult> ffPaths;
    {
        // Build a set of FF Q names for quick lookup
        std::unordered_map<std::string, std::string> ffQNames; // q_canonical -> ff name
        for (auto& ff : graph.flip_flops)
            ffQNames[ff.q_ref.canonical()] = ff.name;

        // For each FF, check its D-side cone for references to other FF Qs
        ConeAnalyzer ffConeAnalyzer(graph, opts.maxDepth);
        std::unordered_map<std::string, ConeSummary> dConeCache;

        for (auto& destFf : graph.flip_flops) {
            ValueRef dRoot;
            dRoot.hier_path = destFf.d_ref.canonical();
            dRoot.base_name = destFf.d_ref.base_name;
            dRoot.kind = ValueRef::FfDSink;

            auto cacheKey = dRoot.canonical();
            auto [cacheIt, inserted] = dConeCache.try_emplace(cacheKey);
            if (inserted)
                cacheIt->second = ffConeAnalyzer.analyzeCone(dRoot);
            auto& dCone = cacheIt->second;

            // Normalize once per D-side cone
            auto normResult = normalizeCone(graph, dCone.cone_nodes,
                                            opts.normalizeBitLanes,
                                            opts.laneMinWidth);

            // Check which signals in the cone are FF Q outputs
            std::unordered_set<std::string> visitedSignals;
            for (auto nodeId : dCone.cone_nodes) {
                auto& node = graph.nodes[nodeId];
                for (auto& input : node.inputs) {
                    auto qIt = ffQNames.find(input.canonical());
                    if (qIt != ffQNames.end() &&
                        visitedSignals.insert(qIt->first).second) {
                        FfPathResult fp;
                        fp.source_ff = qIt->second;
                        fp.dest_ff = destFf.name;
                        fp.has_comb_logic = (dCone.raw_node_count > 1);
                        fp.comb_signal_count = dCone.raw_node_count;
                        fp.normalized_comb_count = normResult.normalized_count;
                        fp.path = dCone.signal_chain;
                        fp.sync_type = fp.has_comb_logic ? "combinational" : "direct";
                        fp.approximate = dCone.approximate;
                        if (dCone.approximate)
                            fp.sync_type = "unknown";
                        fp.provenance_level = dCone.approximate
                            ? "partial_slice" : "provenance_backed";
                        ffPaths.push_back(std::move(fp));
                    }
                }
            }
        }

        // Sort for deterministic output: source_ff, dest_ff
        std::sort(ffPaths.begin(), ffPaths.end(),
                  [](const FfPathResult& a, const FfPathResult& b) {
                      if (a.source_ff != b.source_ff) return a.source_ff < b.source_ff;
                      return a.dest_ff < b.dest_ff;
                  });
    }

    // Step 5: Apply --topk filtering (sort by complexity, keep top N)
    std::vector<RootResult> displayResults = results;
    if (opts.topK > 0 && static_cast<int>(displayResults.size()) > opts.topK) {
        std::sort(displayResults.begin(), displayResults.end(),
                  [](const RootResult& a, const RootResult& b) {
                      return a.cone.raw_node_count > b.cone.raw_node_count;
                  });
        displayResults.resize(static_cast<size_t>(opts.topK));
        // Re-sort for deterministic output
        std::sort(displayResults.begin(), displayResults.end(),
                  [](const RootResult& a, const RootResult& b) {
                      if (a.root_kind != b.root_kind) return a.root_kind < b.root_kind;
                      return a.root_id < b.root_id;
                  });
    }

    // Step 6: Baseline diff (always against full results, not topK-filtered)
    BaselineDiffResult diff;
    if (!opts.baselineFile.empty()) {
        std::vector<std::tuple<std::string, std::string, uint32_t, uint32_t, uint32_t>> currentRoots;
        for (auto& r : results)
            currentRoots.emplace_back(r.root_id, r.root_kind,
                                      r.cone.raw_node_count, r.cone.logic_depth_est,
                                      r.norm.normalized_count);
        diff = computeBaselineDiff(opts.baselineFile, currentRoots);
    }

    // Step 7: Write reports
    if (opts.format == "json" || opts.format == "both") {
        writeJsonReport(opts.outputDir, opts.topModule, displayResults, ffPaths, graph, diff, opts);
    }
    if (opts.format == "md" || opts.format == "both") {
        writeMarkdownReport(opts.outputDir, opts.topModule, displayResults, ffPaths, graph, opts);
    }

    fmt::print("Metrics analysis complete: {} roots analyzed, "
               "{} transform nodes extracted\n",
               results.size(), graph.nodes.size());
    if (opts.format == "json" || opts.format == "both")
        fmt::print("Report: {}/metrics_report.json\n", opts.outputDir);
    if (opts.format == "md" || opts.format == "both")
        fmt::print("Report: {}/metrics_report.md\n", opts.outputDir);

    if (diff.has_baseline && diff.regressions > 0) {
        fmt::print("Baseline comparison: {} regressions, {} improvements, "
                   "raw delta {}\n",
                   diff.regressions, diff.improvements, diff.total_raw_delta);
        if (opts.failOnRegression) {
            fmt::print(stderr, "Error: metrics regression detected (--fail-on-regression)\n");
            return 2;
        }
    }

    return 0;
}

} // namespace metrics

int runMetricsMain(int argc, char* argv[]) {
    std::vector<std::string> compilationArgs;
    auto opts = metrics::parseMetricsArgs(argc, argv, compilationArgs);

    if (opts.showHelp) {
        metrics::printMetricsUsage();
        return 0;
    }
    if (opts.showVersion) {
        fmt::print("svlens metrics " SVLENS_VERSION "\n");
        return 0;
    }
    if (!metrics::validateMetricsOptions(opts))
        return 1;

    svlens::CompilationSession session;
    std::string error;
    if (!session.compile(compilationArgs, &error)) {
        fmt::print(stderr, "Error: {}\n", error);
        return 1;
    }

    return metrics::runMetricsWithCompilation(session.compilation(), opts);
}
