#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "AnalysisEngine.h"
#include "TestUtils.h"

using namespace connect;
using slang::ast::ArgumentDirection;
using testutils::makePort;

static Issue makeIssue(Issue::Type type, Issue::Severity sev,
                       const PortInfo& port, const std::string& detail,
                       std::optional<Connection> conn = std::nullopt) {
    Issue iss;
    iss.type = type;
    iss.severity = sev;
    iss.port = port;
    iss.detail = detail;
    iss.connection = conn;
    return iss;
}

// Build a realistic test scenario:
//   soc_top
//     u_cpu: 6 ports (1 error issue: WIDTH_MISMATCH truncation)
//     u_bus: 8 ports (clean — no issues)
//     u_mem: 4 ports (1 DANGLING_OUTPUT warn, 1 TYPE_MISMATCH error)
//   Connections: cpu->bus (3), bus->mem (2), bus->cpu (2)
static ReportData makeAnalysisTestData() {
    ReportData data;
    data.topModule = "soc_top";
    data.graph.topModule = "soc_top";

    // ---- u_cpu ports (6) ----
    PortInfo cpu_o_data  = makePort("soc_top.u_cpu", "o_data",  ArgumentDirection::Out, 32);
    PortInfo cpu_o_addr  = makePort("soc_top.u_cpu", "o_addr",  ArgumentDirection::Out, 24);
    PortInfo cpu_o_valid = makePort("soc_top.u_cpu", "o_valid", ArgumentDirection::Out, 1);
    PortInfo cpu_i_rdata = makePort("soc_top.u_cpu", "i_rdata", ArgumentDirection::In, 32);
    PortInfo cpu_i_ready = makePort("soc_top.u_cpu", "i_ready", ArgumentDirection::In, 1);
    PortInfo cpu_i_irq   = makePort("soc_top.u_cpu", "i_irq",  ArgumentDirection::In, 1);

    // ---- u_bus ports (8) ----
    PortInfo bus_i_data  = makePort("soc_top.u_bus", "i_data",  ArgumentDirection::In, 16); // narrower: truncation target
    PortInfo bus_i_addr  = makePort("soc_top.u_bus", "i_addr",  ArgumentDirection::In, 24);
    PortInfo bus_i_valid = makePort("soc_top.u_bus", "i_valid", ArgumentDirection::In, 1);
    PortInfo bus_o_rdata = makePort("soc_top.u_bus", "o_rdata", ArgumentDirection::Out, 32);
    PortInfo bus_o_ready = makePort("soc_top.u_bus", "o_ready", ArgumentDirection::Out, 1);
    PortInfo bus_o_maddr = makePort("soc_top.u_bus", "o_maddr", ArgumentDirection::Out, 24);
    PortInfo bus_o_mdata = makePort("soc_top.u_bus", "o_mdata", ArgumentDirection::Out, 16);
    PortInfo bus_o_irq   = makePort("soc_top.u_bus", "o_irq",  ArgumentDirection::Out, 1);

    // ---- u_mem ports (4) ----
    PortInfo mem_i_addr  = makePort("soc_top.u_mem", "i_addr",  ArgumentDirection::In, 24);
    PortInfo mem_i_data  = makePort("soc_top.u_mem", "i_data",  ArgumentDirection::In, 16);
    PortInfo mem_o_rdata = makePort("soc_top.u_mem", "o_rdata", ArgumentDirection::Out, 16);
    PortInfo mem_o_debug = makePort("soc_top.u_mem", "o_debug", ArgumentDirection::Out, 8); // dangling

    // ---- Connections: cpu->bus (3) ----
    Connection c1{cpu_o_data, bus_i_data};   // 32->16 truncation!
    Connection c2{cpu_o_addr, bus_i_addr};
    Connection c3{cpu_o_valid, bus_i_valid};

    // ---- Connections: bus->mem (2) ----
    Connection c4{bus_o_maddr, mem_i_addr};
    Connection c5{bus_o_mdata, mem_i_data};

    // ---- Connections: bus->cpu (2) ----
    Connection c6{bus_o_rdata, cpu_i_rdata};
    Connection c7{bus_o_ready, cpu_i_ready};

    data.graph.connections = {c1, c2, c3, c4, c5, c6, c7};

    data.graph.allPorts = {
        cpu_o_data, cpu_o_addr, cpu_o_valid, cpu_i_rdata, cpu_i_ready, cpu_i_irq,
        bus_i_data, bus_i_addr, bus_i_valid, bus_o_rdata, bus_o_ready, bus_o_maddr, bus_o_mdata, bus_o_irq,
        mem_i_addr, mem_i_data, mem_o_rdata, mem_o_debug
    };

    // Mark connected ports
    data.graph.connectedPorts = {
        "soc_top.u_cpu.o_data", "soc_top.u_cpu.o_addr", "soc_top.u_cpu.o_valid",
        "soc_top.u_cpu.i_rdata", "soc_top.u_cpu.i_ready",
        // cpu_i_irq is NOT connected
        "soc_top.u_bus.i_data", "soc_top.u_bus.i_addr", "soc_top.u_bus.i_valid",
        "soc_top.u_bus.o_rdata", "soc_top.u_bus.o_ready",
        "soc_top.u_bus.o_maddr", "soc_top.u_bus.o_mdata", "soc_top.u_bus.o_irq",
        "soc_top.u_mem.i_addr", "soc_top.u_mem.i_data", "soc_top.u_mem.o_rdata"
        // mem_o_debug is NOT connected (dangling)
    };

    // ---- Issues ----
    // 1) WIDTH_MISMATCH with truncation (source > dest): ERROR on cpu_o_data -> bus_i_data
    data.active.push_back(makeIssue(
        Issue::Type::WIDTH_MISMATCH, Issue::Severity::ERROR,
        cpu_o_data, "width 32 -> 16: truncation",
        Connection{cpu_o_data, bus_i_data}));

    // 2) DANGLING_OUTPUT: WARN on mem_o_debug
    data.active.push_back(makeIssue(
        Issue::Type::DANGLING_OUTPUT, Issue::Severity::WARN,
        mem_o_debug, "output port o_debug is unconnected"));

    // 3) TYPE_MISMATCH: ERROR on mem_i_data (signed vs unsigned)
    PortInfo mem_i_data_signed = mem_i_data;
    mem_i_data_signed.isSigned = true;
    data.active.push_back(makeIssue(
        Issue::Type::TYPE_MISMATCH, Issue::Severity::ERROR,
        mem_i_data, "signed/unsigned mismatch",
        Connection{bus_o_mdata, mem_i_data_signed}));

    return data;
}

