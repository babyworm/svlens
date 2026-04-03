cmake_minimum_required(VERSION 3.20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(svlens VERSION 0.2.3 LANGUAGES CXX)

# C++20 required by slang library headers
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(slang QUIET)
if(NOT slang_FOUND)
    message(FATAL_ERROR
        "slang not found. Install it first:\n"
        "  ./scripts/setup-deps.sh --prefix \$HOME/.local\n"
        "Then re-run cmake with:\n"
        "  cmake -B build -DCMAKE_PREFIX_PATH=\$HOME/.local\n"
        "See README.md for details.")
endif()
# fmt: prefer slang's bundled copy; fall back to system install
if(NOT TARGET slang::fmt)
    find_package(fmt REQUIRED)
    set(FMT_TARGET fmt::fmt)
else()
    set(FMT_TARGET slang::fmt)
endif()

include(FetchContent)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)

include(CTest)
if(BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.2
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# Shared/common frontend library
add_library(svlens-common-lib STATIC
    src/CommonCli.cpp
    src/CompilationSession.cpp
    src/cdc/filelist_parser.cpp
)
target_include_directories(svlens-common-lib PUBLIC src include)
target_link_libraries(svlens-common-lib PUBLIC slang::slang)

# conn analysis library
add_library(svlens-conn-lib OBJECT
    src/ConnRunner.cpp
    src/ConnRunnerUtils.cpp
    src/ConnectionExtractor.cpp
    src/WidthChecker.cpp
    src/TypeChecker.cpp
    src/DanglingChecker.cpp
    src/UndrivenChecker.cpp
    src/ProtocolChecker.cpp
    src/ConventionChecker.cpp
    src/CheckerRunner.cpp
    src/WaiverFilter.cpp
    src/JsonReport.cpp
    src/MarkdownReport.cpp
    src/CsvReport.cpp
    src/TableReport.cpp
    src/DotReport.cpp
    src/HtmlReport.cpp
    src/GraphDiff.cpp
    src/ExpectChecker.cpp
    src/ClockResetAnalyzer.cpp
    src/InterfaceGrouper.cpp
    src/TraceEngine.cpp
    src/AnalysisEngine.cpp
    src/ConnectionFilters.cpp
)
target_include_directories(svlens-conn-lib PUBLIC src include)
target_link_libraries(svlens-conn-lib PUBLIC svlens-common-lib slang::slang ${FMT_TARGET} yaml-cpp::yaml-cpp)

add_library(svlens-cdc-lib OBJECT
    src/CdcRunnerUtils.cpp
    src/cdc/ast_utils.cpp
    src/cdc/clock_tree.cpp
    src/cdc/clock_yaml_parser.cpp
    src/cdc/connectivity.cpp
    src/cdc/crossing_detector.cpp
    src/cdc/ff_classifier.cpp
    src/cdc/report_generator.cpp
    src/cdc/sdc_parser.cpp
    src/cdc/sync_verifier.cpp
    src/cdc/waiver.cpp
    src/CdcRunner.cpp
)
target_include_directories(svlens-cdc-lib PUBLIC src include)
target_link_libraries(svlens-cdc-lib PUBLIC svlens-common-lib slang::slang)

add_executable(svlens src/svlens_main.cpp)
target_link_libraries(svlens PRIVATE svlens-common-lib svlens-conn-lib svlens-cdc-lib ${FMT_TARGET})

install(TARGETS svlens RUNTIME DESTINATION bin)

target_compile_definitions(svlens PRIVATE SVLENS_VERSION="${PROJECT_VERSION}")
target_compile_definitions(svlens-conn-lib PRIVATE SVLENS_VERSION="${PROJECT_VERSION}")
target_compile_definitions(svlens-cdc-lib PRIVATE SVLENS_VERSION="${PROJECT_VERSION}")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
