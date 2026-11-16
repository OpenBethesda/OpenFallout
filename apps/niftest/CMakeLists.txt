set(NIFTEST
    niftest.cpp
)
source_group(components\\nif\\tests FILES ${NIFTEST})

# Main executable
openfallout_add_executable(niftest
    ${NIFTEST}
)

target_link_libraries(niftest
    components
)

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(niftest)
endif()
