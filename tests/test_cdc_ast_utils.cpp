// Direct unit tests for sv_cdccheck::splitAssignmentByLHS — the
// shared helper extracted in Round 7 to cover the ZipCPU concat-LHS
// 2-FF sync idiom. These tests are TDD-style locks: the previous
// regression coverage was end-to-end (golden fixtures + ZipCPU
// afifo upstream); this suite asserts the (lhs_name, rhs_signals)
// emission contract directly so future rule refinements can be
// done red-first.
//
// Each test compiles a tiny module containing exactly one
// always_ff with one nonblocking assignment, walks the AST to grab
// the AssignmentExpression, then asks splitAssignmentByLHS to
// split it and accumulates the emissions in a vector.

#include <catch2/catch_test_macros.hpp>

#include "TestHelpersCdc.h"
#include "sv-cdccheck/ast_utils.h"

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/statements/MiscStatements.h"
#include "slang/ast/statements/ConditionalStatements.h"

#include <string>
#include <vector>

namespace {

using sv_cdccheck::splitAssignmentByLHS;

struct Emit {
    std::string lhs;
    std::vector<std::string> rhs;
};

// Walks a Statement tree and pushes every AssignmentExpression into out.
void collectAssignmentExprs(
    const slang::ast::Statement& stmt,
    std::vector<const slang::ast::AssignmentExpression*>& out)
{
    using namespace slang::ast;
    switch (stmt.kind) {
        case StatementKind::ExpressionStatement: {
            auto& es = stmt.as<ExpressionStatement>();
            if (es.expr.kind == ExpressionKind::Assignment)
                out.push_back(&es.expr.as<AssignmentExpression>());
            break;
        }
        case StatementKind::Timed:
            collectAssignmentExprs(stmt.as<TimedStatement>().stmt, out);
            break;
        case StatementKind::Block:
            collectAssignmentExprs(stmt.as<BlockStatement>().body, out);
            break;
        case StatementKind::List:
            for (auto* c : stmt.as<StatementList>().list)
                if (c) collectAssignmentExprs(*c, out);
            break;
        case StatementKind::Conditional: {
            auto& c = stmt.as<ConditionalStatement>();
            collectAssignmentExprs(c.ifTrue, out);
            if (c.ifFalse) collectAssignmentExprs(*c.ifFalse, out);
            break;
        }
        default: break;
    }
}

// Compile `src`, find every AssignmentExpression in every always_ff,
// run splitAssignmentByLHS, and return the accumulated emissions.
std::vector<Emit> collectEmissions(const std::string& src,
                                   const std::string& tag) {
    auto compiled = testutils::cdc::compileInlineSV(src, tag);
    REQUIRE(compiled);

    std::vector<const slang::ast::AssignmentExpression*> assigns;
    auto& root = compiled.compilation->getRoot();
    for (auto& member : root.members()) {
        if (member.kind != slang::ast::SymbolKind::Instance) continue;
        auto& inst = member.as<slang::ast::InstanceSymbol>();
        for (auto& body_member : inst.body.members()) {
            if (body_member.kind != slang::ast::SymbolKind::ProceduralBlock)
                continue;
            auto& block = body_member.as<slang::ast::ProceduralBlockSymbol>();
            collectAssignmentExprs(block.getBody(), assigns);
        }
    }
    REQUIRE_FALSE(assigns.empty());

    std::vector<Emit> emissions;
    for (auto* a : assigns) {
        splitAssignmentByLHS(*a,
            [&emissions](std::string lhs, std::vector<std::string> rhs) {
                emissions.push_back({std::move(lhs), std::move(rhs)});
            });
    }
    return emissions;
}

} // namespace

TEST_CASE("splitAssignmentByLHS: NamedValue LHS emits one pair",
          "[cdc][ast_utils][split]") {
    auto emits = collectEmissions(R"(
        module nv (input logic clk, input logic d);
            logic q;
            always_ff @(posedge clk) q <= d;
        endmodule
    )", "split_nv");
    REQUIRE(emits.size() == 1);
    CHECK(emits[0].lhs == "q");
    REQUIRE(emits[0].rhs.size() == 1);
    CHECK(emits[0].rhs[0] == "d");
}

