#include <catch2/catch_test_macros.hpp>
#include "ConnectionExtractor.h"
#include "TestUtils.h"
#include "WidthChecker.h"

#include <algorithm>
#include <string>
#include <string_view>

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

TEST_CASE("Extractor: sole always_comb copy preserves direct lanes", "[extractor][procedural]") {
    auto result = compileFile("sv/procedural_glue.sv");
    REQUIRE(result);

    ConnectionExtractor extractor(*result.compilation, "procedural_glue_top");
    auto graph = extractor.extract();

    bool foundDirectProceduralConnection = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() == "procedural_glue_top.u_prod.o_data" &&
            conn.dest.fullPath() == "procedural_glue_top.u_cons.i_data") {
            foundDirectProceduralConnection = true;
            CHECK(conn.kind == ConnectionKind::Direct);
            REQUIRE(conn.sourceBits);
            REQUIRE(conn.destBits);
            CHECK(conn.sourceBits->low == 0);
            CHECK(conn.sourceBits->high == 7);
            CHECK(conn.destBits->low == 0);
            CHECK(conn.destBits->high == 7);
        }
    }

    CHECK(foundDirectProceduralConnection);
}

TEST_CASE("Extractor: sole unconditional combinational assignment preserves direct lanes",
          "[extractor][procedural][slice]") {
    auto result = compileFile("sv/procedural_exact_glue.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "procedural_exact_glue");
    const auto graph = extractor.extract();

    const std::string source = "procedural_exact_glue.u_source.data_o";
    auto hasConnection = [&](const std::string& dest, ConnectionKind kind, int64_t sourceLow, int64_t sourceHigh,
                             int64_t destLow, int64_t destHigh) {
        return std::any_of(graph.connections.begin(), graph.connections.end(), [&](const Connection& conn) {
            return conn.source.fullPath() == source && conn.dest.fullPath() == dest && conn.kind == kind &&
                   conn.sourceBits && conn.destBits && conn.sourceBits->low == sourceLow &&
                   conn.sourceBits->high == sourceHigh && conn.destBits->low == destLow &&
                   conn.destBits->high == destHigh;
        });
    };
    const std::string top = "procedural_exact_glue.";
    CHECK(hasConnection(top + "u_whole.data_i", ConnectionKind::Direct, 0, 7, 0, 7));
    CHECK(hasConnection(top + "u_high.data_i", ConnectionKind::Direct, 4, 7, 0, 3));

    bool coarseExact = false;
    bool legacyApproximate = false;
    bool overwrittenApproximate = false;
    bool conditionalApproximate = false;
    bool compoundApproximate = false;
    bool unsafeExact = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != source)
            continue;
        const auto dest = conn.dest.fullPath();
        if (dest == top + "u_whole.data_i" || dest == top + "u_high.data_i")
            coarseExact |= conn.kind == ConnectionKind::Approximate || !conn.sourceBits || !conn.destBits;
        if (dest == top + "u_low.data_i") {
            legacyApproximate |= conn.kind == ConnectionKind::Approximate;
            unsafeExact |= conn.kind == ConnectionKind::Direct;
        }
        if (dest == top + "u_overwritten.data_i") {
            overwrittenApproximate |= conn.kind == ConnectionKind::Approximate;
            unsafeExact |= conn.kind == ConnectionKind::Direct;
        }
        if (dest == top + "u_conditional.data_i") {
            conditionalApproximate |= conn.kind == ConnectionKind::Approximate;
            unsafeExact |= conn.kind == ConnectionKind::Direct;
        }
        if (dest == top + "u_compound.data_i") {
            compoundApproximate |= conn.kind == ConnectionKind::Approximate;
            unsafeExact |= conn.kind == ConnectionKind::Direct;
        }
    }
    CHECK_FALSE(coarseExact);
    CHECK(legacyApproximate);
    CHECK(overwrittenApproximate);
    CHECK(conditionalApproximate);
    CHECK(compoundApproximate);
    CHECK_FALSE(unsafeExact);
}

TEST_CASE("Extractor: procedural mux dependencies stay directed", "[extractor][procedural][mux]") {
    auto result = compileFile("sv/procedural_mux_glue.sv");
    REQUIRE(result);

    ConnectionExtractor extractor(*result.compilation, "procedural_mux_glue_top");
    auto graph = extractor.extract();

    auto hasEdge = [&](std::string_view source, std::string_view dest) {
        for (const auto& conn : graph.connections) {
            if (conn.source.fullPath() == source && conn.dest.fullPath() == dest) {
                CHECK(conn.kind == ConnectionKind::Approximate);
                return true;
            }
        }
        return false;
    };

    for (auto sink : {"u_mux", "u_case", "u_ternary", "u_legacy"}) {
        const std::string dest = std::string("procedural_mux_glue_top.") + sink + ".i_data";
        CHECK(hasEdge("procedural_mux_glue_top.u_a.o_data", dest));
        CHECK(hasEdge("procedural_mux_glue_top.u_b.o_data", dest));
        CHECK(hasEdge("procedural_mux_glue_top.u_select.o_select", dest));
    }

    CHECK_FALSE(hasEdge("procedural_mux_glue_top.u_b.o_data", "procedural_mux_glue_top.u_tap.i_data"));
    for (auto sink : {"u_mux", "u_case"}) {
        for (auto source : {"u_a", "u_b"}) {
            const std::string sourcePath = std::string("procedural_mux_glue_top.") + source + ".o_data";
            const std::string destPath = std::string("procedural_mux_glue_top.") + sink + ".i_data";
            CHECK(std::any_of(graph.connections.begin(), graph.connections.end(), [&](const Connection& conn) {
                return conn.source.fullPath() == sourcePath && conn.dest.fullPath() == destPath &&
                       conn.kind == ConnectionKind::Approximate && conn.sourceBits && conn.destBits &&
                       conn.sourceBits->low == 0 && conn.sourceBits->high == 7 && conn.destBits->low == 0 &&
                       conn.destBits->high == 7;
            }));
        }
    }
}