TEST_CASE("AnalysisEngine module health scores computed correctly", "[AnalysisEngine]") {
    auto data = makeAnalysisTestData();
    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.moduleHealth.size() == 3);

    // Sorted by score ascending (worst first)
    // u_mem: 1 error + 1 warn = 1.0 - 0.15 - 0.05 = 0.80
    // u_cpu: 1 error            = 1.0 - 0.15        = 0.85
    // u_bus: clean              = 1.0
    CHECK(result.moduleHealth[0].shortName == "u_mem");
    CHECK_THAT(result.moduleHealth[0].score, Catch::Matchers::WithinAbs(0.80, 0.001));
    CHECK(result.moduleHealth[0].errorCount == 1);
    CHECK(result.moduleHealth[0].warnCount == 1);

    CHECK(result.moduleHealth[1].shortName == "u_cpu");
    CHECK_THAT(result.moduleHealth[1].score, Catch::Matchers::WithinAbs(0.85, 0.001));
    CHECK(result.moduleHealth[1].errorCount == 1);
    CHECK(result.moduleHealth[1].warnCount == 0);

    CHECK(result.moduleHealth[2].shortName == "u_bus");
    CHECK_THAT(result.moduleHealth[2].score, Catch::Matchers::WithinAbs(1.0, 0.001));
    CHECK(result.moduleHealth[2].errorCount == 0);
    CHECK(result.moduleHealth[2].warnCount == 0);
}

