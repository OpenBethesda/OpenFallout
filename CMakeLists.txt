cmake_minimum_required(VERSION 3.16.0)

#  set the timestamps of extracted contents to the time of extraction
# remove if cmake version is >= 3.24
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Supported Macs: macOS 26.0 or later on Apple Silicon. The default must be set before project(). A caller may pass a
# higher deployment target; the property lists of both apps take their minimum system version from it. An empty value
# (as cached by a build folder configured before this default existed) takes the default.
set(OPENFALLOUT_MINIMUM_MACOS "26.0")
if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${OPENFALLOUT_MINIMUM_MACOS}" CACHE STRING
        "Minimum macOS version (${OPENFALLOUT_MINIMUM_MACOS} or later)" FORCE)
endif()

project(OpenFallout)

if (NOT APPLE)
    message(FATAL_ERROR "OpenFallout builds on macOS only (macOS ${OPENFALLOUT_MINIMUM_MACOS} or later, Apple Silicon).")
endif()
if (CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS OPENFALLOUT_MINIMUM_MACOS)
    message(FATAL_ERROR "OpenFallout supports macOS ${OPENFALLOUT_MINIMUM_MACOS} or later: "
        "CMAKE_OSX_DEPLOYMENT_TARGET is '${CMAKE_OSX_DEPLOYMENT_TARGET}'. Pass ${OPENFALLOUT_MINIMUM_MACOS} or higher.")
endif()
if ((CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    OR (NOT CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64"))
    message(FATAL_ERROR "OpenFallout builds for Apple Silicon only (arm64): CMAKE_OSX_ARCHITECTURES is "
        "'${CMAKE_OSX_ARCHITECTURES}', the target processor is '${CMAKE_SYSTEM_PROCESSOR}'.")
endif()
# End of supported Macs

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DEPLOY_QT_TRANSLATIONS "Deploy standard Qt translations to resources folder. Needed when OpenFallout applications are deployed with Qt libraries" ON)

# Apps and tools
option(BUILD_OPENFALLOUT             "Build OpenFallout" ON)
option(BUILD_LAUNCHER           "Build Launcher" ON)
option(BUILD_MWINIIMPORTER      "Build MWiniImporter" ON)
option(BUILD_ESSIMPORTER        "Build ESS (Morrowind save game) importer" ON)
option(BUILD_BSATOOL            "Build BSA extractor" ON)
option(BUILD_ESMTOOL            "Build ESM inspector" ON)
option(BUILD_NIFTEST            "Build nif file tester" ON)
option(BUILD_DOCS               "Build documentation." OFF )
option(BUILD_WITH_CODE_COVERAGE "Enable code coverage with gconv" OFF)
option(BUILD_COMPONENTS_TESTS   "Build components library tests" OFF)
option(BUILD_BENCHMARKS         "Build benchmarks with Google Benchmark" OFF)
option(BUILD_NAVMESHTOOL        "Build navmesh tool" ON)
option(BUILD_BULLETOBJECTTOOL   "Build Bullet object tool" ON)
option(BUILD_OPENFALLOUT_TESTS       "Build OpenFallout tests" OFF)

set(OpenGL_GL_PREFERENCE LEGACY)  # Use LEGACY as we use GL2; GLNVD is for GL3 and up.

if (BUILD_LAUNCHER)
    set(USE_QT TRUE)
else()
    set(USE_QT FALSE)
endif()

# If the user doesn't supply a CMAKE_BUILD_TYPE via command line, choose one for them.
IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS None Debug Release RelWithDebInfo MinSizeRel)
ENDIF()

set(CMAKE_FIND_FRAMEWORK LAST) # prefer dylibs over frameworks
set(APP_BUNDLE_NAME "${CMAKE_PROJECT_NAME}.app")

set(APP_BUNDLE_DIR "${OpenFallout_BINARY_DIR}/${APP_BUNDLE_NAME}")
set(OPENFALLOUT_ENGINE_APP_DIR "${APP_BUNDLE_DIR}/Contents/Helpers/OpenFallout Engine.app")

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/)

# Version
message(STATUS "Configuring OpenFallout...")

set(OPENFALLOUT_VERSION_MAJOR 0)
set(OPENFALLOUT_VERSION_MINOR 1)
set(OPENFALLOUT_VERSION_RELEASE 1)
set(OPENFALLOUT_LUA_API_REVISION 197)
set(OPENFALLOUT_POSTPROCESSING_API_REVISION 5)

set(OPENFALLOUT_VERSION_COMMITHASH "")
set(OPENFALLOUT_VERSION_TAGHASH "")
set(OPENFALLOUT_VERSION_COMMITDATE "")

set(OPENFALLOUT_VERSION "${OPENFALLOUT_VERSION_MAJOR}.${OPENFALLOUT_VERSION_MINOR}.${OPENFALLOUT_VERSION_RELEASE}")

set(OPENFALLOUT_DOC_BASEURL "https://openmw.readthedocs.io/en/")

set(GIT_CHECKOUT FALSE)
if(EXISTS ${PROJECT_SOURCE_DIR}/.git)
    find_package(Git)

    if(GIT_FOUND)
        set(GIT_CHECKOUT TRUE)
    else(GIT_FOUND)
        message(WARNING "Git executable not found")
    endif(GIT_FOUND)

    if(GIT_FOUND)
        execute_process (
                COMMAND ${GIT_EXECUTABLE} log -1 --format='%aI'
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                RESULT_VARIABLE EXITCODE3
                OUTPUT_VARIABLE OPENFALLOUT_VERSION_COMMITDATE
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT EXITCODE3)
            string(SUBSTRING ${OPENFALLOUT_VERSION_COMMITDATE} 1 10 OPENFALLOUT_VERSION_COMMITDATE)
        endif(NOT EXITCODE3)
    endif(GIT_FOUND)