TEST_CASE("Extractor: guarded procedural copies retain selected source lanes as may-flow",
          "[extractor][procedural][mux][slice]") {
    auto result = compileFile("sv/procedural_branch_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "procedural_branch_slices");
    const auto graph = extractor.extract();
    const std::string top = "procedural_branch_slices.";

    auto hasRangedMayFlow = [&](const std::string& source, const std::string& dest, int64_t low, int64_t high) {
        bool found = false;
        for (const auto& conn : graph.connections) {
            if (conn.source.fullPath() != top + source || conn.dest.fullPath() != top + dest)
                continue;
            CHECK(conn.kind == ConnectionKind::Approximate);
            if (!conn.sourceBits && !conn.destBits)
                continue; // coarse may-flow remains for transitive unsupported paths
            REQUIRE(conn.sourceBits);
            REQUIRE(conn.destBits);
            CHECK(conn.sourceBits->low == low);
            CHECK(conn.sourceBits->high == high);
            CHECK(conn.destBits->low == 0);
            CHECK(conn.destBits->high == 3);
            found = true;
        }
        return found;
    };
    CHECK(hasRangedMayFlow("u_a.data_o", "u_selected.data_i", 4, 7));
    CHECK(hasRangedMayFlow("u_b.data_o", "u_selected.data_i", 0, 3));
    CHECK(hasRangedMayFlow("u_b.data_o", "u_decoded.data_i", 4, 7));
    CHECK(hasRangedMayFlow("u_a.data_o", "u_decoded.data_i", 0, 3));

    for (const auto& dest : {"u_selected.data_i", "u_decoded.data_i"}) {
        const bool selectInfluence =
            std::any_of(graph.connections.begin(), graph.connections.end(), [&](const Connection& conn) {
                return conn.source.fullPath() == top + "u_select.select_o" && conn.dest.fullPath() == top + dest &&
                       conn.kind == ConnectionKind::Approximate;
            });
        CHECK(selectInfluence);
    }
}

TEST_CASE("Extractor: constant procedural if excludes the inactive source", "[extractor][procedural][constant]") {
    auto result = compileFile("sv/procedural_branch_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "procedural_branch_slices");
    const auto graph = extractor.extract();
    const std::string top = "procedural_branch_slices.";
    bool selected = false;
    bool inactive = false;
    for (const auto& conn : graph.connections) {
        if (conn.dest.fullPath() != top + "u_constant.data_i")
            continue;
        if (conn.source.fullPath() == top + "u_b.data_o") {
            selected = true;
            CHECK(conn.kind == ConnectionKind::Approximate);
        }
        inactive |= conn.source.fullPath() == top + "u_a.data_o";
    }
    CHECK(selected);
    CHECK_FALSE(inactive);
}

TEST_CASE("Extractor: constant procedural case excludes the inactive source", "[extractor][procedural][constant]") {
    auto result = compileFile("sv/procedural_branch_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "procedural_branch_slices");
    const auto graph = extractor.extract();
    const std::string top = "procedural_branch_slices.";
    bool selected = false;
    bool inactive = false;
    for (const auto& conn : graph.connections) {
        if (conn.dest.fullPath() != top + "u_constant_case.data_i")
            continue;
        selected |= conn.source.fullPath() == top + "u_b.data_o";
        inactive |= conn.source.fullPath() == top + "u_a.data_o";
    }
    CHECK(selected);
    CHECK_FALSE(inactive);
}

TEST_CASE("Extractor: constant parameter excludes a nested case branch", "[extractor][procedural][constant]") {
    auto result = compileFile("sv/procedural_branch_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "procedural_branch_slices");
    const auto graph = extractor.extract();
    const std::string top = "procedural_branch_slices.";
    bool selected = false;
    bool inactive = false;
    for (const auto& conn : graph.connections) {
        if (conn.dest.fullPath() != top + "u_nested.data_i")
            continue;
        selected |= conn.source.fullPath() == top + "u_b.data_o";
        inactive |= conn.source.fullPath() == top + "u_a.data_o";
    }
    CHECK(selected);
    CHECK_FALSE(inactive);
}

TEST_CASE("Extractor: constant wildcard cases and arithmetic if exclude inactive sources",
          "[extractor][procedural][constant]") {
    auto result = compileFile("sv/procedural_branch_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "procedural_branch_slices");
    const auto graph = extractor.extract();
    const std::string top = "procedural_branch_slices.";
    for (const auto* sink : {"u_casez.data_i", "u_casex.data_i", "u_computed_if.data_i"}) {
        bool selected = false;
        bool inactive = false;
        for (const auto& conn : graph.connections) {
            if (conn.dest.fullPath() != top + sink)
                continue;
            selected |= conn.source.fullPath() == top + "u_b.data_o";
            inactive |= conn.source.fullPath() == top + "u_a.data_o";
        }
        CHECK(selected);
        CHECK_FALSE(inactive);
    }
}

TEST_CASE("Extractor: runtime wildcard cases keep both possible sources", "[extractor][procedural][mux]") {
    auto result = compileFile("sv/procedural_branch_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "procedural_branch_slices");
    const auto graph = extractor.extract();
    const std::string top = "procedural_branch_slices.";
    for (const auto* sink : {"u_runtime_casez.data_i", "u_runtime_casex.data_i"}) {
        bool sourceA = false;
        bool sourceB = false;
        for (const auto& conn : graph.connections) {
            if (conn.dest.fullPath() != top + sink)
                continue;
            sourceA |= conn.source.fullPath() == top + "u_a.data_o" && conn.kind == ConnectionKind::Approximate;
            sourceB |= conn.source.fullPath() == top + "u_b.data_o" && conn.kind == ConnectionKind::Approximate;
        }
        CHECK(sourceA);
        CHECK(sourceB);
    }
}

TEST_CASE("Extractor: generated procedural mux and assign reach parent-scope ports",
          "[extractor][procedural][generate]") {
    auto result = compileFile("sv/procedural_generate_mux.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "procedural_generate_mux_top");
    const auto graph = extractor.extract();

    auto hasEdge = [&](const std::string& source, const std::string& dest) {
        for (const auto& conn : graph.connections) {
            if (conn.source.fullPath() == source && conn.dest.fullPath() == dest)
                return true;
        }
        return false;
    };
    CHECK(hasEdge("procedural_generate_mux_top.u_a.data_o", "procedural_generate_mux_top.u_out.data_i"));
    CHECK(hasEdge("procedural_generate_mux_top.u_b.data_o", "procedural_generate_mux_top.u_out.data_i"));
    CHECK(hasEdge("procedural_generate_mux_top.u_a.data_o", "procedural_generate_mux_top.u_tap.data_i"));
    CHECK_FALSE(hasEdge("procedural_generate_mux_top.u_b.data_o", "procedural_generate_mux_top.u_tap.data_i"));
}

TEST_CASE("Extractor: generated local nets do not merge sibling lanes", "[extractor][procedural][generate]") {
    auto result = compileFile("sv/procedural_generate_local.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "procedural_generate_local_top");
    const auto graph = extractor.extract();

    auto hasEdge = [&](const std::string& source, const std::string& dest) {
        for (const auto& conn : graph.connections) {
            if (conn.source.fullPath() == source && conn.dest.fullPath() == dest)
                return true;
        }
        return false;
    };
    const std::string a = "procedural_generate_local_top.u_a.data_o";
    const std::string b = "procedural_generate_local_top.u_b.data_o";
    const std::string lane0 = "procedural_generate_local_top.g_lane[0].u_sink.data_i";
    const std::string lane1 = "procedural_generate_local_top.g_lane[1].u_sink.data_i";
    CHECK(hasEdge(a, lane0));
    CHECK(hasEdge(b, lane1));
    CHECK_FALSE(hasEdge(a, lane1));
    CHECK_FALSE(hasEdge(b, lane0));
}

TEST_CASE("Extractor: modport members feed procedural mux output", "[extractor][procedural][modport]") {
    auto result = compileFile("sv/procedural_modport_mux.sv");
    REQUIRE(result);

    ConnectionExtractor extractor(*result.compilation, "procedural_modport_mux_top");
    auto graph = extractor.extract();

    auto hasEdge = [&](std::string_view source, std::string_view dest) {
        for (const auto& conn : graph.connections) {
            if (conn.source.fullPath() == source && conn.dest.fullPath() == dest)
                return true;
        }
        return false;
    };

    CHECK(hasEdge("procedural_modport_mux_top.u_a.bus.data", "procedural_modport_mux_top.u_out.i_data"));
    CHECK(hasEdge("procedural_modport_mux_top.u_b.bus.data", "procedural_modport_mux_top.u_out.i_data"));
    CHECK_FALSE(hasEdge("procedural_modport_mux_top.u_b.bus.data", "procedural_modport_mux_top.u_tap.i_data"));
}

TEST_CASE("Extractor: whole-interface ports connect used members by direction", "[extractor][interface][whole]") {
    auto result = compileFile("sv/interface_whole_port.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_port");
    const auto graph = extractor.extract();

    bool forwardData = false;
    bool reverseData = false;
    bool forwardValid = false;
    bool crossedInstances = false;
    for (const auto& conn : graph.connections) {
        const auto src = conn.source.fullPath();
        const auto dest = conn.dest.fullPath();
        forwardData |= src == "interface_whole_port.u_prod.bus.data" && dest == "interface_whole_port.u_cons.bus.data";
        reverseData |= src == "interface_whole_port.u_cons.bus.data" && dest == "interface_whole_port.u_prod.bus.data";
        forwardValid |=
            src == "interface_whole_port.u_prod.bus.valid" && dest == "interface_whole_port.u_cons.bus.valid";
        crossedInstances |=
            src == "interface_whole_port.u_prod.bus.data" && dest == "interface_whole_port.u_other_cons.bus.data";
    }
    CHECK(forwardData);
    CHECK_FALSE(reverseData);
    CHECK_FALSE(forwardValid); // consumer never reads valid
    CHECK_FALSE(crossedInstances);
}

TEST_CASE("Extractor: whole-interface member slices connect only overlapping bits",
          "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_slices");
    const auto graph = extractor.extract();

    bool lowRange = false;
    bool broadOrHighRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_slices.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_slices.u_reader.bus.data")
            continue;
        if (conn.sourceBits && conn.destBits && conn.sourceBits->low == 0 && conn.sourceBits->high == 7 &&
            conn.destBits->low == 0 && conn.destBits->high == 7)
            lowRange = true;
        else
            broadOrHighRange = true;
    }
    CHECK(lowRange);
    CHECK_FALSE(broadOrHighRange);
}

TEST_CASE("Extractor: indexed whole-interface slices map the same member lanes through a modport",
          "[extractor][interface][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_indexed_slices");
    const auto graph = extractor.extract();

    int matching = 0;
    for (const auto& connection : graph.connections) {
        if (connection.source.fullPath() != "interface_indexed_slices.u_writer.bus.data" ||
            connection.dest.fullPath() != "interface_indexed_slices.u_reader.bus.data")
            continue;
        REQUIRE(connection.sourceBits);
        REQUIRE(connection.destBits);
        CHECK(connection.kind == ConnectionKind::Direct);
        CHECK(connection.sourceBits->low == 4);
        CHECK(connection.sourceBits->high == 11);
        CHECK(connection.destBits->low == 4);
        CHECK(connection.destBits->high == 11);
        ++matching;
    }
    CHECK(matching == 1);
}

TEST_CASE("Extractor: high whole-interface slice keeps member bit ordinals", "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_high_slices");
    const auto graph = extractor.extract();

    bool highRange = false;
    bool broadOrLowRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_high_slices.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_high_slices.u_reader.bus.data")
            continue;
        if (conn.sourceBits && conn.destBits && conn.sourceBits->low == 8 && conn.sourceBits->high == 15 &&
            conn.destBits->low == 8 && conn.destBits->high == 15)
            highRange = true;
        else
            broadOrLowRange = true;
    }
    CHECK(highRange);
    CHECK_FALSE(broadOrLowRange);
}

TEST_CASE("Extractor: computed whole-interface slice read stays approximate", "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_expr_slices");
    const auto graph = extractor.extract();

    size_t approximateRanges = 0;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_expr_slices.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_expr_slices.u_reader.bus.data")
            continue;
        CHECK(conn.kind == ConnectionKind::Approximate);
        REQUIRE(conn.sourceBits);
        REQUIRE(conn.destBits);
        CHECK(conn.sourceBits->low == 0);
        CHECK(conn.sourceBits->high == 7);
        CHECK(conn.destBits->low == 0);
        CHECK(conn.destBits->high == 7);
        ++approximateRanges;
    }
    CHECK(approximateRanges == 1);
}

TEST_CASE("Extractor: computed member reads keep disjoint input slices", "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_pair_expr_slices");
    const auto graph = extractor.extract();

    bool lowRange = false;
    bool highRange = false;
    bool broadRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_pair_expr_slices.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_pair_expr_slices.u_reader.bus.data")
            continue;
        CHECK(conn.kind == ConnectionKind::Approximate);
        if (conn.sourceBits && conn.destBits && conn.sourceBits->low == 0 && conn.sourceBits->high == 3 &&
            conn.destBits->low == 0 && conn.destBits->high == 3)
            lowRange = true;
        else if (conn.sourceBits && conn.destBits && conn.sourceBits->low == 8 && conn.sourceBits->high == 11 &&
                 conn.destBits->low == 8 && conn.destBits->high == 11)
            highRange = true;
        else
            broadRange = true;
    }
    CHECK(lowRange);
    CHECK(highRange);
    CHECK_FALSE(broadRange);
}

