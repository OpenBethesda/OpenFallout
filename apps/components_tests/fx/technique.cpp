#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <osg/Program>
#include <osg/StateSet>

#include <components/files/configurationmanager.hpp>
#include <components/fx/pass.hpp>
#include <components/fx/technique.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/settings/settings.hpp>
#include <components/testing/util.hpp>

namespace
{
    constexpr VFS::Path::NormalizedView techniquePropertiesPath("shaders/technique_properties.omwfx");

    TestingOpenMW::VFSTestFile techniqueProperties(R"(
    fragment main {}
    vertex main {}
    technique {
        passes = main;
        version = "0.1a";
        description = "description";
        author = "author";
        glsl_version = 330;
        glsl_profile = "compatability";
        glsl_extensions = GL_EXT_gpu_shader4, GL_ARB_uniform_buffer_object;
        flags = disable_sunglare;
        hdr = true;
    }
)");

    constexpr VFS::Path::NormalizedView rendertargetPropertiesPath("shaders/rendertarget_properties.omwfx");

    TestingOpenMW::VFSTestFile rendertargetProperties{ R"(
    render_target rendertarget {
        width_ratio = 0.5;
        height_ratio = 0.5;
        internal_format = r16f;
        source_type = float;
        source_format = red;
        mipmaps = true;
        wrap_s = clamp_to_edge;
        wrap_t = repeat;
        min_filter = linear;
        mag_filter = nearest;
    }
    fragment downsample2x(target=rendertarget) {

        omw_In vec2 omw_TexCoord;

        void main()
        {
            omw_FragColor.r = omw_GetLastShader(omw_TexCoord).r;
        }
    }
    fragment main { }
    technique { passes = downsample2x, main; }
)" };

    constexpr VFS::Path::NormalizedView uniformPropertiesPath("shaders/uniform_properties.omwfx");

    TestingOpenMW::VFSTestFile uniformProperties{ R"(
    uniform_vec4 uVec4 {
        default = vec4(0,0,0,0);
        min = vec4(0,1,0,0);
        max = vec4(0,0,1,0);
        step = 0.5;
        header = "header";
        static = true;
        description = "description";
    }
    fragment main { }
    technique { passes = main; }
)" };

    constexpr VFS::Path::NormalizedView missingSamplerSourcePath("shaders/missing_sampler_source.omwfx");

    TestingOpenMW::VFSTestFile missingSamplerSource{ R"(
    sampler_1d mysampler1d { }
    fragment main { }
    technique { passes = main; }
)" };

    constexpr VFS::Path::NormalizedView repeatedSharedBlockPath("shaders/repeated_shared_block.omwfx");

    TestingOpenMW::VFSTestFile repeatedSharedBlock{ R"(
    shared {
        float myfloat = 1.0;
    }
    shared {}
    fragment main { }
    technique { passes = main; }
)" };

    constexpr VFS::Path::NormalizedView invalidNumberInfPath("shaders/invalid_number_inf.omwfx");

    TestingOpenMW::VFSTestFile invalidNumberInf{ R"(
    uniform_vec4 uVec4 {
        step = inf;
    }
    fragment main { }
    technique { passes = main; }
)" };

    constexpr VFS::Path::NormalizedView invalidNumberNegativeInfPath("shaders/invalid_number_negative_inf.omwfx");

    TestingOpenMW::VFSTestFile invalidNumberNegativeInf{ R"(
    uniform_vec4 uVec4 {
        step = -inf;
    }
    fragment main { }
    technique { passes = main; }
)" };

    constexpr VFS::Path::NormalizedView invalidNumberUnsignedLongPath("shaders/invalid_number_ulong.omwfx");

    TestingOpenMW::VFSTestFile invalidNumberUnsignedLong{ R"(
    uniform_vec4 uVec4 {
        step = 18446744073709551615;
    }
    fragment main { }
    technique { passes = main; }
)" };

    constexpr VFS::Path::NormalizedView invalidNumberHexFloatPath("shaders/invalid_number_hex.omwfx");

    TestingOpenMW::VFSTestFile invalidNumberHexFloat{ R"(
    uniform_vec4 uVec4 {
        step = 0x1.fffffep+12;
    }
    fragment main { }
    technique { passes = main; }
)" };