endif(EXISTS ${PROJECT_SOURCE_DIR}/.git)

# Macros
include(OpenFalloutMacros)
include(WholeArchive)

# doxygen main page

configure_file ("${OpenFallout_SOURCE_DIR}/docs/mainpage.hpp.cmake" "${OpenFallout_BINARY_DIR}/docs/mainpage.hpp")

option(OPENFALLOUT_USE_SYSTEM_BULLET "Use system provided bullet physics library" ON)
if(OPENFALLOUT_USE_SYSTEM_BULLET)
    set(_bullet_static_default OFF)
else()
    set(_bullet_static_default ON)
endif()
option(BULLET_STATIC "Link static build of Bullet into the binaries" ${_bullet_static_default})

option(OPENFALLOUT_USE_SYSTEM_OSG "Use system provided OpenSceneGraph libraries" ON)
if(OPENFALLOUT_USE_SYSTEM_OSG)
    set(_osg_static_default OFF)
else()
    set(_osg_static_default ON)
endif()
option(OSG_STATIC "Link static build of OpenSceneGraph into the binaries" ${_osg_static_default})

option(OPENFALLOUT_USE_SYSTEM_MYGUI "Use system provided mygui library" ON)
if(OPENFALLOUT_USE_SYSTEM_MYGUI)
    set(_mygui_static_default OFF)
else()
    set(_mygui_static_default ON)
endif()
option(MYGUI_STATIC "Link static build of Mygui into the binaries" ${_mygui_static_default})

option(OPENFALLOUT_USE_SYSTEM_RECASTNAVIGATION "Use system provided recastnavigation library" OFF)
if(OPENFALLOUT_USE_SYSTEM_RECASTNAVIGATION)
    set(_recastnavigation_static_default OFF)
    find_package(RecastNavigation REQUIRED CONFIG)
else()
    set(_recastnavigation_static_default ON)
endif()
option(RECASTNAVIGATION_STATIC "Build recastnavigation static libraries" ${_recastnavigation_static_default})

option(OPENFALLOUT_USE_SYSTEM_SQLITE3 "Use system provided SQLite3 library" ON)

option(OPENFALLOUT_USE_SYSTEM_BENCHMARK "Use system Google Benchmark library." OFF)
option(OPENFALLOUT_USE_SYSTEM_GOOGLETEST "Use system Google Test library." OFF)

option(OPENFALLOUT_UNITY_BUILD "Use fewer compilation units to speed up compile time" FALSE)
option(OPENFALLOUT_LTO_BUILD "Build OpenFallout with Link-Time Optimization (Needs ~2GB of RAM)" OFF)

# don't spawn multiple moc/uic child processes in parallel - the build system already parallelises the build
set(CMAKE_AUTOGEN_PARALLEL 1)

set(OPENFALLOUT_RESOURCE_FILES "../Resources/resources")

# Dependencies
# The Metal build draws with OpenGL ES 3.0, which ANGLE's Metal backend turns into Metal; it needs ANGLE and an
# OpenSceneGraph built for OpenGL ES 3 (OSG_DIR or the front of CMAKE_PREFIX_PATH). Off is the desktop OpenGL build.
# OPENGL_INCLUDE_DIR and OPENGL_gl_LIBRARY name the graphics library either way and do not link Apple's OpenGL here.
option(OPENFALLOUT_METAL "Build the Metal renderer: OpenGL ES 3.0 through ANGLE (OPENFALLOUT_ANGLE_ROOT)" OFF)
if (OPENFALLOUT_METAL)
    set(OPENFALLOUT_ANGLE_ROOT "" CACHE PATH "ANGLE: lib/libEGL.dylib, lib/libGLESv2.dylib and include/")
    # find_path and find_library keep what they found: a new root looks again (CI/test_angle_root.py)
    if (NOT "${OPENFALLOUT_ANGLE_ROOT}" STREQUAL "${OPENFALLOUT_ANGLE_ROOT_SEARCHED}")
        unset(ANGLE_INCLUDE_DIR CACHE)
        unset(ANGLE_EGL_LIBRARY CACHE)
        unset(ANGLE_GLESV2_LIBRARY CACHE)
        set(OPENFALLOUT_ANGLE_ROOT_SEARCHED "${OPENFALLOUT_ANGLE_ROOT}" CACHE INTERNAL "")
    endif()
    find_path(ANGLE_INCLUDE_DIR GLES3/gl3.h PATHS "${OPENFALLOUT_ANGLE_ROOT}/include" NO_DEFAULT_PATH)
    find_library(ANGLE_EGL_LIBRARY EGL PATHS "${OPENFALLOUT_ANGLE_ROOT}/lib" NO_DEFAULT_PATH)
    find_library(ANGLE_GLESV2_LIBRARY GLESv2 PATHS "${OPENFALLOUT_ANGLE_ROOT}/lib" NO_DEFAULT_PATH)
    if (NOT ANGLE_INCLUDE_DIR OR NOT ANGLE_EGL_LIBRARY OR NOT ANGLE_GLESV2_LIBRARY)
        message(FATAL_ERROR "OPENFALLOUT_METAL needs ANGLE: OPENFALLOUT_ANGLE_ROOT='${OPENFALLOUT_ANGLE_ROOT}' must hold "
            "lib/libEGL.dylib, lib/libGLESv2.dylib and include/GLES3/gl3.h")
    endif()
    message(STATUS "Metal build: ANGLE ${ANGLE_GLESV2_LIBRARY} ${ANGLE_EGL_LIBRARY}")
    set(OPENGL_INCLUDE_DIR "${ANGLE_INCLUDE_DIR}")
    set(OPENGL_gl_LIBRARY "${ANGLE_GLESV2_LIBRARY}" "${ANGLE_EGL_LIBRARY}")
    add_compile_definitions(OPENFALLOUT_METAL)