TEST_CASE("AnalysisEngine overall score is average of module scores", "[AnalysisEngine]") {
    auto data = makeAnalysisTestData();
    AnalysisEngine engine;
    auto result = engine.analyze(data);

    // (0.80 + 0.85 + 1.0) / 3 = 0.8833...
    double expected = (0.80 + 0.85 + 1.0) / 3.0;
    CHECK_THAT(result.overallScore, Catch::Matchers::WithinAbs(expected, 0.001));
}

TEST_CASE("AnalysisEngine coupling matrix counts connections per module pair", "[AnalysisEngine]") {
    auto data = makeAnalysisTestData();
    AnalysisEngine engine;
    auto result = engine.analyze(data);

    // cpu->bus: 3, bus->cpu: 2, bus->mem: 2
    REQUIRE(result.coupling.size() == 3);

    // Sorted by connectionCount descending
    CHECK(result.coupling[0].srcModule == "u_cpu");
    CHECK(result.coupling[0].dstModule == "u_bus");
    CHECK(result.coupling[0].connectionCount == 3);
}

TEST_CASE("AnalysisEngine coupling sorted by connection count descending", "[AnalysisEngine]") {
    auto data = makeAnalysisTestData();
    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.coupling.size() == 3);
    CHECK(result.coupling[0].connectionCount == 3);

    // bus->cpu and bus->mem both have 2, order between them is stable but both are 2
    CHECK(result.coupling[1].connectionCount == 2);
    CHECK(result.coupling[2].connectionCount == 2);
}

TEST_CASE("AnalysisEngine risk assessment classifies WIDTH_MISMATCH truncation as HIGH", "[AnalysisEngine]") {
    auto data = makeAnalysisTestData();
    AnalysisEngine engine;
    auto result = engine.analyze(data);

    // Find the WIDTH_MISMATCH risk
    bool foundHigh = false;
    for (const auto& risk : result.risks) {
        if (risk.issue.type == Issue::Type::WIDTH_MISMATCH) {
            CHECK(risk.level == RiskItem::Level::HIGH);
            CHECK_FALSE(risk.reason.empty());
            foundHigh = true;
        }
    }
    CHECK(foundHigh);
}

TEST_CASE("AnalysisEngine risk assessment classifies DANGLING_OUTPUT as LOW", "[AnalysisEngine]") {
    auto data = makeAnalysisTestData();
    AnalysisEngine engine;
    auto result = engine.analyze(data);

    bool foundLow = false;
    for (const auto& risk : result.risks) {
        if (risk.issue.type == Issue::Type::DANGLING_OUTPUT) {
            CHECK(risk.level == RiskItem::Level::LOW);
            CHECK_FALSE(risk.reason.empty());
            foundLow = true;
        }
    }
    CHECK(foundLow);
}

TEST_CASE("AnalysisEngine empty graph produces score 1.0", "[AnalysisEngine]") {
    ReportData data;
    data.topModule = "empty_top";
    data.graph.topModule = "empty_top";

    AnalysisEngine engine;
    auto result = engine.analyze(data);

    CHECK_THAT(result.overallScore, Catch::Matchers::WithinAbs(1.0, 0.001));
    CHECK(result.moduleHealth.empty());
    CHECK(result.coupling.empty());
    CHECK(result.risks.empty());
    CHECK(result.totalPorts == 0);
    CHECK(result.totalConnections == 0);
    CHECK(result.totalIssues == 0);
}

TEST_CASE("AnalysisEngine risk sorting: HIGH before MEDIUM before LOW", "[AnalysisEngine]") {
    auto data = makeAnalysisTestData();
    AnalysisEngine engine;
    auto result = engine.analyze(data);

    // We have: WIDTH_MISMATCH(HIGH), TYPE_MISMATCH(MEDIUM), DANGLING_OUTPUT(LOW)
    REQUIRE(result.risks.size() == 3);
    CHECK(result.risks[0].level == RiskItem::Level::HIGH);
    CHECK(result.risks[1].level == RiskItem::Level::MEDIUM);
    CHECK(result.risks[2].level == RiskItem::Level::LOW);
}

