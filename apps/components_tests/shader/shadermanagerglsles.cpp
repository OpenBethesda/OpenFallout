#include <components/files/conversion.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/testing/util.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{
    using namespace testing;
    using namespace Shader;

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