else()
    # Force CMake to use the installed version of OpenGL on macOS
    set(_SAVE_CMAKE_FIND_FRAMEWORK ${CMAKE_FIND_FRAMEWORK})
    set(CMAKE_FIND_FRAMEWORK ONLY)
    find_package(OpenGL REQUIRED)
    set(CMAKE_FIND_FRAMEWORK ${_SAVE_CMAKE_FIND_FRAMEWORK})
    unset(_SAVE_CMAKE_FIND_FRAMEWORK)
endif()

# MyGUI render system
# A MyGUI built here (extern/) draws nothing itself: components/myguiplatform does. In the Metal build its OpenGL render
# system (4, the default extern/ gives) would look for desktop OpenGL, so it takes the dummy one (1). The value follows
# OPENFALLOUT_METAL on every configure of a build folder: the value the OpenGL build had, also one a person set, comes
# back when the option goes off, unless a new one is given at that time (CI/test_mygui_render_system.py).
if (OPENFALLOUT_METAL)
    if (NOT DEFINED CACHE{OPENFALLOUT_MYGUI_RENDERSYSTEM_OPENGL})
        set(OPENFALLOUT_MYGUI_RENDERSYSTEM_OPENGL "${MYGUI_RENDERSYSTEM}" CACHE INTERNAL "")
    endif()
    set(MYGUI_RENDERSYSTEM 1 CACHE STRING "" FORCE)
elseif (DEFINED CACHE{OPENFALLOUT_MYGUI_RENDERSYSTEM_OPENGL})
    if (MYGUI_RENDERSYSTEM STREQUAL "1")
        if (OPENFALLOUT_MYGUI_RENDERSYSTEM_OPENGL STREQUAL "")
            unset(MYGUI_RENDERSYSTEM CACHE)
        else()
            set(MYGUI_RENDERSYSTEM "${OPENFALLOUT_MYGUI_RENDERSYSTEM_OPENGL}" CACHE STRING "" FORCE)
        endif()
    endif()
    unset(OPENFALLOUT_MYGUI_RENDERSYSTEM_OPENGL CACHE)
endif()
# End of MyGUI render system

find_package(LZ4 REQUIRED)

if (USE_QT)
    find_package(QT REQUIRED COMPONENTS Core NAMES Qt6)
    find_package(Qt6 COMPONENTS Core Widgets LinguistTools Svg REQUIRED)
    message(STATUS "Using Qt${QT_VERSION}")
endif()

set(USED_OSG_COMPONENTS
    osgAnimation
    osgDB
    osgGA
    osgFX
    osgParticle
    osgText
    osgUtil
    osgShadow
    osgSim
    osgViewer
    )
set(USED_OSG_PLUGINS
    osgdb_bmp
    osgdb_dae
    osgdb_dds
    osgdb_freetype
    osgdb_jpeg
    osgdb_osg
    osgdb_png
    osgdb_serializers_osg
    osgdb_tga)

option(OPENFALLOUT_USE_SYSTEM_ICU "Use system ICU library instead of internal. If disabled, requires autotools" ON)
if(OPENFALLOUT_USE_SYSTEM_ICU)
    find_package(ICU REQUIRED COMPONENTS uc i18n data)
endif()

option(OPENFALLOUT_USE_SYSTEM_YAML_CPP "Use system yaml-cpp library instead of internal." ON)
if(OPENFALLOUT_USE_SYSTEM_YAML_CPP)
    set(_yaml_cpp_static_default OFF)
else()
    set(_yaml_cpp_static_default ON)
endif()
option(YAML_CPP_STATIC "Link static build of yaml-cpp into the binaries" ${_yaml_cpp_static_default})
if (OPENFALLOUT_USE_SYSTEM_YAML_CPP)
    find_package(yaml-cpp REQUIRED)
endif()

if ((BUILD_COMPONENTS_TESTS OR BUILD_OPENFALLOUT_TESTS) AND OPENFALLOUT_USE_SYSTEM_GOOGLETEST)
    find_package(GTest 1.10 REQUIRED)
    find_package(GMock 1.10 REQUIRED)
endif()

add_subdirectory(extern)

# Sound setup

# Require at least FFmpeg 4.0
SET(FFVER_OK FALSE)

find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL SWSCALE SWRESAMPLE)

