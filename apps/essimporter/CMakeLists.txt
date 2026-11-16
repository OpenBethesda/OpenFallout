set(ESSIMPORTER_FILES
    main.cpp
    importer.cpp
    importplayer.cpp
    importnpcc.cpp
    importcrec.cpp
    importcellref.cpp
    importinventory.cpp
    importklst.cpp
    importcntc.cpp
    importgame.cpp
    importinfo.cpp
    importdial.cpp
    importques.cpp
    importjour.cpp
    importscri.cpp
    importscpt.cpp
    importproj.cpp
    importsplm.cpp
    converter.cpp
    convertacdt.cpp
    convertnpcc.cpp
    convertinventory.cpp
    convertcrec.cpp
    convertcntc.cpp
    convertscri.cpp
    convertscpt.cpp
    convertplayer.cpp
)

openfallout_add_executable(openfallout-essimporter
    ${ESSIMPORTER_FILES}
)

target_link_libraries(openfallout-essimporter
    Boost::program_options
    components
)

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(openfallout-essimporter)
endif()
