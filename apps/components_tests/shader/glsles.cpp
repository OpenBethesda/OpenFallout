#include <components/shader/glsles.hpp>

#include <algorithm>
#include <regex>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{
    using namespace testing;
    using namespace Shader;

    GlslEsShader vertex(std::string_view source)
    {
        return convertToGlslEs(source, osg::Shader::VERTEX);
    }

    GlslEsShader fragment(std::string_view source)
    {
        return convertToGlslEs(source, osg::Shader::FRAGMENT);
    }

    bool contains(const std::string& text, const std::string& pattern)
    {
        return std::regex_search(text, std::regex(pattern));
    }

    std::size_t count(const std::string& text, const std::string& what)
    {
        std::size_t result = 0;
        for (std::size_t pos = text.find(what); pos != std::string::npos; pos = text.find(what, pos + 1))
            ++result;
        return result;
    }

    // The part of the converted text that came from the original, after the converter's own preamble
    std::string body(const std::string& converted)
    {
        const std::size_t start = converted.find("#line 1 0\n");
        return start == std::string::npos ? std::string() : converted.substr(start + 10);
    }

    TEST(ShaderGlslEsTest, converted_shader_starts_with_the_glsl_es_300_version)
    {
        const GlslEsShader result = vertex("#version 120\nvoid main() { gl_Position = vec4(0.0); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, StartsWith("#version 300 es\n"));
        EXPECT_EQ(count(result.mSource, "#version"), 1);
    }

    TEST(ShaderGlslEsTest, every_line_of_the_original_keeps_its_line_number)
    {
        const std::string source = "#version 120\n#extension GL_EXT_gpu_shader4: require\n\nvoid main()\n{\n}\n";
        const std::string converted = body(fragment(source).mSource);
        EXPECT_EQ(count(converted, "\n"), count(source, "\n"));
        EXPECT_THAT(converted, HasSubstr("\n\n\nvoid main()\n{\n}\n"));
    }

    TEST(ShaderGlslEsTest, extension_directives_are_removed)
    {
        const GlslEsShader result
            = fragment("#version 120\n#if 1\n    #extension GL_EXT_gpu_shader4: require\n#endif\n");
        EXPECT_THAT(result.mSource, Not(HasSubstr("#extension")));
    }

    TEST(ShaderGlslEsTest, vertex_attributes_and_varyings_become_inputs_and_outputs)
    {
        const GlslEsShader result = vertex(
            "#version 120\n"
            "attribute vec4 extra;\n"
            "varying vec2 uv;\n"
            "centroid varying vec4 passColor;\n"
            "void main() { uv = extra.xy; passColor = vec4(1.0); gl_Position = extra; }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, HasSubstr("\nin vec4 extra;\n"));
        EXPECT_THAT(result.mSource, HasSubstr("\nout vec2 uv;\n"));
        EXPECT_THAT(result.mSource, HasSubstr("\ncentroid out vec4 passColor;\n"));
        EXPECT_FALSE(contains(result.mSource, R"(\b(attribute|varying)\b)"));
    }

    TEST(ShaderGlslEsTest, fragment_varyings_become_inputs)
    {
        const GlslEsShader result
            = fragment("#version 120\nvarying vec2 uv;\ncentroid varying vec4 passColor;\nvoid main() { }\n");
        EXPECT_THAT(result.mSource, HasSubstr("\nin vec2 uv;\n"));
        EXPECT_THAT(result.mSource, HasSubstr("\ncentroid in vec4 passColor;\n"));
    }

    TEST(ShaderGlslEsTest, frag_color_is_written_to_a_declared_output)
    {
        const GlslEsShader result = fragment("#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, HasSubstr("layout(location = 0) out vec4 of_FragData[1];"));
        EXPECT_THAT(body(result.mSource), HasSubstr("of_FragData[0] = vec4(1.0);"));
        EXPECT_THAT(result.mSource, Not(HasSubstr("gl_FragColor")));
    }

    TEST(ShaderGlslEsTest, frag_data_outputs_cover_the_highest_buffer_written)
    {
        const GlslEsShader result = fragment(
            "#version 120\nvoid main() { gl_FragData[0] = vec4(1.0); gl_FragData[1].xyz = gl_FragData[0].rgb; }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, HasSubstr("layout(location = 0) out vec4 of_FragData[2];"));
        EXPECT_THAT(body(result.mSource), HasSubstr("of_FragData[1].xyz = of_FragData[0].rgb;"));
    }

    TEST(ShaderGlslEsTest, frag_data_with_an_index_that_is_not_a_number_is_an_error)
    {
        const GlslEsShader result = fragment("#version 120\nvoid main() { int i = 0; gl_FragData[i] = vec4(1.0); }\n");
        EXPECT_THAT(result.mErrors, Contains(HasSubstr("gl_FragData")));
    }

    TEST(ShaderGlslEsTest, fragment_without_colour_output_declares_none)
    {
        const GlslEsShader result = fragment("#version 120\nvoid main() { }\n");
        EXPECT_THAT(result.mSource, Not(HasSubstr("of_FragData")));
    }

    TEST(ShaderGlslEsTest, desktop_texture_functions_become_the_overloaded_ones)
    {
        const GlslEsShader result = fragment(
            "#version 120\n"
            "uniform sampler2D a;\nuniform samplerCube b;\nuniform sampler2DArray c;\nuniform sampler3D d;\n"
            "void main() { gl_FragColor = texture2D(a, vec2(0.0)) + texture2DLod(a, vec2(0.0), 1.0)"
            " + textureCube(b, vec3(0.0)) + texture2DArray(c, vec3(0.0)) + texture3D(d, vec3(0.0))"
            " + texture2DProj(a, vec3(1.0)) + vec4(vec2(textureSize2D(a, 0)), 0.0, 0.0); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        const std::string converted = body(result.mSource);
        EXPECT_THAT(converted, HasSubstr("texture(a, vec2(0.0)) + textureLod(a, vec2(0.0), 1.0)"));
        EXPECT_THAT(converted, HasSubstr("texture(b, vec3(0.0)) + texture(c, vec3(0.0)) + texture(d, vec3(0.0))"));
        EXPECT_THAT(converted, HasSubstr("textureProj(a, vec3(1.0)) + vec4(vec2(textureSize(a, 0))"));
    }

    TEST(ShaderGlslEsTest, shadow_lookups_keep_their_vec4_result)
    {
        const GlslEsShader result = fragment(
            "#version 120\nuniform sampler2DShadow s;\n"
            "void main() { gl_FragColor = vec4(shadow2DProj(s, vec4(1.0)).r + shadow2D(s, vec3(1.0)).r); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource,
            HasSubstr("vec4 shadow2DProj(sampler2DShadow s, vec4 c) { return vec4(textureProj(s, c)); }"));
        EXPECT_THAT(
            result.mSource, HasSubstr("vec4 shadow2D(sampler2DShadow s, vec3 c) { return vec4(texture(s, c)); }"));
    }

    TEST(ShaderGlslEsTest, every_stage_declares_precision_for_floats_ints_and_samplers)
    {
        for (const GlslEsShader& result :
            { vertex("#version 120\nvoid main() {}\n"), fragment("#version 120\nvoid main() {}\n") })
        {
            EXPECT_THAT(result.mSource, HasSubstr("precision highp float;"));
            EXPECT_THAT(result.mSource, HasSubstr("precision highp int;"));
            for (const char* sampler : { "sampler2D", "sampler3D", "samplerCube", "sampler2DShadow", "sampler2DArray" })
                EXPECT_THAT(result.mSource, HasSubstr(std::string("precision highp ") + sampler + ";"));
        }
    }

    TEST(ShaderGlslEsTest, built_in_matrices_become_the_uniforms_openscenegraph_sets)
    {
        const GlslEsShader result = vertex(
            "#version 120\n"
            "void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_ProjectionMatrix * gl_ModelViewMatrix"
            " * vec4(gl_NormalMatrix * vec3(1.0), 1.0); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, HasSubstr("uniform mat4 osg_ModelViewProjectionMatrix;"));
        EXPECT_THAT(result.mSource, HasSubstr("uniform mat4 osg_ProjectionMatrix;"));
        EXPECT_THAT(result.mSource, HasSubstr("uniform mat4 osg_ModelViewMatrix;"));
        EXPECT_THAT(result.mSource, HasSubstr("uniform mat3 osg_NormalMatrix;"));
        EXPECT_THAT(body(result.mSource),
            HasSubstr(
                "osg_ModelViewProjectionMatrix * osg_ProjectionMatrix * osg_ModelViewMatrix * vec4(osg_NormalMatrix"));
    }

    TEST(ShaderGlslEsTest, inverse_model_view_matrix_is_computed)
    {
        const GlslEsShader result
            = fragment("#version 120\nvoid main() { gl_FragColor = gl_ModelViewMatrixInverse * vec4(1.0); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, HasSubstr("uniform mat4 osg_ModelViewMatrix;"));
        EXPECT_THAT(body(result.mSource), HasSubstr("inverse(osg_ModelViewMatrix) * vec4(1.0)"));
    }

    TEST(ShaderGlslEsTest, built_in_vertex_attributes_become_the_inputs_openscenegraph_binds)
    {
        const GlslEsShader result = vertex(
            "#version 120\n"
            "void main() { gl_Position = gl_Vertex + gl_Color + gl_MultiTexCoord3 + vec4(gl_Normal, 0.0); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, HasSubstr("in vec4 osg_Vertex;"));
        EXPECT_THAT(result.mSource, HasSubstr("in vec3 osg_Normal;"));
        EXPECT_THAT(result.mSource, HasSubstr("in vec4 osg_Color;"));
        EXPECT_THAT(result.mSource, HasSubstr("in vec4 osg_MultiTexCoord3;"));
        EXPECT_THAT(result.mSource, Not(HasSubstr("osg_MultiTexCoord0")));
        EXPECT_THAT(
            body(result.mSource), HasSubstr("osg_Vertex + osg_Color + osg_MultiTexCoord3 + vec4(osg_Normal, 0.0)"));
    }

    TEST(ShaderGlslEsTest, uniform_initialisers_are_removed_and_returned)
    {
        const GlslEsShader result = vertex(
            "#version 120\n"
            "uniform vec2 scaling = vec2(1.0, 1.0);\n"
            "uniform bool alphaTest = true;\n"
            "uniform float plain;\n"
            "void main() { }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, HasSubstr("\nuniform vec2 scaling;\n"));
        EXPECT_THAT(result.mSource, HasSubstr("\nuniform bool alphaTest;\n"));
        ASSERT_EQ(result.mUniformInitialisers.size(), 2);
        EXPECT_EQ(result.mUniformInitialisers[0].mType, "vec2");
        EXPECT_EQ(result.mUniformInitialisers[0].mName, "scaling");
        EXPECT_EQ(result.mUniformInitialisers[0].mValue, "vec2(1.0, 1.0)");
        EXPECT_EQ(result.mUniformInitialisers[1].mType, "bool");
        EXPECT_EQ(result.mUniformInitialisers[1].mName, "alphaTest");
        EXPECT_EQ(result.mUniformInitialisers[1].mValue, "true");
    }

    TEST(ShaderGlslEsTest, an_initialiser_inside_a_comment_is_not_taken)
    {
        const GlslEsShader result = vertex(
            "#version 120\n"
            "/* uniform float a = 1.0;\n"
            "uniform float b = 2.0; */\n"
            "// uniform float c = 3.0;\n"
            "uniform float a; /* = 4.0; */\n"
            "void main() { }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mUniformInitialisers, IsEmpty());
    }

    TEST(ShaderGlslEsTest, an_initialiser_inside_an_inactive_branch_is_not_taken)
    {
        const GlslEsShader result = vertex(
            "#version 120\n"
            "#define LEVEL 2\n"
            "#if 0\n"
            "uniform float a = 1.0;\n"
            "#elif LEVEL > 1 && defined(LEVEL)\n"
            "uniform float a = 2.0;\n"
            "#else\n"
            "uniform float a = 3.0;\n"
            "#endif\n"
            "#ifdef MISSING\n"
            "uniform bool b = true;\n"
            "#else\n"
            "  #ifndef LEVEL\n"
            "uniform bool b = true;\n"
            "  #endif\n"
            "uniform bool b = false;\n"
            "#endif\n"
            "void main() { }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        ASSERT_EQ(result.mUniformInitialisers.size(), 2);
        EXPECT_EQ(result.mUniformInitialisers[0].mName, "a");
        EXPECT_EQ(result.mUniformInitialisers[0].mValue, "2.0");
        EXPECT_EQ(result.mUniformInitialisers[1].mName, "b");
        EXPECT_EQ(result.mUniformInitialisers[1].mValue, "false");
    }

    // A macro is replaced by its tokens, not by its value: 1 + 2 * 3 == 7
    TEST(ShaderGlslEsTest, a_macro_in_a_condition_is_replaced_by_its_text)
    {
        const GlslEsShader result = vertex(
            "#version 120\n"
            "#define N 1 + 2\n"
            "#if N * 3 == 7\n"
            "uniform float a = 1.0;\n"
            "#else\n"
            "uniform float a = 2.0;\n"
            "#endif\n"
            "void main() { }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        ASSERT_EQ(result.mUniformInitialisers.size(), 1);
        EXPECT_EQ(result.mUniformInitialisers[0].mValue, "1.0");
    }

    TEST(ShaderGlslEsTest, an_initialiser_folded_in_twice_is_taken_once)
    {
        const GlslEsShader result = vertex(foldLinkedShaders("#version 120\nuniform bool b = true;\nvoid main() { }\n",
            { "#version 120\nuniform bool b = true;\nvoid foo() { }\n" }));
        EXPECT_THAT(result.mErrors, IsEmpty());
        ASSERT_EQ(result.mUniformInitialisers.size(), 1);
        EXPECT_EQ(result.mUniformInitialisers[0].mValue, "true");
    }

    TEST(ShaderGlslEsTest, one_uniform_with_two_initial_values_is_an_error)
    {
        const GlslEsShader result
            = vertex("#version 120\nuniform float a = 1.0;\nuniform float a = 2.0;\nvoid main() { }\n");
        EXPECT_THAT(result.mErrors, Contains(AllOf(HasSubstr("a"), HasSubstr("1.0"), HasSubstr("2.0"))));
    }

    TEST(ShaderGlslEsTest, an_initialiser_in_a_branch_the_converter_cannot_decide_is_an_error)
    {
        const GlslEsShader result = vertex(
            "#version 120\n#if defined(GL_EXT_gpu_shader4)\nuniform float a = 1.0;\n#endif\nvoid main() { }\n");
        EXPECT_THAT(result.mErrors, Contains(HasSubstr("uniform float a = 1.0;")));
    }

    TEST(ShaderGlslEsTest, clip_vertex_becomes_clip_distances_against_the_clip_plane_uniforms)
    {
        const GlslEsShader result = vertex(
            "#version 120\nvoid main() { gl_ClipVertex = vec4(1.0); gl_Position = vec4(1.0); }\nvoid domain() { }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mDropped, IsEmpty());
        EXPECT_THAT(result.mSource, Not(HasSubstr("gl_ClipVertex")));
        // The extension is asked for before any declaration, and only when the context has it
        EXPECT_THAT(result.mSource,
            StartsWith("#version 300 es\n#ifdef GL_ANGLE_clip_cull_distance\n"
                       "#extension GL_ANGLE_clip_cull_distance : enable\n#endif\n"));
        ASSERT_EQ(sClipPlaneCount, 2);
        for (unsigned int i = 0; i < sClipPlaneCount; ++i)
        {
            EXPECT_THAT(result.mSource, HasSubstr("uniform vec4 " + clipPlaneUniformName(i) + ";"));
            EXPECT_THAT(result.mSource,
                HasSubstr("gl_ClipDistance[" + std::to_string(i) + "] = dot(of_ClipVertex, " + clipPlaneUniformName(i)
                    + ");"));
        }
        // The shader's own main runs first; the distances follow from the eye-space position it wrote
        EXPECT_THAT(body(result.mSource), HasSubstr("void of_main() { of_ClipVertex = vec4(1.0);"));
        EXPECT_THAT(body(result.mSource), HasSubstr("void domain() { }"));
        EXPECT_LT(result.mSource.find("void of_main()"), result.mSource.find("void main()"));
        EXPECT_THAT(result.mSource, HasSubstr("void main()\n{\n    of_main();\n"));
    }

    TEST(ShaderGlslEsTest, shaders_without_clip_vertex_write_no_clip_distance)
    {
        for (const GlslEsShader& result : { vertex("#version 120\nvoid main() { gl_Position = vec4(1.0); }\n"),
                 fragment("#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n") })
        {
            EXPECT_THAT(result.mErrors, IsEmpty());
            EXPECT_THAT(result.mSource, Not(HasSubstr("gl_ClipDistance")));
            EXPECT_THAT(result.mSource, Not(HasSubstr("clip_cull_distance")));
            EXPECT_THAT(result.mSource, Not(HasSubstr("of_main")));
        }
    }

    TEST(ShaderGlslEsTest, clip_plane_uniforms_have_one_name_each)
    {
        EXPECT_EQ(clipPlaneUniformName(0), "of_ClipPlane0");
        EXPECT_EQ(clipPlaneUniformName(1), "of_ClipPlane1");
    }

    TEST(ShaderGlslEsTest, built_ins_glsl_es_keeps_are_left_alone)
    {
        const GlslEsShader result = fragment(
            "#version 120\nvoid main() { if (gl_FrontFacing) gl_FragColor = gl_FragCoord; gl_FragDepth = 0.0; }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(
            body(result.mSource), HasSubstr("if (gl_FrontFacing) of_FragData[0] = gl_FragCoord; gl_FragDepth = 0.0;"));
    }

    // A post-processing pass shares one header between its stages, with lines like these that only one stage expands.
    // A vertex attribute there takes the vertex stage's name in both stages: OpenSceneGraph's own pass over the text
    // (which the engine's copy runs on every stage) would otherwise declare a vertex input for it in a fragment shader,
    // before the default precision, which GLSL ES refuses
    TEST(ShaderGlslEsTest, a_define_naming_a_built_in_of_the_other_stage_is_left_alone)
    {
        const GlslEsShader vertexResult = vertex(
            "#version 120\n#define omw_FragColor gl_FragColor\n#define omw_Vertex gl_Vertex\n"
            "void main() { gl_Position = omw_Vertex; }\n");
        EXPECT_THAT(vertexResult.mErrors, IsEmpty());
        EXPECT_THAT(vertexResult.mSource, HasSubstr("#define omw_FragColor gl_FragColor\n"));
        EXPECT_THAT(vertexResult.mSource, HasSubstr("#define omw_Vertex osg_Vertex\n"));
        EXPECT_THAT(vertexResult.mSource, HasSubstr("in vec4 osg_Vertex;"));
        EXPECT_THAT(vertexResult.mSource, Not(HasSubstr("of_FragData")));

        const GlslEsShader fragmentResult = fragment(
            "#version 120\n#define omw_FragColor gl_FragColor\n#define omw_Vertex gl_Vertex\n"
            "void main() { omw_FragColor = vec4(1.0); }\n");
        EXPECT_THAT(fragmentResult.mErrors, IsEmpty());
        EXPECT_THAT(fragmentResult.mSource, HasSubstr("#define omw_FragColor of_FragData[0]\n"));
        EXPECT_THAT(fragmentResult.mSource, HasSubstr("layout(location = 0) out vec4 of_FragData[1];"));
        EXPECT_THAT(fragmentResult.mSource, HasSubstr("#define omw_Vertex osg_Vertex\n"));
        EXPECT_THAT(fragmentResult.mSource, Not(HasSubstr("gl_Vertex")));
        EXPECT_THAT(fragmentResult.mSource, Not(HasSubstr("in vec4 osg_Vertex")));
    }

    // A comment is one space to the preprocessor, even one over several lines: the #define goes on after it
    TEST(ShaderGlslEsTest, a_define_goes_on_past_a_comment_over_several_lines)
    {
        const GlslEsShader result = fragment(
            "#version 120\n"
            "#define omw_Position(x) \\\n"
            "    /* a comment\n"
            "       on two lines */ \\\n"
            "    (x + gl_Vertex)\n"
            "void main() { gl_FragColor = vec4(1.0); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, Not(HasSubstr("in vec4 osg_Vertex")));
        // The comment's line breaks are kept (the line numbers stay right), each continuing the #define
        const std::size_t start = result.mSource.find("#define omw_Position(x)");
        const std::size_t last = result.mSource.find("(x + osg_Vertex)\n");
        ASSERT_NE(start, std::string::npos);
        ASSERT_NE(last, std::string::npos);
        const std::string defineLines = result.mSource.substr(start, last - start);
        EXPECT_EQ(count(defineLines, "\n"), 3u);
        EXPECT_EQ(count(defineLines, "\\\n"), 3u);
    }

    TEST(ShaderGlslEsTest, a_built_in_of_the_other_stage_outside_a_define_is_an_error)
    {
        EXPECT_THAT(fragment("#version 120\nvoid main() { gl_FragColor = gl_Vertex; }\n").mErrors,
            Contains(HasSubstr("gl_Vertex")));
        EXPECT_THAT(vertex("#version 120\nvoid main() { gl_FragColor = vec4(1.0); }\n").mErrors,
            Contains(HasSubstr("gl_FragColor")));
    }

    // GLSL 1.20 lets a global variable start from a uniform; GLSL ES 3.00 takes only constant initialisers there
    TEST(ShaderGlslEsTest, global_initialisers_that_read_uniforms_move_to_the_start_of_main)
    {
        const GlslEsShader result = fragment(
            "#version 120\n"
            "uniform float t;\n"
            "float time = fract(t);\n"
            "const float k = 2.0;\n"
            "vec2 v = vec2(1.0);\n"
            "float twice = time * k;\n"
            "void main() { gl_FragColor = vec4(twice); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, HasSubstr("\nvoid of_initialiseGlobals();\n#line 1 0\n"));
        const std::string text = body(result.mSource);
        EXPECT_THAT(text, HasSubstr("\nfloat time;\n#define OF_GLOBAL_INIT_0\n#line 4 0\n"));
        EXPECT_THAT(text, HasSubstr("\nconst float k = 2.0;\nvec2 v = vec2(1.0);\n"));
        EXPECT_THAT(text, HasSubstr("\nfloat twice;\n#define OF_GLOBAL_INIT_1\n#line 7 0\n"));
        EXPECT_THAT(text, HasSubstr("void main() {of_initialiseGlobals(); of_FragData[0] = vec4(twice); }\n"));
        EXPECT_THAT(text,
            HasSubstr("void of_initialiseGlobals()\n{\n#ifdef OF_GLOBAL_INIT_0\n#line 3 0\n    time = fract(t);\n"
                      "#endif\n#ifdef OF_GLOBAL_INIT_1\n#line 6 0\n    twice = time * k;\n#endif\n}\n"));
    }

    TEST(ShaderGlslEsTest, a_global_initialiser_moved_to_main_keeps_its_preprocessor_branch)
    {
        const GlslEsShader result = vertex(
            "#version 120\n"
            "uniform float t;\n"
            "#if A\n"
            "float x = t;\n"
            "#else\n"
            "float x = 2.0 * t;\n"
            "#endif\n"
            "void main()\n"
            "{\n"
            "    gl_Position = vec4(x);\n"
            "}\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        const std::string text = body(result.mSource);
        EXPECT_THAT(text,
            HasSubstr("#if A\nfloat x;\n#define OF_GLOBAL_INIT_0\n#line 5 0\n#else\nfloat x;\n"
                      "#define OF_GLOBAL_INIT_1\n#line 7 0\n#endif\n"));
        EXPECT_THAT(text, HasSubstr("void main()\n{of_initialiseGlobals();\n    gl_Position"));
        EXPECT_THAT(text,
            HasSubstr("#ifdef OF_GLOBAL_INIT_0\n#line 4 0\n    x = t;\n#endif\n#ifdef OF_GLOBAL_INIT_1\n"
                      "#line 6 0\n    x = 2.0 * t;\n#endif\n"));
    }

    // A linked shader is folded in after main, and so are its globals
    TEST(ShaderGlslEsTest, a_global_initialiser_after_main_is_moved_too)
    {
        const GlslEsShader result = fragment(
            "#version 120\n"
            "float level();\n"
            "void main() { gl_FragColor = vec4(level()); }\n"
            "#line 1 100\n"
            "uniform float t;\n"
            "float stored = t;\n"
            "float level() { return stored; }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        const std::string text = body(result.mSource);
        EXPECT_THAT(text, HasSubstr("\nfloat stored;\n#define OF_GLOBAL_INIT_0\n#line 3 100\n"));
        EXPECT_THAT(text, HasSubstr("#ifdef OF_GLOBAL_INIT_0\n#line 2 100\n    stored = t;\n#endif\n"));
        EXPECT_LT(text.find("float level() {"), text.find("void of_initialiseGlobals()\n{"));
    }

    TEST(ShaderGlslEsTest, shaders_with_constant_globals_only_are_left_as_they_are)
    {
        const GlslEsShader result = fragment(
            "#version 120\nconst float k = 2.0;\nfloat a = k * 2.0, b = 1.0;\nvec3 c = vec3(sin(k));\n"
            "void main() { gl_FragColor = vec4(c, a + b); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(result.mSource, Not(HasSubstr("of_initialiseGlobals")));
        EXPECT_THAT(result.mSource, HasSubstr("\nfloat a = k * 2.0, b = 1.0;\nvec3 c = vec3(sin(k));\n"));
    }

    TEST(ShaderGlslEsTest, names_glsl_es_reserves_are_renamed)
    {
        const GlslEsShader result = fragment(
            "#version 120\n"
            "uniform sampler2D tex;\n"
            "vec3 filter(vec3 sample) { return sample * 2.0; }\n"
            "void main() { vec3 sample = texture2D(tex, vec2(0.0)).rgb; gl_FragColor = vec4(filter(sample), 1.0); }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        const std::string text = body(result.mSource);
        EXPECT_THAT(text, HasSubstr("vec3 of_filter(vec3 of_sample) { return of_sample * 2.0; }"));
        EXPECT_THAT(text, HasSubstr("vec3 of_sample = texture(tex, vec2(0.0)).rgb;"));
        EXPECT_THAT(text, HasSubstr("vec4(of_filter(of_sample), 1.0)"));
    }

    // A uniform is set by its name from outside the shader: renaming it would lose its value
    TEST(ShaderGlslEsTest, a_uniform_with_a_name_glsl_es_reserves_is_an_error)
    {
        const GlslEsShader result = fragment(
            "#version 120\nuniform sampler2D texture;\nvoid main() { gl_FragColor = texture2D(texture, vec2(0.0)); "
            "}\n");
        EXPECT_THAT(result.mErrors, Contains(HasSubstr("texture")));
    }

    // Each name of a declaration of several uniforms is set from outside the shader
    TEST(ShaderGlslEsTest, every_uniform_of_a_declaration_of_several_is_checked)
    {
        EXPECT_THAT(
            fragment("#version 120\nuniform float gain, filter;\nvoid main() { gl_FragColor = vec4(gain); }\n").mErrors,
            Contains(HasSubstr("uniform filter")));
        EXPECT_THAT(
            fragment("#version 120\nuniform vec2 offsets[2], texture;\nvoid main() { gl_FragColor = vec4(0.0); }\n")
                .mErrors,
            Contains(HasSubstr("uniform texture")));
        EXPECT_THAT(fragment("#version 120\nuniform float gain, level[2], scale;\n"
                             "void main() { gl_FragColor = vec4(gain * scale); }\n")
                        .mErrors,
            IsEmpty());
    }

    TEST(ShaderGlslEsTest, multiview_view_index_keeps_its_name)
    {
        const GlslEsShader result = fragment(
            "#version 120\n#if OMW_MULTIVIEW\nint view() { return int(gl_ViewID_OVR); }\n#endif\nvoid main() { }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(body(result.mSource), HasSubstr("int(gl_ViewID_OVR)"));
    }

    TEST(ShaderGlslEsTest, desktop_built_ins_without_an_equivalent_are_errors)
    {
        const GlslEsShader result
            = fragment("#version 120\nvoid main() { gl_FragColor = gl_LightSource[0].diffuse; }\n");
        EXPECT_THAT(result.mErrors, Contains(HasSubstr("gl_LightSource")));
    }

    TEST(ShaderGlslEsTest, desktop_built_ins_in_a_branch_the_compiler_skips_are_not_errors)
    {
        const GlslEsShader result = fragment(
            "#version 120\n"
            "void main()\n"
            "{\n"
            "#if 0\n"
            "    gl_FragColor = gl_LightSource[0].diffuse;\n"
            "#else\n"
            "    gl_FragColor = vec4(1.0);\n"
            "#endif\n"
            "}\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(body(result.mSource), HasSubstr("gl_LightSource"));
    }

    TEST(ShaderGlslEsTest, comments_are_dropped_with_their_line_breaks_kept)
    {
        const GlslEsShader result = fragment(
            "#version 120\n// gl_LightSource and varying\n/* attribute\n gl_FragColor */\nvoid main() { }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_EQ(body(result.mSource), "\n\n\n\nvoid main() { }\n");
        EXPECT_THAT(result.mSource, Not(HasSubstr("of_FragData")));
    }

    // A comment between two words stands for a space, as in GLSL
    TEST(ShaderGlslEsTest, a_comment_between_two_words_keeps_them_apart)
    {
        const GlslEsShader result = fragment("#version 120\nuniform float/* explanation */gain;\nvoid main() { }\n");
        EXPECT_THAT(result.mErrors, IsEmpty());
        EXPECT_THAT(body(result.mSource), HasSubstr("uniform float gain;"));
    }

    TEST(ShaderGlslEsTest, shaders_written_for_a_newer_glsl_are_errors)
    {
        const GlslEsShader result = vertex("#version 430 compatibility\nvoid main() { }\n");
        EXPECT_THAT(result.mErrors, Contains(HasSubstr("430")));
    }

    TEST(ShaderGlslEsTest, only_vertex_and_fragment_shaders_are_converted)
    {
        EXPECT_THAT(convertToGlslEs("#version 120\n", osg::Shader::COMPUTE).mErrors, Not(IsEmpty()));
    }

    TEST(ShaderGlslEsTest, folding_appends_each_linked_shader_once)
    {
        const std::string folded = foldLinkedShaders(
            "#version 120\nvoid foo();\nvoid main() { foo(); }\n", { "#version 120\nvoid foo() { }\n" });
        EXPECT_THAT(folded, HasSubstr("void main() { foo(); }\n"));
        EXPECT_THAT(folded, HasSubstr("void foo() { }\n"));
        EXPECT_LT(folded.find("void main()"), folded.find("void foo() { }"));
    }

    TEST(ShaderGlslEsTest, folding_gives_linked_shaders_their_own_source_numbers)
    {
        const std::string folded = foldLinkedShaders(
            "void main() { }\n", { "#line 0 1\nvoid a() { }\n#line 3 0\nvoid b() { }\n", "void c() { }\n" });
        EXPECT_THAT(folded, HasSubstr("#line 1 100\n#line 0 101\nvoid a() { }\n#line 3 100\nvoid b() { }\n"));
        EXPECT_THAT(folded, HasSubstr("#line 1 200\nvoid c() { }\n"));
    }

    TEST(ShaderGlslEsTest, folding_guards_declarations_made_more_than_once)
    {
        const std::string folded
            = foldLinkedShaders("#if 0\nuniform vec2 screenRes;\n#endif\nvarying vec3 pos;\nuniform float once;\n",
                { "uniform vec2 screenRes;\nvarying vec3 pos;\n" });
        EXPECT_EQ(count(folded, "#ifndef OF_DECLARED_screenRes\n#define OF_DECLARED_screenRes\n"), 2);
        EXPECT_EQ(count(folded, "#ifndef OF_DECLARED_pos\n#define OF_DECLARED_pos\n"), 2);
        EXPECT_THAT(folded, Not(HasSubstr("OF_DECLARED_once")));
        EXPECT_EQ(count(folded, "uniform vec2 screenRes;"), 2);
    }

    TEST(ShaderGlslEsTest, folding_guards_keep_line_numbers)
    {
        const std::string folded = foldLinkedShaders(
            "uniform vec2 screenRes;\nvoid main() { }\n", { "uniform vec2 screenRes;\nvoid foo() { }\n" });
        EXPECT_THAT(folded, HasSubstr("uniform vec2 screenRes;\n#endif\n#line 2 0\nvoid main() { }\n"));
        EXPECT_THAT(folded, HasSubstr("uniform vec2 screenRes;\n#endif\n#line 2 100\nvoid foo() { }\n"));
    }

    TEST(ShaderGlslEsTest, initialisers_become_uniforms_with_their_values)
    {
        bool flag = false;
        ASSERT_TRUE(makeUniform({ "bool", "a", "true" }));
        EXPECT_TRUE(makeUniform({ "bool", "a", "true" })->get(flag));
        EXPECT_TRUE(flag);
        EXPECT_EQ(makeUniform({ "bool", "a", "true" })->getName(), "a");

        float number = 0;
        EXPECT_TRUE(makeUniform({ "float", "b", "2.5" })->get(number));
        EXPECT_EQ(number, 2.5f);

        int whole = 0;
        EXPECT_TRUE(makeUniform({ "int", "c", "-3" })->get(whole));
        EXPECT_EQ(whole, -3);

        osg::Vec2f vec2;
        EXPECT_TRUE(makeUniform({ "vec2", "d", "vec2(1.0, 0.5)" })->get(vec2));
        EXPECT_EQ(vec2, osg::Vec2f(1.f, 0.5f));

        osg::Vec3f vec3;
        EXPECT_TRUE(makeUniform({ "vec3", "e", "vec3(2.0)" })->get(vec3));
        EXPECT_EQ(vec3, osg::Vec3f(2.f, 2.f, 2.f));

        osg::Vec4f vec4;
        EXPECT_TRUE(makeUniform({ "vec4", "f", "vec4(1, 2.0, 3.0, 4.0)" })->get(vec4));
        EXPECT_EQ(vec4, osg::Vec4f(1.f, 2.f, 3.f, 4.f));
    }

    TEST(ShaderGlslEsTest, initialisers_that_are_not_literals_are_not_read)
    {
        EXPECT_FALSE(makeUniform({ "float", "a", "someConstant * 2.0" }));
        EXPECT_FALSE(makeUniform({ "mat4", "b", "mat4(1.0)" }));
        EXPECT_FALSE(makeUniform({ "vec2", "c", "vec2(1.0, 2.0, 3.0)" }));
    }
}