if(FFmpeg_FOUND)
    SET(FFVER_OK TRUE)

    if(FFmpeg_AVFORMAT_VERSION VERSION_LESS "58.12.100")
        message(STATUS "libavformat is too old! (${FFmpeg_AVFORMAT_VERSION}, wanted 58.12.100)")
        set(FFVER_OK FALSE)
    endif()
    if(FFmpeg_AVCODEC_VERSION VERSION_LESS "58.18.100")
        message(STATUS "libavcodec is too old! (${FFmpeg_AVCODEC_VERSION}, wanted 58.18.100)")
        set(FFVER_OK FALSE)
    endif()
    if(FFmpeg_AVUTIL_VERSION VERSION_LESS "56.14.100")
        message(STATUS "libavutil is too old! (${FFmpeg_AVUTIL_VERSION}, wanted 56.14.100)")
        set(FFVER_OK FALSE)
    endif()
    if(FFmpeg_SWSCALE_VERSION VERSION_LESS "5.1.100")
        message(STATUS "libswscale is too old! (${FFmpeg_SWSCALE_VERSION}, wanted 5.1.100)")
        set(FFVER_OK FALSE)
    endif()
    if(FFmpeg_SWRESAMPLE_VERSION VERSION_LESS "3.1.100")
        message(STATUS "libswresample is too old! (${FFmpeg_SWRESAMPLE_VERSION}, wanted 3.1.100)")
        set(FFVER_OK FALSE)
    endif()

    if(NOT FFVER_OK)
        message(FATAL_ERROR "FFmpeg version is too old, 4.0 is required" )
    endif()
endif()

if(NOT FFmpeg_FOUND)
    message(FATAL_ERROR "FFmpeg was not found" )
endif()

# Required for building the FFmpeg headers
add_compile_definitions(__STDC_CONSTANT_MACROS)

if(OPENFALLOUT_USE_SYSTEM_BULLET)
    set(REQUIRED_BULLET_VERSION 286) # Bullet 286 required due to runtime bugfixes for btCapsuleShape

    # First, try BulletConfig-float64.cmake which comes with Debian derivatives.
    # This file does not define the Bullet version in a CMake-friendly way.
    find_package(Bullet CONFIGS BulletConfig-float64.cmake QUIET COMPONENTS BulletCollision LinearMath)
    if (BULLET_FOUND)
        string(REPLACE "." "" _bullet_version_num ${BULLET_VERSION_STRING})
        if (_bullet_version_num VERSION_LESS REQUIRED_BULLET_VERSION)
            message(FATAL_ERROR "System bullet version too old, OpenFallout requires at least ${REQUIRED_BULLET_VERSION}, got ${_bullet_version_num}")
        endif()
        # Fix the relative include:
        set(BULLET_INCLUDE_DIRS "${BULLET_ROOT_DIR}/${BULLET_INCLUDE_DIRS}")
        include(FindPackageMessage)
        find_package_message(Bullet "Found Bullet: ${BULLET_LIBRARIES} ${BULLET_VERSION_STRING}" "${BULLET_VERSION_STRING}-float64")
    else()
        find_package(Bullet ${REQUIRED_BULLET_VERSION} REQUIRED COMPONENTS BulletCollision LinearMath)
    endif()

    # Only link the Bullet libraries that we need:
    string(REGEX MATCHALL "((optimized|debug);)?[^;]*(BulletCollision|LinearMath)[^;]*" BULLET_LIBRARIES "${BULLET_LIBRARIES}")

    include(cmake/CheckBulletPrecision.cmake)
    if (HAS_DOUBLE_PRECISION_BULLET)
        message(STATUS "Bullet uses double precision")
    else()
        message(FATAL_ERROR "Bullet does not uses double precision")
    endif()
endif()

# Look for stdint.h
include(CheckIncludeFile)
check_include_file(stdint.h HAVE_STDINT_H)
if(NOT HAVE_STDINT_H)
    unset(HAVE_STDINT_H CACHE)
    message(FATAL_ERROR "stdint.h was not found" )
endif()

if(OPENFALLOUT_USE_SYSTEM_OSG)
    find_package(OpenSceneGraph 3.6.5 REQUIRED ${USED_OSG_COMPONENTS})  # Bump to 3.6.6 when released

    if(OSG_STATIC)
        find_package(OSGPlugins REQUIRED COMPONENTS ${USED_OSG_PLUGINS})
    endif()
endif()

include_directories(BEFORE SYSTEM ${OPENSCENEGRAPH_INCLUDE_DIRS})

# Each build needs the OpenSceneGraph of its own profile: GLES3 for the Metal build, a desktop one otherwise.
foreach (_dir ${OPENSCENEGRAPH_INCLUDE_DIRS})
    if (EXISTS "${_dir}/osg/GL")
        file(STRINGS "${_dir}/osg/GL" _osg_gles3 REGEX "^#define OSG_GLES3_AVAILABLE")
        if (OPENFALLOUT_METAL AND NOT _osg_gles3)
            message(FATAL_ERROR "OPENFALLOUT_METAL needs an OpenSceneGraph built with OPENGL_PROFILE=GLES3, not ${_dir}")
        elseif (NOT OPENFALLOUT_METAL AND _osg_gles3)
            message(FATAL_ERROR "The OpenGL build needs a desktop OpenSceneGraph; ${_dir} is built for OpenGL ES 3")
        endif()
        break()
    endif()
endforeach()

if(OSG_STATIC)
    if(NOT COLLADA_DOM_VERSION_MAJOR)
        set(COLLADA_DOM_VERSION_MAJOR 2)
    endif()

    if(NOT COLLADA_DOM_VERSION_MINOR)
        set(COLLADA_DOM_VERSION_MINOR 5)
    endif()

    find_package(collada_dom 2.5)

    link_directories(${COLLADA_DOM_LIBRARY_DIRS})

    add_compile_definitions(OSG_LIBRARY_STATIC)
endif()

include(cmake/CheckOsgMultiview.cmake)
if(HAVE_MULTIVIEW)
    add_compile_definitions(OSG_HAS_MULTIVIEW)
endif(HAVE_MULTIVIEW)

set(BOOST_COMPONENTS iostreams program_options)

