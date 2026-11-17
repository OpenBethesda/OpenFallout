#ifndef OPENFALLOUT_COMPONENTS_SCENEUTIL_GLEXTCONSTANTS_H
#define OPENFALLOUT_COMPONENTS_SCENEUTIL_GLEXTCONSTANTS_H

// Extension constants the engine names beyond OpenSceneGraph's headers: the desktop extension header in the OpenGL
// build, the OpenGL ES one in the Metal build (see gldialect.hpp).
#include <osg/GL>

#ifdef OPENFALLOUT_METAL
#include <GLES2/gl2ext.h>
#else
#include <SDL_opengl_glext.h>
#endif

#endif