TEST_CASE("AnalysisEngine module health port counts", "[AnalysisEngine]") {
    auto data = makeAnalysisTestData();
    AnalysisEngine engine;
    auto result = engine.analyze(data);

    // Find u_cpu module
    const ModuleHealth* cpu = nullptr;
    for (const auto& m : result.moduleHealth) {
        if (m.shortName == "u_cpu") { cpu = &m; break; }
    }
    REQUIRE(cpu != nullptr);
    CHECK(cpu->totalPorts == 6);
    CHECK(cpu->connectedPorts == 5); // i_irq is not connected

    // Find u_bus module
    const ModuleHealth* bus = nullptr;
    for (const auto& m : result.moduleHealth) {
        if (m.shortName == "u_bus") { bus = &m; break; }
    }
    REQUIRE(bus != nullptr);
    CHECK(bus->totalPorts == 8);
    CHECK(bus->connectedPorts == 8); // all connected

    // Find u_mem module
    const ModuleHealth* mem = nullptr;
    for (const auto& m : result.moduleHealth) {
        if (m.shortName == "u_mem") { mem = &m; break; }
    }
    REQUIRE(mem != nullptr);
    CHECK(mem->totalPorts == 4);
    CHECK(mem->connectedPorts == 3); // o_debug not connected
}

TEST_CASE("AnalysisEngine summary totals are correct", "[AnalysisEngine]") {
    auto data = makeAnalysisTestData();
    AnalysisEngine engine;
    auto result = engine.analyze(data);

    CHECK(result.totalPorts == 18);
    CHECK(result.totalConnections == 7);
    CHECK(result.totalIssues == 3);
}

TEST_CASE("AnalysisEngine health score clamped to 0.0 minimum", "[AnalysisEngine]") {
    ReportData data;
    data.topModule = "top";
    data.graph.topModule = "top";

    // Single module with many errors
    PortInfo p1 = makePort("top.u_bad", "p1", ArgumentDirection::In, 8);
    PortInfo p2 = makePort("top.u_bad", "p2", ArgumentDirection::In, 8);
    data.graph.allPorts = {p1, p2};

    // 8 errors => 8*0.15 = 1.2, so score should clamp to 0.0
    for (int i = 0; i < 8; ++i) {
        data.active.push_back(makeIssue(
            Issue::Type::UNDRIVEN_INPUT, Issue::Severity::ERROR,
            p1, "undriven"));
    }

    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.moduleHealth.size() == 1);
    CHECK_THAT(result.moduleHealth[0].score, Catch::Matchers::WithinAbs(0.0, 0.001));
}

TEST_CASE("AnalysisEngine WIDTH_MISMATCH extension classified as LOW", "[AnalysisEngine]") {
    ReportData data;
    data.topModule = "top";
    data.graph.topModule = "top";

    PortInfo src = makePort("top.u_a", "o_narrow", ArgumentDirection::Out, 8);
    PortInfo dst = makePort("top.u_b", "i_wide",   ArgumentDirection::In, 16);
    data.graph.allPorts = {src, dst};
    data.graph.connections.push_back({src, dst});
    data.graph.connectedPorts = {"top.u_a.o_narrow", "top.u_b.i_wide"};

    // WIDTH_MISMATCH with extension (source < dest)
    data.active.push_back(makeIssue(
        Issue::Type::WIDTH_MISMATCH, Issue::Severity::WARN,
        src, "width 8 -> 16: zero extension",
        Connection{src, dst}));

    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.risks.size() == 1);
    CHECK(result.risks[0].level == RiskItem::Level::LOW);
}

// ---------- Part B: Missing risk classification tests ----------

TEST_CASE("AnalysisEngine UNDRIVEN_INPUT classified as MEDIUM", "[AnalysisEngine]") {
    ReportData data;
    data.topModule = "top";
    data.graph.topModule = "top";

    PortInfo p = makePort("top.u_x", "i_clk", ArgumentDirection::In);
    data.graph.allPorts = {p};

    data.active.push_back(makeIssue(
        Issue::Type::UNDRIVEN_INPUT, Issue::Severity::ERROR,
        p, "undriven input"));

    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.risks.size() == 1);
    CHECK(result.risks[0].level == RiskItem::Level::MEDIUM);
    CHECK(result.risks[0].issue.type == Issue::Type::UNDRIVEN_INPUT);
    CHECK_FALSE(result.risks[0].reason.empty());
}

