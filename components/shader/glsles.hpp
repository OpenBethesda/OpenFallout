#ifndef OPENFALLOUT_COMPONENTS_SHADER_GLSLES_H
#define OPENFALLOUT_COMPONENTS_SHADER_GLSLES_H

#include <string>
#include <string_view>
#include <vector>

#include <osg/Shader>
#include <osg/Uniform>
#include <osg/ref_ptr>

namespace Shader
{
    // The shaders are written in GLSL 1.20 for desktop OpenGL. The Metal build compiles them as GLSL ES 3.00, the only
    // dialect OpenGL ES 3.0 takes, after the text conversion below. It is text in, text out: no graphics calls.
    //
    // The rules live here and not in a data file shipped with the shaders: they are pinned by unit tests, they need
    // more than search and replace (the outputs and inputs a shader uses decide what is declared), and nobody but the
    // engine edits them. When the OpenGL build is deleted the converted text becomes the shader source and this file
    // goes.

    // GLSL ES has no uniform initialisers (`uniform bool x = true;`). The converter removes them and returns them so
    // that the program can set the value itself when it is linked.
    struct UniformInitialiser
    {
        std::string mType;
        std::string mName;
        std::string mValue;
    };

    struct GlslEsShader
    {
        std::string mSource;
        std::vector<UniformInitialiser> mUniformInitialisers;
        // Desktop features the converted shader goes without (for example user clip planes); the shader still compiles.
        std::vector<std::string> mDropped;
        // What could not be converted. The source is not usable when this is not empty.
        std::vector<std::string> mErrors;
    };

    // Converts the finished text of one vertex or fragment shader (templates resolved, linked shaders folded in).
    GlslEsShader convertToGlslEs(std::string_view source, osg::Shader::Type stage);

    // OpenGL ES links one shader object per stage. Appends the finished text of each library shader the main shader
    // links, renumbers their #line source numbers so that compiler messages still name the right file, and guards
    // every uniform, varying or attribute declared more than once so that the first declaration the preprocessor
    // keeps wins.
    std::string foldLinkedShaders(std::string_view main, const std::vector<std::string>& linked);

    // The uniform an initialiser stands for, with its value; nullptr if the type or the value is not one this reads
    // (bool, int, float and their vectors, written as literals).
    osg::ref_ptr<osg::Uniform> makeUniform(const UniformInitialiser& initialiser);
}

#endif