TEST_CASE("splitAssignmentByLHS: paired width-matched concat is positional",
          "[cdc][ast_utils][split]") {
    // {a, b} <= {c, d} -- ZipCPU afifo idiom. Positional matching
    // means a sees only c, b sees only d. NO false a<-d edge.
    auto emits = collectEmissions(R"(
        module pos (input logic clk, input logic c, d);
            logic a, b;
            always_ff @(posedge clk) {a, b} <= {c, d};
        endmodule
    )", "split_pos");
    REQUIRE(emits.size() == 2);
    // The slang Concatenation walks operands MSB-first, so the
    // emission order matches the source order: a (first), b (second).
    CHECK(emits[0].lhs == "a");
    REQUIRE(emits[0].rhs.size() == 1);
    CHECK(emits[0].rhs[0] == "c");
    CHECK(emits[1].lhs == "b");
    REQUIRE(emits[1].rhs.size() == 1);
    CHECK(emits[1].rhs[0] == "d");
}

TEST_CASE("splitAssignmentByLHS: RHS not a concat falls back to broadcast",
          "[cdc][ast_utils][split]") {
    // {a, b} <= {2{e}} — RHS is a single Replication expression,
    // not a positional Concatenation against the LHS. Fall back
    // to broadcasting the full RHS fanin (e) to each LHS operand.
    auto emits = collectEmissions(R"(
        module bcast (input logic clk, input logic e);
            logic a, b;
            always_ff @(posedge clk) {a, b} <= {2{e}};
        endmodule
    )", "split_bcast");
    REQUIRE(emits.size() == 2);
    CHECK(emits[0].lhs == "a");
    REQUIRE(emits[0].rhs.size() == 1);
    CHECK(emits[0].rhs[0] == "e");
    CHECK(emits[1].lhs == "b");
    REQUIRE(emits[1].rhs.size() == 1);
    CHECK(emits[1].rhs[0] == "e");
}

