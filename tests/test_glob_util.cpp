#include <catch2/catch_test_macros.hpp>
#include "GlobUtil.h"

using connect::globMatch;

TEST_CASE("globMatch: exact match") {
    CHECK(globMatch("abc", "abc"));
    CHECK_FALSE(globMatch("abc", "abd"));
    CHECK_FALSE(globMatch("abc", "ab"));
    CHECK_FALSE(globMatch("abc", "abcd"));
}

TEST_CASE("globMatch: star wildcard") {
    CHECK(globMatch("*", ""));
    CHECK(globMatch("*", "anything"));
    CHECK(globMatch("a*", "abc"));
    CHECK(globMatch("*c", "abc"));
    CHECK(globMatch("a*c", "abc"));
    CHECK(globMatch("a*c", "axyzc"));
    CHECK_FALSE(globMatch("a*c", "axyzd"));
}

TEST_CASE("globMatch: question mark wildcard") {
    CHECK(globMatch("a?c", "abc"));
    CHECK_FALSE(globMatch("a?c", "ac"));
    CHECK_FALSE(globMatch("a?c", "abbc"));
    CHECK_FALSE(globMatch("?", ""));
}

TEST_CASE("globMatch: multiple stars") {
    CHECK(globMatch("a*b*c", "axbxc"));
    CHECK(globMatch("a*b*c", "abc"));
    CHECK(globMatch("**", "abc"));
    CHECK(globMatch("*.*", "a.b"));
    CHECK(globMatch("*.*.*", "a.b.c"));
    CHECK_FALSE(globMatch("*.*.*", "a.b"));
}

TEST_CASE("globMatch: empty strings") {
    CHECK(globMatch("", ""));
    CHECK_FALSE(globMatch("", "a"));
    CHECK(globMatch("*", ""));
}

TEST_CASE("globMatch: hierarchical paths") {
    CHECK(globMatch("*.u_cpu.o_addr", "top.u_cpu.o_addr"));
    CHECK(globMatch("top.*.sig_*", "top.u_a.sig_data"));
    CHECK_FALSE(globMatch("top.*.sig_*", "top.u_a.other"));
}
