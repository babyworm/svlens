#include <catch2/catch_test_macros.hpp>
#include "sv-cdccheck/waiver.h"

using namespace sv_cdccheck;

TEST_CASE("CDC WaiverManager: parses exact crossing waiver", "[cdc][waiver]") {
    WaiverManager mgr;
    std::string yaml = R"(
waivers:
  - id: WAIVE-001
    crossing: "top.u_a.sig -> top.u_b.sig"
    reason: "known safe"
    owner: "alice@test.com"
)";

    REQUIRE(mgr.loadString(yaml));
    REQUIRE(mgr.getWaivers().size() == 1);
    CHECK(mgr.getWaivers()[0].id == "WAIVE-001");
    CHECK(mgr.isWaived("top.u_a.sig", "top.u_b.sig"));
    CHECK_FALSE(mgr.isWaived("top.u_a.sig", "top.u_c.sig"));
}

TEST_CASE("CDC WaiverManager: pattern waiver matches either source or dest", "[cdc][waiver]") {
    WaiverManager mgr;
    std::string yaml = R"(
waivers:
  - id: WAIVE-DBG
    pattern: "top.u_debug.*"
    reason: "debug-only"
)";

    REQUIRE(mgr.loadString(yaml));
    CHECK(mgr.isWaived("top.u_debug.foo", "top.u_core.bar"));
    CHECK(mgr.isWaived("top.u_core.bar", "top.u_debug.baz"));
    CHECK_FALSE(mgr.isWaived("top.u_core.a", "top.u_core.b"));
}

TEST_CASE("CDC WaiverManager: missing file returns false", "[cdc][waiver]") {
    WaiverManager mgr;
    CHECK_FALSE(mgr.loadFile("/nonexistent/waiver.yaml"));
}
