openfallout_add_executable(openfallout_settings_access_benchmark access.cpp)
target_link_libraries(openfallout_settings_access_benchmark benchmark::benchmark components)

target_compile_definitions(openfallout_settings_access_benchmark
    PRIVATE OPENFALLOUT_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}")

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(openfallout_settings_access_benchmark)
endif()
