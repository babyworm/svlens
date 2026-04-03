add_executable(sv-conncheck-tests
    TestUtils.cpp
    test_width_checker.cpp
    test_type_checker.cpp
    test_dangling_checker.cpp
    test_undriven_checker.cpp
    test_protocol_checker.cpp
    test_convention_checker.cpp
    test_checker_runner.cpp
    test_waiver_filter.cpp
    test_reports.cpp
    test_extractor.cpp
    test_connection_filters.cpp
    test_fixture_resolution.cpp
    test_integration_advanced.cpp
    test_dot_report.cpp
    test_html_report.cpp
    test_graph_diff.cpp
    test_expect_checker.cpp
    test_clock_reset.cpp
    test_interface_grouper.cpp
    test_trace_engine.cpp
    test_analysis_engine.cpp
)
target_link_libraries(sv-conncheck-tests PRIVATE sv-conncheck-lib Catch2::Catch2WithMain)
target_compile_definitions(sv-conncheck-tests PRIVATE TEST_SV_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sv")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test_waivers.yaml DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/waiver_integration.yaml DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/expected_conn.yaml DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/sv DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
include(CTest)
include(Catch)
catch_discover_tests(sv-conncheck-tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME integration_shell
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_integration.sh $<TARGET_FILE:sv-conncheck>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