    constexpr VFS::Path::NormalizedView invalidNumberDoublePath("shaders/invalid_number_double.omwfx");

    TestingOpenMW::VFSTestFile invalidNumberDouble{ R"(
    uniform_vec4 uVec4 {
        step = 1.79769e+50;
    }
    fragment main { }
    technique { passes = main; }
)" };

    constexpr VFS::Path::NormalizedView notConvertiblePath("shaders/not_convertible.omwfx");

    TestingOpenMW::VFSTestFile notConvertible{ R"(
    fragment main {
        omw_In vec2 omw_TexCoord;

        void main()
        {
            omw_FragColor = gl_LightSource[0].diffuse;
        }
    }
    technique { passes = main; }
)" };

    using namespace testing;
    using namespace Fx;

    // The shaders of each pass of a compiled technique, as the post-processor gives them to the graphics library
    std::vector<osg::ref_ptr<osg::Shader>> passShaders(Pass& pass)
    {
        osg::ref_ptr<osg::StateSet> stateSet = new osg::StateSet;
        pass.prepareStateSet(stateSet, pass.getName());
        std::vector<osg::ref_ptr<osg::Shader>> shaders;
        if (const auto* program = dynamic_cast<const osg::Program*>(
                stateSet->getAttribute(osg::StateAttribute::PROGRAM)))
            for (unsigned int i = 0; i < program->getNumShaders(); ++i)
                shaders.emplace_back(const_cast<osg::Shader*>(program->getShader(i)));
        return shaders;
    }

    struct TechniqueTest : Test
    {
        std::unique_ptr<VFS::Manager> mVFS;
        Resource::ImageManager mImageManager;
        std::unique_ptr<Technique> mTechnique;

        TechniqueTest()
            : mVFS(TestingOpenMW::createTestVFS({
                { techniquePropertiesPath, &techniqueProperties },
                { rendertargetPropertiesPath, &rendertargetProperties },
                { uniformPropertiesPath, &uniformProperties },
                { missingSamplerSourcePath, &missingSamplerSource },
                { repeatedSharedBlockPath, &repeatedSharedBlock },
                { invalidNumberInfPath, &invalidNumberInf },
                { invalidNumberNegativeInfPath, &invalidNumberNegativeInf },
                { invalidNumberUnsignedLongPath, &invalidNumberUnsignedLong },
                { invalidNumberHexFloatPath, &invalidNumberHexFloat },
                { invalidNumberDoublePath, &invalidNumberDouble },
                { notConvertiblePath, &notConvertible },
            }))
            , mImageManager(mVFS.get(), 0)
        {
        }

        void compile(const std::string& name)
        {
            mTechnique = std::make_unique<Technique>(
                *mVFS.get(), mImageManager, Technique::makeFileName(name), name, 1, 1, true, true);
            mTechnique->compile();
        }

        // As the Metal build compiles a technique: no uniform buffer (OpenGL ES 3.0 reports GLSL ES 3.00, below the 3.30
        // the buffer path asks for) and the passes converted to GLSL ES 3.00
        void compileForGlslEs(const std::string& name, bool convert = true)
        {
            mTechnique = std::make_unique<Technique>(
                *mVFS.get(), mImageManager, Technique::makeFileName(name), name, 1, 1, false, true);
            mTechnique->setConvertToGlslEs(convert);
            mTechnique->compile();
        }

        void expectFailure(const std::string& name, std::string_view errorString)
        {
            internal::CaptureStdout();

            compile(name);

            std::string output = internal::GetCapturedStdout();
            Log(Debug::Error) << output;
            EXPECT_THAT(output, HasSubstr(errorString));
        }
    };

