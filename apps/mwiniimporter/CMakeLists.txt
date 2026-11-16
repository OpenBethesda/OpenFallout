set(MWINIIMPORT
    main.cpp
    importer.cpp
)

set(MWINIIMPORT_HEADER
    importer.hpp
)

source_group(launcher FILES ${MWINIIMPORT} ${MWINIIMPORT_HEADER})

openfallout_add_executable(openfallout-iniimporter
    ${MWINIIMPORT}
)

target_link_libraries(openfallout-iniimporter
    Boost::program_options
    components
)

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(openfallout-iniimporter)
endif()