TEST_CASE("Extractor: procedural guard reads only the selected interface bit", "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_guard_slices");
    const auto graph = extractor.extract();

    bool selectedBit = false;
    bool broadRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_guard_slices.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_guard_slices.u_reader.bus.data")
            continue;
        if (conn.kind == ConnectionKind::Approximate && conn.sourceBits && conn.destBits && conn.sourceBits->low == 2 &&
            conn.sourceBits->high == 2 && conn.destBits->low == 2 && conn.destBits->high == 2)
            selectedBit = true;
        else
            broadRange = true;
    }
    CHECK(selectedBit);
    CHECK_FALSE(broadRange);
}

TEST_CASE("Extractor: ascending whole-interface slice uses ordinal port bits", "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_ascending_slices");
    const auto graph = extractor.extract();

    bool ascendingRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_ascending_slices.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_ascending_slices.u_reader.bus.data")
            continue;
        REQUIRE(conn.sourceBits);
        REQUIRE(conn.destBits);
        CHECK(conn.sourceBits->low == 8);
        CHECK(conn.sourceBits->high == 15);
        CHECK(conn.destBits->low == 8);
        CHECK(conn.destBits->high == 15);
        ascendingRange = true;
    }
    CHECK(ascendingRange);
}

TEST_CASE("Extractor: modport driver reaches whole-interface member slices", "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_mixed_slice");
    const auto graph = extractor.extract();

    bool lowRange = false;
    bool highRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_mixed_slice.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_mixed_slice.u_reader.bus.data")
            continue;
        if (conn.sourceBits && conn.destBits && conn.sourceBits->low == 0 && conn.sourceBits->high == 7 &&
            conn.destBits->low == 0 && conn.destBits->high == 7)
            lowRange = true;
        if (conn.sourceBits && conn.destBits && conn.sourceBits->low == 8 && conn.sourceBits->high == 15 &&
            conn.destBits->low == 8 && conn.destBits->high == 15)
            highRange = true;
    }
    CHECK(lowRange);
    CHECK(highRange);
}

TEST_CASE("Extractor: whole-interface slice driver reaches a modport reader", "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_reverse_mixed_slice");
    const auto graph = extractor.extract();

    bool lowRange = false;
    bool broadOrHighRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_reverse_mixed_slice.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_reverse_mixed_slice.u_reader.bus.data")
            continue;
        if (conn.sourceBits && conn.destBits && conn.sourceBits->low == 0 && conn.sourceBits->high == 7 &&
            conn.destBits->low == 0 && conn.destBits->high == 7)
            lowRange = true;
        else
            broadOrHighRange = true;
    }
    CHECK(lowRange);
    CHECK_FALSE(broadOrHighRange);
}

TEST_CASE("Extractor: nested whole-interface slice forwarding preserves bit range",
          "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_nested_slices");
    const auto graph = extractor.extract();

    bool lowRange = false;
    bool broadOrHighRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_nested_slices.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_nested_slices.u_wrap.u_leaf.bus.data")
            continue;
        if (conn.sourceBits && conn.destBits && conn.sourceBits->low == 0 && conn.sourceBits->high == 7 &&
            conn.destBits->low == 0 && conn.destBits->high == 7)
            lowRange = true;
        else
            broadOrHighRange = true;
    }
    CHECK(lowRange);
    CHECK_FALSE(broadOrHighRange);
}

TEST_CASE("Extractor: whole-interface slice bridge maps distinct source and destination bits",
          "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_slice_bridge");
    const auto graph = extractor.extract();

    bool mappedRange = false;
    bool unrelatedRange = false;
    bool crossedInterfaces = false;
    for (const auto& conn : graph.connections) {
        crossedInterfaces |= (conn.source.fullPath() == "interface_whole_slice_bridge.u_writer.bus.data" &&
                              conn.dest.fullPath() == "interface_whole_slice_bridge.u_other_reader.bus.data") ||
                             (conn.source.fullPath() == "interface_whole_slice_bridge.u_other_writer.bus.data" &&
                              conn.dest.fullPath() == "interface_whole_slice_bridge.u_reader.bus.data");
        if (conn.source.fullPath() != "interface_whole_slice_bridge.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_slice_bridge.u_reader.bus.data")
            continue;
        if (conn.sourceBits && conn.destBits && conn.sourceBits->low == 8 && conn.sourceBits->high == 15 &&
            conn.destBits->low == 0 && conn.destBits->high == 7)
            mappedRange = true;
        else
            unrelatedRange = true;
    }
    CHECK(mappedRange);
    CHECK_FALSE(unrelatedRange);
    CHECK_FALSE(crossedInterfaces);
}

TEST_CASE("Extractor: two whole-interface slice bridges preserve end-to-end bit range",
          "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_slice_chain");
    const auto graph = extractor.extract();

    bool mappedRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_slice_chain.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_slice_chain.u_reader.bus.data")
            continue;
        CHECK(conn.kind == ConnectionKind::Direct);
        REQUIRE(conn.sourceBits);
        REQUIRE(conn.destBits);
        CHECK(conn.sourceBits->low == 8);
        CHECK(conn.sourceBits->high == 15);
        CHECK(conn.destBits->low == 8);
        CHECK(conn.destBits->high == 15);
        mappedRange = true;
    }
    CHECK(mappedRange);
}

TEST_CASE("Extractor: conditional whole-interface slice forwarding stays approximate",
          "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_branch_bridge");
    const auto graph = extractor.extract();

    bool mappedRange = false;
    bool directRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "interface_whole_branch_bridge.u_writer.bus.data" ||
            conn.dest.fullPath() != "interface_whole_branch_bridge.u_reader.bus.data")
            continue;
        if (conn.sourceBits && conn.destBits && conn.sourceBits->low == 8 && conn.sourceBits->high == 15 &&
            conn.destBits->low == 0 && conn.destBits->high == 7)
            mappedRange = true;
        directRange |= conn.kind == ConnectionKind::Direct;
    }
    CHECK(mappedRange);
    CHECK_FALSE(directRange);
}

TEST_CASE("Extractor: ternary whole-interface slice forwarding retains both may-flow sources",
          "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_mux_bridge");
    const auto graph = extractor.extract();

    bool sourceA = false;
    bool sourceB = false;
    bool directRange = false;
    for (const auto& conn : graph.connections) {
        if (conn.dest.fullPath() != "interface_whole_mux_bridge.u_reader.bus.data" ||
            (conn.source.fullPath() != "interface_whole_mux_bridge.u_writer_a.bus.data" &&
             conn.source.fullPath() != "interface_whole_mux_bridge.u_writer_b.bus.data"))
            continue;
        if (!conn.sourceBits || !conn.destBits || conn.sourceBits->low != 8 || conn.sourceBits->high != 15 ||
            conn.destBits->low != 0 || conn.destBits->high != 7)
            continue;
        sourceA |= conn.source.fullPath() == "interface_whole_mux_bridge.u_writer_a.bus.data";
        sourceB |= conn.source.fullPath() == "interface_whole_mux_bridge.u_writer_b.bus.data";
        directRange |= conn.kind == ConnectionKind::Direct;
    }
    CHECK(sourceA);
    CHECK(sourceB);
    CHECK_FALSE(directRange);
}

TEST_CASE("Extractor: constant ternary interface slice forwards only the selected source",
          "[extractor][interface][whole][slice]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_const_mux_bridge");
    const auto graph = extractor.extract();

    bool selectedSource = false;
    bool unselectedSource = false;
    bool unselectedInputPort = false;
    for (const auto& conn : graph.connections) {
        unselectedInputPort |= conn.source.fullPath() == "interface_whole_const_mux_bridge.u_writer_b.bus.data" &&
                               conn.dest.fullPath() == "interface_whole_const_mux_bridge.u_bridge.in_b.data";
        if (conn.dest.fullPath() != "interface_whole_const_mux_bridge.u_reader.bus.data")
            continue;
        if (conn.source.fullPath() == "interface_whole_const_mux_bridge.u_writer_a.bus.data") {
            selectedSource = true;
            CHECK(conn.kind == ConnectionKind::Direct);
            REQUIRE(conn.sourceBits);
            REQUIRE(conn.destBits);
            CHECK(conn.sourceBits->low == 8);
            CHECK(conn.sourceBits->high == 15);
            CHECK(conn.destBits->low == 0);
            CHECK(conn.destBits->high == 7);
        }
        unselectedSource |= conn.source.fullPath() == "interface_whole_const_mux_bridge.u_writer_b.bus.data";
    }
    CHECK(selectedSource);
    CHECK_FALSE(unselectedSource);
    CHECK_FALSE(unselectedInputPort);
}

TEST_CASE("Extractor: constant procedural if excludes the inactive whole-interface input",
          "[extractor][interface][whole][procedural][constant]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_const_if_bridge");
    const auto graph = extractor.extract();

    const std::string top = "interface_whole_const_if_bridge.";
    bool selected = false;
    bool inactive = false;
    bool inactiveInputPort = false;
    for (const auto& conn : graph.connections) {
        inactiveInputPort |=
            conn.source.fullPath() == top + "u_writer_a.bus.data" && conn.dest.fullPath() == top + "u_bridge.in_a.data";
        if (conn.dest.fullPath() != top + "u_reader.bus.data")
            continue;
        selected |= conn.source.fullPath() == top + "u_writer_b.bus.data";
        inactive |= conn.source.fullPath() == top + "u_writer_a.bus.data";
    }
    CHECK(selected);
    CHECK_FALSE(inactive);
    CHECK_FALSE(inactiveInputPort);
}

