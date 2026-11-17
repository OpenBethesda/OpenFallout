#include <components/files/conversion.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/testing/util.hpp>

#include <osg/State>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <osgViewer/Viewer>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{
    using namespace testing;
    using namespace Shader;

    // The engine's copy of OpenSceneGraph runs its renaming of built-ins over every stage as it compiles it
    // (State::convertVertexShaderSourceToOsgBuiltIns, with the attribute aliases and matrix uniforms of OpenGL ES 3).
    // A converted text must give it nothing to rename: the declarations it would add go before the default precision.
    bool openSceneGraphLeavesAlone(const std::string& source)
    {
        osg::ref_ptr<osg::State> state = new osg::State;
        state->setUseVertexAttributeAliasing(true);
        state->setUseModelViewAndProjectionUniforms(true);
        std::string text = source;
        state->convertVertexShaderSourceToOsgBuiltIns(text);
        return text == source;
    }

    // The shader manager as the Metal build uses it: shaders come out as GLSL ES 3.00, one shader object per stage
    struct ShaderManagerGlslEsTest : Test
    {
        ShaderManager mManager;

        ShaderManagerGlslEsTest()
        {
            mManager.setShaderPath(TestingOpenMW::outputDir());
            mManager.setConvertToGlslEs(true);
        }

        std::string write(const std::string& name, const std::string& content)
        {
            const std::string templateName
                = "lib/" + std::string(UnitTest::GetInstance()->current_test_info()->name()) + "/" + name;
            std::ofstream(TestingOpenMW::outputFilePathWithSubDir(templateName)) << content;
            return templateName;
        }
    };

    TEST_F(ShaderManagerGlslEsTest, linked_library_shaders_are_folded_into_one_shader_per_stage)
    {
        const std::string library
            = write("library.glsl", "#version 120\nvarying vec2 uv;\nvoid foo() { uv = vec2(1.0); }\n");
        const std::string vertex = write("main.vert",
            "#version 120\n@link \"" + library + "\"\nvarying vec2 uv;\nvoid foo();\n"
            "void main() { foo(); gl_Position = gl_Vertex; }\n");
        const std::string fragment = write(
            "main.frag", "#version 120\nvarying vec2 uv;\nvoid main() { gl_FragColor = vec4(uv, 0.0, 1.0); }\n");

        const osg::ref_ptr<osg::Shader> shader = mManager.getShader(vertex, {}, osg::Shader::VERTEX);
        ASSERT_TRUE(shader);
        EXPECT_THAT(shader->getShaderSource(), StartsWith("#version 300 es\n"));
        EXPECT_THAT(shader->getShaderSource(), HasSubstr("void foo() { uv = vec2(1.0); }"));
        EXPECT_THAT(shader->getShaderSource(), HasSubstr("in vec4 osg_Vertex;"));

        const osg::ref_ptr<osg::Program> program
            = mManager.getProgram(shader, mManager.getShader(fragment, {}, osg::Shader::FRAGMENT));
        EXPECT_EQ(program->getNumShaders(), 2);
    }

    TEST_F(ShaderManagerGlslEsTest, a_shader_that_cannot_be_converted_is_not_given)
    {
        const std::string fragment
            = write("main.frag", "#version 120\nvoid main() { gl_FragColor = gl_LightSource[0].diffuse; }\n");
        EXPECT_FALSE(mManager.getShader(fragment, {}, osg::Shader::FRAGMENT));
    }

    TEST_F(ShaderManagerGlslEsTest, defines_reach_the_linked_shaders)
    {
        const std::string library = write("library.glsl", "#version 120\nfloat foo() { return @value; }\n");
        const std::string vertex = write("main.vert",
            "#version 120\n@link \"" + library + "\"\nfloat foo();\nvoid main() { gl_Position = vec4(foo()); }\n");
        const osg::ref_ptr<osg::Shader> shader
            = mManager.getShader(vertex, { { "value", "2.5" } }, osg::Shader::VERTEX);
        ASSERT_TRUE(shader);
        EXPECT_THAT(shader->getShaderSource(), HasSubstr("float foo() { return 2.5; }"));
    }

    // GLSL ES has no uniform initialisers: the program sets the values the conversion took out when it is linked
    float initialValue(const osg::Program& program, const std::string& name)
    {
        float value = -1;
        for (const osg::ref_ptr<osg::Uniform>& uniform : getInitialValues(program))
            if (uniform->getName() == name)
                EXPECT_TRUE(uniform->get(value));
        return value;
    }

    TEST_F(ShaderManagerGlslEsTest, a_program_sets_the_initial_values_of_its_shaders)
    {
        const std::string vertex = write(
            "main.vert", "#version 120\nuniform float fade = 0.5;\nvoid main() { gl_Position = gl_Vertex * fade; }\n");
        const std::string fragment = write(
            "main.frag", "#version 120\nuniform float alpha = 0.25;\nvoid main() { gl_FragColor = vec4(alpha); }\n");
        const osg::ref_ptr<osg::Program> program
            = mManager.getProgram(mManager.getShader(vertex, {}, osg::Shader::VERTEX),
                mManager.getShader(fragment, {}, osg::Shader::FRAGMENT));
        ASSERT_TRUE(program);
        EXPECT_EQ(initialValue(*program, "fade"), 0.5f);
        EXPECT_EQ(initialValue(*program, "alpha"), 0.25f);
    }

    TEST_F(ShaderManagerGlslEsTest, a_shader_rebuilt_for_new_defines_brings_its_new_initial_values)
    {
        ShaderManager::DefineMap globalDefines = getDefaultDefines();
        globalDefines["fadeValue"] = "0.5";
        mManager.setGlobalDefines(globalDefines);
        const std::string vertex = write("main.vert",
            "#version 120\nuniform float fade = @fadeValue;\nvoid main() { gl_Position = gl_Vertex * fade; }\n");
        const std::string fragment = write("main.frag", "#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n");
        const osg::ref_ptr<osg::Program> program
            = mManager.getProgram(mManager.getShader(vertex, {}, osg::Shader::VERTEX),
                mManager.getShader(fragment, {}, osg::Shader::FRAGMENT));
        EXPECT_EQ(initialValue(*program, "fade"), 0.5f);

        globalDefines["fadeValue"] = "2.0";
        mManager.setGlobalDefines(globalDefines);
        EXPECT_EQ(initialValue(*program, "fade"), 2.0f);
    }

    TEST_F(ShaderManagerGlslEsTest, a_hot_reloaded_shader_brings_its_new_initial_values)
    {
        const std::string vertex = write(
            "main.vert", "#version 120\nuniform float fade = 0.5;\nvoid main() { gl_Position = gl_Vertex * fade; }\n");
        const std::string fragment = write("main.frag", "#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n");
        const osg::ref_ptr<osg::Program> program
            = mManager.getProgram(mManager.getShader(vertex, {}, osg::Shader::VERTEX),
                mManager.getShader(fragment, {}, osg::Shader::FRAGMENT));
        EXPECT_EQ(initialValue(*program, "fade"), 0.5f);

        write(
            "main.vert", "#version 120\nuniform float fade = 3.0;\nvoid main() { gl_Position = gl_Vertex * fade; }\n");
        std::filesystem::last_write_time(TestingOpenMW::outputFilePathWithSubDir(vertex),
            std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
        osgViewer::Viewer viewer;
        mManager.triggerShaderReload();
        mManager.update(viewer);
        EXPECT_EQ(initialValue(*program, "fade"), 3.0f);
    }

    TEST_F(ShaderManagerGlslEsTest, a_hot_reload_that_cannot_read_a_linked_template_keeps_the_previous_text)
    {
        const std::string library = write("library.glsl", "#version 120\nfloat foo() { return 1.0; }\n");
        const std::string vertex = write("main.vert",
            "#version 120\n@link \"" + library + "\"\nfloat foo();\nvoid main() { gl_Position = vec4(foo()); }\n");
        const osg::ref_ptr<osg::Shader> shader = mManager.getShader(vertex, {}, osg::Shader::VERTEX);
        ASSERT_TRUE(shader);
        const std::string before = shader->getShaderSource();

        write("library.glsl", "#version 120\n#include \"missing.glsl\"\nfloat foo() { return 2.0; }\n");
        std::filesystem::last_write_time(TestingOpenMW::outputFilePathWithSubDir(library),
            std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
        osgViewer::Viewer viewer;
        mManager.triggerShaderReload();
        mManager.update(viewer);
        EXPECT_EQ(shader->getShaderSource(), before);

        ShaderManager::DefineMap globalDefines = getDefaultDefines();
        mManager.setGlobalDefines(globalDefines);
        EXPECT_EQ(shader->getShaderSource(), before);
        EXPECT_THAT(shader->getShaderSource(), HasSubstr("return 1.0;"));
    }

    // Marks a file as written after the last hot reload
    void touch(const std::string& templateName)
    {
        std::filesystem::last_write_time(TestingOpenMW::outputFilePathWithSubDir(templateName),
            std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
    }

    TEST_F(ShaderManagerGlslEsTest, a_library_a_hot_reload_newly_links_is_watched_by_later_reloads)
    {
        const std::string library = write("library.glsl", "#version 120\nfloat foo() { return 1.0; }\n");
        const std::string vertex = write("main.vert", "#version 120\nvoid main() { gl_Position = vec4(0.0); }\n");
        const osg::ref_ptr<osg::Shader> shader = mManager.getShader(vertex, {}, osg::Shader::VERTEX);
        ASSERT_TRUE(shader);

        write("main.vert",
            "#version 120\n@link \"" + library + "\"\nfloat foo();\nvoid main() { gl_Position = vec4(foo()); }\n");
        touch(vertex);
        osgViewer::Viewer viewer;
        mManager.triggerShaderReload();
        mManager.update(viewer);
        ASSERT_THAT(shader->getShaderSource(), HasSubstr("return 1.0;"));

        write("library.glsl", "#version 120\nfloat foo() { return 2.0; }\n");
        touch(library);
        mManager.triggerShaderReload();
        mManager.update(viewer);
        EXPECT_THAT(shader->getShaderSource(), HasSubstr("return 2.0;"));
    }

    TEST_F(ShaderManagerGlslEsTest, a_library_new_defines_newly_link_is_watched_by_hot_reloads)
    {
        ShaderManager::DefineMap globalDefines = getDefaultDefines();
        globalDefines["useLibrary"] = "0";
        mManager.setGlobalDefines(globalDefines);
        const std::string library = write("library.glsl", "#version 120\nfloat foo() { return 1.0; }\n");
        const std::string vertex = write("main.vert",
            "#version 120\n@link \"" + library
                + "\" if @useLibrary\nfloat foo();\nvoid main() { gl_Position = vec4(0.0); }\n");
        const osg::ref_ptr<osg::Shader> shader = mManager.getShader(vertex, {}, osg::Shader::VERTEX);
        ASSERT_TRUE(shader);
        ASSERT_THAT(shader->getShaderSource(), Not(HasSubstr("return 1.0;")));

        globalDefines["useLibrary"] = "1";
        mManager.setGlobalDefines(globalDefines);
        ASSERT_THAT(shader->getShaderSource(), HasSubstr("return 1.0;"));

        write("library.glsl", "#version 120\nfloat foo() { return 2.0; }\n");
        touch(library);
        osgViewer::Viewer viewer;
        mManager.triggerShaderReload();
        mManager.update(viewer);
        EXPECT_THAT(shader->getShaderSource(), HasSubstr("return 2.0;"));
    }

    // Every vertex and fragment shader the engine ships, with its options all off and all on, must convert. With
    // OPENFALLOUT_GLSL_ES_DUMP=<folder> the converted text is written there, for a compile check against ANGLE.
    TEST(ShaderGlslEsCorpusTest, every_shader_the_engine_ships_converts)
    {
        const std::filesystem::path root = std::filesystem::path(OPENFALLOUT_PROJECT_SOURCE_DIR) / "files" / "shaders";
        ASSERT_TRUE(std::filesystem::exists(root / "compatibility"));
        const char* dump = std::getenv("OPENFALLOUT_GLSL_ES_DUMP");

        const ShaderManager::DefineMap off = { { "adjustCoverage", "0" }, { "additiveBlending", "0" },
            { "alphaFunc", "519" }, { "alphaToCoverage", "0" }, { "blendMap", "0" }, { "bumpMap", "0" },
            { "bumpMapUV", "0" }, { "darkMap", "0" }, { "darkMapUV", "0" }, { "decalMap", "0" }, { "decalMapUV", "0" },
            { "detailMap", "0" }, { "detailMapUV", "0" }, { "diffuseMap", "0" }, { "diffuseMapUV", "0" },
            { "diffuseParallax", "0" }, { "disableNormalOffsetShadows", "0" }, { "emissiveMap", "0" },
            { "emissiveMapUV", "0" }, { "envMap", "0" }, { "envMapUV", "0" }, { "glossMap", "0" },
            { "glossMapUV", "0" }, { "hdrExposureTime", "0.500000" }, { "limitShadowMapDistance", "0" },
            { "normalMap", "0" }, { "normalMapUV", "0" }, { "parallax", "0" }, { "particleOcclusion", "0" },
            { "perspectiveShadowMaps", "0" }, { "rainRippleDetail", "0" }, { "reconstructNormalZ", "0" },
            { "rippleMapSize", "512.0" }, { "rippleMapWorldScale", "2.000000" }, { "shadowNormalOffset", "0.0" },
            { "shadow_texture_unit_list", "" }, { "shadows_enabled", "0" }, { "softParticles", "0" },
            { "specularMap", "0" }, { "specularMapUV", "0" }, { "sunlightScattering", "0" },
            { "useShadowDebugOverlay", "0" }, { "wobblyShores", "0" }, { "maxLights", "8" },
            { "lightingMethodClustered", "0" }, { "simpleLighting", "0" }, { "groundcoverFadeStart", "900.000000" },
            { "groundcoverFadeEnd", "1000.000000" }, { "groundcoverStompMode", "0" },
            { "groundcoverStompIntensity", "1" }, { "distorionRTRatio", "0.250000" } };
        ShaderManager::DefineMap on = off;
        for (auto& [key, value] : on)
            if (value == "0")
                value = "1";
        on["alphaFunc"] = "516";
        on["shadow_texture_unit_list"] = "0,1,2";
        on["shadowNormalOffset"] = "1.000000";
        on["rainRippleDetail"] = "2";
        on["groundcoverStompMode"] = "2";
        on["diffuseMapUV"] = "1";
        on["lightingMethodClustered"] = "0";
        on["simpleLighting"] = "0";
        on["useGPUShader4"] = "1";

        std::size_t converted = 0;
        for (const auto& [variant, defines] : { std::pair{ "off", off }, std::pair{ "on", on } })
        {
            for (const std::string reverseZ : { "0", "1" })
            {
                ShaderManager manager;
                manager.setShaderPath(root);
                manager.setConvertToGlslEs(true);
                ShaderManager::DefineMap globals = getDefaultDefines();
                globals["reverseZ"] = reverseZ;
                manager.setGlobalDefines(globals);
                for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "compatibility"))
                {
                    const std::string extension = entry.path().extension().string();
                    if (extension != ".vert" && extension != ".frag")
                        continue;
                    const std::string name = std::filesystem::relative(entry.path(), root).generic_string();
                    const osg::ref_ptr<osg::Shader> shader = manager.getShader(name, defines);
                    EXPECT_TRUE(shader) << name << " with options " << variant << ", reverse-z " << reverseZ;
                    if (!shader)
                        continue;
                    ++converted;
                    EXPECT_TRUE(openSceneGraphLeavesAlone(shader->getShaderSource()))
                        << name << " with options " << variant << ", reverse-z " << reverseZ;
                    if (dump)
                    {
                        std::string file = std::string(variant) + "_z" + reverseZ + "_" + name;
                        std::replace(file.begin(), file.end(), '/', '_');
                        std::ofstream(std::filesystem::path(dump) / file) << shader->getShaderSource();
                    }
                }
            }
        }
        EXPECT_GT(converted, 0);
    }
}
