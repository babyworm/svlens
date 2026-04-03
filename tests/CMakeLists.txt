add_executable(svlens-tests
    TestUtils.cpp
    test_width_checker.cpp
    test_type_checker.cpp
    test_dangling_checker.cpp
    test_undriven_checker.cpp
    test_protocol_checker.cpp
    test_convention_checker.cpp
    test_checker_runner.cpp
    test_waiver_filter.cpp
    test_common_cli.cpp
    test_cli_parsers.cpp
    test_cdc_clock_database.cpp
    test_cdc_clock_tree.cpp
    test_cdc_ff_classifier.cpp
    test_cdc_filelist.cpp
    test_cdc_edge_cases.cpp
    test_cdc_sdc_parser.cpp
    test_cdc_connectivity_crossing.cpp
    test_cdc_sync_verifier.cpp
    test_cdc_waiver.cpp
    test_cdc_clock_yaml.cpp
    test_cdc_report_generator.cpp
    test_conn_runner.cpp
    test_cdc_runner.cpp
    test_clock_database.cpp
    test_clock_tree.cpp
    test_clock_yaml.cpp
    test_connectivity_and_crossing.cpp
    test_corner_cases.cpp
    test_coverage_gaps.cpp
    test_edge_cases.cpp
    test_ff_classifier.cpp
    test_fifo_depth.cpp
    test_filelist.cpp
    test_final_features.cpp
    test_golden_reference.cpp
    test_integration_fixtures.cpp
    test_inter_module_e2e.cpp
    test_issue_fixes.cpp
    test_phase2_hierarchy.cpp
    test_phase3_advanced.cpp
    test_phase4_usability.cpp
    test_remaining_features.cpp
    test_report_generator.cpp
    test_sdc_parser.cpp
    test_spyglass_rules.cpp
    test_sync_patterns.cpp
    test_sync_verifier.cpp
    test_reports.cpp
    test_compilation_session.cpp
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
    test_glob_util.cpp
    test_conn_runner_utils.cpp
)
target_link_libraries(svlens-tests PRIVATE svlens-conn-lib svlens-cdc-lib Catch2::Catch2WithMain)
target_compile_definitions(svlens-tests PRIVATE
    TEST_SV_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sv"
    TEST_CDC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cdc/basic"
    SOURCE_DIR="${CMAKE_SOURCE_DIR}")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test_waivers.yaml DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/waiver_integration.yaml DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/expected_conn.yaml DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/sv DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/basic DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/golden DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
include(CTest)
include(Catch)
catch_discover_tests(svlens-tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME integration_shell
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_integration.sh $<TARGET_FILE:svlens>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME integration_svlens
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_svlens_integration.sh
            $<TARGET_FILE:svlens>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME integration_cdc_golden
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_cdc_golden.sh
            $<TARGET_FILE:svlens>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