    TEST_F(TechniqueTest, technique_properties)
    {
        std::unordered_set<std::string> targetExtensions = { "GL_EXT_gpu_shader4", "GL_ARB_uniform_buffer_object" };

        compile("technique_properties");

        EXPECT_EQ(mTechnique->getVersion(), "0.1a");
        EXPECT_EQ(mTechnique->getDescription(), "description");
        EXPECT_EQ(mTechnique->getAuthor(), "author");
        EXPECT_EQ(mTechnique->getGLSLVersion(), 330);
        EXPECT_EQ(mTechnique->getGLSLProfile(), "compatability");
        EXPECT_EQ(mTechnique->getGLSLExtensions(), targetExtensions);
        EXPECT_EQ(mTechnique->getFlags(), Technique::Flag_Disable_SunGlare);
        EXPECT_EQ(mTechnique->getHDR(), true);
        EXPECT_EQ(mTechnique->getPasses().size(), 1);
        EXPECT_EQ(mTechnique->getPasses().front()->getName(), "main");
    }

    TEST_F(TechniqueTest, rendertarget_properties)
    {
        compile("rendertarget_properties");

        EXPECT_EQ(mTechnique->getRenderTargetsMap().size(), 1);

        const std::string_view name = mTechnique->getRenderTargetsMap().begin()->first;
        auto& rt = mTechnique->getRenderTargetsMap().begin()->second;
        auto& texture = rt.mTarget;

        EXPECT_EQ(name, "rendertarget");
        EXPECT_EQ(rt.mMipMap, true);
        EXPECT_EQ(rt.mSize.mWidthRatio, 0.5f);
        EXPECT_EQ(rt.mSize.mHeightRatio, 0.5f);
        EXPECT_EQ(texture->getWrap(osg::Texture::WRAP_S), osg::Texture::CLAMP_TO_EDGE);
        EXPECT_EQ(texture->getWrap(osg::Texture::WRAP_T), osg::Texture::REPEAT);
        EXPECT_EQ(texture->getFilter(osg::Texture::MIN_FILTER), osg::Texture::LINEAR);
        EXPECT_EQ(texture->getFilter(osg::Texture::MAG_FILTER), osg::Texture::NEAREST);
        EXPECT_EQ(texture->getSourceType(), static_cast<GLenum>(GL_FLOAT));
        EXPECT_EQ(texture->getSourceFormat(), static_cast<GLenum>(GL_RED));
        EXPECT_EQ(texture->getInternalFormat(), static_cast<GLint>(GL_R16F));

        EXPECT_EQ(mTechnique->getPasses().size(), 2);
        EXPECT_EQ(mTechnique->getPasses()[0]->getTarget(), "rendertarget");
    }

    TEST_F(TechniqueTest, uniform_properties)
    {
        compile("uniform_properties");

        EXPECT_EQ(mTechnique->getUniformMap().size(), 1);

        const auto& uniform = mTechnique->getUniformMap().front();

        EXPECT_TRUE(uniform->mStatic);
        EXPECT_DOUBLE_EQ(uniform->mStep, 0.5);
        EXPECT_EQ(uniform->getDefault<osg::Vec4f>(), osg::Vec4f(0, 0, 0, 0));
        EXPECT_EQ(uniform->getMin<osg::Vec4f>(), osg::Vec4f(0, 1, 0, 0));
        EXPECT_EQ(uniform->getMax<osg::Vec4f>(), osg::Vec4f(0, 0, 1, 0));
        EXPECT_EQ(uniform->mHeader, "header");
        EXPECT_EQ(uniform->mDescription, "description");
        EXPECT_EQ(uniform->mName, "uVec4");
    }

    TEST_F(TechniqueTest, fail_with_missing_source_for_sampler)
    {
        expectFailure("missing_sampler_source", "sampler_1d 'mysampler1d' requires a filename");
    }

    TEST_F(TechniqueTest, fail_with_repeated_shared_block)
    {
        expectFailure("repeated_shared_block", "repeated 'shared' block");
    }

    TEST_F(TechniqueTest, fail_with_invalid_float)
    {
        expectFailure("invalid_number_inf", "expected float value");
        expectFailure("invalid_number_negative_inf", "expected float value");
        expectFailure("invalid_number_hex", "expected float value");
    }

    TEST_F(TechniqueTest, fail_with_out_of_range)
    {
        expectFailure("invalid_number_ulong", "number out of range");
        expectFailure("invalid_number_double", "number out of range");
    }