TEST_CASE("AnalysisEngine PROTOCOL_INCOMPLETE classified as HIGH", "[AnalysisEngine]") {
    ReportData data;
    data.topModule = "top";
    data.graph.topModule = "top";

    PortInfo p = makePort("top.u_bus", "axi_awvalid", ArgumentDirection::Out);
    data.graph.allPorts = {p};

    data.active.push_back(makeIssue(
        Issue::Type::PROTOCOL_INCOMPLETE, Issue::Severity::WARN,
        p, "missing AWREADY in AXI4 interface"));

    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.risks.size() == 1);
    CHECK(result.risks[0].level == RiskItem::Level::HIGH);
    CHECK(result.risks[0].issue.type == Issue::Type::PROTOCOL_INCOMPLETE);
    CHECK_FALSE(result.risks[0].reason.empty());
}

TEST_CASE("AnalysisEngine EXPECT_MISSING classified as MEDIUM", "[AnalysisEngine]") {
    ReportData data;
    data.topModule = "top";
    data.graph.topModule = "top";

    PortInfo p = makePort("top.u_cpu", "o_data", ArgumentDirection::Out, 32);
    data.graph.allPorts = {p};

    data.active.push_back(makeIssue(
        Issue::Type::EXPECT_MISSING, Issue::Severity::ERROR,
        p, "expected connection u_cpu.o_data -> u_bus.i_data not found"));

    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.risks.size() == 1);
    CHECK(result.risks[0].level == RiskItem::Level::MEDIUM);
    CHECK(result.risks[0].issue.type == Issue::Type::EXPECT_MISSING);
    CHECK_FALSE(result.risks[0].reason.empty());
}

TEST_CASE("AnalysisEngine EXPECT_FORBIDDEN classified as HIGH", "[AnalysisEngine]") {
    ReportData data;
    data.topModule = "top";
    data.graph.topModule = "top";

    PortInfo src = makePort("top.u_debug", "o_secret", ArgumentDirection::Out, 64);
    PortInfo dst = makePort("top.u_ext", "i_data", ArgumentDirection::In, 64);
    data.graph.allPorts = {src, dst};
    data.graph.connections.push_back({src, dst});
    data.graph.connectedPorts = {"top.u_debug.o_secret", "top.u_ext.i_data"};

    data.active.push_back(makeIssue(
        Issue::Type::EXPECT_FORBIDDEN, Issue::Severity::ERROR,
        src, "forbidden connection u_debug -> u_ext",
        Connection{src, dst}));

    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.risks.size() == 1);
    CHECK(result.risks[0].level == RiskItem::Level::HIGH);
    CHECK(result.risks[0].issue.type == Issue::Type::EXPECT_FORBIDDEN);
    CHECK_FALSE(result.risks[0].reason.empty());
}

TEST_CASE("AnalysisEngine CONVENTION classified as LOW", "[AnalysisEngine]") {
    ReportData data;
    data.topModule = "top";
    data.graph.topModule = "top";

    PortInfo p = makePort("top.u_core", "data_in", ArgumentDirection::In, 16);
    data.graph.allPorts = {p};

    data.active.push_back(makeIssue(
        Issue::Type::CONVENTION, Issue::Severity::INFO,
        p, "input port 'data_in' missing 'i_' prefix"));

    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.risks.size() == 1);
    CHECK(result.risks[0].level == RiskItem::Level::LOW);
    CHECK(result.risks[0].issue.type == Issue::Type::CONVENTION);
    CHECK_FALSE(result.risks[0].reason.empty());
}