TEST_CASE("Extractor: constant procedural case excludes the inactive whole-interface input",
          "[extractor][interface][whole][procedural][constant]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_const_case_bridge");
    const auto graph = extractor.extract();

    const std::string top = "interface_whole_const_case_bridge.";
    bool selected = false;
    bool inactive = false;
    bool inactiveInputPort = false;
    for (const auto& conn : graph.connections) {
        inactiveInputPort |=
            conn.source.fullPath() == top + "u_writer_a.bus.data" && conn.dest.fullPath() == top + "u_bridge.in_a.data";
        if (conn.dest.fullPath() != top + "u_reader.bus.data")
            continue;
        selected |= conn.source.fullPath() == top + "u_writer_b.bus.data";
        inactive |= conn.source.fullPath() == top + "u_writer_a.bus.data";
    }
    CHECK(selected);
    CHECK_FALSE(inactive);
    CHECK_FALSE(inactiveInputPort);
}

TEST_CASE("Extractor: constant casez excludes the inactive whole-interface input",
          "[extractor][interface][whole][procedural][constant]") {
    auto result = compileFile("sv/interface_whole_slices.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_const_casez_bridge");
    const auto graph = extractor.extract();

    const std::string top = "interface_whole_const_casez_bridge.";
    bool selected = false;
    bool inactive = false;
    bool inactiveInputPort = false;
    for (const auto& conn : graph.connections) {
        inactiveInputPort |=
            conn.source.fullPath() == top + "u_writer_a.bus.data" && conn.dest.fullPath() == top + "u_bridge.in_a.data";
        if (conn.dest.fullPath() != top + "u_reader.bus.data")
            continue;
        selected |= conn.source.fullPath() == top + "u_writer_b.bus.data";
        inactive |= conn.source.fullPath() == top + "u_writer_a.bus.data";
    }
    CHECK(selected);
    CHECK_FALSE(inactive);
    CHECK_FALSE(inactiveInputPort);
}

