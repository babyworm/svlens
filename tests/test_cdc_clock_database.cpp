#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/types.h"

using namespace sv_cdccheck;

TEST_CASE("CDC ClockDatabase: addSource keeps pointers stable", "[cdc][clock_db]") {
    ClockDatabase db;

    auto src1 = std::make_unique<ClockSource>();
    src1->name = "sys_clk";
    auto* ptr1 = db.addSource(std::move(src1));

    auto src2 = std::make_unique<ClockSource>();
    src2->name = "ext_clk";
    auto* ptr2 = db.addSource(std::move(src2));

    REQUIRE(ptr1 != nullptr);
    REQUIRE(ptr2 != nullptr);
    CHECK(ptr1->name == "sys_clk");
    CHECK(ptr2->name == "ext_clk");
    CHECK(ptr1 != ptr2);
}

TEST_CASE("CDC ClockDatabase: addNet populates lookup by path", "[cdc][clock_db]") {
    ClockDatabase db;

    auto src = std::make_unique<ClockSource>();
    src->name = "sys_clk";
    auto* srcPtr = db.addSource(std::move(src));

    auto net = std::make_unique<ClockNet>();
    net->hier_path = "top.u_core.sys_clk";
    net->source = srcPtr;
    auto* netPtr = db.addNet(std::move(net));

    REQUIRE(netPtr != nullptr);
    CHECK(db.net_by_path.count("top.u_core.sys_clk") == 1);
    CHECK(db.net_by_path.at("top.u_core.sys_clk") == netPtr);
}

TEST_CASE("CDC ClockDatabase: findOrCreateDomain reuses same source/edge domain", "[cdc][clock_db]") {
    ClockDatabase db;

    auto src = std::make_unique<ClockSource>();
    src->name = "sys_clk";
    auto* srcPtr = db.addSource(std::move(src));

    auto* domA = db.findOrCreateDomain(srcPtr, Edge::Posedge);
    auto* domB = db.findOrCreateDomain(srcPtr, Edge::Posedge);
    auto* domC = db.findOrCreateDomain(srcPtr, Edge::Negedge);

    REQUIRE(domA != nullptr);
    REQUIRE(domB != nullptr);
    REQUIRE(domC != nullptr);
    CHECK(domA == domB);
    CHECK(domA != domC);
    CHECK(domA->isSameDomain(*domB));
    CHECK_FALSE(domA->isSameDomain(*domC));
}

TEST_CASE("CDC ClockDatabase: isAsynchronous honors explicit relationships", "[cdc][clock_db]") {
    ClockDatabase db;

    auto sys = std::make_unique<ClockSource>();
    sys->name = "sys_clk";
    auto* sysPtr = db.addSource(std::move(sys));

    auto ext = std::make_unique<ClockSource>();
    ext->name = "ext_clk";
    auto* extPtr = db.addSource(std::move(ext));

    auto* sysDom = db.findOrCreateDomain(sysPtr, Edge::Posedge);
    auto* extDom = db.findOrCreateDomain(extPtr, Edge::Posedge);

    CHECK(db.isAsynchronous(sysDom, extDom));
    db.relationships.push_back({sysPtr, extPtr, DomainRelationship::Type::SameSource});
    CHECK_FALSE(db.isAsynchronous(sysDom, extDom));
}

TEST_CASE("isAsynchronous: PhysicallyExclusive clocks are NOT async (never coexist per SDC)", "[cdc][clock_db]") {
    ClockDatabase db;
    auto s1 = std::make_unique<ClockSource>();
    s1->id = "mux_clk_a"; s1->name = "mux_clk_a";
    auto* p1 = db.addSource(std::move(s1));

    auto s2 = std::make_unique<ClockSource>();
    s2->id = "mux_clk_b"; s2->name = "mux_clk_b";
    auto* p2 = db.addSource(std::move(s2));

    db.relationships.push_back(
        {p1, p2, DomainRelationship::Type::PhysicallyExclusive});

    auto* d1 = db.findOrCreateDomain(p1, Edge::Posedge);
    auto* d2 = db.findOrCreateDomain(p2, Edge::Posedge);

    CHECK_FALSE(db.isAsynchronous(d1, d2));
}

TEST_CASE("isAsynchronous: LogicallyExclusive clocks are NOT async (never coexist per SDC)", "[cdc][clock_db]") {
    ClockDatabase db;
    auto s1 = std::make_unique<ClockSource>();
    s1->id = "le_a"; s1->name = "le_a";
    auto* p1 = db.addSource(std::move(s1));

    auto s2 = std::make_unique<ClockSource>();
    s2->id = "le_b"; s2->name = "le_b";
    auto* p2 = db.addSource(std::move(s2));

    db.relationships.push_back(
        {p1, p2, DomainRelationship::Type::LogicallyExclusive});

    auto* d1 = db.findOrCreateDomain(p1, Edge::Posedge);
    auto* d2 = db.findOrCreateDomain(p2, Edge::Posedge);

    CHECK_FALSE(db.isAsynchronous(d1, d2));
}

TEST_CASE("isAsynchronous: Divided clocks are NOT async", "[cdc][clock_db]") {
    ClockDatabase db;
    auto s1 = std::make_unique<ClockSource>();
    s1->id = "pll"; s1->name = "pll";
    auto* p1 = db.addSource(std::move(s1));

    auto s2 = std::make_unique<ClockSource>();
    s2->id = "pll_div2"; s2->name = "pll_div2";
    auto* p2 = db.addSource(std::move(s2));

    db.relationships.push_back(
        {p1, p2, DomainRelationship::Type::Divided});

    auto* d1 = db.findOrCreateDomain(p1, Edge::Posedge);
    auto* d2 = db.findOrCreateDomain(p2, Edge::Posedge);

    CHECK_FALSE(db.isAsynchronous(d1, d2));
}