    TEST_F(TechniqueTest, passes_are_glsl_es_300_when_the_build_converts_shaders)
    {
        compileForGlslEs("rendertarget_properties");
        ASSERT_TRUE(mTechnique->isValid()) << mTechnique->getLastError();
        ASSERT_EQ(mTechnique->getPasses().size(), 2);
        for (const auto& pass : mTechnique->getPasses())
        {
            const auto shaders = passShaders(*pass);
            ASSERT_EQ(shaders.size(), 2);
            for (const auto& shader : shaders)
            {
                EXPECT_THAT(shader->getShaderSource(), StartsWith("#version 300 es\n")) << pass->getName();
                EXPECT_THAT(shader->getShaderSource(), Not(HasSubstr("gl_FragColor")));
                EXPECT_THAT(shader->getShaderSource(), Not(HasSubstr("texture2D")));
            }
        }
    }

    TEST_F(TechniqueTest, passes_stay_glsl_120_when_the_build_does_not_convert_shaders)
    {
        compileForGlslEs("rendertarget_properties", false);
        ASSERT_TRUE(mTechnique->isValid()) << mTechnique->getLastError();
        for (const auto& pass : mTechnique->getPasses())
            for (const auto& shader : passShaders(*pass))
            {
                EXPECT_THAT(shader->getShaderSource(), HasSubstr("#version 120"));
                EXPECT_THAT(shader->getShaderSource(), Not(HasSubstr("#version 300 es")));
            }
    }

    TEST_F(TechniqueTest, a_pass_that_cannot_be_converted_fails_its_technique)
    {
        compileForGlslEs("not_convertible");
        EXPECT_FALSE(mTechnique->isValid());
        EXPECT_EQ(mTechnique->getStatus(), Technique::Status::Parse_Error);
        EXPECT_THAT(mTechnique->getLastError(), HasSubstr("GLSL ES 3.00"));
        EXPECT_THAT(mTechnique->getLastError(), HasSubstr("gl_LightSource"));
    }

    // Every post-processing technique the engine ships compiles to GLSL ES 3.00 passes the way the Metal build makes
    // them. With OPENFALLOUT_GLSL_ES_DUMP=<folder> the passes are written there, for a compile check against ANGLE.
    TEST(TechniqueGlslEsCorpusTest, every_technique_the_engine_ships_converts)
    {
        const std::filesystem::path root
            = std::filesystem::path(OPENFALLOUT_PROJECT_SOURCE_DIR) / "files" / "data" / "shaders";
        std::vector<std::unique_ptr<TestingOpenMW::VFSTestFile>> files;
        std::vector<std::string> names;
        VFS::FileMap map;
        for (const auto& entry : std::filesystem::directory_iterator(root))
        {
            if (entry.path().extension().string() != "." + std::string(Technique::sExt))
                continue;
            std::ifstream stream(entry.path());
            std::stringstream text;
            text << stream.rdbuf();
            files.push_back(std::make_unique<TestingOpenMW::VFSTestFile>(text.str()));
            names.push_back(entry.path().stem().string());
            map.emplace(Technique::makeFileName(names.back()), files.back().get());
        }
        ASSERT_FALSE(names.empty());
        const std::unique_ptr<VFS::Manager> vfs = TestingOpenMW::createTestVFS(std::move(map));
        Resource::ImageManager imageManager(vfs.get(), 0);
        const char* dump = std::getenv("OPENFALLOUT_GLSL_ES_DUMP");

        for (const std::string& name : names)
        {
            Technique technique(*vfs, imageManager, Technique::makeFileName(name), name, 1280, 720, false, true);
            technique.setConvertToGlslEs(true);
            EXPECT_TRUE(technique.compile()) << name << ": " << technique.getLastError();
            for (const auto& pass : technique.getPasses())
                for (const auto& shader : passShaders(*pass))
                {
                    EXPECT_THAT(shader->getShaderSource(), StartsWith("#version 300 es\n")) << name;
                    if (dump)
                        std::ofstream(std::filesystem::path(dump)
                            / ("fx_" + name + "_" + pass->getName()
                                + (shader->getType() == osg::Shader::VERTEX ? ".vert" : ".frag")))
                            << shader->getShaderSource();
                }
        }
    }
}