TEST_CASE("Extractor: ordinary ternary bus forwards both sources approximately", "[extractor][bit_flow][ternary]") {
    auto result = compileFile("sv/ternary_bit_flow.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "ternary_bit_flow");
    const auto graph = extractor.extract();

    bool sourceA = false;
    bool sourceB = false;
    for (const auto& conn : graph.connections) {
        if (conn.dest.fullPath() != "ternary_bit_flow.u_sink.data_i")
            continue;
        if (conn.source.fullPath() != "ternary_bit_flow.u_a.data_o" &&
            conn.source.fullPath() != "ternary_bit_flow.u_b.data_o")
            continue;
        CHECK(conn.kind == ConnectionKind::Approximate);
        REQUIRE(conn.sourceBits);
        REQUIRE(conn.destBits);
        CHECK(conn.sourceBits->low == 0);
        CHECK(conn.sourceBits->high == 7);
        CHECK(conn.destBits->low == 0);
        CHECK(conn.destBits->high == 7);
        sourceA |= conn.source.fullPath() == "ternary_bit_flow.u_a.data_o";
        sourceB |= conn.source.fullPath() == "ternary_bit_flow.u_b.data_o";
    }
    CHECK(sourceA);
    CHECK(sourceB);
}

TEST_CASE("Extractor: generated-scope whole-interface member use stays directed", "[extractor][interface][whole]") {
    auto result = compileFile("sv/interface_whole_generate.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_generate");
    const auto graph = extractor.extract();

    bool forwardData = false;
    bool reverseData = false;
    bool crossedInstances = false;
    bool unusedValid = false;
    for (const auto& conn : graph.connections) {
        const auto src = conn.source.fullPath();
        const auto dest = conn.dest.fullPath();
        forwardData |=
            src == "interface_whole_generate.u_prod.bus.data" && dest == "interface_whole_generate.u_cons.bus.data";
        reverseData |=
            src == "interface_whole_generate.u_cons.bus.data" && dest == "interface_whole_generate.u_prod.bus.data";
        crossedInstances |= src == "interface_whole_generate.u_prod.bus.data" &&
                            dest == "interface_whole_generate.u_other_cons.bus.data";
        unusedValid |= dest == "interface_whole_generate.u_cons.bus.valid";
    }
    CHECK(forwardData);
    CHECK_FALSE(reverseData);
    CHECK_FALSE(crossedInstances);
    CHECK_FALSE(unusedValid);
}

TEST_CASE("Extractor: nested whole-interface forwarding reaches leaf members", "[extractor][interface][whole]") {
    auto result = compileFile("sv/interface_whole_forward.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_whole_forward");
    const auto graph = extractor.extract();

    bool reachedLeaf = false;
    bool crossedInstances = false;
    for (const auto& conn : graph.connections) {
        const auto src = conn.source.fullPath();
        const auto dest = conn.dest.fullPath();
        reachedLeaf |= src == "interface_whole_forward.u_prod.bus.data" &&
                       dest == "interface_whole_forward.u_wrap.u_leaf.bus.data";
        crossedInstances |= src == "interface_whole_forward.u_prod.bus.data" &&
                            dest == "interface_whole_forward.u_other_wrap.u_leaf.bus.data";
    }
    CHECK(reachedLeaf);
    CHECK_FALSE(crossedInstances);
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

TEST_CASE("Extractor: AXI-lite-style modport channels preserve direction and isolation",
          "[extractor][interface][axi]") {
    auto result = compileFile("sv/interface_axi_lite.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_axi_lite");
    const auto graph = extractor.extract();

    auto hasEdge = [&](const std::string& source, const std::string& dest) {
        for (const auto& conn : graph.connections) {
            if (conn.source.fullPath() == source && conn.dest.fullPath() == dest)
                return true;
        }
        return false;
    };
    CHECK(hasEdge("interface_axi_lite.u_mgr_a.bus.awaddr", "interface_axi_lite.u_sub_a.bus.awaddr"));
    CHECK(hasEdge("interface_axi_lite.u_mgr_a.bus.wdata", "interface_axi_lite.u_sub_a.bus.wdata"));
    CHECK(hasEdge("interface_axi_lite.u_sub_a.bus.awready", "interface_axi_lite.u_mgr_a.bus.awready"));
    CHECK(hasEdge("interface_axi_lite.u_sub_a.bus.bresp", "interface_axi_lite.u_mgr_a.bus.bresp"));
    CHECK_FALSE(hasEdge("interface_axi_lite.u_sub_a.bus.awaddr", "interface_axi_lite.u_mgr_a.bus.awaddr"));
    CHECK_FALSE(hasEdge("interface_axi_lite.u_mgr_a.bus.wdata", "interface_axi_lite.u_sub_b.bus.wdata"));
}

TEST_CASE("Extractor: parameterized interface-array elements do not cross-connect", "[extractor][interface][array]") {
    auto result = compileFile("sv/interface_array_modport.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_array_modport");
    const auto graph = extractor.extract();

    auto hasEdge = [&](const std::string& source, const std::string& dest) {
        for (const auto& conn : graph.connections) {
            if (conn.source.fullPath() == source && conn.dest.fullPath() == dest)
                return true;
        }
        return false;
    };
    CHECK(hasEdge("interface_array_modport.u_w0.bus.data", "interface_array_modport.u_r0.bus.data"));
    CHECK(hasEdge("interface_array_modport.u_w1.bus.data", "interface_array_modport.u_r1.bus.data"));
    CHECK_FALSE(hasEdge("interface_array_modport.u_w0.bus.data", "interface_array_modport.u_r1.bus.data"));
    CHECK_FALSE(hasEdge("interface_array_modport.u_w1.bus.data", "interface_array_modport.u_r0.bus.data"));
    bool foundParameterizedWidth = false;
    for (const auto& port : graph.allPorts) {
        if (port.fullPath() == "interface_array_modport.u_w0.bus.data") {
            foundParameterizedWidth = true;
            CHECK(port.width == 12);
        }
    }
    CHECK(foundParameterizedWidth);
}

TEST_CASE("Extractor: generated interface-array lanes keep elaborated indexes", "[extractor][interface][array]") {
    auto result = compileFile("sv/interface_array_generate.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "interface_array_generate");
    const auto graph = extractor.extract();

    auto hasEdge = [&](const std::string& source, const std::string& dest) {
        for (const auto& conn : graph.connections) {
            if (conn.source.fullPath() == source && conn.dest.fullPath() == dest)
                return true;
        }
        return false;
    };
    const std::string w0 = "interface_array_generate.g_lane[0].u_w.bus.data";
    const std::string r0 = "interface_array_generate.g_lane[0].u_r.bus.data";
    const std::string w1 = "interface_array_generate.g_lane[1].u_w.bus.data";
    const std::string r1 = "interface_array_generate.g_lane[1].u_r.bus.data";
    CHECK(hasEdge(w0, r0));
    CHECK(hasEdge(w1, r1));
    CHECK_FALSE(hasEdge(w0, r1));
    CHECK_FALSE(hasEdge(w1, r0));
}

TEST_CASE("Extractor: overlapping port slices retain exact ordinal bit ranges", "[extractor][slice]") {
    auto result = compileFile("sv/slice_overlap_ports.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "slice_overlap_top");
    const auto graph = extractor.extract();

    const Connection* low = nullptr;
    const Connection* high = nullptr;
    const Connection* bit = nullptr;
    bool crossedOther = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "slice_overlap_top.u_src.o_data")
            continue;
        if (conn.dest.fullPath() == "slice_overlap_top.u_low.i_data")
            low = &conn;
        if (conn.dest.fullPath() == "slice_overlap_top.u_high.i_data")
            high = &conn;
        if (conn.dest.fullPath() == "slice_overlap_top.u_bit.i_data")
            bit = &conn;
        crossedOther |= conn.dest.fullPath() == "slice_overlap_top.u_other.i_data";
    }
    REQUIRE(low);
    REQUIRE(high);
    REQUIRE(bit);
    REQUIRE(low->sourceBits);
    REQUIRE(low->destBits);
    CHECK(low->sourceBits->low == 0);
    CHECK(low->sourceBits->high == 3);
    CHECK(low->destBits->low == 0);
    CHECK(low->destBits->high == 3);
    REQUIRE(high->sourceBits);
    REQUIRE(high->destBits);
    CHECK(high->sourceBits->low == 4);
    CHECK(high->sourceBits->high == 7);
    CHECK(high->destBits->low == 0);
    CHECK(high->destBits->high == 3);
    REQUIRE(bit->sourceBits);
    REQUIRE(bit->destBits);
    CHECK(bit->sourceBits->low == 5);
    CHECK(bit->sourceBits->high == 5);
    CHECK(bit->destBits->low == 0);
    CHECK(bit->destBits->high == 0);
    CHECK_FALSE(crossedOther);
    WidthChecker checker;
    CHECK(checker.check(graph).empty());
}

TEST_CASE("Extractor: disjoint partial drivers map only their byte lanes", "[extractor][slice]") {
    auto result = compileFile("sv/slice_partial_drivers.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "slice_partial_drivers_top");
    const auto graph = extractor.extract();

    const Connection* low = nullptr;
    const Connection* high = nullptr;
    bool wrongLowSink = false;
    for (const auto& conn : graph.connections) {
        if (conn.dest.fullPath() == "slice_partial_drivers_top.u_full.i_data") {
            if (conn.source.fullPath() == "slice_partial_drivers_top.u_low.o_data")
                low = &conn;
            if (conn.source.fullPath() == "slice_partial_drivers_top.u_high.o_data")
                high = &conn;
        }
        wrongLowSink |= conn.source.fullPath() == "slice_partial_drivers_top.u_high.o_data" &&
                        conn.dest.fullPath() == "slice_partial_drivers_top.u_low_sink.i_data";
    }
    REQUIRE(low);
    REQUIRE(high);
    REQUIRE(low->destBits);
    REQUIRE(high->destBits);
    CHECK(low->destBits->low == 0);
    CHECK(low->destBits->high == 3);
    CHECK(high->destBits->low == 4);
    CHECK(high->destBits->high == 7);
    CHECK_FALSE(wrongLowSink);
    WidthChecker checker;
    CHECK(checker.check(graph).empty());
}

TEST_CASE("Extractor: ascending declared ranges map ordinal port bits correctly", "[extractor][slice]") {
    auto result = compileFile("sv/slice_ascending.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "slice_ascending_top");
    const auto graph = extractor.extract();

    const Connection* high = nullptr;
    const Connection* low = nullptr;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "slice_ascending_top.u_src.o_data")
            continue;
        if (conn.dest.fullPath() == "slice_ascending_top.u_high.i_data")
            high = &conn;
        if (conn.dest.fullPath() == "slice_ascending_top.u_low.i_data")
            low = &conn;
    }
    REQUIRE(high);
    REQUIRE(low);
    REQUIRE(high->sourceBits);
    REQUIRE(low->sourceBits);
    CHECK(high->sourceBits->low == 4);
    CHECK(high->sourceBits->high == 7);
    CHECK(low->sourceBits->low == 0);
    CHECK(low->sourceBits->high == 3);
}

TEST_CASE("Extractor: dynamic index rendezvous is never a direct connection", "[extractor][slice][dynamic]") {
    auto result = compileFile("sv/dynamic_index_ports.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "dynamic_index_top");
    const auto graph = extractor.extract();
    bool foundApproximate = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() == "dynamic_index_top.u_src.o_data" &&
            conn.dest.fullPath() == "dynamic_index_top.u_sink.i_data") {
            foundApproximate = true;
            CHECK(conn.kind == ConnectionKind::Approximate);
        }
    }
    CHECK(foundApproximate);
}

TEST_CASE("Extractor: dynamic unpacked-array write reaches each possible consumer conservatively",
          "[extractor][slice][dynamic]") {
    auto result = compileFile("sv/dynamic_array_write.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "dynamic_array_write_top");
    const auto graph = extractor.extract();

    bool zero = false;
    bool one = false;
    bool portDynamic = false;
    for (const auto& conn : graph.connections) {
        const auto source = conn.source.fullPath();
        const auto dest = conn.dest.fullPath();
        if (source == "dynamic_array_write_top.u_source.o_data" && dest == "dynamic_array_write_top.u_zero.i_data") {
            CHECK(conn.kind == ConnectionKind::Approximate);
            CHECK_FALSE(conn.sourceBits);
            zero = true;
        }
        if (source == "dynamic_array_write_top.u_source.o_data" && dest == "dynamic_array_write_top.u_one.i_data") {
            CHECK(conn.kind == ConnectionKind::Approximate);
            CHECK_FALSE(conn.sourceBits);
            one = true;
        }
        if (source == "dynamic_array_write_top.u_port_source.o_data" &&
            dest == "dynamic_array_write_top.u_port_dynamic.i_data") {
            CHECK(conn.kind == ConnectionKind::Approximate);
            CHECK_FALSE(conn.sourceBits);
            portDynamic = true;
        }
    }
    CHECK(zero);
    CHECK(one);
    CHECK(portDynamic);
}

TEST_CASE("Extractor: concatenations preserve positional bit flow through chains", "[extractor][slice][concat]") {
    auto result = compileFile("sv/concat_bit_flow.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "concat_bit_flow_top");
    const auto graph = extractor.extract();

    auto findEdge = [&](const std::string& source, const std::string& dest) -> const Connection* {
        for (const auto& conn : graph.connections) {
            if (conn.source.fullPath() == source && conn.dest.fullPath() == dest)
                return &conn;
        }
        return nullptr;
    };
    const std::string top = "concat_bit_flow_top.";
    CHECK(findEdge(top + "u_a.o_data", top + "u_pair_hi.i_data"));
    CHECK(findEdge(top + "u_b.o_data", top + "u_pair_lo.i_data"));
    CHECK_FALSE(findEdge(top + "u_a.o_data", top + "u_pair_lo.i_data"));
    CHECK_FALSE(findEdge(top + "u_b.o_data", top + "u_pair_hi.i_data"));
    const auto* proceduralHigh = findEdge(top + "u_a.o_data", top + "u_procedural_hi.i_data");
    const auto* proceduralLow = findEdge(top + "u_b.o_data", top + "u_procedural_lo.i_data");
    REQUIRE(proceduralHigh);
    REQUIRE(proceduralLow);
    CHECK(proceduralHigh->kind == ConnectionKind::Approximate);
    CHECK(proceduralLow->kind == ConnectionKind::Approximate);
    CHECK_FALSE(findEdge(top + "u_a.o_data", top + "u_procedural_lo.i_data"));
    CHECK_FALSE(findEdge(top + "u_b.o_data", top + "u_procedural_hi.i_data"));
    const auto* selectHigh = findEdge(top + "u_select.o_select", top + "u_selected_hi.i_data");
    const auto* selectLow = findEdge(top + "u_select.o_select", top + "u_selected_lo.i_data");
    REQUIRE(selectHigh);
    REQUIRE(selectLow);
    CHECK(selectHigh->kind == ConnectionKind::Approximate);
    CHECK(selectLow->kind == ConnectionKind::Approximate);
    CHECK_FALSE(findEdge(top + "u_select.o_select", top + "u_pair_hi.i_data"));
    const auto* arithmeticA = findEdge(top + "u_a.o_data", top + "u_arithmetic_hi.i_data");
    const auto* arithmeticB = findEdge(top + "u_b.o_data", top + "u_arithmetic_hi.i_data");
    REQUIRE(arithmeticA);
    REQUIRE(arithmeticB);
    CHECK(arithmeticA->kind == ConnectionKind::Approximate);
    CHECK(arithmeticB->kind == ConnectionKind::Approximate);
    CHECK(findEdge(top + "u_a.o_data", top + "u_arithmetic_lo.i_data"));
    CHECK_FALSE(findEdge(top + "u_b.o_data", top + "u_arithmetic_lo.i_data"));

    const auto* high = findEdge(top + "u_wide.o_data", top + "u_wide_hi.i_data");
    const auto* low = findEdge(top + "u_wide.o_data", top + "u_wide_lo.i_data");
    const auto* chainedHigh = findEdge(top + "u_wide.o_data", top + "u_chained_hi.i_data");
    const auto* chainedLow = findEdge(top + "u_wide.o_data", top + "u_chained_lo.i_data");
    const auto* chainedAlias = findEdge(top + "u_wide.o_data", top + "u_chained_alias.i_data");
    REQUIRE(high);
    REQUIRE(low);
    REQUIRE(chainedHigh);
    REQUIRE(chainedLow);
    REQUIRE(chainedAlias);
    REQUIRE(high->sourceBits);
    REQUIRE(low->sourceBits);
    CHECK(high->sourceBits->low == 4);
    CHECK(high->sourceBits->high == 7);
    CHECK(low->sourceBits->low == 0);
    CHECK(low->sourceBits->high == 3);
    CHECK_FALSE(findEdge(top + "u_wide.o_data", top + "u_pair_hi.i_data"));

    const auto* assembledHigh = findEdge(top + "u_a.o_data", top + "u_assembled.i_data");
    const auto* assembledLow = findEdge(top + "u_b.o_data", top + "u_assembled.i_data");
    REQUIRE(assembledHigh);
    REQUIRE(assembledLow);
    REQUIRE(assembledHigh->destBits);
    REQUIRE(assembledLow->destBits);
    CHECK(assembledHigh->destBits->low == 4);
    CHECK(assembledHigh->destBits->high == 7);
    CHECK(assembledLow->destBits->low == 0);
    CHECK(assembledLow->destBits->high == 3);

    size_t reversedBits = 0;
    bool mappedFirst = false;
    bool mappedLast = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != top + "u_wide.o_data" || conn.dest.fullPath() != top + "u_reversed.i_data" ||
            !conn.sourceBits || !conn.destBits)
            continue;
        ++reversedBits;
        mappedFirst |= conn.sourceBits->low == 0 && conn.destBits->low == 7;
        mappedLast |= conn.sourceBits->low == 7 && conn.destBits->low == 0;
    }
    CHECK(reversedBits == 8);
    CHECK(mappedFirst);
    CHECK(mappedLast);

    const auto* unequalHigh = findEdge(top + "u_wide.o_data", top + "u_unequal_hi.i_data");
    const auto* unequalLow = findEdge(top + "u_wide.o_data", top + "u_unequal_lo.i_data");
    REQUIRE(unequalHigh);
    REQUIRE(unequalLow);
    REQUIRE(unequalHigh->sourceBits);
    REQUIRE(unequalLow->sourceBits);
    CHECK(unequalHigh->sourceBits->low == 6);
    CHECK(unequalHigh->sourceBits->high == 7);
    CHECK(unequalLow->sourceBits->low == 0);
    CHECK(unequalLow->sourceBits->high == 5);

    const auto* constantHigh = findEdge(top + "u_wide.o_data", top + "u_constant_hi.i_data");
    const auto* constantLow = findEdge(top + "u_wide.o_data", top + "u_constant_lo.i_data");
    REQUIRE(constantHigh);
    REQUIRE(constantLow);
    REQUIRE(constantHigh->destBits);
    CHECK(constantHigh->destBits->low == 2);
    CHECK(constantHigh->destBits->high == 3);
}

TEST_CASE("Extractor: bounded bit-flow gaps retain scope and source location", "[extractor][slice][gap]") {
    auto result = compileFile("sv/bit_flow_gaps.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "bit_flow_gaps_top");
    const auto graph = extractor.extract();

    CHECK(graph.bitFlowGapCount == 8);
    CHECK(graph.bitFlowGapReasons.at("width_limit") == 1);
    CHECK(graph.bitFlowGapReasons.at("unresolved_source_range") == 7);
    REQUIRE(graph.bitFlowGaps.size() == 5);
    CHECK(graph.bitFlowGaps[0].scopePath == "bit_flow_gaps_top");
    CHECK(graph.bitFlowGaps[0].reason == "width_limit");
    CHECK(graph.bitFlowGaps[0].lhsWidth == 4097);
    CHECK(graph.bitFlowGaps[0].rhsWidth == 4097);
    CHECK(graph.bitFlowGaps[0].location.valid());
    CHECK(graph.bitFlowGaps[1].reason == "unresolved_source_range");
    CHECK(graph.bitFlowGaps[1].location.valid());

    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() == "bit_flow_gaps_top.u_wide_source.o_data" &&
            conn.dest.fullPath() == "bit_flow_gaps_top.u_wide_sink.i_data") {
            CHECK_FALSE(conn.sourceBits);
            CHECK_FALSE(conn.destBits);
        }
    }
}

