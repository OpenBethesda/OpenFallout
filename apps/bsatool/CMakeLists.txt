set(BSATOOL
    bsatool.cpp
)
source_group(apps\\bsatool FILES ${BSATOOL})

# Main executable
openfallout_add_executable(bsatool
    ${BSATOOL}
)

target_link_libraries(bsatool
    Boost::program_options
    components
)

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(bsatool)
endif()
