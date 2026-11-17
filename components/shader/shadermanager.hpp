#ifndef OPENFALLOUT_COMPONENTS_SHADERMANAGER_H
#define OPENFALLOUT_COMPONENTS_SHADERMANAGER_H

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <osg/Program>
#include <osg/Shader>
#include <osg/Uniform>
#include <osg/ref_ptr>

namespace osgViewer
{
    class Viewer;
}

namespace Shader
{
    struct HotReloadManager;
    /// @brief Reads shader template files and turns them into a concrete shader, based on a list of define's.
    /// @par Shader templates can get the value of a define with the syntax @define.
    class ShaderManager
    {
    public:
        friend HotReloadManager;
        ShaderManager();
        ~ShaderManager();

        void setShaderPath(const std::filesystem::path& path);

        typedef std::map<std::string, std::string> DefineMap;

        /// Create or retrieve a shader instance.
        /// @param templateName The path of the shader template.
        /// @param defines Define values that can be retrieved by the shader template.
        /// @param shaderType The type of shader (usually vertex or fragment shader).
        /// @note May return nullptr on failure.
        /// @note Thread safe, use getShaderInternal within the ShaderManager if you've already locked the mutex.
        osg::ref_ptr<osg::Shader> getShader(std::string templateName, const DefineMap& defines = {},
            std::optional<osg::Shader::Type> type = std::nullopt);

        typedef std::map<std::string, int> SamplerBindingMap;

        /// @param samplers Sampler uniforms to bind once per linked program instead of on every stateset.
        osg::ref_ptr<osg::Program> getProgram(const std::string& templateName, const DefineMap& defines = {},
            const osg::Program* programTemplate = nullptr, const SamplerBindingMap& samplers = {});

        osg::ref_ptr<osg::Program> getProgram(osg::ref_ptr<osg::Shader> vertexShader,
            osg::ref_ptr<osg::Shader> fragmentShader, const osg::Program* programTemplate = nullptr,
            const SamplerBindingMap& samplers = {});

        const osg::Program* getProgramTemplate() const { return mProgramTemplate; }
        void setProgramTemplate(const osg::Program* program) { mProgramTemplate = program; }

        /// Clone an osg::Program including bindUniformBlocks that osg::Program::clone does not copy for some reason.
        static osg::ref_ptr<osg::Program> cloneProgram(const osg::Program*);

        /// Get (a copy of) the DefineMap used to construct all shaders
        DefineMap getGlobalDefines();

        /// Set the DefineMap used to construct all shaders
        /// @param defines The DefineMap to use
        /// @note This will change the source code for any shaders already created, potentially causing problems if
        /// they're being used to render a frame. It is recommended that any associated Viewers have their threading
        /// stopped while this function is running if any shaders are in use.
        void setGlobalDefines(DefineMap& globalDefines);

        void releaseGLObjects(osg::State* state);

        bool createSourceFromTemplate(std::string& source, std::vector<std::string>& linkedShaderTemplateNames,
            const std::string& templateName, const ShaderManager::DefineMap& defines);

        void setMaxTextureUnits(int maxTextureUnits) { mMaxTextureUnits = maxTextureUnits; }
        int getMaxTextureUnits() const { return mMaxTextureUnits; }
        int getAvailableTextureUnits() const { return mMaxTextureUnits - mReservedTextureUnits; }

        enum class Slot
        {
            OpaqueDepthTexture,
            OpaqueColorTexture,
            SkyTexture,
            ShadowMaps,
            SLOT_COUNT
        };

        int reserveGlobalTextureUnits(Slot slot, int count = 1);

        void update(osgViewer::Viewer& viewer);
        void setHotReloadEnabled(bool value);
        void triggerShaderReload();

        /// Whether shaders are converted to GLSL ES 3.00 with their linked library shaders folded in. The Metal build
        /// always converts and the OpenGL build never does (SceneUtil::GLDialect); tests may choose either.
        void setConvertToGlslEs(bool value) { mConvertToGlslEs = value; }

    private:
        typedef std::vector<osg::ref_ptr<osg::Uniform>> UniformList;

        osg::ref_ptr<osg::Shader> getShaderInternal(std::string templateName, const DefineMap& defines = {},
            std::optional<osg::Shader::Type> type = std::nullopt);
        // Reads a template and its includes once; end() if it cannot be read
        std::map<std::string, std::string>::iterator loadTemplate(const std::string& templateName);
        // Reads a template and its includes from disk; false, leaving source as it is, if it cannot be read
        bool readTemplate(const std::string& templateName, std::string& source);
        // Turns the source createSourceFromTemplate gives into the text the graphics library compiles
        bool finishSource(std::string& source, std::vector<std::string>& linkedShaderNames,
            const std::string& templateName, osg::Shader::Type type, const DefineMap& defines,
            UniformList& initialValues);
        void getLinkedShaders(osg::ref_ptr<osg::Shader> shader, const std::vector<std::string>& linkedShaderNames,
            const DefineMap& defines);
        void addLinkedShaders(osg::ref_ptr<osg::Shader> shader, osg::ref_ptr<osg::Program> program);

        std::filesystem::path mPath;

        DefineMap mGlobalDefines;

        // <name, code>
        typedef std::map<std::string, std::string> TemplateMap;
        TemplateMap mShaderTemplates;

        typedef std::pair<std::string, DefineMap> MapKey;
        typedef std::map<MapKey, osg::ref_ptr<osg::Shader>> ShaderMap;
        ShaderMap mShaders;

        typedef std::map<std::pair<osg::ref_ptr<osg::Shader>, osg::ref_ptr<osg::Shader>>, osg::ref_ptr<osg::Program>>
            ProgramMap;
        ProgramMap mPrograms;

        typedef std::vector<osg::ref_ptr<osg::Shader>> ShaderList;
        typedef std::map<osg::ref_ptr<osg::Shader>, ShaderList> LinkedShadersMap;
        LinkedShadersMap mLinkedShaders;

        bool mConvertToGlslEs;

        std::mutex mMutex;

        osg::ref_ptr<const osg::Program> mProgramTemplate;

        int mMaxTextureUnits = 0;
        int mReservedTextureUnits = 0;
        std::unique_ptr<HotReloadManager> mHotReloadManager;
        struct ReservedTextureUnits
        {
            int index = -1;
            int count = 0;
        };
        std::array<ReservedTextureUnits, static_cast<int>(Slot::SLOT_COUNT)> mReservedTextureUnitsBySlot = {};
    };

    bool parseForeachDirective(std::string& source, const std::string& templateName, size_t foundPos);
    bool parseLinkDirective(
        std::string& source, std::string& linkTarget, const std::string& templateName, size_t foundPos);

    bool parseDefines(std::string& source, const ShaderManager::DefineMap& defines,
        const ShaderManager::DefineMap& globalDefines, const std::string& templateName);

    bool parseDirectives(std::string& source, std::vector<std::string>& linkedShaderTemplateNames,
        const ShaderManager::DefineMap& defines, const ShaderManager::DefineMap& globalDefines,
        const std::string& templateName);

    ShaderManager::DefineMap getDefaultDefines();

    /// The uniform values a program made by the ShaderManager sets each time it is linked: those of the uniform
    /// initialisers the GLSL ES conversion took out of its shaders (GLSL ES has none). They follow the shaders' current
    /// text, also after a rebuild for new defines or a hot reload. Always empty in the OpenGL build.
    std::vector<osg::ref_ptr<osg::Uniform>> getInitialValues(const osg::Program& program);
}

#endif
