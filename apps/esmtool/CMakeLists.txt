set(ESMTOOL
    esmtool.cpp
    labels.hpp
    labels.cpp
    record.hpp
    record.cpp
    arguments.hpp
    tes4.hpp
    tes4.cpp
)
source_group(apps\\esmtool FILES ${ESMTOOL})

# Main executable
openfallout_add_executable(esmtool
    ${ESMTOOL}
)

target_link_libraries(esmtool
    Boost::program_options
    components
)

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(esmtool)
endif()
