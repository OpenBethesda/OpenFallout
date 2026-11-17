#include "glsles.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <map>
#include <optional>
#include <regex>
#include <set>

#include <components/misc/strings/conversion.hpp>

namespace Shader
{
    namespace
    {
        bool isIdentifierStart(char c)
        {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
        }

        bool isIdentifierPart(char c)
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        // Built-in matrices of desktop GLSL, as the uniforms OpenSceneGraph's OpenGL ES profile sets on every draw
        struct BuiltIn
        {
            std::string_view mDesktopName;
            std::string_view mName;
            std::string_view mType;
        };

        constexpr std::array sMatrices = {
            BuiltIn{ "gl_ModelViewMatrix", "osg_ModelViewMatrix", "mat4" },
            BuiltIn{ "gl_ProjectionMatrix", "osg_ProjectionMatrix", "mat4" },
            BuiltIn{ "gl_ModelViewProjectionMatrix", "osg_ModelViewProjectionMatrix", "mat4" },
            BuiltIn{ "gl_NormalMatrix", "osg_NormalMatrix", "mat3" },
        };

        // Built-in vertex attributes of desktop GLSL, as the inputs OpenSceneGraph binds when it aliases them
        constexpr std::array sAttributes = {
            BuiltIn{ "gl_Vertex", "osg_Vertex", "vec4" },
            BuiltIn{ "gl_Normal", "osg_Normal", "vec3" },
            BuiltIn{ "gl_Color", "osg_Color", "vec4" },
            BuiltIn{ "gl_SecondaryColor", "osg_SecondaryColor", "vec4" },
            BuiltIn{ "gl_FogCoord", "osg_FogCoord", "float" },
            BuiltIn{ "gl_MultiTexCoord0", "osg_MultiTexCoord0", "vec4" },
            BuiltIn{ "gl_MultiTexCoord1", "osg_MultiTexCoord1", "vec4" },
            BuiltIn{ "gl_MultiTexCoord2", "osg_MultiTexCoord2", "vec4" },
            BuiltIn{ "gl_MultiTexCoord3", "osg_MultiTexCoord3", "vec4" },
            BuiltIn{ "gl_MultiTexCoord4", "osg_MultiTexCoord4", "vec4" },
            BuiltIn{ "gl_MultiTexCoord5", "osg_MultiTexCoord5", "vec4" },
            BuiltIn{ "gl_MultiTexCoord6", "osg_MultiTexCoord6", "vec4" },
            BuiltIn{ "gl_MultiTexCoord7", "osg_MultiTexCoord7", "vec4" },
        };

        // Texture functions of GLSL 1.20 (and of GL_EXT_gpu_shader4 / GL_EXT_texture_array) and their GLSL ES 3.00
        // names
        const std::map<std::string_view, std::string_view> sTextureFunctions = {
            { "texture2D", "texture" },
            { "texture3D", "texture" },
            { "textureCube", "texture" },
            { "texture2DArray", "texture" },
            { "texture2DLod", "textureLod" },
            { "texture3DLod", "textureLod" },
            { "textureCubeLod", "textureLod" },
            { "texture2DArrayLod", "textureLod" },
            { "texture2DProj", "textureProj" },
            { "texture3DProj", "textureProj" },
            { "texture2DProjLod", "textureProjLod" },
            { "texture2DGrad", "textureGrad" },
            { "textureSize2D", "textureSize" },
            { "texelFetch2D", "texelFetch" },
        };

        // shadow2D and shadow2DProj return a vec4 in GLSL 1.20; their GLSL ES 3.00 forms return a float
        struct Helper
        {
            std::string_view mName;
            std::string_view mDefinition;
        };

        constexpr std::array sHelpers = {
            Helper{ "shadow2D", "vec4 shadow2D(sampler2DShadow s, vec3 c) { return vec4(texture(s, c)); }" },
            Helper{
                "shadow2DProj", "vec4 shadow2DProj(sampler2DShadow s, vec4 c) { return vec4(textureProj(s, c)); }" },
        };

