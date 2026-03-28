#include <catch2/catch_test_macros.hpp>
#include "GraphDiff.h"
#include <fstream>

using namespace connect;

static DiffInput makeBaseline() {
    DiffInput input;
    input.connections.push_back({"soc_top.u_cpu.o_data", "soc_top.u_bus.i_data", "OK"});
    input.connections.push_back({"soc_top.u_bus.o_mem_addr", "soc_top.u_mem.i_addr", "OK"});
    return input;
}

static DiffInput makeCurrent() {
    DiffInput input;
    // Changed: now WIDTH_MISMATCH
    input.connections.push_back({"soc_top.u_cpu.o_data", "soc_top.u_bus.i_data", "WIDTH_MISMATCH"});
    // Removed: bus -> mem gone
    // Added: new dma connection
    input.connections.push_back({"soc_top.u_dma.o_addr", "soc_top.u_bus.i_dma_addr", "OK"});
    return input;
}

TEST_CASE("GraphDiff: detects added connections") {
    auto baseline = makeBaseline();
    auto current = makeCurrent();
    auto result = computeDiff(baseline, current);

    REQUIRE(result.added.size() == 1);
    CHECK(result.added[0].source == "soc_top.u_dma.o_addr");
    CHECK(result.added[0].dest == "soc_top.u_bus.i_dma_addr");
    CHECK(result.added[0].status == "OK");
}

TEST_CASE("GraphDiff: detects removed connections") {
    auto baseline = makeBaseline();
    auto current = makeCurrent();
    auto result = computeDiff(baseline, current);

    REQUIRE(result.removed.size() == 1);
    CHECK(result.removed[0].source == "soc_top.u_bus.o_mem_addr");
    CHECK(result.removed[0].dest == "soc_top.u_mem.i_addr");
    CHECK(result.removed[0].status == "OK");
}

TEST_CASE("GraphDiff: detects changed status") {
    auto baseline = makeBaseline();
    auto current = makeCurrent();
    auto result = computeDiff(baseline, current);

    REQUIRE(result.changed.size() == 1);
    CHECK(result.changed[0].source == "soc_top.u_cpu.o_data");
    CHECK(result.changed[0].dest == "soc_top.u_bus.i_data");
    CHECK(result.changed[0].oldStatus == "OK");
    CHECK(result.changed[0].newStatus == "WIDTH_MISMATCH");
}

TEST_CASE("GraphDiff: identical inputs produce empty diff") {
    auto baseline = makeBaseline();
    auto same = makeBaseline();
    auto result = computeDiff(baseline, same);

    CHECK(result.empty());
    CHECK(result.added.empty());
    CHECK(result.removed.empty());
    CHECK(result.changed.empty());
}

TEST_CASE("GraphDiff: loadDiffInputFromJson parses connections") {
    // Write a small JSON file
    const std::string path = "test_diff_input.json";
    {
        std::ofstream f(path);
        f << R"({
  "version": "1.0",
  "top": "soc_top",
  "summary": { "connections_analyzed": 2, "errors": 1, "warnings": 0, "info": 0, "waived": 0 },
  "issues": [],
  "connections": [
    { "source": "soc_top.u_cpu.o_data[31:0]", "dest": "soc_top.u_bus.i_data[15:0]", "status": "WIDTH_MISMATCH" },
    { "source": "soc_top.u_bus.o_mem_addr[31:0]", "dest": "soc_top.u_mem.i_addr[31:0]", "status": "OK" }
  ]
})";
    }

    auto input = loadDiffInputFromJson(path);
    REQUIRE(input.connections.size() == 2);

    // Width annotations should be stripped
    CHECK(input.connections[0].source == "soc_top.u_cpu.o_data");
    CHECK(input.connections[0].dest == "soc_top.u_bus.i_data");
    CHECK(input.connections[0].status == "WIDTH_MISMATCH");

    CHECK(input.connections[1].source == "soc_top.u_bus.o_mem_addr");
    CHECK(input.connections[1].dest == "soc_top.u_mem.i_addr");
    CHECK(input.connections[1].status == "OK");

    // Cleanup
    std::remove(path.c_str());
}

TEST_CASE("GraphDiff: loadDiffInputFromJson throws on missing file") {
    CHECK_THROWS_AS(loadDiffInputFromJson("nonexistent_file.json"), std::runtime_error);
}
