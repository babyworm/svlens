#include "ConnRunnerUtils.h"

#include "CheckerRunner.h"
#include "ClockResetAnalyzer.h"
#include "ConnectionExtractor.h"
#include "ConventionChecker.h"
#include "CsvReport.h"
#include "DanglingChecker.h"
#include "DotReport.h"
#include "ExpectChecker.h"
#include "GraphDiff.h"
#include "InterfaceGrouper.h"
#include "HtmlReport.h"
#include "TraceEngine.h"
#include "JsonReport.h"
#include "MarkdownReport.h"
#include "ProtocolChecker.h"
#include "TableReport.h"
#include "TypeChecker.h"
#include "UndrivenChecker.h"
#include "WidthChecker.h"

#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace connect {

bool buildConnectionGraph(slang::ast::Compilation& compilation,
                          const ConnCliOptions& opts,
                          ConnectionGraph& graph) {
    ConnectionExtractor extractor(compilation, opts.topModule, opts.depth);
    graph = extractor.extract();

    if (graph.allPorts.empty()) {
        fmt::print(stderr, "Warning: no ports found for top module '{}'\n", opts.topModule);
    }
    return true;
}

std::vector<Issue> runConnCheckers(const ConnCliOptions& opts,
                                   const ConnectionGraph& graph) {
    CheckerRunner runner;
    if (opts.checkWidth)
        runner.addChecker(std::make_unique<WidthChecker>());
    if (opts.checkType)
        runner.addChecker(std::make_unique<TypeChecker>());
    if (opts.checkDangling)
        runner.addChecker(std::make_unique<DanglingChecker>());
    if (opts.checkUndriven)
        runner.addChecker(std::make_unique<UndrivenChecker>());
    if (opts.checkProtocol)
        runner.addChecker(std::make_unique<ProtocolChecker>());
    if (opts.checkConvention) {
        auto rules = opts.conventionFile.empty()
            ? ConventionRules{}
            : loadConventionRules(opts.conventionFile);
        runner.addChecker(std::make_unique<ConventionChecker>(rules));
    }
    if (!opts.expectFile.empty())
        runner.addChecker(std::make_unique<ExpectChecker>(opts.expectFile));

    return runner.runAll(graph);
}

template <typename Generator>
static void writeReportFile(const std::string& outputDir,
                            const std::string& filename,
                            const ReportData& data) {
    fs::create_directories(outputDir);
    std::string path = (fs::path(outputDir) / filename).string();
    std::ofstream ofs(path);
    if (ofs) {
        Generator gen;
        gen.generate(data, ofs);
    } else {
        fmt::print(stderr, "Error: cannot write to {}\n", path);
    }
}

void generateConnReports(const ConnCliOptions& opts, const ReportData& data) {
    if (opts.format == "table" || opts.format == "all") {
        TableReportGenerator tableGen;
        tableGen.generate(data, std::cout);
    }
    if (opts.format == "json" || opts.format == "all")
        writeReportFile<JsonReportGenerator>(opts.outputDir, "connect_report.json", data);
    if (opts.format == "md" || opts.format == "all")
        writeReportFile<MarkdownReportGenerator>(opts.outputDir, "connect_report.md", data);
    if (opts.format == "csv" || opts.format == "all")
        writeReportFile<CsvReportGenerator>(opts.outputDir, "connection_matrix.csv", data);
    if (opts.format == "dot" || opts.format == "all")
        writeReportFile<DotReportGenerator>(opts.outputDir, "connectivity.dot", data);
    if (opts.format == "html" || opts.format == "all")
        writeReportFile<HtmlReportGenerator>(opts.outputDir, "connect_report.html", data);
}

void printConnInterfaceSummary(const ConnCliOptions& opts, const ReportData& data) {
    if (opts.format != "table" && opts.format != "all")
        return;

    InterfaceGrouper grouper;
    auto groups = grouper.classify(data.graph);
    if (!groups.empty()) {
        fmt::print("\n=== Interface Summary ===\n");
        for (const auto& g : groups) {
            auto dotPos = g.instancePath.rfind('.');
            std::string shortName = (dotPos != std::string::npos)
                ? g.instancePath.substr(dotPos + 1) : g.instancePath;
            fmt::print("  {:16s}: {} {} ({} signals, prefix: {})\n",
                       shortName, g.protocol, g.role,
                       g.matchedPorts.size(), g.prefix);
        }
        fmt::print("\n");
    }
}