        // Built-ins that GLSL ES 3.00 has under the same name
        const std::set<std::string_view> sKeptBuiltIns = {
            "gl_Position",
            "gl_PointSize",
            "gl_VertexID",
            "gl_InstanceID",
            "gl_FragCoord",
            "gl_FrontFacing",
            "gl_PointCoord",
            "gl_FragDepth",
            "gl_DepthRange",
            "gl_MaxVertexAttribs",
            "gl_MaxVertexUniformVectors",
            "gl_MaxVertexOutputVectors",
            "gl_MaxFragmentInputVectors",
            "gl_MaxVertexTextureImageUnits",
            "gl_MaxCombinedTextureImageUnits",
            "gl_MaxTextureImageUnits",
            "gl_MaxFragmentUniformVectors",
            "gl_MaxDrawBuffers",
            "gl_MinProgramTexelOffset",
            "gl_MaxProgramTexelOffset",
        };

        constexpr std::array sSamplerTypes = { "sampler2D", "sampler3D", "samplerCube", "sampler2DShadow",
            "sampler2DArray", "sampler2DArrayShadow", "samplerCubeShadow" };

        constexpr std::string_view sClipVertex = "of_ClipVertex";
        constexpr std::string_view sFragData = "of_FragData";

        // Removes #version and #extension lines (keeping the line, so that line numbers stay right) and the uniform
        // initialisers GLSL ES does not allow.
        void convertLines(std::string_view source, GlslEsShader& result, std::string& out)
        {
            static const std::regex version(R"(^\s*#\s*version\s+(\d+).*$)");
            static const std::regex extension(R"(^\s*#\s*extension\b.*$)");
            static const std::regex initialiser(
                R"(^(\s*uniform\s+([A-Za-z_]\w*)\s+([A-Za-z_]\w*))\s*=\s*([^;]*);(.*)$)");
            static const std::regex anyInitialiser(R"(^\s*uniform\b[^;]*=)");

            std::size_t pos = 0;
            while (pos < source.size())
            {
                std::size_t end = source.find('\n', pos);
                const bool lastLine = end == std::string_view::npos;
                if (lastLine)
                    end = source.size();
                const std::string line(source.substr(pos, end - pos));
                std::smatch match;
                if (std::regex_match(line, match, version))
                {
                    const int number = Misc::StringUtils::toNumeric<int>(match[1].str(), 0);
                    if (number > 120)
                        result.mErrors.push_back(std::format(
                            "written for GLSL {}, which OpenGL ES 3.0 cannot take in place of GLSL 1.20", number));
                }
                else if (std::regex_match(line, extension))
                {
                }
                else if (std::regex_match(line, match, initialiser))
                {
                    result.mUniformInitialisers.push_back({ match[2].str(), match[3].str(), match[4].str() });
                    out += match[1].str() + ";" + match[5].str();
                }
                else
                {
                    if (std::regex_search(line, anyInitialiser))
                        result.mErrors.push_back("uniform initialiser not understood: " + line);
                    out += line;
                }
                if (!lastLine)
                    out += '\n';
                pos = end + 1;
            }
        }

        struct Uses
        {
            std::set<std::string_view> mMatrices;
            std::set<std::string_view> mAttributes;
            std::set<std::string_view> mHelpers;
            std::set<std::string> mUnknown;
            int mFragData = -1;
            bool mDynamicFragData = false;
            bool mClipVertex = false;
        };

