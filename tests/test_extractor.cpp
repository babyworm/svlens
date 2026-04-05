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

    ConnectionExtractor extractor(*result.compilation, "interface_modport_top");
    auto graph = extractor.extract();

    bool foundBusConnection = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() == "interface_modport_top.u_prod.bus" &&
            conn.dest.fullPath() == "interface_modport_top.u_cons.bus") {
            foundBusConnection = true;
        }
    }

    CHECK(foundBusConnection);
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

TEST_CASE("Extractor: modport connections create approximate interface links", "[extractor][modport]") {
    auto result = compileFile("sv/interface_modport.sv");
    REQUIRE(result);

    ConnectionExtractor extractor(*result.compilation, "interface_modport_top");
    auto graph = extractor.extract();

    bool foundInterfaceLink = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.instancePath == "interface_modport_top.u_prod" &&
            conn.dest.instancePath == "interface_modport_top.u_cons") {
            foundInterfaceLink = true;
            CHECK(conn.kind == ConnectionKind::Approximate);
        }
    }

    CHECK(foundInterfaceLink);
}