TEST_CASE("splitAssignmentByLHS: arity mismatch falls back to broadcast",
          "[cdc][ast_utils][split]") {
    // {a, b} (2 ops) <= {1'b0, c, d} (3 ops, total 3 bits) —
    // arity mismatch on the operand list. Even though widths
    // could be made to match, the helper requires same arity for
    // the positional path. Falls back to broadcast: a and b each
    // see fanin {c, d} (the literal 1'b0 contributes no name).
    auto emits = collectEmissions(R"(
        module amis (input logic clk, input logic c, d);
            logic a, b;
            always_ff @(posedge clk) {a, b} <= {1'b0, c, d};
        endmodule
    )", "split_amis");
    REQUIRE(emits.size() == 2);
    CHECK(emits[0].lhs == "a");
    CHECK(emits[1].lhs == "b");
    // Both operands receive the broadcast fanin -- order of names
    // mirrors the order they appear in the RHS expression.
    REQUIRE(emits[0].rhs.size() == 2);
    REQUIRE(emits[1].rhs.size() == 2);
    CHECK(emits[0].rhs == std::vector<std::string>{"c", "d"});
    CHECK(emits[1].rhs == std::vector<std::string>{"c", "d"});
}

TEST_CASE("splitAssignmentByLHS: width mismatch falls back to broadcast",
          "[cdc][ast_utils][split]") {
    // {a (1-bit), b (1-bit)} <= {c (2-bit), d (1-bit) -- truncated} —
    // arity 2 vs 2 but per-position widths don't all match. Falls
    // back to broadcast.
    auto emits = collectEmissions(R"(
        module wmis (input logic clk, input logic [1:0] c, input logic d);
            logic a, b;
            always_ff @(posedge clk) {a, b} <= {c, d};
        endmodule
    )", "split_wmis");
    REQUIRE(emits.size() == 2);
    CHECK(emits[0].lhs == "a");
    CHECK(emits[1].lhs == "b");
    REQUIRE(emits[0].rhs.size() == 2);
    CHECK(emits[0].rhs == std::vector<std::string>{"c", "d"});
    REQUIRE(emits[1].rhs.size() == 2);
    CHECK(emits[1].rhs == std::vector<std::string>{"c", "d"});
}

TEST_CASE("splitAssignmentByLHS: single-element concat is positional",
          "[cdc][ast_utils][split]") {
    // {a} <= {d} — degenerate but should still go through the
    // positional path: arity 1 vs 1, widths match, emit one pair.
    auto emits = collectEmissions(R"(
        module se (input logic clk, input logic d);
            logic a;
            always_ff @(posedge clk) {a} <= {d};
        endmodule
    )", "split_se");
    REQUIRE(emits.size() == 1);
    CHECK(emits[0].lhs == "a");
    REQUIRE(emits[0].rhs.size() == 1);
    CHECK(emits[0].rhs[0] == "d");
}

TEST_CASE("splitAssignmentByLHS: bit-select LHS resolves to base register",
          "[cdc][ast_utils][split][bitsel]") {
    // {a, b[0]} <= {c, d} — b[0] is an ElementSelect over the
    // multi-bit register `b`. The CDC connectivity tracker should
    // record that `b` (the whole register) receives `d` on bit 0,
    // i.e., emit (a, [c]) and (b, [d]). The over-conservative
    // option of dropping bit-select LHS misses real CDC crossings
    // when a multi-bit register has any bit fed from a different
    // domain.
    auto emits = collectEmissions(R"(
        module bsel (input logic clk, input logic c, d);
            logic a;
            logic [1:0] b;
            always_ff @(posedge clk) {a, b[0]} <= {c, d};
        endmodule
    )", "split_bsel");
    REQUIRE(emits.size() == 2);
    CHECK(emits[0].lhs == "a");
    REQUIRE(emits[0].rhs.size() == 1);
    CHECK(emits[0].rhs[0] == "c");
    CHECK(emits[1].lhs == "b");
    REQUIRE(emits[1].rhs.size() == 1);
    CHECK(emits[1].rhs[0] == "d");
}

TEST_CASE("splitAssignmentByLHS: 5-level deep nested concat works without crashing",
          "[cdc][ast_utils][split][depth_guard]") {
    // Round 14 US-D01: lock the recursive depth-guard behavior in
    // splitConcatPair. A 5-level paired concat is well within the
    // kMaxConcatDepth=64 bound, so it must split positionally and
    // emit ALL leaves. This test exercises the deep-recursion path
    // without requiring 64-deep input (which would be impractical
    // to construct).
    auto emits = collectEmissions(R"(
        module deep5 (input logic clk, input logic a0, b0, c0, d0,
                                                  e0, f0, g0, h0,
                                                  a1, b1, c1, d1,
                                                  e1, f1, g1, h1);
            logic la, lb, lc, ld, le, lf, lg, lh;
            // Five concat brace levels: outermost wraps two 4-level
            // subtrees; each subtree wraps two 3-level subtrees; etc.
            always_ff @(posedge clk)
                {{{{{la,lb},{lc,ld}}, {{le,lf},{lg,lh}}}}} <=
                {{{{{a0,b0},{c0,d0}}, {{e0,f0},{g0,h0}}}}};
        endmodule
    )", "split_deep5");
    REQUIRE(emits.size() == 8);
    CHECK(emits[0].lhs == "la"); CHECK(emits[0].rhs[0] == "a0");
    CHECK(emits[3].lhs == "ld"); CHECK(emits[3].rhs[0] == "d0");
    CHECK(emits[7].lhs == "lh"); CHECK(emits[7].rhs[0] == "h0");
}

TEST_CASE("splitAssignmentByLHS: nested LHS with width-mismatch falls back to broadcast",
          "[cdc][ast_utils][split][nested_broadcast]") {
    // Architect Round 13 recommendation: lock the broadcast
    // fallback path's recursive LHS walk. When inner widths don't
    // match the per-position widths, splitConcatPair falls back to
    // the broadcast branch which uses a local lambda to walk
    // nested concats and emit each leaf with the full RHS fanin.
    auto emits = collectEmissions(R"(
        module nestbcast (input logic clk,
                          input logic [3:0] wide_in);
            logic a, b, c, d;
            // 2-level LHS but RHS is a single signal (NOT a concat),
            // so positional matching fails -> broadcast walks the
            // nested LHS tree and names each leaf.
            always_ff @(posedge clk) {{a, b}, {c, d}} <= wide_in;
        endmodule
    )", "split_nestbcast");
    REQUIRE(emits.size() == 4);
    CHECK(emits[0].lhs == "a");
    CHECK(emits[1].lhs == "b");
    CHECK(emits[2].lhs == "c");
    CHECK(emits[3].lhs == "d");
    // Every leaf gets the full RHS fanin (single signal "wide_in").
    for (const auto& e : emits) {
        REQUIRE(e.rhs.size() == 1);
        CHECK(e.rhs[0] == "wide_in");
    }
}

TEST_CASE("splitAssignmentByLHS: 3-level nested concat LHS recurses fully",
          "[cdc][ast_utils][split][nested3]") {
    // Round 13 US-C01. The Round 12 LOW finding noted that the
    // nested-concat path is hand-inlined for 2 levels only; a
    // 3-level pattern `{{{a,b},{c,d}},{e,f}}` would fall through
    // to broadcast, losing per-position fanin information. After
    // the recursive refactor each leaf gets its matching RHS slice.
    auto emits = collectEmissions(R"(
        module nest3 (input logic clk, input logic x, y, z, w, p, q);
            logic a, b, c, d, e, f;
            always_ff @(posedge clk) {{{a,b},{c,d}},{e,f}} <= {{{x,y},{z,w}},{p,q}};
        endmodule
    )", "split_nest3");
    REQUIRE(emits.size() == 6);
    CHECK(emits[0].lhs == "a"); CHECK(emits[0].rhs[0] == "x");
    CHECK(emits[1].lhs == "b"); CHECK(emits[1].rhs[0] == "y");
    CHECK(emits[2].lhs == "c"); CHECK(emits[2].rhs[0] == "z");
    CHECK(emits[3].lhs == "d"); CHECK(emits[3].rhs[0] == "w");
    CHECK(emits[4].lhs == "e"); CHECK(emits[4].rhs[0] == "p");
    CHECK(emits[5].lhs == "f"); CHECK(emits[5].rhs[0] == "q");
}

TEST_CASE("splitAssignmentByLHS: nested concat LHS recurses into operands",
          "[cdc][ast_utils][split][nested]") {
    // Code-reviewer Round 8 Finding #4 (MEDIUM). Nested concat
    // LHS like `{a, {b, c}} <= {x, {y, z}}` is legal SystemVerilog
    // and appears in shift-register sync chains. Before this fix,
    // extractLHSBaseName returned "" for the inner concatenation
    // operand and the entire `{b, c}` subgroup was silently
    // dropped from connectivity. Result: a CDC crossing through
    // `b` or `c` would be missed without diagnostic.
    auto emits = collectEmissions(R"(
        module nest (input logic clk, input logic x, y, z);
            logic a, b, c;
            always_ff @(posedge clk) {a, {b, c}} <= {x, {y, z}};
        endmodule
    )", "split_nest");
    // Three emissions: (a, [x]), (b, [y]), (c, [z]).
    REQUIRE(emits.size() == 3);
    CHECK(emits[0].lhs == "a");
    REQUIRE(emits[0].rhs.size() == 1);
    CHECK(emits[0].rhs[0] == "x");
    CHECK(emits[1].lhs == "b");
    REQUIRE(emits[1].rhs.size() == 1);
    CHECK(emits[1].rhs[0] == "y");
    CHECK(emits[2].lhs == "c");
    REQUIRE(emits[2].rhs.size() == 1);
    CHECK(emits[2].rhs[0] == "z");
}

TEST_CASE("collectReferencedSignals: parameter references must NOT count as fanin",
          "[cdc][ast_utils][param]") {
    // Parameters are compile-time constants, not runtime signals, so
    // they must not appear in the fanin signal list. The OpenTitan
    // prim_flop pattern `q_o <= ResetValue` was inflating fanin.size()
    // to 2 (incorrectly counting `ResetValue` as a runtime fanin),
    // which broke the relaxed findNextFF (which requires fanin.size()
    // <= 1) and silently disabled 2-FF sync detection for every
    // OpenTitan-style synchronizer that parameterizes the reset.
    auto emits = collectEmissions(R"(
        module pflop #(parameter logic ResetValue = 1'b0)
            (input logic clk, rstn, d_i, output logic q_o);
            always_ff @(posedge clk or negedge rstn)
                if (!rstn) q_o <= ResetValue;  // parameter, not signal
                else       q_o <= d_i;
        endmodule
    )", "param_reset");
    // Two emissions: one for the reset branch (q_o), one for the
    // data branch (q_o). Each must show fanin = ["d_i"] only --
    // ResetValue (parameter) must NOT appear.
    REQUIRE(emits.size() >= 1);
    for (const auto& e : emits) {
        for (const auto& s : e.rhs) {
            CHECK(s != "ResetValue");
        }
    }
    // At least one emission should show the data-path fanin "d_i".
    bool found_d_i = false;
    for (const auto& e : emits)
        for (const auto& s : e.rhs)
            if (s == "d_i") found_d_i = true;
    CHECK(found_d_i);
}

TEST_CASE("splitAssignmentByLHS: range-select LHS resolves to base register",
          "[cdc][ast_utils][split][bitsel]") {
    // {a, b[1:0]} <= {c, d} — b[1:0] is a RangeSelect, similar to
    // ElementSelect. Should emit (b, [d]) for the range-select
    // slice as well.
    auto emits = collectEmissions(R"(
        module rsel (input logic clk, input logic c, input logic [1:0] d);
            logic a;
            logic [3:0] b;
            always_ff @(posedge clk) {a, b[1:0]} <= {c, d};
        endmodule
    )", "split_rsel");
    REQUIRE(emits.size() == 2);
    CHECK(emits[0].lhs == "a");
    CHECK(emits[1].lhs == "b");
}
