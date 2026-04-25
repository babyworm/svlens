#include <catch2/catch_test_macros.hpp>
#include "ConnectionExtractor.h"
#include "TestUtils.h"
#include "WidthChecker.h"

using namespace connect;
using testutils::compileFile;

TEST_CASE("Extractor: clean design has connections and all ports") {
    auto result = compileFile("sv/clean_design.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "clean_top");
    auto graph = extractor.extract();
    CHECK(graph.topModule == "clean_top");
    CHECK(graph.connections.size() >= 2);  // o_data->i_data, o_valid->i_valid
    CHECK(graph.allPorts.size() >= 4);     // 2 outputs + 2 inputs
}

TEST_CASE("Extractor: width mismatch captures port widths") {
    auto result = compileFile("sv/width_mismatch.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "width_mismatch_top");
    auto graph = extractor.extract();
    CHECK(!graph.allPorts.empty());
    bool found32 = false;
    for (auto& port : graph.allPorts) {
        if (port.portName == "o_data" && port.width == 32) found32 = true;
    }
    CHECK(found32);
}

TEST_CASE("Extractor: dangling output port is in allPorts") {
    auto result = compileFile("sv/dangling_output.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "dangling_top");
    auto graph = extractor.extract();
    bool foundDebug = false;
    for (auto& port : graph.allPorts) {
        if (port.portName == "o_debug") foundDebug = true;
    }
    CHECK(foundDebug);
}

TEST_CASE("Extractor: undriven input port is in allPorts") {
    auto result = compileFile("sv/undriven_input.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "undriven_top");
    auto graph = extractor.extract();
    bool foundConfig = false;
    for (auto& port : graph.allPorts) {
        if (port.portName == "i_config") foundConfig = true;
    }
    CHECK(foundConfig);
}

TEST_CASE("Extractor: member access resolves and concat becomes approximate") {
    auto result = compileFile("sv/member_access_and_concat.sv");
    REQUIRE(result);

    ConnectionExtractor extractor(*result.compilation, "member_concat_top");
    auto graph = extractor.extract();

    bool foundMemberAccessConnection = false;
    size_t aggregateEdges = 0;

    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() == "member_concat_top.u_prod.o_valid" &&
            conn.dest.fullPath() == "member_concat_top.u_cons.i_valid") {
            foundMemberAccessConnection = true;
            CHECK(conn.kind == ConnectionKind::Direct);
        }

        if (conn.dest.fullPath() == "member_concat_top.u_cons.i_bus") {
            aggregateEdges++;
            CHECK(conn.kind == ConnectionKind::Approximate);
        }
    }

    CHECK(foundMemberAccessConnection);
    CHECK(aggregateEdges == 2);

    WidthChecker checker;
    CHECK(checker.check(graph).empty());
}

TEST_CASE("Extractor: interface modport ports preserve a bus-level connection", "[extractor][interface][modport]") {
    auto result = compileFile("sv/interface_modport.sv");
    REQUIRE(result);

    ConnectionExtractor extractor(*result.compilation, "interface_modport");
    auto graph = extractor.extract();

    // With per-signal expansion, we should have per-signal connections
    // through the interface (data, valid from master->slave; ready from slave->master)
    REQUIRE(graph.connections.size() >= 1);
}

TEST_CASE("Extractor: procedural always_comb glue creates an approximate alias connection", "[extractor][procedural]") {
    auto result = compileFile("sv/procedural_glue.sv");
    REQUIRE(result);

    ConnectionExtractor extractor(*result.compilation, "procedural_glue_top");
    auto graph = extractor.extract();

    bool foundApproximateProceduralConnection = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() == "procedural_glue_top.u_prod.o_data" &&
            conn.dest.fullPath() == "procedural_glue_top.u_cons.i_data") {
            foundApproximateProceduralConnection = true;
            CHECK(conn.kind == ConnectionKind::Approximate);
        }
    }

    CHECK(foundApproximateProceduralConnection);
}

TEST_CASE("Extractor: modport connections create interface links", "[extractor][modport]") {
    auto result = compileFile("sv/interface_modport.sv");
    REQUIRE(result);

    ConnectionExtractor extractor(*result.compilation, "interface_modport");
    auto graph = extractor.extract();

    // Round 30 US-R05: modport-expanded connections now include both
    // the legacy Approximate edge (keyed by scope::ifaceInst.signal)
    // AND a new Direct edge (keyed by the underlying signal's
    // absolute hier path) so consumer-side modport member access can
    // rendezvous on the same key. The test ensures interface links
    // are still produced; per-edge kind is no longer asserted here
    // because both kinds coexist by design.
    bool foundInterfaceLink = false;
    bool foundApproximate = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.instancePath == "interface_modport.u_prod" &&
            conn.dest.instancePath == "interface_modport.u_cons") {
            foundInterfaceLink = true;
            if (conn.kind == ConnectionKind::Approximate)
                foundApproximate = true;
        }
    }

    CHECK(foundInterfaceLink);
    // Legacy Approximate edge preserved -- the abs-path Direct entry
    // is additive, not a replacement.
    CHECK(foundApproximate);
}

TEST_CASE("Extractor: interface modport produces per-signal edges", "[extractor][interface]") {
    auto result = compileFile("sv/interface_modport.sv");
    REQUIRE(result);

    ConnectionExtractor extractor(*result.compilation, "interface_modport");
    auto graph = extractor.extract();

    // Should have connections through the interface
    REQUIRE(graph.connections.size() >= 1);

    // Check that we have per-signal port entries for modport members
    bool hasData = false;
    bool hasValid = false;
    bool hasReady = false;
    for (auto& port : graph.allPorts) {
        if (port.portName == "bus.data") hasData = true;
        if (port.portName == "bus.valid") hasValid = true;
        if (port.portName == "bus.ready") hasReady = true;
    }
    CHECK(hasData);
    CHECK(hasValid);
    CHECK(hasReady);

    // Check that per-signal connections exist between producer and consumer
    bool hasDataEdge = false;
    bool hasValidEdge = false;
    bool hasReadyEdge = false;
    for (const auto& conn : graph.connections) {
        // data: master output -> slave input (producer drives, consumer receives)
        if (conn.source.portName == "bus.data" &&
            conn.source.instancePath == "interface_modport.u_prod" &&
            conn.dest.portName == "bus.data" &&
            conn.dest.instancePath == "interface_modport.u_cons") {
            hasDataEdge = true;
        }
        // valid: master output -> slave input
        if (conn.source.portName == "bus.valid" &&
            conn.source.instancePath == "interface_modport.u_prod" &&
            conn.dest.portName == "bus.valid" &&
            conn.dest.instancePath == "interface_modport.u_cons") {
            hasValidEdge = true;
        }
        // ready: slave output -> master input (consumer drives, producer receives)
        if (conn.source.portName == "bus.ready" &&
            conn.source.instancePath == "interface_modport.u_cons" &&
            conn.dest.portName == "bus.ready" &&
            conn.dest.instancePath == "interface_modport.u_prod") {
            hasReadyEdge = true;
        }
    }
    CHECK(hasDataEdge);
    CHECK(hasValidEdge);
    CHECK(hasReadyEdge);
}
