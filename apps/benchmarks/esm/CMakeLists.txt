openfallout_add_executable(openfallout_esm_refid_benchmark benchrefid.cpp)
target_link_libraries(openfallout_esm_refid_benchmark benchmark::benchmark components)

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(openfallout_esm_refid_benchmark)
endif()