find_package(Boost 1.70.0 CONFIG REQUIRED COMPONENTS ${BOOST_COMPONENTS} OPTIONAL_COMPONENTS ${BOOST_OPTIONAL_COMPONENTS})

if(OPENFALLOUT_USE_SYSTEM_MYGUI)
    find_package(MyGUI 3.4.3 REQUIRED)
endif()
find_package(SDL2 2.26 REQUIRED)
find_package(OpenAL REQUIRED)
find_package(ZLIB REQUIRED)

option(USE_LUAJIT "Switch Lua/LuaJit (TRUE is highly recommended)" TRUE)
if(USE_LUAJIT)
    find_package(LuaJit REQUIRED)
    set(LUA_INCLUDE_DIR ${LuaJit_INCLUDE_DIR})
    set(LUA_LIBRARIES ${LuaJit_LIBRARIES})
else(USE_LUAJIT)
    find_package(Lua REQUIRED)
    add_compile_definitions(NO_LUAJIT)
endif(USE_LUAJIT)
include(cmake/CheckLuaCustomAllocator.cmake)

# C++ library binding to Lua
set(SOL_INCLUDE_DIR ${OpenFallout_SOURCE_DIR}/extern/sol3)
set(SOL_CONFIG_DIR ${OpenFallout_SOURCE_DIR}/extern/sol_config)

include_directories(
    BEFORE SYSTEM
    ${MyGUI_INCLUDE_DIRS}
    ${OPENAL_INCLUDE_DIR}
    ${OPENGL_INCLUDE_DIR}
    ${BULLET_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIR}
    ${SOL_INCLUDE_DIR}
    ${SOL_CONFIG_DIR}
    ${ICU_INCLUDE_DIRS}
)
# Metal include order
if (OPENFALLOUT_METAL)
    # A dependency set may keep a desktop OpenSceneGraph in the one folder of all its headers, which the call above has
    # just put in front. The compiler must read the headers of the OpenSceneGraph for OpenGL ES 3 first, and the check
    # below stops the configure step if any folder in front of them holds another OpenSceneGraph.
    include_directories(BEFORE SYSTEM ${OPENSCENEGRAPH_INCLUDE_DIRS})
    get_property(_include_dirs DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
    foreach (_dir ${_include_dirs})
        if (EXISTS "${_dir}/osg/GL")
            file(STRINGS "${_dir}/osg/GL" _osg_gles3 REGEX "^#define OSG_GLES3_AVAILABLE")
            if (NOT _osg_gles3)
                message(FATAL_ERROR "OPENFALLOUT_METAL: the compiler would read the desktop OpenSceneGraph headers in "
                                    "${_dir} before the ones built with OPENGL_PROFILE=GLES3")
            endif()
            break()
        endif()
    endforeach()
endif()
# End of Metal include order
include_directories(".")

if(MYGUI_STATIC)
    add_compile_definitions(MYGUI_STATIC)
endif (MYGUI_STATIC)

# Mac app property lists
configure_file(${OpenFallout_SOURCE_DIR}/files/mac/openfallout-Info.plist.in
    "${APP_BUNDLE_DIR}/Contents/Info.plist")

configure_file(${OpenFallout_SOURCE_DIR}/files/mac/openmw.icns
    "${APP_BUNDLE_DIR}/Contents/Resources/OpenFallout.icns" COPYONLY)

if (BUILD_OPENFALLOUT)
    # The game is started through this nested app (Contents/Helpers) so that the Qt launcher stays the main
    # executable of OpenFallout.app while `open` can still start the engine with Game Mode. Its executable is a
    # small shim (apps/openfallout/macos) that replaces itself with Contents/MacOS/openfallout of the outer app.
    # Written in every build with the engine, also without the launcher, so that the nested app is complete.
    configure_file("${OpenFallout_SOURCE_DIR}/files/mac/openfallout-engine-Info.plist.in"
        "${OPENFALLOUT_ENGINE_APP_DIR}/Contents/Info.plist")
    configure_file("${OpenFallout_SOURCE_DIR}/files/mac/openmw.icns"
        "${OPENFALLOUT_ENGINE_APP_DIR}/Contents/Resources/OpenFallout.icns" COPYONLY)
endif()
# End of Mac app property lists

add_subdirectory(files/)

# Specify build paths

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${APP_BUNDLE_DIR}/Contents/MacOS")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${APP_BUNDLE_DIR}/Contents/MacOS")

# Read by install(TARGETS) only; the Mac app is installed as a copy of the build folder and fixup_bundle deletes its run
# paths (cmake/OpenFalloutAngle.cmake)
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Other files

pack_resource_file(${OpenFallout_SOURCE_DIR}/files/settings-default.cfg
    "${OpenFallout_BINARY_DIR}" "defaults.bin")

configure_file(${OpenFallout_SOURCE_DIR}/files/openfallout.cfg
    "${OpenFallout_BINARY_DIR}/openfallout.cfg")

configure_resource_file(${OpenFallout_SOURCE_DIR}/files/gamecontrollerdb.txt
    "${OpenFallout_BINARY_DIR}" "gamecontrollerdb.txt")

if(OPENFALLOUT_LTO_BUILD)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT HAVE_IPO OUTPUT HAVE_IPO_OUTPUT)
    if(HAVE_IPO)
        message(STATUS "LTO enabled for Release configuration.")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(WARNING "Requested option OPENFALLOUT_LTO_BUILD not supported by this compiler: ${HAVE_IPO_OUTPUT}")
    endif()
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")

