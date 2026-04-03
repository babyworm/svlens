#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/types.h"

using namespace sv_cdccheck;

TEST_CASE("ClockDatabase: addSource and pointer stability", "[clock_db]") {
    ClockDatabase db;

    auto src1 = std::make_unique<ClockSource>();
    src1->id = "pll0_sys";
    src1->name = "sys_clk";
    src1->type = ClockSource::Type::Primary;
    src1->origin_signal = "sys_clk";

    auto* ptr1 = db.addSource(std::move(src1));
    REQUIRE(ptr1 != nullptr);
    CHECK(ptr1->name == "sys_clk");

    // Add second source — first pointer must remain valid
    auto src2 = std::make_unique<ClockSource>();
    src2->id = "ext_osc";
    src2->name = "ext_clk";
    auto* ptr2 = db.addSource(std::move(src2));

    CHECK(ptr1->name == "sys_clk"); // stability
    CHECK(ptr2->name == "ext_clk");
    CHECK(ptr1 != ptr2);
}

TEST_CASE("ClockDatabase: addNet and lookup by path", "[clock_db]") {
    ClockDatabase db;

    auto src = std::make_unique<ClockSource>();
    src->name = "sys_clk";
    auto* src_ptr = db.addSource(std::move(src));

    auto net = std::make_unique<ClockNet>();
    net->hier_path = "top.u_subsys.sys_clk";
    net->source = src_ptr;
    net->edge = Edge::Posedge;
    auto* net_ptr = db.addNet(std::move(net));

    CHECK(net_ptr->hier_path == "top.u_subsys.sys_clk");
    CHECK(net_ptr->source == src_ptr);

    // Lookup
    CHECK(db.net_by_path.count("top.u_subsys.sys_clk") == 1);
    CHECK(db.net_by_path["top.u_subsys.sys_clk"] == net_ptr);
    CHECK(db.net_by_path.count("nonexistent") == 0);
}

TEST_CASE("ClockDatabase: findOrCreateDomain", "[clock_db]") {
    ClockDatabase db;

    auto src = std::make_unique<ClockSource>();
    src->name = "sys_clk";
    auto* src_ptr = db.addSource(std::move(src));

    // First call creates
    auto* dom1 = db.findOrCreateDomain(src_ptr, Edge::Posedge);
    REQUIRE(dom1 != nullptr);
    CHECK(dom1->canonical_name == "sys_clk");
    CHECK(dom1->source == src_ptr);
    CHECK(dom1->edge == Edge::Posedge);

    // Second call with same args returns same domain
    auto* dom2 = db.findOrCreateDomain(src_ptr, Edge::Posedge);
    CHECK(dom1 == dom2);

    // Different edge = different domain
    auto* dom3 = db.findOrCreateDomain(src_ptr, Edge::Negedge);
    CHECK(dom3 != dom1);
    CHECK(dom3->edge == Edge::Negedge);
}

TEST_CASE("ClockDatabase: isSameDomain via ClockSource pointer equality", "[clock_db]") {
    ClockDatabase db;

    auto src1 = std::make_unique<ClockSource>();
    src1->name = "sys_clk";
    auto* ptr1 = db.addSource(std::move(src1));

    auto* dom_a = db.findOrCreateDomain(ptr1, Edge::Posedge);
    auto* dom_b = db.findOrCreateDomain(ptr1, Edge::Posedge);

    // Same source + same edge = same domain
    CHECK(dom_a->isSameDomain(*dom_b));

    // Same source + different edge = not same domain
    auto* dom_c = db.findOrCreateDomain(ptr1, Edge::Negedge);
    CHECK_FALSE(dom_a->isSameDomain(*dom_c));
}

TEST_CASE("ClockDatabase: isAsynchronous with explicit relationships", "[clock_db]") {
    ClockDatabase db;

    auto src_sys = std::make_unique<ClockSource>();
    src_sys->name = "sys_clk";
    auto* sys = db.addSource(std::move(src_sys));

    auto src_ext = std::make_unique<ClockSource>();
    src_ext->name = "ext_clk";
    auto* ext = db.addSource(std::move(src_ext));

    auto src_div = std::make_unique<ClockSource>();
    src_div->name = "div_clk";
    src_div->master = sys;
    src_div->divide_by = 2;
    auto* div = db.addSource(std::move(src_div));

    auto* dom_sys = db.findOrCreateDomain(sys, Edge::Posedge);
    auto* dom_ext = db.findOrCreateDomain(ext, Edge::Posedge);
    auto* dom_div = db.findOrCreateDomain(div, Edge::Posedge);

    // No relationship declared yet → assume async
    CHECK(db.isAsynchronous(dom_sys, dom_ext) == true);

    // Same source → not async
    CHECK(db.isAsynchronous(dom_sys, dom_sys) == false);

    // Explicit async relationship
    db.relationships.push_back({sys, ext, DomainRelationship::Type::Asynchronous});
    CHECK(db.isAsynchronous(dom_sys, dom_ext) == true);

    // Explicit divided relationship
    db.relationships.push_back({sys, div, DomainRelationship::Type::Divided});
    CHECK(db.isAsynchronous(dom_sys, dom_div) == false);
}

TEST_CASE("ClockDatabase: hierarchical nets share domain via source pointer", "[clock_db]") {
    ClockDatabase db;

    // One physical clock source
    auto src = std::make_unique<ClockSource>();
    src->name = "sys_clk";
    auto* src_ptr = db.addSource(std::move(src));

    // Multiple nets at different hierarchy levels — all same source
    auto net1 = std::make_unique<ClockNet>();
    net1->hier_path = "top.core_clk";
    net1->source = src_ptr;
    db.addNet(std::move(net1));

    auto net2 = std::make_unique<ClockNet>();
    net2->hier_path = "top.u_subsys.sys_clk";
    net2->source = src_ptr;
    db.addNet(std::move(net2));

    auto net3 = std::make_unique<ClockNet>();
    net3->hier_path = "top.u_subsys.u_core.proc_clk";
    net3->source = src_ptr;
    db.addNet(std::move(net3));

    // All three share the same ClockSource* → same domain
    CHECK(db.net_by_path["top.core_clk"]->source ==
          db.net_by_path["top.u_subsys.sys_clk"]->source);
    CHECK(db.net_by_path["top.u_subsys.sys_clk"]->source ==
          db.net_by_path["top.u_subsys.u_core.proc_clk"]->source);
}
