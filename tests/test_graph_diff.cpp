#include <catch2/catch_test_macros.hpp>
#include "GraphDiff.h"
#include <cstdio>
#include <fstream>

using namespace connect;

// RAII guard to ensure temp files are cleaned up even on assertion failure
struct FileCleanup {
    std::string path;
    ~FileCleanup() { std::remove(path.c_str()); }
};

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
    FileCleanup guard{"test_diff_input.json"};
    {
        std::ofstream f(guard.path);
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

    auto input = loadDiffInputFromJson(guard.path);
    REQUIRE(input.connections.size() == 2);

    // Width annotations should be stripped
    CHECK(input.connections[0].source == "soc_top.u_cpu.o_data");
    CHECK(input.connections[0].dest == "soc_top.u_bus.i_data");
    CHECK(input.connections[0].status == "WIDTH_MISMATCH");

    CHECK(input.connections[1].source == "soc_top.u_bus.o_mem_addr");
    CHECK(input.connections[1].dest == "soc_top.u_mem.i_addr");
    CHECK(input.connections[1].status == "OK");
}

TEST_CASE("GraphDiff: loadDiffInputFromJson throws on missing file") {
    CHECK_THROWS_AS(loadDiffInputFromJson("nonexistent_file.json"), std::runtime_error);
}

TEST_CASE("GraphDiff: loadDiffInputFromJson handles empty connections") {
    FileCleanup guard{"test_empty_conn.json"};
    {
        std::ofstream f(guard.path);
        f << R"({"version":"1.0","connections":[]})";
    }
    auto input = loadDiffInputFromJson(guard.path);
    CHECK(input.connections.empty());
}

TEST_CASE("GraphDiff: computeDiff handles duplicate connections (last-wins)") {
    DiffInput a, b;
    // Baseline has two entries for the same (source, dest) pair.
    // std::map insert overwrites, so last entry wins: status = "WIDTH_MISMATCH"
    a.connections.push_back({"x.o", "y.i", "OK"});
    a.connections.push_back({"x.o", "y.i", "WIDTH_MISMATCH"});

    // Current has the same pair with status OK
    b.connections.push_back({"x.o", "y.i", "OK"});

    auto diff = computeDiff(a, b);

    // Baseline last-wins is WIDTH_MISMATCH, current is OK -> changed
    CHECK(diff.added.empty());
    CHECK(diff.removed.empty());
    REQUIRE(diff.changed.size() == 1);
    CHECK(diff.changed[0].oldStatus == "WIDTH_MISMATCH");
    CHECK(diff.changed[0].newStatus == "OK");
}

TEST_CASE("GraphDiff: handles large number of connections") {
    DiffInput base, curr;
    for (int i = 0; i < 1000; i++) {
        base.connections.push_back(
            {"a.o_" + std::to_string(i), "b.i_" + std::to_string(i), "OK"});
    }
    curr = base; // identical

    auto diff = computeDiff(base, curr);
    CHECK(diff.empty());

    // Now add one extra connection to current
    curr.connections.push_back({"a.o_new", "b.i_new", "OK"});
    diff = computeDiff(base, curr);
    CHECK(diff.removed.empty());
    CHECK(diff.changed.empty());
    REQUIRE(diff.added.size() == 1);
    CHECK(diff.added[0].source == "a.o_new");
    CHECK(diff.added[0].dest == "b.i_new");
}