        // Renames what GLSL ES 3.00 calls differently, word by word. Comments are dropped (their line breaks kept):
        // OpenSceneGraph's own pass over the text would otherwise turn a built-in name in a comment into a second
        // declaration.
        std::string convertNames(const std::string& source, osg::Shader::Type stage, Uses& uses)
        {
            const bool isVertex = stage == osg::Shader::VERTEX;
            std::string out;
            out.reserve(source.size() + source.size() / 8);
            std::size_t i = 0;
            while (i < source.size())
            {
                const char c = source[i];
                if (c == '/' && i + 1 < source.size() && (source[i + 1] == '/' || source[i + 1] == '*'))
                {
                    const bool line = source[i + 1] == '/';
                    std::size_t end = line ? source.find('\n', i) : source.find("*/", i + 2);
                    end = end == std::string::npos ? source.size() : (line ? end : end + 2);
                    out.append(
                        static_cast<std::size_t>(std::count(source.begin() + i, source.begin() + end, '\n')), '\n');
                    i = end;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c))
                    || (c == '.' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i + 1]))))
                {
                    std::size_t end = i + 1;
                    while (end < source.size() && (isIdentifierPart(source[end]) || source[end] == '.'))
                        ++end;
                    out.append(source, i, end - i);
                    i = end;
                    continue;
                }
                if (!isIdentifierStart(c))
                {
                    out += c;
                    ++i;
                    continue;
                }

                std::size_t end = i + 1;
                while (end < source.size() && isIdentifierPart(source[end]))
                    ++end;
                const std::string_view word = std::string_view(source).substr(i, end - i);
                i = end;

                if (word == "attribute")
                {
                    out += "in";
                    continue;
                }
                if (word == "varying")
                {
                    out += isVertex ? "out" : "in";
                    continue;
                }
                if (const auto it = sTextureFunctions.find(word); it != sTextureFunctions.end())
                {
                    out += it->second;
                    continue;
                }
                if (const auto it = std::find_if(
                        sHelpers.begin(), sHelpers.end(), [&](const Helper& helper) { return helper.mName == word; });
                    it != sHelpers.end())
                {
                    uses.mHelpers.insert(it->mName);
                    out += word;
                    continue;
                }
                if (!word.starts_with("gl_"))
                {
                    out += word;
                    continue;
                }

                if (const auto it = std::find_if(sMatrices.begin(), sMatrices.end(),
                        [&](const BuiltIn& builtIn) { return builtIn.mDesktopName == word; });
                    it != sMatrices.end())
                {
                    uses.mMatrices.insert(it->mName);
                    out += it->mName;
                }
                else if (word == "gl_ModelViewMatrixInverse")
                {
                    uses.mMatrices.insert("osg_ModelViewMatrix");
                    out += "inverse(osg_ModelViewMatrix)";
                }
                else if (const auto attribute = std::find_if(sAttributes.begin(), sAttributes.end(),
                             [&](const BuiltIn& builtIn) { return builtIn.mDesktopName == word; });
                    isVertex && attribute != sAttributes.end())
                {
                    uses.mAttributes.insert(attribute->mName);
                    out += attribute->mName;
                }
                else if (!isVertex && word == "gl_FragColor")
                {
                    uses.mFragData = std::max(uses.mFragData, 0);
                    out += std::string(sFragData) + "[0]";
                }
                else if (!isVertex && word == "gl_FragData")
                {
                    out += sFragData;
                    std::size_t next = source.find_first_not_of(" \t", i);
                    std::size_t close = next == std::string::npos ? next : source.find(']', next);
                    int index = -1;
                    if (next != std::string::npos && source[next] == '[' && close != std::string::npos)
                    {
                        std::string_view digits = std::string_view(source).substr(next + 1, close - next - 1);
                        const std::size_t first = digits.find_first_not_of(" \t");
                        const std::size_t last = digits.find_last_not_of(" \t");
                        if (first != std::string_view::npos)
                            digits = digits.substr(first, last - first + 1);
                        if (!digits.empty() && std::all_of(digits.begin(), digits.end(), [](char d) {
                                return std::isdigit(static_cast<unsigned char>(d));
                            }))
                            index = Misc::StringUtils::toNumeric<int>(digits, -1);
                    }
                    if (index < 0)
                        uses.mDynamicFragData = true;
                    else
                        uses.mFragData = std::max(uses.mFragData, index);
                }
                else if (isVertex && word == "gl_ClipVertex")
                {
                    uses.mClipVertex = true;
                    out += sClipVertex;
                }
                else
                {
                    if (!sKeptBuiltIns.contains(word))
                        uses.mUnknown.insert(std::string(word));
                    out += word;
                }
            }
            return out;
        }

        std::string preamble(osg::Shader::Type stage, const Uses& uses)
        {
            std::string out = "#version 300 es\n";
            out += "precision highp float;\nprecision highp int;\n";
            for (const char* sampler : sSamplerTypes)
                out += std::format("precision highp {};\n", sampler);
            for (const BuiltIn& attribute : sAttributes)
                if (uses.mAttributes.contains(attribute.mName))
                    out += std::format("in {} {};\n", attribute.mType, attribute.mName);
            for (const BuiltIn& matrix : sMatrices)
                if (uses.mMatrices.contains(matrix.mName))
                    out += std::format("uniform {} {};\n", matrix.mType, matrix.mName);
            if (stage == osg::Shader::FRAGMENT && uses.mFragData >= 0)
                out += std::format("layout(location = 0) out vec4 {}[{}];\n", sFragData, uses.mFragData + 1);
            if (uses.mClipVertex)
                out += std::format("vec4 {};\n", sClipVertex);
            for (const Helper& helper : sHelpers)
                if (uses.mHelpers.contains(helper.mName))
                    out += std::string(helper.mDefinition) + "\n";
            out += "#line 1 0\n";
            return out;
        }

        struct LineDirective
        {
            int mLine;
            std::optional<int> mSource;
        };

        std::optional<LineDirective> readLineDirective(std::string_view line)
        {
            static const std::regex directive(R"(^\s*#\s*line\s+(\d+)(?:\s+(\d+))?\s*$)");
            std::match_results<std::string_view::const_iterator> match;
            if (!std::regex_match(line.begin(), line.end(), match, directive))
                return std::nullopt;
            LineDirective result{ Misc::StringUtils::toNumeric<int>(match[1].str(), 0), std::nullopt };
            if (match[2].matched)
                result.mSource = Misc::StringUtils::toNumeric<int>(match[2].str(), 0);
            return result;
        }

        std::vector<std::string_view> splitLines(std::string_view text)
        {
            std::vector<std::string_view> lines;
            std::size_t pos = 0;
            while (pos < text.size())
            {
                std::size_t end = text.find('\n', pos);
                if (end == std::string_view::npos)
                    end = text.size();
                lines.push_back(text.substr(pos, end - pos));
                pos = end + 1;
            }
            return lines;
        }

        // Moves every #line source number of a linked shader up by an offset, and starts it on its own number
        std::string renumberSources(std::string_view text, int offset)
        {
            std::string out = std::format("#line 1 {}\n", offset);
            for (std::string_view line : splitLines(text))
            {
                if (const std::optional<LineDirective> directive = readLineDirective(line);
                    directive && directive->mSource)
                    out += std::format("#line {} {}", directive->mLine, *directive->mSource + offset);
                else
                    out += line;
                out += '\n';
            }
            return out;
        }

        std::optional<std::string> declaredName(std::string_view line)
        {
            static const std::regex declaration(
                R"(^\s*(?:(?:centroid|flat|invariant)\s+)?(?:uniform|varying|attribute)\s+(?:(?:lowp|mediump|highp)\s+)?)"
                R"([A-Za-z_]\w*\s+([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;\s*(?://.*)?$)");
            std::match_results<std::string_view::const_iterator> match;
            if (!std::regex_match(line.begin(), line.end(), match, declaration))
                return std::nullopt;
            return match[1].str();
        }
    }

    GlslEsShader convertToGlslEs(std::string_view source, osg::Shader::Type stage)
    {
        GlslEsShader result;
        if (stage != osg::Shader::VERTEX && stage != osg::Shader::FRAGMENT)
        {
            result.mErrors.push_back("only vertex and fragment shaders are converted to GLSL ES 3.00");
            return result;
        }

        std::string lines;
        lines.reserve(source.size());
        convertLines(source, result, lines);

        Uses uses;
        const std::string body = convertNames(lines, stage, uses);
        for (const std::string& name : uses.mUnknown)
            result.mErrors.push_back(name + " has no GLSL ES 3.00 equivalent");
        if (uses.mDynamicFragData)
            result.mErrors.push_back("gl_FragData is indexed by something other than a number");
        if (uses.mClipVertex)
            result.mDropped.push_back("gl_ClipVertex (user clip planes)");

        result.mSource = preamble(stage, uses) + body;
        return result;
    }

    std::string foldLinkedShaders(std::string_view main, const std::vector<std::string>& linked)
    {
        std::string folded(main);
        if (!folded.empty() && folded.back() != '\n')
            folded += '\n';
        for (std::size_t i = 0; i < linked.size(); ++i)
            folded += renumberSources(linked[i], static_cast<int>(i + 1) * 100);

        const std::vector<std::string_view> lines = splitLines(folded);
        std::map<std::string, int> declarations;
        for (std::string_view line : lines)
            if (const std::optional<std::string> name = declaredName(line))
                ++declarations[*name];

        std::string out;
        out.reserve(folded.size());
        int lineNumber = 1;
        int sourceNumber = 0;
        for (std::string_view line : lines)
        {
            if (const std::optional<LineDirective> directive = readLineDirective(line))
            {
                lineNumber = directive->mLine;
                sourceNumber = directive->mSource.value_or(sourceNumber);
                out += line;
                out += '\n';
                continue;
            }
            const std::optional<std::string> name = declaredName(line);
            if (name && declarations[*name] > 1)
                out += std::format(
                    "#ifndef OF_DECLARED_{0}\n#define OF_DECLARED_{0}\n#line {1} {2}\n{3}\n#endif\n#line {4} {2}\n",
                    *name, lineNumber, sourceNumber, line, lineNumber + 1);
            else
            {
                out += line;
                out += '\n';
            }
            ++lineNumber;
        }
        return out;
    }

    namespace
    {
        std::optional<float> readNumber(std::string_view text)
        {
            static const std::regex number(R"(^\s*[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?[fF]?\s*$)");
            const std::string value(text);
            if (!std::regex_match(value, number))
                return std::nullopt;
            std::string digits = value;
            digits.erase(
                std::remove_if(digits.begin(), digits.end(),
                    [](char c) { return c == 'f' || c == 'F' || std::isspace(static_cast<unsigned char>(c)); }),
                digits.end());
            return Misc::StringUtils::toNumeric<float>(digits);
        }

        // The components of vecN(a, b, ...) or vecN(a), spread to N
        std::optional<std::vector<float>> readVector(std::string_view text, std::size_t size)
        {
            static const std::regex vector(R"(^\s*vec([234])\s*\(([^()]*)\)\s*$)");
            const std::string value(text);
            std::smatch match;
            if (!std::regex_match(value, match, vector) || std::stoul(match[1].str()) != size)
                return std::nullopt;
            std::vector<float> components;
            const std::string list = match[2].str();
            std::size_t pos = 0;
            while (pos <= list.size())
            {
                std::size_t end = list.find(',', pos);
                if (end == std::string::npos)
                    end = list.size();
                const std::optional<float> component = readNumber(std::string_view(list).substr(pos, end - pos));
                if (!component)
                    return std::nullopt;
                components.push_back(*component);
                pos = end + 1;
            }
            if (components.size() == 1)
                components.resize(size, components.front());
            if (components.size() != size)
                return std::nullopt;
            return components;
        }
    }

    osg::ref_ptr<osg::Uniform> makeUniform(const UniformInitialiser& initialiser)
    {
        const std::string& name = initialiser.mName;
        const std::string_view value = initialiser.mValue;
        const std::string& type = initialiser.mType;
        if (type == "bool")
        {
            if (value == "true" || value == "false")
                return new osg::Uniform(name.c_str(), value == "true");
            return nullptr;
        }
        if (type == "int")
        {
            static const std::regex integer(R"(^\s*[+-]?\d+\s*$)");
            if (!std::regex_match(initialiser.mValue, integer))
                return nullptr;
            return new osg::Uniform(name.c_str(), std::stoi(initialiser.mValue));
        }
        if (type == "float")
        {
            const std::optional<float> number = readNumber(value);
            return number ? new osg::Uniform(name.c_str(), *number) : nullptr;
        }
        if (type == "vec2" || type == "vec3" || type == "vec4")
        {
            const std::optional<std::vector<float>> c = readVector(value, static_cast<std::size_t>(type[3] - '0'));
            if (!c)
                return nullptr;
            if (c->size() == 2)
                return new osg::Uniform(name.c_str(), osg::Vec2f((*c)[0], (*c)[1]));
            if (c->size() == 3)
                return new osg::Uniform(name.c_str(), osg::Vec3f((*c)[0], (*c)[1], (*c)[2]));
            return new osg::Uniform(name.c_str(), osg::Vec4f((*c)[0], (*c)[1], (*c)[2], (*c)[3]));
        }
        return nullptr;
    }
}
