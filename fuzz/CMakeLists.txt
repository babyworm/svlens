# libFuzzer-based fuzz harnesses for svlens parsers.
# Enable with: cmake -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DSVLENS_ENABLE_FUZZ=ON
#
# Each harness links the relevant parser library plus -fsanitize=fuzzer.
# Run with corpus from corpus/<name>/:
#   ./build-fuzz/fuzz/fuzz_filelist fuzz/corpus/filelist
#
# Each binary supports the standard libFuzzer flags (-runs, -max_total_time, etc).

set(FUZZ_FLAGS "-fsanitize=fuzzer,address,undefined" "-g" "-O1")

function(svlens_add_fuzz_target NAME LINK_LIB)
    add_executable(${NAME} ${NAME}.cpp)
    target_compile_options(${NAME} PRIVATE ${FUZZ_FLAGS})
    target_link_options(${NAME} PRIVATE ${FUZZ_FLAGS})
    target_link_libraries(${NAME} PRIVATE ${LINK_LIB})
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
endfunction()

svlens_add_fuzz_target(fuzz_filelist   svlens-common-lib)
svlens_add_fuzz_target(fuzz_sdc        svlens-cdc-lib)
svlens_add_fuzz_target(fuzz_waiver     svlens-cdc-lib)
svlens_add_fuzz_target(fuzz_clock_yaml svlens-cdc-lib)