# Extern

add_subdirectory (extern/osg-ffmpeg-videoplayer)
add_subdirectory (extern/oics)
add_subdirectory (extern/Base64)

add_compile_options("-Wall" "-Wextra" "-Wundef" "-Wextra-semi" "-Wno-unused-parameter" "-pedantic" "-Wno-long-long" "-Wnon-virtual-dtor" "-Wunused" "-Wshadow")

if (OPENFALLOUT_CXX_FLAGS)
    separate_arguments(OPENFALLOUT_CXX_FLAGS NATIVE_COMMAND "${OPENFALLOUT_CXX_FLAGS}")
    add_compile_options(${OPENFALLOUT_CXX_FLAGS})
endif()

# Components
add_subdirectory (components)

# Apps and tools
if (BUILD_OPENFALLOUT OR BUILD_OPENFALLOUT_TESTS)
    add_subdirectory( apps/openfallout )
endif()

if (BUILD_BSATOOL)
    add_subdirectory( apps/bsatool )
endif()

if (BUILD_ESMTOOL)
    add_subdirectory( apps/esmtool )
endif()

if (BUILD_LAUNCHER)
    add_subdirectory( apps/launcher )
endif()

if (BUILD_MWINIIMPORTER)
    add_subdirectory( apps/mwiniimporter )
endif()

if (BUILD_ESSIMPORTER)
    add_subdirectory (apps/essimporter )
endif()

if (BUILD_NIFTEST)
    add_subdirectory(apps/niftest)
endif()

if (BUILD_COMPONENTS_TESTS)
    add_subdirectory(apps/components_tests)
endif()

if (BUILD_BENCHMARKS)
    add_subdirectory(apps/benchmarks)
endif()

if (BUILD_NAVMESHTOOL)
    add_subdirectory(apps/navmeshtool)
endif()

if (BUILD_BULLETOBJECTTOOL)
    add_subdirectory(apps/bulletobjecttool)
endif()

if (BUILD_OPENFALLOUT_TESTS)
    add_subdirectory(apps/openfallout_tests)
endif()


