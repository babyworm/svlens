#include <catch2/catch_test_macros.hpp>
#include "TestHelpersCdc.h"
#include "sv-cdccheck/clock_tree.h"
#include "sv-cdccheck/ff_classifier.h"

using namespace sv_cdccheck;

TEST_CASE("CDC FFClassifier: detects FFs in two-clock design", "[cdc][ff]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module two_clk(input logic clk_a, clk_b, rst_n, d);
            logic q_a, q_b;
            always_ff @(posedge clk_a or negedge rst_n) begin
                if (!rst_n) q_a <= 1'b0; else q_a <= d;
            end
            always_ff @(posedge clk_b or negedge rst_n) begin
                if (!rst_n) q_b <= 1'b0; else q_b <= q_a;
            end
        endmodule
    )", "cdc_ff");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();

    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    const auto& ffs = classifier.getFFNodes();
    REQUIRE(ffs.size() >= 2);
    bool foundDifferent = false;
    for (size_t i = 0; i < ffs.size(); ++i) {
        for (size_t j = i + 1; j < ffs.size(); ++j) {
            if (ffs[i]->domain != ffs[j]->domain)
                foundDifferent = true;
        }
    }
    CHECK(foundDifferent);
}

TEST_CASE("CDC FFClassifier: always_comb does not create FFs", "[cdc][ff]") {
    auto compiled = testutils::cdc::compileInlineSV(R"(
        module no_ff(input logic a, b, output logic y);
            always_comb begin
                y = a & b;
            end
        endmodule
    )", "cdc_no_ff");
    REQUIRE(compiled);

    ClockDatabase db;
    ClockTreeAnalyzer clockAnalyzer(*compiled.compilation, db);
    clockAnalyzer.analyze();

    FFClassifier classifier(*compiled.compilation, db);
    classifier.analyze();

    CHECK(classifier.getFFNodes().empty());
}
