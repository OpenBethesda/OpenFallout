set(LAUNCHER
    datafilespage.cpp
    graphicspage.cpp
    sdlinit.cpp
    main.cpp
    maindialog.cpp
    textslotmsgbox.cpp
    settingspage.cpp

    utils/cellnameloader.cpp
    utils/profilescombobox.cpp
    utils/textinputdialog.cpp
    utils/lineedit.cpp
    utils/openalutil.cpp
)

set(LAUNCHER_HEADER
    datafilespage.hpp
    graphicspage.hpp
    sdlinit.hpp
    maindialog.hpp
    textslotmsgbox.hpp
    settingspage.hpp

    utils/cellnameloader.hpp
    utils/profilescombobox.hpp
    utils/textinputdialog.hpp
    utils/lineedit.hpp
    utils/openalutil.hpp
)

source_group(launcher FILES ${LAUNCHER} ${LAUNCHER_HEADER})

set(QT_USE_QTGUI 1)

set (LAUNCHER_RES ${CMAKE_SOURCE_DIR}/files/launcher/launcher.qrc)

QT_ADD_RESOURCES(RCC_SRCS ${LAUNCHER_RES})

# Main executable
openfallout_add_executable(openfallout-launcher
    ${LAUNCHER}
    ${LAUNCHER_HEADER}
    ${RCC_SRCS}
)

add_dependencies(openfallout-launcher qm-files)

target_link_libraries(openfallout-launcher
    SDL2::SDL2
    ${OPENAL_LIBRARY}
    components_qt
)

target_link_libraries(openfallout-launcher Qt::Widgets Qt::Core Qt::Svg)

if (BUILD_WITH_CODE_COVERAGE)
    openfallout_enable_coverage(openfallout-launcher)
endif()

if(USE_QT)
    set_property(TARGET openfallout-launcher PROPERTY AUTOMOC ON)
    set_property(TARGET openfallout-launcher PROPERTY AUTOUIC ON)
    set_property(TARGET openfallout-launcher PROPERTY AUTOUIC_SEARCH_PATHS ui)
endif(USE_QT)