TEST_CASE("Extractor: constant unpacked-array elements retain positional bit flow", "[extractor][slice][array]") {
    auto result = compileFile("sv/unpacked_array_element.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "unpacked_array_element_top");
    const auto graph = extractor.extract();

    bool foundExact = false;
    bool foundDynamic = false;
    bool dynamicFromSource = false;
    bool dynamicFromOther = false;
    bool crossedElements = false;
    bool foundWholeArrayPort = false;
    bool redundantCoarseRow = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() == "unpacked_array_element_top.u_whole.o_data" &&
            conn.dest.fullPath() == "unpacked_array_element_top.u_whole_sink.i_data") {
            CHECK(conn.kind == ConnectionKind::Approximate);
            foundWholeArrayPort = true;
        }
        if (conn.source.fullPath() == "unpacked_array_element_top.u_other.o_data" &&
            conn.dest.fullPath() == "unpacked_array_element_top.u_copied.i_data")
            crossedElements = true;
        if (conn.dest.fullPath() == "unpacked_array_element_top.u_dynamic.i_data" && !conn.sourceBits) {
            if (conn.source.fullPath() == "unpacked_array_element_top.u_source.o_data")
                dynamicFromSource = conn.kind == ConnectionKind::Approximate;
            if (conn.source.fullPath() == "unpacked_array_element_top.u_other.o_data")
                dynamicFromOther = conn.kind == ConnectionKind::Approximate;
        }
        if (conn.source.fullPath() != "unpacked_array_element_top.u_source.o_data")
            continue;
        if (conn.dest.fullPath() == "unpacked_array_element_top.u_copied.i_data" && !conn.sourceBits && !conn.destBits)
            redundantCoarseRow = true;
        if (conn.dest.fullPath() == "unpacked_array_element_top.u_copied.i_data" && conn.sourceBits && conn.destBits) {
            CHECK(conn.kind == ConnectionKind::Direct);
            CHECK(conn.sourceBits->low == 0);
            CHECK(conn.sourceBits->high == 7);
            CHECK(conn.destBits->low == 0);
            CHECK(conn.destBits->high == 7);
            foundExact = true;
        }
        if (conn.dest.fullPath() == "unpacked_array_element_top.u_dynamic.i_data" && conn.sourceBits && conn.destBits)
            foundDynamic = true;
    }
    CHECK(foundExact);
    CHECK_FALSE(foundDynamic);
    CHECK(dynamicFromSource);
    CHECK(dynamicFromOther);
    CHECK_FALSE(crossedElements);
    CHECK(foundWholeArrayPort);
    CHECK_FALSE(redundantCoarseRow);
    CHECK(graph.bitFlowGapReasons.at("unresolved_source_range") >= 1);
}

