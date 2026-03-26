add_executable(slang-connect-tests
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
    test_integration_advanced.cpp
    ${CMAKE_SOURCE_DIR}/src/ConnectionExtractor.cpp
    ${CMAKE_SOURCE_DIR}/src/WidthChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/TypeChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/DanglingChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/UndrivenChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/ProtocolChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/ConventionChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/CheckerRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/WaiverFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/JsonReport.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownReport.cpp
    ${CMAKE_SOURCE_DIR}/src/CsvReport.cpp
    ${CMAKE_SOURCE_DIR}/src/TableReport.cpp
)
target_include_directories(slang-connect-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(slang-connect-tests PRIVATE Catch2::Catch2WithMain slang::slang fmt::fmt yaml-cpp::yaml-cpp)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test_waivers.yaml DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/waiver_integration.yaml DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/sv DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
include(CTest)
include(Catch)
catch_discover_tests(slang-connect-tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
