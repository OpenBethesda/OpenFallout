# ANGLE in the packaged app (docs/openfallout/angle.md).
#
# The Metal build (OPENFALLOUT_METAL) and the early notarized test of the OpenGL build (OPENFALLOUT_BUNDLE_ANGLE) put
# the project's ANGLE libraries, unchanged, into OpenFallout.app/Contents/Frameworks and ANGLE's notices into
# Contents/Resources/ANGLE-NOTICES.txt. They come from OPENFALLOUT_ANGLE_ROOT, the unpacked release asset
# (CI/angle_asset.py fetch <dir>). CPack's signing step (cmake/SignMacApplications.cmake) then signs each library on its
# own, with the hardened runtime and a secure timestamp, before the app; CI/check_mac_package.py --angle bundled checks
# that the signed libraries are the published ones.
#
# The top-level CMakeLists.txt includes this file AFTER the Apple bundling block on purpose: install rules run in the
# order they are declared, and fixup_bundle rewrites the install name and deletes the rpaths of every library it finds
# in the bundle. Copied in after it, the libraries keep their bytes and their @rpath install names. In the Metal build,
# fixup_bundle has by then pointed the engine's references to @executable_path/../Frameworks/libEGL.dylib and
# libGLESv2.dylib, the files copied here, and deleted the engine's run paths: the app is installed as a copy of the build
# folder (install(DIRECTORY)), so the build's absolute run paths reach the package only to be deleted there, and
# CMAKE_INSTALL_RPATH_USE_LINK_PATH, which only install(TARGETS) reads, plays no part. CI/test_angle_bundle.py checks it.
#
# OPENFALLOUT_BUNDLE_ANGLE also builds and packages openfallout-angle-probe in Contents/MacOS: a small program that
# proves inside the signed app that the libraries load and that ANGLE's Metal backend gives an OpenGL ES 3.0 context
# (CI/macos/angle/angle_probe.c). It is never part of a release.

option(OPENFALLOUT_BUNDLE_ANGLE
    "Put the ANGLE libraries and the ANGLE probe into the packaged OpenGL app (the early notarized test)" OFF)
set(OPENFALLOUT_ANGLE_ROOT "" CACHE PATH
    "The unpacked ANGLE release asset: lib/libEGL.dylib, lib/libGLESv2.dylib, include/, NOTICES")

set(OPENFALLOUT_ANGLE_PACKAGED OFF)
if (BUILD_LAUNCHER AND BUILD_OPENFALLOUT AND (OPENFALLOUT_METAL OR OPENFALLOUT_BUNDLE_ANGLE))
    set(OPENFALLOUT_ANGLE_PACKAGED ON)
endif()

if (OPENFALLOUT_BUNDLE_ANGLE OR OPENFALLOUT_ANGLE_PACKAGED)
    set(_angle_needed lib/libEGL.dylib lib/libGLESv2.dylib)
    if (OPENFALLOUT_ANGLE_PACKAGED)
        list(APPEND _angle_needed NOTICES)
    endif()
    if (OPENFALLOUT_BUNDLE_ANGLE)
        list(APPEND _angle_needed include/EGL/egl.h include/GLES3/gl3.h)
    endif()
    if (OPENFALLOUT_ANGLE_ROOT STREQUAL "")
        message(FATAL_ERROR "The ANGLE libraries are packaged, but OPENFALLOUT_ANGLE_ROOT is not set. Give it the "
                            "folder that `CI/angle_asset.py fetch <dir>` fills (docs/openfallout/angle.md).")
    endif()
    foreach (_angle_file IN LISTS _angle_needed)
        if (NOT EXISTS "${OPENFALLOUT_ANGLE_ROOT}/${_angle_file}")
            message(FATAL_ERROR "OPENFALLOUT_ANGLE_ROOT (${OPENFALLOUT_ANGLE_ROOT}) has no ${_angle_file}; it must be "
                                "the unpacked ANGLE release asset (docs/openfallout/angle.md)")
        endif()
    endforeach()
endif()

if (OPENFALLOUT_BUNDLE_ANGLE)
    add_executable(openfallout-angle-probe "${CMAKE_CURRENT_LIST_DIR}/../CI/macos/angle/angle_probe.c")
    target_include_directories(openfallout-angle-probe PRIVATE "${OPENFALLOUT_ANGLE_ROOT}/include")
    target_link_libraries(openfallout-angle-probe PRIVATE
        "${OPENFALLOUT_ANGLE_ROOT}/lib/libEGL.dylib" "${OPENFALLOUT_ANGLE_ROOT}/lib/libGLESv2.dylib"
        # the probe asks Metal whether the machine has a device at all; a command line program needs CoreGraphics for that
        "-framework Metal" "-framework CoreGraphics")
    # Built outside the app folder, so that neither the app's install(DIRECTORY) nor fixup_bundle sees it; installed
    # below with the rpath of the packaged app only.
    set_target_properties(openfallout-angle-probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/angle-probe"
        INSTALL_RPATH "@executable_path/../Frameworks")
endif()

if (OPENFALLOUT_ANGLE_PACKAGED)
    # install(FILES) leaves a destination alone when its time stamp equals the source's to the second, and fixup_bundle
    # may have rewritten its copy of a library within that second. Removing that copy first makes the copy below certain.
    install(CODE "
        file(REMOVE
            \"\${CMAKE_INSTALL_PREFIX}/${APP_BUNDLE_NAME}/Contents/Frameworks/libEGL.dylib\"
            \"\${CMAKE_INSTALL_PREFIX}/${APP_BUNDLE_NAME}/Contents/Frameworks/libGLESv2.dylib\")
    " COMPONENT Runtime)
    install(FILES "${OPENFALLOUT_ANGLE_ROOT}/lib/libEGL.dylib" "${OPENFALLOUT_ANGLE_ROOT}/lib/libGLESv2.dylib"
        DESTINATION "${APP_BUNDLE_NAME}/Contents/Frameworks"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        COMPONENT Runtime)
    install(FILES "${OPENFALLOUT_ANGLE_ROOT}/NOTICES" DESTINATION "${APP_BUNDLE_NAME}/Contents/Resources"
        RENAME "ANGLE-NOTICES.txt" COMPONENT Runtime)
    if (OPENFALLOUT_BUNDLE_ANGLE)
        install(TARGETS openfallout-angle-probe RUNTIME DESTINATION "${APP_BUNDLE_NAME}/Contents/MacOS"
            COMPONENT Runtime)
    endif()
endif()