TEST_CASE("Extractor: packed selections inside unpacked elements keep their owning element",
          "[extractor][slice][array]") {
    auto result = compileFile("sv/mixed_array_selection.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "mixed_array_top");
    const auto graph = extractor.extract();

    const std::string left = "mixed_array_top.u_left.o_data";
    const std::string right = "mixed_array_top.u_right.o_data";
    auto hasExact = [&](const std::string& source, const std::string& dest, int64_t first, int64_t last) {
        bool found = false;
        for (const auto& conn : graph.connections) {
            if (conn.source.fullPath() != source || conn.dest.fullPath() != dest || !conn.sourceBits || !conn.destBits)
                continue;
            CHECK(conn.kind == ConnectionKind::Direct);
            CHECK(conn.sourceBits->low == first);
            CHECK(conn.sourceBits->high == last);
            CHECK(conn.destBits->low == 0);
            CHECK(conn.destBits->high == last - first);
            found = true;
        }
        return found;
    };
    CHECK(hasExact(left, "mixed_array_top.u_left_hi.i_data", 4, 7));
    CHECK(hasExact(right, "mixed_array_top.u_right_lo.i_data", 0, 3));
    CHECK(hasExact(left, "mixed_array_top.u_left_bit.i_bit", 6, 6));
    CHECK(hasExact(right, "mixed_array_top.u_chain.i_data", 4, 7));
    CHECK(hasExact("mixed_array_top.u_multi.o_data", "mixed_array_top.u_multi_hi.i_data", 4, 7));
    CHECK(hasExact("mixed_array_top.u_struct.o_data", "mixed_array_top.u_struct_hi.i_data", 4, 7));

    auto hasApprox = [&](const std::string& source, const std::string& dest) {
        return std::any_of(graph.connections.begin(), graph.connections.end(), [&](const Connection& conn) {
            return conn.source.fullPath() == source && conn.dest.fullPath() == dest &&
                   conn.kind == ConnectionKind::Approximate && !conn.sourceBits && !conn.destBits;
        });
    };
    CHECK(hasApprox(left, "mixed_array_top.u_dynamic_outer.i_bit"));
    CHECK(hasApprox(right, "mixed_array_top.u_dynamic_outer.i_bit"));
    CHECK(hasApprox(left, "mixed_array_top.u_dynamic_inner.i_data"));
    CHECK_FALSE(hasApprox(right, "mixed_array_top.u_dynamic_inner.i_data"));
    CHECK(hasApprox("mixed_array_top.u_struct.o_data", "mixed_array_top.u_struct_dynamic.i_data"));
    CHECK_FALSE(hasApprox("mixed_array_top.u_other_field.o_data", "mixed_array_top.u_struct_dynamic.i_data"));
    CHECK_FALSE(hasApprox("mixed_array_top.u_other_lane.o_data", "mixed_array_top.u_dynamic_fixed_lane.i_data"));

    for (const auto& conn : graph.connections) {
        const auto source = conn.source.fullPath();
        const auto dest = conn.dest.fullPath();
        CHECK_FALSE((source == right && dest == "mixed_array_top.u_dynamic_inner.i_data"));
        CHECK_FALSE(
            (source == "mixed_array_top.u_other_field.o_data" && dest == "mixed_array_top.u_struct_dynamic.i_data"));
        CHECK_FALSE(
            (source == "mixed_array_top.u_other_lane.o_data" && dest == "mixed_array_top.u_dynamic_fixed_lane.i_data"));
        CHECK_FALSE(
            (source == "mixed_array_top.u_bank_extra.o_data" &&
             (dest == "mixed_array_top.u_dynamic_outer.i_bit" || dest == "mixed_array_top.u_dynamic_inner.i_data")));
    }

    for (const auto& conn : graph.connections) {
        const auto source = conn.source.fullPath();
        const auto dest = conn.dest.fullPath();
        if (dest == "mixed_array_top.u_dynamic_outer.i_bit" || dest == "mixed_array_top.u_dynamic_inner.i_data" ||
            dest == "mixed_array_top.u_struct_dynamic.i_data")
            CHECK_FALSE(conn.sourceBits);
        CHECK_FALSE((source == left && dest == "mixed_array_top.u_right_lo.i_data"));
        CHECK_FALSE((source == right &&
                     (dest == "mixed_array_top.u_left_hi.i_data" || dest == "mixed_array_top.u_left_bit.i_bit")));
    }
}

TEST_CASE("Extractor: packed-struct array elements retain ordinal bit flow", "[extractor][slice][struct]") {
    auto result = compileFile("sv/packed_struct_array_element.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "packed_struct_array_top");
    const auto graph = extractor.extract();

    bool exactCopied = false;
    bool exactOther = false;
    bool exactAscending = false;
    bool exactWholePort = false;
    bool exactWholeAscendingPort = false;
    bool crossedElement = false;
    bool exactDynamic = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() == "packed_struct_array_top.u_other.o_data" &&
            conn.dest.fullPath() == "packed_struct_array_top.u_copied.i_data")
            crossedElement = true;
        if (conn.source.fullPath() == "packed_struct_array_top.u_source.o_data" &&
            conn.dest.fullPath() == "packed_struct_array_top.u_other_copied.i_data")
            crossedElement = true;
        if (conn.source.fullPath() == "packed_struct_array_top.u_ascending_other.o_data" &&
            conn.dest.fullPath() == "packed_struct_array_top.u_ascending_copied.i_data")
            crossedElement = true;
        if (conn.dest.fullPath() == "packed_struct_array_top.u_selected.i_data" && conn.sourceBits && conn.destBits)
            exactDynamic = true;
        if (conn.source.fullPath() == "packed_struct_array_top.u_source.o_data" &&
            conn.dest.fullPath() == "packed_struct_array_top.u_copied.i_data" && conn.sourceBits && conn.destBits) {
            CHECK(conn.kind == ConnectionKind::Direct);
            CHECK(conn.sourceBits->low == 0);
            CHECK(conn.sourceBits->high == 7);
            CHECK(conn.destBits->low == 0);
            CHECK(conn.destBits->high == 7);
            exactCopied = true;
        }
        if (conn.source.fullPath() == "packed_struct_array_top.u_other.o_data" &&
            conn.dest.fullPath() == "packed_struct_array_top.u_other_copied.i_data" && conn.sourceBits &&
            conn.destBits) {
            CHECK(conn.kind == ConnectionKind::Direct);
            CHECK(conn.sourceBits->low == 0);
            CHECK(conn.sourceBits->high == 7);
            CHECK(conn.destBits->low == 0);
            CHECK(conn.destBits->high == 7);
            exactOther = true;
        }
        if (conn.source.fullPath() == "packed_struct_array_top.u_ascending.o_data" &&
            conn.dest.fullPath() == "packed_struct_array_top.u_ascending_copied.i_data" && conn.sourceBits &&
            conn.destBits) {
            CHECK(conn.kind == ConnectionKind::Direct);
            CHECK(conn.sourceBits->low == 0);
            CHECK(conn.sourceBits->high == 7);
            CHECK(conn.destBits->low == 0);
            CHECK(conn.destBits->high == 7);
            exactAscending = true;
        }
        if (conn.source.fullPath() == "packed_struct_array_top.u_whole.o_data" &&
            conn.dest.fullPath() == "packed_struct_array_top.u_whole_copied.i_data" && conn.sourceBits &&
            conn.destBits) {
            CHECK(conn.kind == ConnectionKind::Direct);
            CHECK(conn.sourceBits->low == 8);
            CHECK(conn.sourceBits->high == 15);
            CHECK(conn.destBits->low == 0);
            CHECK(conn.destBits->high == 7);
            exactWholePort = true;
        }
        if (conn.source.fullPath() == "packed_struct_array_top.u_whole_ascending.o_data" &&
            conn.dest.fullPath() == "packed_struct_array_top.u_whole_ascending_copied.i_data" && conn.sourceBits &&
            conn.destBits) {
            CHECK(conn.kind == ConnectionKind::Direct);
            CHECK(conn.sourceBits->low == 8);
            CHECK(conn.sourceBits->high == 15);
            CHECK(conn.destBits->low == 0);
            CHECK(conn.destBits->high == 7);
            exactWholeAscendingPort = true;
        }
    }
    CHECK(exactCopied);
    CHECK(exactOther);
    CHECK(exactAscending);
    CHECK(exactWholePort);
    CHECK(exactWholeAscendingPort);
    CHECK_FALSE(crossedElement);
    CHECK_FALSE(exactDynamic);
}