# Core CPACK Setup
set(CPACK_PACKAGE_VERSION ${OPENFALLOUT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${OPENFALLOUT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${OPENFALLOUT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${OPENFALLOUT_VERSION_RELEASE})
set(CPACK_PACKAGE_NAME "OpenFallout")
set(CPACK_PACKAGE_VENDOR "OpenFallout")
set(CUSTOM_CPACK_OS_NAME "macOS")
set(CUSTOM_CPACK_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")

set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CUSTOM_CPACK_OS_NAME}-${CUSTOM_CPACK_ARCHITECTURE}")

# Apple bundling
# A package needs the launcher: it is the main executable of OpenFallout.app. An engine-only developer build
# (BUILD_LAUNCHER=OFF) runs from the build folder and makes no package.
if (BUILD_LAUNCHER AND BUILD_OPENFALLOUT)
    if(CMAKE_VERSION VERSION_LESS 3.19)
        message(FATAL_ERROR "macOS packaging requires CMake 3.19 or higher to sign macOS app bundles.")
    endif()

    # Each application is paired with the entitlements file its executables are signed with under the hardened runtime.
    set(CPACK_OPENFALLOUT_APPLICATIONS "")
    set(CPACK_OPENFALLOUT_ENTITLEMENTS "")
    list(APPEND CPACK_OPENFALLOUT_APPLICATIONS "${APP_BUNDLE_NAME}")
    list(APPEND CPACK_OPENFALLOUT_ENTITLEMENTS "${OpenFallout_SOURCE_DIR}/files/mac/openfallout.entitlements")
    install(DIRECTORY "${APP_BUNDLE_DIR}" USE_SOURCE_PERMISSIONS DESTINATION "." COMPONENT Runtime)

    set(QT_BUNDLE_PLUGINS "")
    if (USE_QT)
        foreach (QT_PLUGIN QCocoaIntegrationPlugin QMacStylePlugin QSvgPlugin QSvgIconPlugin)
            get_property(QT_PLUGIN_PATH TARGET Qt${QT_VERSION_MAJOR}::${QT_PLUGIN} PROPERTY LOCATION_RELEASE)
            get_filename_component(QT_PLUGIN_DIR "${QT_PLUGIN_PATH}" DIRECTORY)
            get_filename_component(QT_PLUGIN_GROUP "${QT_PLUGIN_DIR}" NAME)
            get_filename_component(QT_PLUGIN_NAME "${QT_PLUGIN_PATH}" NAME)
            set(QT_PLUGIN_RELATIVE "Contents/PlugIns/${QT_PLUGIN_GROUP}/${QT_PLUGIN_NAME}")
            list(APPEND QT_BUNDLE_PLUGINS "${QT_PLUGIN_RELATIVE}")
            foreach (BUNDLE_NAME IN LISTS CPACK_OPENFALLOUT_APPLICATIONS)
                configure_file("${QT_PLUGIN_PATH}" "${OpenFallout_BINARY_DIR}/${BUNDLE_NAME}/${QT_PLUGIN_RELATIVE}" COPYONLY)
            endforeach()
        endforeach()
        foreach (BUNDLE_NAME IN LISTS CPACK_OPENFALLOUT_APPLICATIONS)
            configure_file("${OpenFallout_SOURCE_DIR}/files/mac/qt.conf" "${OpenFallout_BINARY_DIR}/${BUNDLE_NAME}/Contents/Resources/qt.conf" COPYONLY)
        endforeach()
    endif()

    set(CPACK_GENERATOR "DragNDrop")

    install(CODE "
        set(BU_CHMOD_BUNDLE_ITEMS ON)
        set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH})
        include(BundleUtilities)

        function(gp_item_default_embedded_path_override item default_embedded_path_var)
            if (\${item} MATCHES osgPlugins-${OPENSCENEGRAPH_VERSION})
                set(\${default_embedded_path_var} \"@executable_path/../PlugIns/osgPlugins-${OPENSCENEGRAPH_VERSION}\" PARENT_SCOPE)
            endif()
        endfunction()
    " COMPONENT Runtime)

    set(ABSOLUTE_PLUGINS "")

    set(OSGPlugins_DONT_FIND_DEPENDENCIES 1)
    find_package(OSGPlugins REQUIRED COMPONENTS ${USED_OSG_PLUGINS})

    foreach (PLUGIN_NAME ${USED_OSG_PLUGINS})
        string(TOUPPER ${PLUGIN_NAME} PLUGIN_NAME_UC)
        if(${PLUGIN_NAME_UC}_LIBRARY_RELEASE)
            set(PLUGIN_ABS ${${PLUGIN_NAME_UC}_LIBRARY_RELEASE})
        elseif(${PLUGIN_NAME_UC}_LIBRARY)
            set(PLUGIN_ABS ${${PLUGIN_NAME_UC}_LIBRARY})
        else()
            message(FATAL_ERROR "Can't find library file for ${PLUGIN_NAME}")
            # We used to construct the path manually from OSGPlugins_LIB_DIR and the plugin name.
            # Maybe that could be restored as a fallback?
        endif()
        set(ABSOLUTE_PLUGINS ${PLUGIN_ABS} ${ABSOLUTE_PLUGINS})
    endforeach ()

    set(OSG_PLUGIN_PREFIX_DIR "osgPlugins-${OPENSCENEGRAPH_VERSION}")

    # installs used plugins in bundle at given path (bundle_path must be relative to ${CMAKE_INSTALL_PREFIX})
    # and returns list of install paths for all installed plugins
    function (install_plugins_for_bundle bundle_path plugins_var)
        set(RELATIVE_PLUGIN_INSTALL_BASE "${bundle_path}/Contents/PlugIns/${OSG_PLUGIN_PREFIX_DIR}")

        set(PLUGINS "")
        set(PLUGIN_INSTALL_BASE "\${CMAKE_INSTALL_PREFIX}/${RELATIVE_PLUGIN_INSTALL_BASE}")

        foreach (PLUGIN ${ABSOLUTE_PLUGINS})
            get_filename_component(PLUGIN_RELATIVE ${PLUGIN} NAME)
            get_filename_component(PLUGIN_RELATIVE_WE ${PLUGIN} NAME_WE)

            set(PLUGIN_DYLIB_IN_BUNDLE "${PLUGIN_INSTALL_BASE}/${PLUGIN_RELATIVE}")
            set(PLUGINS ${PLUGINS} "${PLUGIN_DYLIB_IN_BUNDLE}")

            install(CODE "
                copy_resolved_item_into_bundle(\"${PLUGIN}\" \"${PLUGIN_DYLIB_IN_BUNDLE}\")
            " COMPONENT Runtime)
        endforeach ()

        set(${plugins_var} ${PLUGINS} PARENT_SCOPE)
    endfunction (install_plugins_for_bundle)

    foreach (BUNDLE_NAME IN LISTS CPACK_OPENFALLOUT_APPLICATIONS)
        install_plugins_for_bundle("${BUNDLE_NAME}" PLUGINS)
        set(INSTALLED_APP "\${CMAKE_INSTALL_PREFIX}/${BUNDLE_NAME}")
        foreach (QT_PLUGIN_RELATIVE IN LISTS QT_BUNDLE_PLUGINS)
            list(APPEND PLUGINS "${INSTALLED_APP}/${QT_PLUGIN_RELATIVE}")
        endforeach()
        install(CODE "fixup_bundle(\"${INSTALLED_APP}\" \"${PLUGINS}\" \"\")" COMPONENT Runtime)
    endforeach()

    install(FILES
        "${OpenFallout_SOURCE_DIR}/CHANGELOG.md"
        "${OpenFallout_SOURCE_DIR}/README.md"
        "${OpenFallout_SOURCE_DIR}/LICENSE"
        "${OpenFallout_SOURCE_DIR}/AUTHORS.md"
        DESTINATION "." COMPONENT Runtime)

    set(CPACK_PRE_BUILD_SCRIPTS ${CMAKE_SOURCE_DIR}/cmake/SignMacApplications.cmake)

    include(CPack)
    # End of Apple bundling
endif()
include(OpenFalloutAngle) # after the Apple bundling block: its install rules must run after fixup_bundle

# what is necessary to build documentation
if ( BUILD_DOCS )
    # Builds the documentation.
    FIND_PACKAGE( Sphinx REQUIRED )
    FIND_PACKAGE( Doxygen REQUIRED )

    # Doxygen Target -- simply run 'make doc' or 'make doc_pages'
    # output directory for 'make doc'       is "${OpenFallout_BINARY_DIR}/docs/Doxygen"
    # output directory for 'make doc_pages' is "${DOXYGEN_PAGES_OUTPUT_DIR}" if defined
    #                                       or "${OpenFallout_BINARY_DIR}/docs/Pages" otherwise

    # determine output directory for doc_pages
    if (NOT DEFINED DOXYGEN_PAGES_OUTPUT_DIR)
        set(DOXYGEN_PAGES_OUTPUT_DIR "${OpenFallout_BINARY_DIR}/docs/Pages")
    endif ()
    configure_file(${OpenFallout_SOURCE_DIR}/docs/Doxyfile.cmake ${OpenFallout_BINARY_DIR}/docs/Doxyfile @ONLY)
    configure_file(${OpenFallout_SOURCE_DIR}/docs/DoxyfilePages.cmake ${OpenFallout_BINARY_DIR}/docs/DoxyfilePages @ONLY)
    add_custom_target(doc
        ${DOXYGEN_EXECUTABLE} ${OpenFallout_BINARY_DIR}/docs/Doxyfile
        WORKING_DIRECTORY ${OpenFallout_BINARY_DIR}
        COMMENT "Generating Doxygen documentation at ${OpenFallout_BINARY_DIR}/docs/Doxygen"
        VERBATIM)
    add_custom_target(doc_pages
        ${DOXYGEN_EXECUTABLE} ${OpenFallout_BINARY_DIR}/docs/DoxyfilePages
        WORKING_DIRECTORY ${OpenFallout_BINARY_DIR}
        COMMENT "Generating documentation for the github-pages at ${DOXYGEN_PAGES_OUTPUT_DIR}" VERBATIM)
endif ()

# Qt localization
if (USE_QT)
    file(GLOB LAUNCHER_TS_FILES ${CMAKE_SOURCE_DIR}/files/lang/launcher_*.ts)
    file(GLOB COMPONENTS_TS_FILES ${CMAKE_SOURCE_DIR}/files/lang/components_*.ts)
    get_target_property(QT_LUPDATE_EXECUTABLE Qt::lupdate IMPORTED_LOCATION)
    add_custom_target(translations
        COMMAND ${QT_LUPDATE_EXECUTABLE} -locations none ${CMAKE_SOURCE_DIR}/components/contentselector ${CMAKE_SOURCE_DIR}/components/process -ts ${COMPONENTS_TS_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/components
        VERBATIM
        COMMAND_EXPAND_LISTS
        BYPRODUCTS ${COMPONENTS_TS_FILES}

        COMMAND ${QT_LUPDATE_EXECUTABLE} -locations none ${CMAKE_SOURCE_DIR}/apps/launcher -ts ${LAUNCHER_TS_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/apps/launcher
        VERBATIM
        COMMAND_EXPAND_LISTS
        BYPRODUCTS ${LAUNCHER_TS_FILES}
    )

    add_custom_target(purge-obsolete-translations
        COMMAND ${QT_LUPDATE_EXECUTABLE} -locations none -no-obsolete ${CMAKE_SOURCE_DIR}/components/contentselector ${CMAKE_SOURCE_DIR}/components/process -ts ${COMPONENTS_TS_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/components
        VERBATIM
        COMMAND_EXPAND_LISTS

        COMMAND ${QT_LUPDATE_EXECUTABLE} -locations none -no-obsolete ${CMAKE_SOURCE_DIR}/apps/launcher -ts ${LAUNCHER_TS_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/apps/launcher
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    if (BUILD_LAUNCHER)
        set(QT_OPENFALLOUT_TRANSLATIONS_PATH "${APP_BUNDLE_DIR}/Contents/Resources/resources/translations")

        file(GLOB TS_FILES ${COMPONENTS_TS_FILES})

        if (BUILD_LAUNCHER)
            set(TS_FILES ${TS_FILES} ${LAUNCHER_TS_FILES})
        endif ()

        qt_add_translation(QM_FILES ${TS_FILES} OPTIONS -silent)

        if (DEPLOY_QT_TRANSLATIONS)
            # Once we set a Qt 6.2.0 as a minimum required version, we may use "qtpaths --qt-query" instead.
            get_target_property(QT_QMAKE_EXECUTABLE Qt::qmake IMPORTED_LOCATION)
            execute_process(COMMAND "${QT_QMAKE_EXECUTABLE}" -query QT_INSTALL_TRANSLATIONS
                OUTPUT_VARIABLE QT_TRANSLATIONS_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)

            foreach(QM_FILE ${QM_FILES})
                get_filename_component(QM_BASENAME ${QM_FILE} NAME)
                string(REGEX REPLACE "[^_]+_(.*)\\.qm" "\\1" LANG_NAME ${QM_BASENAME})
                if (EXISTS "${QT_TRANSLATIONS_DIR}/qtbase_${LANG_NAME}.qm")
                    set(QM_FILES ${QM_FILES} "${QT_TRANSLATIONS_DIR}/qtbase_${LANG_NAME}.qm")
                elseif (EXISTS "${QT_TRANSLATIONS_DIR}/qt_${LANG_NAME}.qm")
                    set(QM_FILES ${QM_FILES} "${QT_TRANSLATIONS_DIR}/qt_${LANG_NAME}.qm")
                else ()
                    message(FATAL_ERROR "Qt translations for '${LANG_NAME}' locale are not found in the '${QT_TRANSLATIONS_DIR}' folder.")
                endif ()
            endforeach(QM_FILE)

            list(REMOVE_DUPLICATES QM_FILES)
        endif ()

        add_custom_target(qm-files
            COMMAND ${CMAKE_COMMAND} -E make_directory ${QT_OPENFALLOUT_TRANSLATIONS_PATH}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${QM_FILES} ${QT_OPENFALLOUT_TRANSLATIONS_PATH}
            DEPENDS ${QM_FILES}
            COMMENT "Copy *.qm files to resources folder")
    endif ()
endif()