TEST_CASE("AnalysisEngine mixed risk types sorted HIGH then MEDIUM then LOW", "[AnalysisEngine]") {
    ReportData data;
    data.topModule = "top";
    data.graph.topModule = "top";

    PortInfo p1 = makePort("top.u_a", "o_data",  ArgumentDirection::Out, 8);
    PortInfo p2 = makePort("top.u_a", "i_clk",   ArgumentDirection::In);
    PortInfo p3 = makePort("top.u_a", "o_debug",  ArgumentDirection::Out, 4);
    PortInfo p4 = makePort("top.u_a", "data_out", ArgumentDirection::Out, 16);
    data.graph.allPorts = {p1, p2, p3, p4};

    // HIGH: EXPECT_FORBIDDEN
    data.active.push_back(makeIssue(
        Issue::Type::EXPECT_FORBIDDEN, Issue::Severity::ERROR,
        p1, "forbidden connection present"));
    // HIGH: PROTOCOL_INCOMPLETE
    data.active.push_back(makeIssue(
        Issue::Type::PROTOCOL_INCOMPLETE, Issue::Severity::WARN,
        p1, "missing protocol signal"));
    // MEDIUM: UNDRIVEN_INPUT
    data.active.push_back(makeIssue(
        Issue::Type::UNDRIVEN_INPUT, Issue::Severity::ERROR,
        p2, "undriven input"));
    // MEDIUM: EXPECT_MISSING
    data.active.push_back(makeIssue(
        Issue::Type::EXPECT_MISSING, Issue::Severity::ERROR,
        p1, "expected connection missing"));
    // LOW: DANGLING_OUTPUT
    data.active.push_back(makeIssue(
        Issue::Type::DANGLING_OUTPUT, Issue::Severity::WARN,
        p3, "dangling output"));
    // LOW: CONVENTION
    data.active.push_back(makeIssue(
        Issue::Type::CONVENTION, Issue::Severity::INFO,
        p4, "naming convention violation"));

    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.risks.size() == 6);
    // First two should be HIGH
    CHECK(result.risks[0].level == RiskItem::Level::HIGH);
    CHECK(result.risks[1].level == RiskItem::Level::HIGH);
    // Next two should be MEDIUM
    CHECK(result.risks[2].level == RiskItem::Level::MEDIUM);
    CHECK(result.risks[3].level == RiskItem::Level::MEDIUM);
    // Last two should be LOW
    CHECK(result.risks[4].level == RiskItem::Level::LOW);
    CHECK(result.risks[5].level == RiskItem::Level::LOW);
}

TEST_CASE("AnalysisEngine module with multiple issue types — health score accounts for all", "[AnalysisEngine]") {
    ReportData data;
    data.topModule = "top";
    data.graph.topModule = "top";

    PortInfo p1 = makePort("top.u_mod", "o_data", ArgumentDirection::Out, 32);
    PortInfo p2 = makePort("top.u_mod", "i_data", ArgumentDirection::In, 32);
    PortInfo p3 = makePort("top.u_mod", "o_debug", ArgumentDirection::Out, 8);
    PortInfo p4 = makePort("top.u_mod", "data_in", ArgumentDirection::In, 16);
    data.graph.allPorts = {p1, p2, p3, p4};

    // 2 ERRORs + 1 WARN + 1 INFO
    // Score: 1.0 - 2*0.15 - 1*0.05 - 1*0.0 = 1.0 - 0.35 = 0.65
    data.active.push_back(makeIssue(
        Issue::Type::UNDRIVEN_INPUT, Issue::Severity::ERROR,
        p2, "undriven input"));
    data.active.push_back(makeIssue(
        Issue::Type::EXPECT_FORBIDDEN, Issue::Severity::ERROR,
        p1, "forbidden connection"));
    data.active.push_back(makeIssue(
        Issue::Type::DANGLING_OUTPUT, Issue::Severity::WARN,
        p3, "dangling output"));
    data.active.push_back(makeIssue(
        Issue::Type::CONVENTION, Issue::Severity::INFO,
        p4, "naming convention violation"));

    AnalysisEngine engine;
    auto result = engine.analyze(data);

    REQUIRE(result.moduleHealth.size() == 1);
    CHECK(result.moduleHealth[0].shortName == "u_mod");
    CHECK(result.moduleHealth[0].errorCount == 2);
    CHECK(result.moduleHealth[0].warnCount == 1);
    CHECK(result.moduleHealth[0].infoCount == 1);
    CHECK_THAT(result.moduleHealth[0].score, Catch::Matchers::WithinAbs(0.65, 0.001));
}
