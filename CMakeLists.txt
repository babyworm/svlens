cmake_minimum_required(VERSION 3.20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(svlens VERSION 0.3.5 LANGUAGES CXX)

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
        "For offline / preinstalled builds see docs/install.md.\n"
        "See README.md for details.")
endif()

# slang exports `$<$<CONFIG:Debug>:SLANG_DEBUG>` in its INTERFACE_COMPILE_DEFINITIONS,
# which assumes a Debug consumer means slang itself was built with SLANG_DEBUG. Our
# setup-deps.sh always builds slang in Release, so a Debug build of svlens (e.g. the
# coverage CI job) ends up with mismatched class layouts (IntervalMap, SourceLocation,
# CommandLine) and triggers runtime exceptions like "No options defined". Strip the
# generator-expression form here so consumers get a consistent ABI regardless of
# CMAKE_BUILD_TYPE. If a future workflow rebuilds slang in Debug, set
# SVLENS_KEEP_SLANG_DEBUG=ON to opt out of the strip.
option(SVLENS_KEEP_SLANG_DEBUG "Keep slang's Debug-conditional SLANG_DEBUG macro" OFF)
if(NOT SVLENS_KEEP_SLANG_DEBUG)
    get_target_property(_svlens_slang_defs slang::slang INTERFACE_COMPILE_DEFINITIONS)
    if(_svlens_slang_defs)
        # Fresh review R3: regex-based strip of any
        # `$<$<CONFIG:Debug>:...>` generator expression so additional
        # Debug-conditional macros slang adds in the future are also
        # filtered automatically.  The previous literal REMOVE_ITEM
        # only matched SLANG_DEBUG verbatim.
        set(_kept_defs)
        foreach(_def IN LISTS _svlens_slang_defs)
            if(NOT _def MATCHES "^\\$<\\$<CONFIG:Debug>:.+>$")
                list(APPEND _kept_defs "${_def}")
            endif()
        endforeach()
        set_target_properties(slang::slang PROPERTIES
            INTERFACE_COMPILE_DEFINITIONS "${_kept_defs}")
        message(STATUS
            "svlens: stripped slang's $<$<CONFIG:Debug>:...> "
            "generator expressions (slang prebuilt as Release; see "
            "CMakeLists.txt for details). Set SVLENS_KEEP_SLANG_DEBUG=ON "
            "if your slang was built Debug.")
    endif()
endif()
# fmt: prefer slang's bundled copy; fall back to system install
if(NOT TARGET slang::fmt)
    find_package(fmt REQUIRED)
    set(FMT_TARGET fmt::fmt)
else()
    set(FMT_TARGET slang::fmt)
endif()

option(SVLENS_FETCH_DEPS "Allow CMake to fetch missing third-party dependencies" ON)
option(SVLENS_ENABLE_COVERAGE "Enable gcov/llvm-cov coverage instrumentation" OFF)
option(SVLENS_ENABLE_FUZZ "Build libFuzzer-based fuzz harnesses (requires Clang)" OFF)

if(SVLENS_ENABLE_COVERAGE)
    if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        message(FATAL_ERROR "SVLENS_ENABLE_COVERAGE requires GCC or Clang.")
    endif()
    message(STATUS "svlens: coverage instrumentation enabled (--coverage)")
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

if(SVLENS_ENABLE_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "SVLENS_ENABLE_FUZZ requires Clang for libFuzzer support.\n"
            "Re-run cmake with: -DCMAKE_CXX_COMPILER=clang++ -DSVLENS_ENABLE_FUZZ=ON")
    endif()
    message(STATUS "svlens: libFuzzer fuzz harnesses enabled")
endif()

include(FetchContent)

find_package(yaml-cpp QUIET CONFIG)
if(NOT TARGET yaml-cpp::yaml-cpp AND NOT TARGET yaml-cpp)
    find_package(yaml-cpp QUIET)
endif()
if(TARGET yaml-cpp::yaml-cpp)
    set(YAML_CPP_TARGET yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
    set(YAML_CPP_TARGET yaml-cpp)
elseif(SVLENS_FETCH_DEPS)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
    set(YAML_CPP_TARGET yaml-cpp::yaml-cpp)
else()
    message(FATAL_ERROR
        "yaml-cpp not found.\n"
        "Install yaml-cpp and re-run cmake with your dependency prefix, or enable dependency fetching:\n"
        "  cmake -B build -DCMAKE_PREFIX_PATH=\$HOME/.local -DSVLENS_FETCH_DEPS=ON\n"
        "For offline / preinstalled builds use:\n"
        "  cmake -B build-offline -DCMAKE_PREFIX_PATH=\$HOME/.local -DSVLENS_FETCH_DEPS=OFF")
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Catch2 3 QUIET CONFIG)
    if(NOT TARGET Catch2::Catch2WithMain)
        find_package(Catch2 3 QUIET)
    endif()
    if(NOT TARGET Catch2::Catch2WithMain)
        if(SVLENS_FETCH_DEPS)
            FetchContent_Declare(
                Catch2
                GIT_REPOSITORY https://github.com/catchorg/Catch2.git
                GIT_TAG v3.5.2
            )
            FetchContent_MakeAvailable(Catch2)
        else()
            message(FATAL_ERROR
                "Catch2 not found while BUILD_TESTING is enabled.\n"
                "Install Catch2 and re-run cmake with your dependency prefix, or enable dependency fetching:\n"
                "  cmake -B build -DCMAKE_PREFIX_PATH=\$HOME/.local -DSVLENS_FETCH_DEPS=ON\n"
                "For offline / preinstalled test builds use:\n"
                "  cmake -B build-offline -DCMAKE_PREFIX_PATH=\$HOME/.local -DSVLENS_FETCH_DEPS=OFF")
        endif()
    endif()
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
    src/StyleReachability.cpp
    src/StyleSyntaxScanner.cpp
    src/SourceTextScanner.cpp
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
target_link_libraries(svlens-conn-lib PUBLIC svlens-common-lib slang::slang ${FMT_TARGET} ${YAML_CPP_TARGET})

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
target_link_libraries(svlens-cdc-lib PUBLIC svlens-common-lib slang::slang ${YAML_CPP_TARGET})

add_library(svlens-metrics-lib OBJECT
    src/MetricsRunner.cpp
    src/metrics/TransformExtractor.cpp
    src/metrics/ConeAnalyzer.cpp
    src/metrics/Normalization.cpp
    src/metrics/BaselineDiff.cpp
)
target_include_directories(svlens-metrics-lib PUBLIC src include)
target_link_libraries(svlens-metrics-lib PUBLIC svlens-common-lib slang::slang ${FMT_TARGET})

add_executable(svlens src/svlens_main.cpp)
target_link_libraries(svlens PRIVATE svlens-common-lib svlens-conn-lib svlens-cdc-lib svlens-metrics-lib ${FMT_TARGET})

install(TARGETS svlens RUNTIME DESTINATION bin)

target_compile_definitions(svlens PRIVATE SVLENS_VERSION="${PROJECT_VERSION}")
target_compile_definitions(svlens-conn-lib PRIVATE SVLENS_VERSION="${PROJECT_VERSION}")
target_compile_definitions(svlens-cdc-lib PRIVATE SVLENS_VERSION="${PROJECT_VERSION}")
target_compile_definitions(svlens-metrics-lib PRIVATE SVLENS_VERSION="${PROJECT_VERSION}")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(SVLENS_ENABLE_FUZZ)
    add_subdirectory(fuzz)
endif()