TEST_CASE("Extractor: nested packed-array elements map distinct ordinal lanes", "[extractor][slice][packed]") {
    auto result = compileFile("sv/nested_packed_array.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "nested_packed_array_top");
    const auto graph = extractor.extract();

    bool high = false;
    bool low = false;
    bool bit = false;
    bool chain = false;
    bool ascending = false;
    bool crossed = false;
    bool dynamicExact = false;
    for (const auto& conn : graph.connections) {
        const auto source = conn.source.fullPath();
        const auto dest = conn.dest.fullPath();
        if (dest == "nested_packed_array_top.u_dynamic.i_data" && conn.sourceBits)
            dynamicExact = true;
        if (!conn.sourceBits || !conn.destBits)
            continue;
        if (source == "nested_packed_array_top.u_source.o_data" && dest == "nested_packed_array_top.u_high.i_data") {
            high |= conn.kind == ConnectionKind::Direct && conn.sourceBits->low == 40 && conn.sourceBits->high == 47 &&
                    conn.destBits->low == 0 && conn.destBits->high == 7;
            crossed |= conn.sourceBits->low != 40 || conn.sourceBits->high != 47;
        }
        if (source == "nested_packed_array_top.u_source.o_data" && dest == "nested_packed_array_top.u_low.i_data") {
            low |= conn.kind == ConnectionKind::Direct && conn.sourceBits->low == 8 && conn.sourceBits->high == 15 &&
                   conn.destBits->low == 0 && conn.destBits->high == 7;
            crossed |= conn.sourceBits->low != 8 || conn.sourceBits->high != 15;
        }
        if (source == "nested_packed_array_top.u_source.o_data" && dest == "nested_packed_array_top.u_bit.i_bit") {
            bit |= conn.kind == ConnectionKind::Direct && conn.sourceBits->low == 43 && conn.sourceBits->high == 43 &&
                   conn.destBits->low == 0 && conn.destBits->high == 0;
            crossed |= conn.sourceBits->low != 43 || conn.sourceBits->high != 43;
        }
        if (source == "nested_packed_array_top.u_source.o_data" && dest == "nested_packed_array_top.u_chain.i_data") {
            chain |= conn.kind == ConnectionKind::Direct && conn.sourceBits->low == 40 && conn.sourceBits->high == 47 &&
                     conn.destBits->low == 0 && conn.destBits->high == 7;
            crossed |= conn.sourceBits->low != 40 || conn.sourceBits->high != 47;
        }
        if (source == "nested_packed_array_top.u_ascending_source.o_data" &&
            dest == "nested_packed_array_top.u_ascending_high.i_data") {
            ascending |= conn.kind == ConnectionKind::Direct && conn.sourceBits->low == 40 &&
                         conn.sourceBits->high == 47 && conn.destBits->low == 0 && conn.destBits->high == 7;
            crossed |= conn.sourceBits->low != 40 || conn.sourceBits->high != 47;
        }
    }
    CHECK(high);
    CHECK(low);
    CHECK(bit);
    CHECK(chain);
    CHECK(ascending);
    CHECK_FALSE(crossed);
    CHECK_FALSE(dynamicExact);
}

TEST_CASE("Extractor: elaborated generated-bank part selects preserve distinct lanes", "[extractor][slice][generate]") {
    auto result = compileFile("sv/generated_indexed_part_select.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "generated_indexed_part_select_top");
    const auto graph = extractor.extract();

    const std::string source = "generated_indexed_part_select_top.u_source.data_o";
    for (int bank = 0; bank < 4; ++bank) {
        const std::string prefix = "generated_indexed_part_select_top.gen_banks[" + std::to_string(bank) + "].";
        for (const auto& sink : {"u_sink", "u_down"}) {
            const std::string dest = prefix + sink + ".data_i";
            int matching = 0;
            for (const auto& connection : graph.connections) {
                if (connection.source.fullPath() != source || connection.dest.fullPath() != dest)
                    continue;
                REQUIRE(connection.sourceBits);
                REQUIRE(connection.destBits);
                CHECK(connection.kind == ConnectionKind::Direct);
                CHECK(connection.sourceBits->low == bank * 10);
                CHECK(connection.sourceBits->high == bank * 10 + 7);
                CHECK(connection.destBits->low == 0);
                CHECK(connection.destBits->high == 7);
                ++matching;
            }
            CHECK(matching == 1);
        }
        const std::string ascendingDest = prefix + "u_ascending.data_i";
        const bool ascendingLane =
            std::any_of(graph.connections.begin(), graph.connections.end(), [&](const Connection& connection) {
                return connection.source.fullPath() == "generated_indexed_part_select_top.u_ascending_source.data_o" &&
                       connection.dest.fullPath() == ascendingDest && connection.kind == ConnectionKind::Direct &&
                       connection.sourceBits && connection.destBits && connection.sourceBits->low == 24 - bank * 8 &&
                       connection.sourceBits->high == 31 - bank * 8 && connection.destBits->low == 0 &&
                       connection.destBits->high == 7;
            });
        CHECK(ascendingLane);
    }
    CHECK(graph.bitFlowGapReasons.count("unresolved_destination_range") == 0);
    CHECK(std::any_of(graph.connections.begin(), graph.connections.end(), [&](const Connection& connection) {
        return connection.source.fullPath() == source &&
               connection.dest.fullPath() == "generated_indexed_part_select_top.u_dynamic.data_i" &&
               connection.kind == ConnectionKind::Approximate && !connection.sourceBits && !connection.destBits;
    }));
    CHECK_FALSE(std::any_of(graph.connections.begin(), graph.connections.end(), [&](const Connection& connection) {
        return connection.source.fullPath() == source &&
               connection.dest.fullPath() == "generated_indexed_part_select_top.u_dynamic.data_i" &&
               connection.kind == ConnectionKind::Direct && connection.sourceBits;
    }));
}

TEST_CASE("Extractor: implicit width conversions preserve low bits and signed extension",
          "[extractor][slice][conversion]") {
    auto result = compileFile("sv/width_conversion_flow.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "width_conversion_flow_top");
    const auto graph = extractor.extract();

    auto hasRange = [&](const std::string& source, const std::string& dest, int64_t sourceLow, int64_t sourceHigh,
                        int64_t destLow, int64_t destHigh) {
        return std::any_of(graph.connections.begin(), graph.connections.end(), [&](const Connection& conn) {
            return conn.source.fullPath() == source && conn.dest.fullPath() == dest &&
                   conn.kind == ConnectionKind::Direct && conn.sourceBits && conn.destBits &&
                   conn.sourceBits->low == sourceLow && conn.sourceBits->high == sourceHigh &&
                   conn.destBits->low == destLow && conn.destBits->high == destHigh;
        });
    };
    const std::string top = "width_conversion_flow_top.";
    CHECK(hasRange(top + "u_unsigned.o_data", top + "u_zero.i_data", 0, 3, 0, 3));
    CHECK(hasRange(top + "u_signed.o_data", top + "u_sign.i_data", 0, 3, 0, 3));
    CHECK(hasRange(top + "u_ascending_signed.o_data", top + "u_ascending_sign.i_data", 0, 3, 0, 3));
    CHECK(hasRange(top + "u_signed.o_data", top + "u_signed_slice.i_data", 0, 3, 0, 3));
    CHECK(hasRange(top + "u_wide.o_data", top + "u_truncated.i_data", 0, 3, 0, 3));
    for (int64_t bit = 4; bit < 8; ++bit)
        CHECK(hasRange(top + "u_signed.o_data", top + "u_sign.i_data", 3, 3, bit, bit));
    for (int64_t bit = 4; bit < 8; ++bit)
        CHECK(hasRange(top + "u_ascending_signed.o_data", top + "u_ascending_sign.i_data", 3, 3, bit, bit));
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() == top + "u_signed.o_data" &&
            conn.dest.fullPath() == top + "u_signed_slice.i_data" && conn.destBits)
            CHECK(conn.destBits->high <= 3);
        if (conn.source.fullPath() == top + "u_signed.o_data" && conn.dest.fullPath() == top + "u_sign.i_data" &&
            conn.kind == ConnectionKind::Direct)
            REQUIRE(conn.sourceBits);
        if (conn.source.fullPath() == top + "u_wide.o_data" && conn.dest.fullPath() == top + "u_truncated.i_data" &&
            conn.kind == ConnectionKind::Direct) {
            REQUIRE(conn.sourceBits);
            CHECK(conn.sourceBits->high <= 3);
        }
    }
}

TEST_CASE("Extractor: explicit size casts map simple integral widths", "[extractor][slice][conversion]") {
    auto result = compileFile("sv/width_conversion_flow.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "width_conversion_flow_top");
    const auto graph = extractor.extract();
    const std::string top = "width_conversion_flow_top.";
    auto hasRange = [&](const std::string& source, const std::string& dest, int64_t sourceLow, int64_t sourceHigh,
                        int64_t destLow, int64_t destHigh) {
        return std::any_of(graph.connections.begin(), graph.connections.end(), [&](const Connection& conn) {
            return conn.source.fullPath() == source && conn.dest.fullPath() == dest &&
                   conn.kind == ConnectionKind::Direct && conn.sourceBits && conn.destBits &&
                   conn.sourceBits->low == sourceLow && conn.sourceBits->high == sourceHigh &&
                   conn.destBits->low == destLow && conn.destBits->high == destHigh;
        });
    };
    CHECK(hasRange(top + "u_unsigned.o_data", top + "u_cast.i_data", 0, 3, 0, 3));
    CHECK(hasRange(top + "u_signed.o_data", top + "u_cast_signed.i_data", 0, 3, 0, 3));
    for (int64_t bit = 4; bit < 8; ++bit)
        CHECK(hasRange(top + "u_signed.o_data", top + "u_cast_signed.i_data", 3, 3, bit, bit));
    CHECK(hasRange(top + "u_wide.o_data", top + "u_cast_truncated.i_data", 0, 3, 0, 3));
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() == top + "u_unsigned.o_data" && conn.dest.fullPath() == top + "u_cast.i_data" &&
            conn.kind == ConnectionKind::Direct) {
            REQUIRE(conn.destBits);
            CHECK(conn.destBits->high <= 3);
        }
        if (conn.source.fullPath() == top + "u_signed.o_data" && conn.dest.fullPath() == top + "u_cast_signed.i_data" &&
            conn.kind == ConnectionKind::Direct)
            REQUIRE(conn.sourceBits);
        if (conn.source.fullPath() == top + "u_wide.o_data" &&
            conn.dest.fullPath() == top + "u_cast_truncated.i_data" && conn.kind == ConnectionKind::Direct) {
            REQUIRE(conn.sourceBits);
            CHECK(conn.sourceBits->high <= 3);
        }
    }
    CHECK(graph.bitFlowGapReasons.find("width_changing_conversion") == graph.bitFlowGapReasons.end());
}

TEST_CASE("Extractor: explicit size casts map concatenated source lanes", "[extractor][slice][conversion]") {
    auto result = compileFile("sv/width_conversion_flow.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "width_conversion_flow_top");
    const auto graph = extractor.extract();
    const std::string top = "width_conversion_flow_top.";
    auto hasRange = [&](const std::string& source, const std::string& dest, int64_t sourceLow, int64_t sourceHigh,
                        int64_t destLow, int64_t destHigh) {
        return std::any_of(graph.connections.begin(), graph.connections.end(), [&](const Connection& conn) {
            return conn.source.fullPath() == source && conn.dest.fullPath() == dest &&
                   conn.kind == ConnectionKind::Direct && conn.sourceBits && conn.destBits &&
                   conn.sourceBits->low == sourceLow && conn.sourceBits->high == sourceHigh &&
                   conn.destBits->low == destLow && conn.destBits->high == destHigh;
        });
    };
    CHECK(hasRange(top + "u_wide.o_data", top + "u_cast_concat_truncated.i_data", 0, 3, 0, 3));
    CHECK(hasRange(top + "u_unsigned.o_data", top + "u_cast_concat_truncated.i_data", 0, 1, 4, 5));
    CHECK(hasRange(top + "u_signed.o_data", top + "u_cast_concat_extended.i_data", 0, 3, 0, 3));
    CHECK(hasRange(top + "u_unsigned.o_data", top + "u_cast_concat_extended.i_data", 0, 3, 4, 7));
    for (const auto& conn : graph.connections) {
        if (conn.dest.fullPath() == top + "u_cast_concat_truncated.i_data" &&
            conn.source.fullPath() == top + "u_unsigned.o_data" && conn.sourceBits)
            CHECK(conn.sourceBits->high <= 1);
        if (conn.dest.fullPath() == top + "u_cast_concat_extended.i_data" && conn.destBits)
            CHECK(conn.destBits->high <= 7);
        if ((conn.dest.fullPath() == top + "u_cast_concat_truncated.i_data" ||
             conn.dest.fullPath() == top + "u_cast_concat_extended.i_data") &&
            conn.kind == ConnectionKind::Direct) {
            REQUIRE(conn.sourceBits);
            REQUIRE(conn.destBits);
        }
    }
    CHECK(graph.bitFlowGapReasons.find("width_changing_conversion") == graph.bitFlowGapReasons.end());
}

TEST_CASE("Extractor: sign-changing type cast stays approximate", "[extractor][slice][conversion]") {
    auto result = compileFile("sv/unsupported_width_cast.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "unsupported_width_cast");
    const auto graph = extractor.extract();

    bool approximate = false;
    bool direct = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "unsupported_width_cast.u_source.data_o" ||
            conn.dest.fullPath() != "unsupported_width_cast.u_sink.data_i")
            continue;
        approximate |= conn.kind == ConnectionKind::Approximate;
        direct |= conn.kind == ConnectionKind::Direct;
    }
    CHECK(approximate);
    CHECK_FALSE(direct);
    CHECK(graph.bitFlowGapReasons.at("state_changing_conversion") >= 1);
}

TEST_CASE("Extractor: state-changing type cast is not a direct bit alias", "[extractor][slice][conversion]") {
    auto result = compileFile("sv/state_changing_cast.sv");
    REQUIRE(result);
    ConnectionExtractor extractor(*result.compilation, "state_changing_cast");
    const auto graph = extractor.extract();

    bool approximate = false;
    bool direct = false;
    for (const auto& conn : graph.connections) {
        if (conn.source.fullPath() != "state_changing_cast.u_source.data_o" ||
            conn.dest.fullPath() != "state_changing_cast.u_sink.data_i")
            continue;
        approximate |= conn.kind == ConnectionKind::Approximate;
        direct |= conn.kind == ConnectionKind::Direct;
    }
    CHECK(approximate);
    CHECK_FALSE(direct);
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
