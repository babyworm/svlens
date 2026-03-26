#include <catch2/catch_test_macros.hpp>
#include "WaiverFilter.h"

using namespace connect;
using slang::ast::ArgumentDirection;

static Issue makeIssue(Issue::Type type, const std::string& inst, const std::string& port) {
    Issue issue;
    issue.type = type;
    issue.severity = Issue::Severity::WARN;
    issue.port.instancePath = inst;
    issue.port.portName = port;
    issue.port.direction = ArgumentDirection::Out;
    issue.port.width = 8;
    issue.detail = "test";
    return issue;
}

TEST_CASE("WaiverFilter: no waivers passes all issues through") {
    WaiverFilter filter;
    std::vector<Issue> issues = { makeIssue(Issue::Type::DANGLING_OUTPUT, "top.u_a", "o_debug") };
    auto result = filter.apply(issues);
    CHECK(result.active.size() == 1);
    CHECK(result.waived.empty());
}

TEST_CASE("WaiverFilter: pattern match waives matching issues") {
    WaiverFilter filter("test_waivers.yaml");
    std::vector<Issue> issues = {
        makeIssue(Issue::Type::DANGLING_OUTPUT, "top.u_a", "o_debug"),
        makeIssue(Issue::Type::DANGLING_OUTPUT, "top.u_a", "o_valid")
    };
    auto result = filter.apply(issues);
    CHECK(result.active.size() == 1);
    CHECK(result.active[0].port.portName == "o_valid");
    CHECK(result.waived.size() == 1);
    CHECK(result.waived[0].port.portName == "o_debug");
}

TEST_CASE("WaiverFilter: wildcard type waives all issue types") {
    WaiverFilter filter("test_waivers.yaml");
    std::vector<Issue> issues = {
        makeIssue(Issue::Type::WIDTH_MISMATCH, "top.u_test_block", "o_data"),
        makeIssue(Issue::Type::DANGLING_OUTPUT, "top.u_test_block", "o_debug")
    };
    auto result = filter.apply(issues);
    CHECK(result.active.empty());
    CHECK(result.waived.size() == 2);
}

TEST_CASE("WaiverFilter: type mismatch does not waive") {
    WaiverFilter filter("test_waivers.yaml");
    std::vector<Issue> issues = { makeIssue(Issue::Type::WIDTH_MISMATCH, "top.u_a", "o_debug") };
    auto result = filter.apply(issues);
    CHECK(result.active.size() == 1);
    CHECK(result.waived.empty());
}
