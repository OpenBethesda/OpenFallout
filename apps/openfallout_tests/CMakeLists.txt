include_directories(SYSTEM ${GTEST_INCLUDE_DIRS})
include_directories(SYSTEM ${GMOCK_INCLUDE_DIRS})

file(GLOB UNITTEST_SRC_FILES
    main.cpp

    options.cpp
    shellinterface.cpp

    macos/testsavedwindowstate.cpp

    mwworld/teststore.cpp
    mwworld/testduration.cpp
    mwworld/testtimestamp.cpp
    mwworld/testptr.cpp
    mwworld/testweather.cpp
    mwworld/testpositioncellgrid.cpp
    mwworld/testplaceholderrecords.cpp
    mwworld/testcelllighting.cpp
    mwworld/testcellclimate.cpp
    mwworld/testcellwater.cpp
    mwworld/testworldspacestart.cpp

    mwclass/testesm4npcbody.cpp
    mwclass/testesm4npcparts.cpp


    mwgui/tooltips.cpp
    mwgui/weightedsearch.cpp

    mwinput/testcontrollerdefaults.cpp
    mwinput/testtriggerfeel.cpp
    mwinput/testtriggerdevice.cpp

    mwlua/testobjectcache.cpp

    mwmechanics/testfalloutpackages.cpp

    mwrender/testclipcullnode.cpp
    mwrender/testfalloutanimation.cpp
    mwrender/testfalloutface.cpp
    mwrender/testfogmanager.cpp
    mwrender/testmenuscene.cpp
    mwrender/testskyfallback.cpp
    mwrender/testwaterlook.cpp

    mwscript/testactorvaluetext.cpp
    mwscript/testgameoptions.cpp

    mwsound/testfalloutsound.cpp
    mwsound/testfalloutmusic.cpp
    mwsound/testheadcache.cpp
    mwsound/testheadtracking.cpp
    mwsound/testwarmqueue.cpp
)

source_group(apps\\openfallout-tests FILES ${UNITTEST_SRC_FILES})

openfallout_add_executable(openfallout-tests ${UNITTEST_SRC_FILES})

target_link_libraries(openfallout-tests
    GTest::GTest
    GMock::GMock
    openfallout-lib
)

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(openfallout-tests)
endif()

target_compile_definitions(openfallout-tests
    PRIVATE OPENFALLOUT_DATA_DIR=u8"${CMAKE_CURRENT_BINARY_DIR}/data"
            OPENFALLOUT_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}")