void runConnDiffMode(const ConnCliOptions& opts, const ReportData& data) {
    if (opts.diffFile.empty())
        return;

    try {
        auto baseline = loadDiffInputFromJson(opts.diffFile);

        std::unordered_map<std::string, std::string> issueMap;
        for (const auto& issue : data.active) {
            if (issue.connection.has_value()) {
                auto key = issue.connection->source.fullPath() + "|" + issue.connection->dest.fullPath();
                issueMap.emplace(key, Issue::typeToString(issue.type));
            }
        }

        DiffInput current;
        for (const auto& conn : data.graph.connections) {
            auto key = conn.source.fullPath() + "|" + conn.dest.fullPath();
            auto it = issueMap.find(key);
            std::string status = (it != issueMap.end()) ? it->second : "OK";
            current.connections.push_back({conn.source.fullPath(), conn.dest.fullPath(), status});
        }

        auto diff = computeDiff(baseline, current);

        if (diff.empty()) {
            fmt::print("\nDiff: no connectivity changes vs baseline.\n");
        } else {
            fmt::print("\n=== Connectivity Diff vs {} ===\n", opts.diffFile);
            for (const auto& c : diff.added)
                fmt::print("  + ADDED:   {} -> {}  [{}]\n", c.source, c.dest, c.status);
            for (const auto& c : diff.removed)
                fmt::print("  - REMOVED: {} -> {}  [was: {}]\n", c.source, c.dest, c.status);
            for (const auto& c : diff.changed)
                fmt::print("  ~ CHANGED: {} -> {}  status: {} -> {}\n",
                           c.source, c.dest, c.oldStatus, c.newStatus);
            fmt::print("  Total: +{} -{} ~{}\n",
                       diff.added.size(), diff.removed.size(), diff.changed.size());
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "Error loading diff baseline: {}\n", e.what());
    }
}

void printConnClockResetTopology(const ReportData& data) {
    ClockResetAnalyzer analyzer;
    auto topo = analyzer.analyze(data.graph);

    fmt::print("\n=== Clock Topology ===\n");
    for (const auto& [portName, instances] : topo.clockGroups) {
        fmt::print("  {} ->", portName);
        for (size_t i = 0; i < instances.size(); ++i) {
            const auto& inst = instances[i];
            auto dotPos = inst.rfind('.');
            std::string shortName = (dotPos != std::string::npos)
                ? inst.substr(dotPos + 1) : inst;
            if (i > 0) fmt::print(",");
            fmt::print(" {}", shortName);
        }
        fmt::print("\n");
    }

    fmt::print("\n=== Reset Topology ===\n");
    for (const auto& [portName, instances] : topo.resetGroups) {
        fmt::print("  {} ->", portName);
        for (size_t i = 0; i < instances.size(); ++i) {
            const auto& inst = instances[i];
            auto dotPos = inst.rfind('.');
            std::string shortName = (dotPos != std::string::npos)
                ? inst.substr(dotPos + 1) : inst;
            if (i > 0) fmt::print(",");
            fmt::print(" {}", shortName);
        }
        fmt::print("\n");
    }

    if (!topo.warnings.empty()) {
        fmt::print("\n=== Clock/Reset Warnings ===\n");
        for (const auto& inst : topo.warnings) {
            auto dotPos = inst.rfind('.');
            std::string shortName = (dotPos != std::string::npos)
                ? inst.substr(dotPos + 1) : inst;
            fmt::print("  ⚠ {} ({}): has clock input but no reset port detected\n", inst, shortName);
        }
    }
    fmt::print("\n");
}

void runConnSignalTrace(const ConnCliOptions& opts, const ReportData& data) {
    if (opts.traceSignal.empty())
        return;

    TraceEngine traceEngine(data.graph);
    auto fanOutHops = traceEngine.traceFanOut(opts.traceSignal);
    fmt::print("\n{}", TraceEngine::formatTrace(fanOutHops, opts.traceSignal, true));
    auto fanInHops = traceEngine.traceFanIn(opts.traceSignal);
    fmt::print("\n{}", TraceEngine::formatTrace(fanInHops, opts.traceSignal, false));
}

} // namespace connect
