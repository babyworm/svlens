add_executable(slang-connect-tests
    test_width_checker.cpp
    test_type_checker.cpp
    test_dangling_checker.cpp
    test_undriven_checker.cpp
    test_checker_runner.cpp
    test_waiver_filter.cpp
    ${CMAKE_SOURCE_DIR}/src/WidthChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/TypeChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/DanglingChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/UndrivenChecker.cpp
    ${CMAKE_SOURCE_DIR}/src/CheckerRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/WaiverFilter.cpp
)
target_include_directories(slang-connect-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(slang-connect-tests PRIVATE Catch2::Catch2WithMain slang::slang fmt::fmt yaml-cpp::yaml-cpp)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test_waivers.yaml DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
include(CTest)
include(Catch)
catch_discover_tests(slang-connect-tests)
