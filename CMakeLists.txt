cmake_minimum_required(VERSION 3.20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(sv-conncheck LANGUAGES CXX)

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

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# Library target (shared between main executable and tests)
add_library(sv-conncheck-lib OBJECT
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
)
target_include_directories(sv-conncheck-lib PUBLIC src)
target_link_libraries(sv-conncheck-lib PUBLIC slang::slang ${FMT_TARGET} yaml-cpp::yaml-cpp)

add_executable(sv-conncheck src/main.cpp)
target_link_libraries(sv-conncheck PRIVATE sv-conncheck-lib)

install(TARGETS sv-conncheck RUNTIME DESTINATION bin)

enable_testing()
add_subdirectory(tests)
