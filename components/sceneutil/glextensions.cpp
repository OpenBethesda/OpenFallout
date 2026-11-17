#include "glextensions.hpp"

#include <array>
#include <string>

#include <osg/ClipPlane>
#include <osg/Fog>
#include <osg/GraphicsContext>
#include <osg/Light>
#include <osg/LineStipple>
#include <osg/Material>
#include <osg/Multisample>
#include <osg/Point>
#include <osg/PointSprite>
#include <osg/PolygonOffset>
#include <osg/Transform>

#include <components/debug/debuglog.hpp>
#include <components/shader/glsles.hpp>

#include "gldialect.hpp"

namespace SceneUtil
{
    namespace
    {
#ifdef OPENFALLOUT_METAL
        template <class T>
        void loadFunction(T& function, const std::string& name)
        {
            function = nullptr;
            if (!name.empty())
                osg::setGLExtensionFuncPtr(function, name.c_str());
        }

        // OpenSceneGraph's OpenGL ES 3 profile still gives some desktop answers. Corrected here once per context, so
        // that the rest of the engine sets its state the same way in both builds. (Only the OpenGL ES 3 build of
        // OpenSceneGraph has all of these members.)
        void adaptToOpenGLES(osg::State& state, osg::GLExtensions& extensions)
        {
            const unsigned int contextID = state.getContextID();
            const auto supported
                = [&](const char* extension) { return osg::isGLExtensionSupported(contextID, extension); };

            // Switches desktop OpenGL has and OpenGL ES 3.0 does not. OpenSceneGraph passes every mode of a state set
            // to glEnable and glDisable; a mode marked invalid is left alone. The shaders do this work in both builds.
            constexpr std::array desktopModes = { GL_NORMALIZE, GL_RESCALE_NORMAL, GL_ALPHA_TEST, GL_LIGHTING,
                GL_COLOR_MATERIAL, GL_FOG, GL_POINT_SMOOTH, GL_POINT_SPRITE_ARB, GL_VERTEX_PROGRAM_POINT_SIZE,
                GL_TEXTURE_CUBE_MAP_SEAMLESS, GL_LINE_STIPPLE, GL_POLYGON_OFFSET_LINE, GL_POLYGON_OFFSET_POINT };
            for (GLenum mode : desktopModes)
                state.setModeValidity(mode, false);
            // A user clip plane is a clip distance in OpenGL ES (GL_CLIP_DISTANCE0 + i is GL_CLIP_PLANE0 + i); the converted
            // shaders write the first Shader::sClipPlaneCount (components/shader/glsles.hpp)
            const bool clipDistances = supported("GL_ANGLE_clip_cull_distance");
            for (GLenum i = 0; i < 8; ++i)
            {
                state.setModeValidity(GL_LIGHT0 + i, false);
                state.setModeValidity(GL_CLIP_PLANE0 + i, clipDistances && i < Shader::sClipPlaneCount);
            }
            state.setModeValidity(GL_DEPTH_CLAMP, supported("GL_EXT_depth_clamp"));
            state.setModeValidity(GL_FRAMEBUFFER_SRGB, supported("GL_EXT_sRGB_write_control"));
            state.setModeValidity(GL_MULTISAMPLE_ARB, supported("GL_EXT_multisample_compatibility"));

            // OpenSceneGraph takes "version 1.2 or later" to mean that the texture LOD bias parameter exists
            extensions.isTextureLODBiasSupported = false;

            // The S3TC formats come as three extensions in OpenGL ES, under other names than desktop OpenGL's
            extensions.isTextureCompressionS3TCSupported = extensions.isTextureCompressionS3TCSupported
                || (supported("GL_EXT_texture_compression_dxt1") && supported("GL_ANGLE_texture_compression_dxt3")
                    && supported("GL_ANGLE_texture_compression_dxt5"));

            // Per-draw-buffer switches are OpenGL ES 3.2; ES 3.0 has them only as extension functions
            const std::string indexed = supported("GL_OES_draw_buffers_indexed") ? "OES"
                : supported("GL_EXT_draw_buffers_indexed")                       ? "EXT"
                                                                                 : "";
            const auto indexedName = [&](const char* name) { return indexed.empty() ? "" : name + indexed; };
            loadFunction(extensions.glEnablei, indexedName("glEnablei"));
            loadFunction(extensions.glDisablei, indexedName("glDisablei"));
            loadFunction(extensions.glColorMaski, indexedName("glColorMaski"));
            loadFunction(extensions.glBlendFunci, indexedName("glBlendFunci"));
            loadFunction(extensions.glBlendFuncSeparatei, indexedName("glBlendFuncSeparatei"));
            loadFunction(extensions.glBlendEquationi, indexedName("glBlendEquationi"));
            loadFunction(extensions.glBlendEquationSeparatei, indexedName("glBlendEquationSeparatei"));

            // Object labels are OpenGL ES 3.2; KHR_debug has them under its own name
            loadFunction(extensions.glObjectLabel,
                supported("GL_KHR_debug") ? std::string("glObjectLabel") + GLDialect::sDebugFunctionSuffix : "");

            // Asked before the log line: OpenSceneGraph logs each question at its INFO level, through the engine's
            // log, and a log line that is being written holds the log's lock (a question inside it never returned)
            const bool depthClamp = supported("GL_EXT_depth_clamp");
            Log(Debug::Info) << "OpenGL ES: per-draw-buffer switches " << (indexed.empty() ? "missing" : indexed)
                             << ", S3TC textures " << (extensions.isTextureCompressionS3TCSupported ? "yes" : "no")
                             << ", depth clamp " << (depthClamp ? "yes" : "no")
                             << ", clip distances " << (clipDistances ? "yes" : "no");
        }
#endif
        std::set<osg::observer_ptr<osg::GLExtensions>> sGLExtensions;

        class GLExtensionsObserver : public osg::Observer
        {
        public:
            static GLExtensionsObserver sInstance;

            ~GLExtensionsObserver() override
            {
                for (auto& ptr : sGLExtensions)
                {
                    osg::ref_ptr<osg::GLExtensions> ref;
                    if (ptr.lock(ref))
                        ref->removeObserver(this);
                }
            }

            void objectDeleted(void* referenced) override
            {
                sGLExtensions.erase(static_cast<osg::GLExtensions*>(referenced));
            }
        };

        // construct after sGLExtensions so this gets destroyed first.
        GLExtensionsObserver GLExtensionsObserver::sInstance{};
    }

    bool glExtensionsReady()
    {
        return !sGLExtensions.empty();
    }

    osg::GLExtensions& getGLExtensions()
    {
        if (sGLExtensions.empty())
            throw std::runtime_error(
                "GetGLExtensionsOperation was not used when the current context was created or there is no current "
                "context");
        return **sGLExtensions.begin();
    }

    GetGLExtensionsOperation::GetGLExtensionsOperation()
        : GraphicsOperation("GetGLExtensionsOperation", false)
    {
    }

    void GetGLExtensionsOperation::operator()(osg::GraphicsContext* graphicsContext)
    {
        auto [itr, _] = sGLExtensions.emplace(graphicsContext->getState()->get<osg::GLExtensions>());
        (*itr)->addObserver(&GLExtensionsObserver::sInstance);
#ifdef OPENFALLOUT_METAL
        osg::ref_ptr<osg::GLExtensions> extensions;
        if (itr->lock(extensions))
            adaptToOpenGLES(*graphicsContext->getState(), *extensions);
#endif
    }
}
