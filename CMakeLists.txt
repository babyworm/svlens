cmake_minimum_required(VERSION 3.20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(slang-connect LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(slang REQUIRED)
find_package(fmt REQUIRED)

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
add_library(slang-connect-lib OBJECT
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
)
target_include_directories(slang-connect-lib PUBLIC src)
target_link_libraries(slang-connect-lib PUBLIC slang::slang fmt::fmt yaml-cpp::yaml-cpp)

add_executable(slang-connect src/main.cpp)
target_link_libraries(slang-connect PRIVATE slang-connect-lib)

install(TARGETS slang-connect RUNTIME DESTINATION bin)

enable_testing()
add_subdirectory(tests)
