set(NAVMESHTOOL_LIB
    worldspacedata.cpp
    navmesh.cpp
)

source_group(apps\\navmeshtool FILES ${NAVMESHTOOL_LIB} main.cpp)

add_library(openfallout-navmeshtool-lib STATIC ${NAVMESHTOOL_LIB})

openfallout_add_executable(openfallout-navmeshtool main.cpp)

target_link_libraries(openfallout-navmeshtool openfallout-navmeshtool-lib)

target_link_libraries(openfallout-navmeshtool-lib
    Boost::program_options
    components
)

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(openfallout-navmeshtool)
endif()
