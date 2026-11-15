set(BULLETMESHTOOL
    main.cpp
)
source_group(apps\\bulletobjecttool FILES ${BULLETMESHTOOL})

openfallout_add_executable(openfallout-bulletobjecttool ${BULLETMESHTOOL})

target_link_libraries(openfallout-bulletobjecttool
    Boost::program_options
    components
)

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(openfallout-bulletobjecttool)
endif()
