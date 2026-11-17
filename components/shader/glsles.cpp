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

        // shadow2D and shadow2DProj return a vec4 in GLSL 1.20; their GLSL ES 3.00 forms return a float. They also
        // clamp the reference depth to [0, 1], as the comparison does on the fixed-point shadow maps of the OpenGL
        // build: ANGLE's Metal backend keeps a 24-bit depth texture as 32-bit float depth and compares the reference as
        // it is, so a surface beyond a shadow map's far plane would read as shadowed.
        struct Helper
        {
            std::string_view mName;
            std::string_view mDefinition;
        };

        constexpr std::array sHelpers = {
            Helper{ "shadow2D",
                "vec4 shadow2D(sampler2DShadow s, vec3 c) { "
                "return vec4(texture(s, vec3(c.xy, clamp(c.z, 0.0, 1.0)))); }" },
            Helper{ "shadow2DProj",
                "vec4 shadow2DProj(sampler2DShadow s, vec4 c) { vec3 p = c.xyz / c.w; "
                "return vec4(texture(s, vec3(p.xy, clamp(p.z, 0.0, 1.0)))); }" },
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
            // GL_OVR_multiview has it under the same name in GLSL ES
            "gl_ViewID_OVR",
        };

        constexpr std::array sSamplerTypes = { "sampler2D", "sampler3D", "samplerCube", "sampler2DShadow",
            "sampler2DArray", "sampler2DArrayShadow", "samplerCubeShadow" };

        // Names GLSL 1.20 leaves free and GLSL ES 3.00 reserves. Each becomes of_<name>.
        const std::set<std::string_view> sReservedNames = { "active", "atomic_uint", "coherent", "common", "filter",
            "partition", "patch", "resource", "sample", "subroutine", "superp" };

        // A uniform is set by its name from outside the shader, so it cannot be renamed; nor can it be called
        // texture, the name of the GLSL ES 3.00 function the GLSL 1.20 texture functions become.
        bool isUnusableUniformName(std::string_view name)
        {
            return sReservedNames.contains(name) || name == "texture";
        }

        // The names a declaration declares, from the text after its type: "a, b[2], c = 1.0" gives a, b and c
        std::vector<std::string> declaratorNames(std::string_view declarators)
        {
            std::vector<std::string> names;
            int depth = 0;
            bool expectName = true;
            for (std::size_t i = 0; i < declarators.size(); ++i)
            {
                const char c = declarators[i];
                if (c == '(' || c == '[' || c == '{')
                    ++depth;
                else if (c == ')' || c == ']' || c == '}')
                    --depth;
                else if (c == ',' && depth == 0)
                    expectName = true;
                else if (expectName && depth == 0 && isIdentifierStart(c))
                {
                    std::size_t end = i + 1;
                    while (end < declarators.size() && isIdentifierPart(declarators[end]))
                        ++end;
                    names.emplace_back(declarators.substr(i, end - i));
                    expectName = false;
                    i = end - 1;
                }
            }
            return names;
        }

        // What a constant expression may name besides numbers and constants: constructors and the built-in
        // functions other than the texture functions
        const std::set<std::string_view> sConstantExpressionNames = { "true", "false", "bool", "int", "uint", "float",
            "vec2", "vec3", "vec4", "bvec2", "bvec3", "bvec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4",
            "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3",
            "mat4x4", "radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh",
            "asinh", "acosh", "atanh", "pow", "exp", "log", "exp2", "log2", "sqrt", "inversesqrt", "abs", "sign",
            "floor", "trunc", "round", "roundEven", "ceil", "fract", "mod", "min", "max", "clamp", "mix", "step",
            "smoothstep", "length", "distance", "dot", "cross", "normalize", "faceforward", "reflect", "refract",
            "matrixCompMult", "outerProduct", "transpose", "determinant", "inverse", "lessThan", "lessThanEqual",
            "greaterThan", "greaterThanEqual", "equal", "notEqual", "any", "all", "not" };

        constexpr std::string_view sGlobalInitialiserMacro = "OF_GLOBAL_INIT_";
        constexpr std::string_view sInitialiseGlobals = "of_initialiseGlobals";

        constexpr std::string_view sClipVertex = "of_ClipVertex";
        constexpr std::string_view sFragData = "of_FragData";
        // The shader's own main, when a main of the converter's runs after it
        constexpr std::string_view sShaderMain = "of_main";
        constexpr std::string_view sClipDistanceExtension = "GL_ANGLE_clip_cull_distance";

        // The text with every comment turned into spaces, line breaks kept
        std::string blankComments(std::string_view source)
        {
            std::string out(source);
            std::size_t i = 0;
            while (i + 1 < out.size())
            {
                if (out[i] != '/' || (out[i + 1] != '/' && out[i + 1] != '*'))
                {
                    ++i;
                    continue;
                }
                const bool line = out[i + 1] == '/';
                std::size_t end = line ? out.find('\n', i) : out.find("*/", i + 2);
                end = end == std::string::npos ? out.size() : (line ? end : end + 2);
                for (; i < end; ++i)
                    if (out[i] != '\n')
                        out[i] = ' ';
            }
            return out;
        }

        // Which lines the GLSL preprocessor keeps, as far as the text alone decides it. The template defines are
        // already written in, so most conditions are numbers; a condition on something only the graphics library
        // knows (an extension macro, for example) leaves its lines undecided.
        class Branches
        {
        public:
            enum class Active
            {
                No,
                Yes,
                Unknown,
            };

            // Reads one line; true if it was a conditional or #define/#undef directive
            bool read(std::string_view line)
            {
                static const std::regex directive(R"(^\s*#\s*(\w+)\s*(.*?)\s*$)");
                std::match_results<std::string_view::const_iterator> match;
                if (!std::regex_match(line.begin(), line.end(), match, directive))
                    return false;
                const std::string keyword = match[1].str();
                const std::string rest = match[2].str();
                if (keyword == "if" || keyword == "ifdef" || keyword == "ifndef")
                {
                    Active condition = Active::Unknown;
                    if (keyword == "if")
                        condition = evaluate(rest);
                    else
                    {
                        condition = isDefined(rest);
                        if (keyword == "ifndef")
                            condition = negate(condition);
                    }
                    mStack.push_back({ active(), condition, condition });
                    return true;
                }
                if (keyword == "elif" || keyword == "else")
                {
                    if (mStack.empty())
                        return true;
                    Group& group = mStack.back();
                    const Active previous = group.mTaken;
                    Active condition = keyword == "else" ? Active::Yes : evaluate(rest);
                    // The branch is taken if no earlier branch was and this condition holds
                    group.mCurrent = both(negate(previous), condition);
                    group.mTaken = either(previous, condition);
                    return true;
                }
                if (keyword == "endif")
                {
                    if (!mStack.empty())
                        mStack.pop_back();
                    return true;
                }
                if (keyword == "define" || keyword == "undef")
                {
                    const Active here = active();
                    static const std::regex name(R"(^([A-Za-z_]\w*)(\()?\s*(.*)$)");
                    std::smatch nameMatch;
                    if (here == Active::No || !std::regex_match(rest, nameMatch, name))
                        return true;
                    Macro& macro = mMacros[nameMatch[1].str()];
                    if (here == Active::Unknown || nameMatch[2].matched)
                        macro = { Active::Unknown, "" };
                    else if (keyword == "define")
                        macro = { Active::Yes, nameMatch[3].str() };
                    else
                        macro = { Active::No, "" };
                    return true;
                }
                return false;
            }

            Active active() const
            {
                Active result = Active::Yes;
                for (const Group& group : mStack)
                    result = both(result, both(group.mOuter, group.mCurrent));
                return result;
            }

        private:
            struct Group
            {
                Active mOuter;
                Active mCurrent;
                Active mTaken;
            };

            struct Macro
            {
                Active mDefined = Active::No;
                std::string mValue;
            };

            static Active negate(Active value)
            {
                return value == Active::Unknown ? value : (value == Active::Yes ? Active::No : Active::Yes);
            }

            static Active both(Active a, Active b)
            {
                if (a == Active::No || b == Active::No)
                    return Active::No;
                return a == Active::Yes && b == Active::Yes ? Active::Yes : Active::Unknown;
            }

            static Active either(Active a, Active b)
            {
                if (a == Active::Yes || b == Active::Yes)
                    return Active::Yes;
                return a == Active::No && b == Active::No ? Active::No : Active::Unknown;
            }

            Active isDefined(std::string_view name) const
            {
                const std::string key(name);
                if (const auto it = mMacros.find(key); it != mMacros.end())
                    return it->second.mDefined;
                // The converted text is GLSL ES 3.00
                if (key == "GL_ES" || key == "__VERSION__" || key == "__LINE__" || key == "__FILE__"
                    || key == "GL_FRAGMENT_PRECISION_HIGH")
                    return Active::Yes;
                if (key.starts_with("GL_") || key.starts_with("__"))
                    return Active::Unknown;
                return Active::No;
            }

            Active evaluate(const std::string& expression) const
            {
                Expression parser{ *this, expression, 0, 0 };
                const std::optional<long long> value = parser.parse();
                if (!value)
                    return Active::Unknown;
                return *value != 0 ? Active::Yes : Active::No;
            }

            // An integer #if expression: numbers, defined, macros with a number as value, the C operators.
            // std::nullopt for anything else.
            struct Expression
            {
                const Branches& mBranches;
                std::string mText;
                std::size_t mPos;
                int mDepth;

                void skipSpace()
                {
                    while (mPos < mText.size() && std::isspace(static_cast<unsigned char>(mText[mPos])))
                        ++mPos;
                }

                bool take(std::string_view token)
                {
                    skipSpace();
                    if (mText.compare(mPos, token.size(), token) != 0)
                        return false;
                    // "<" must not take the first half of "<<" or "<=", and so on
                    if (token.size() == 1 && mPos + 1 < mText.size())
                    {
                        const char next = mText[mPos + 1];
                        if ((token == "<" || token == ">") && (next == token[0] || next == '='))
                            return false;
                        if ((token == "&" || token == "|") && next == token[0])
                            return false;
                        if ((token == "!" || token == "=") && next == '=')
                            return false;
                    }
                    mPos += token.size();
                    return true;
                }

                std::optional<long long> parse()
                {
                    std::optional<long long> value = binary(0);
                    skipSpace();
                    if (mPos != mText.size())
                        return std::nullopt;
                    return value;
                }

                std::optional<long long> binary(int level)
                {
                    static const std::array<std::vector<std::string_view>, 10> levels = { {
                        { "||" },
                        { "&&" },
                        { "|" },
                        { "^" },
                        { "&" },
                        { "==", "!=" },
                        { "<=", ">=", "<", ">" },
                        { "<<", ">>" },
                        { "+", "-" },
                        { "*", "/", "%" },
                    } };
                    if (level == static_cast<int>(levels.size()))
                        return unary();
                    std::optional<long long> left = binary(level + 1);
                    while (left)
                    {
                        std::string_view op;
                        for (std::string_view candidate : levels[level])
                            if (take(candidate))
                            {
                                op = candidate;
                                break;
                            }
                        if (op.empty())
                            return left;
                        const std::optional<long long> right = binary(level + 1);
                        if (!right)
                            return std::nullopt;
                        const long long a = *left;
                        const long long b = *right;
                        if ((op == "/" || op == "%") && b == 0)
                            return std::nullopt;
                        if (op == "||")
                            left = a || b;
                        else if (op == "&&")
                            left = a && b;
                        else if (op == "|")
                            left = a | b;
                        else if (op == "^")
                            left = a ^ b;
                        else if (op == "&")
                            left = a & b;
                        else if (op == "==")
                            left = a == b;
                        else if (op == "!=")
                            left = a != b;
                        else if (op == "<=")
                            left = a <= b;
                        else if (op == ">=")
                            left = a >= b;
                        else if (op == "<")
                            left = a < b;
                        else if (op == ">")
                            left = a > b;
                        else if (op == "<<")
                            left = (b < 0 || b > 62) ? std::optional<long long>() : a << b;
                        else if (op == ">>")
                            left = (b < 0 || b > 62) ? std::optional<long long>() : a >> b;
                        else if (op == "+")
                            left = a + b;
                        else if (op == "-")
                            left = a - b;
                        else if (op == "*")
                            left = a * b;
                        else if (op == "/")
                            left = a / b;
                        else
                            left = a % b;
                    }
                    return left;
                }

                std::optional<long long> unary()
                {
                    if (take("!"))
                    {
                        const std::optional<long long> value = unary();
                        return value ? std::optional<long long>(!*value) : std::nullopt;
                    }
                    if (take("-"))
                    {
                        const std::optional<long long> value = unary();
                        return value ? std::optional<long long>(-*value) : std::nullopt;
                    }
                    if (take("+"))
                        return unary();
                    if (take("~"))
                    {
                        const std::optional<long long> value = unary();
                        return value ? std::optional<long long>(~*value) : std::nullopt;
                    }
                    if (take("("))
                    {
                        const std::optional<long long> value = binary(0);
                        if (!take(")"))
                            return std::nullopt;
                        return value;
                    }
                    skipSpace();
                    if (mPos >= mText.size())
                        return std::nullopt;
                    if (std::isdigit(static_cast<unsigned char>(mText[mPos])))
                    {
                        std::size_t end = mPos;
                        while (end < mText.size() && isIdentifierPart(mText[end]))
                            ++end;
                        std::string digits = mText.substr(mPos, end - mPos);
                        mPos = end;
                        while (!digits.empty() && (digits.back() == 'u' || digits.back() == 'U'))
                            digits.pop_back();
                        // Octal and hexadecimal numbers are left undecided
                        if (digits.empty() || (digits.size() > 1 && digits.front() == '0')
                            || !std::all_of(digits.begin(), digits.end(),
                                [](char d) { return std::isdigit(static_cast<unsigned char>(d)); }))
                            return std::nullopt;
                        return Misc::StringUtils::toNumeric<long long>(digits, 0);
                    }
                    if (!isIdentifierStart(mText[mPos]))
                        return std::nullopt;
                    std::size_t end = mPos;
                    while (end < mText.size() && isIdentifierPart(mText[end]))
                        ++end;
                    const std::string word = mText.substr(mPos, end - mPos);
                    mPos = end;
                    if (word == "defined")
                    {
                        const bool parenthesis = take("(");
                        skipSpace();
                        std::size_t nameEnd = mPos;
                        while (nameEnd < mText.size() && isIdentifierPart(mText[nameEnd]))
                            ++nameEnd;
                        const std::string name = mText.substr(mPos, nameEnd - mPos);
                        mPos = nameEnd;
                        if (name.empty() || (parenthesis && !take(")")))
                            return std::nullopt;
                        const Active defined = mBranches.isDefined(name);
                        if (defined == Active::Unknown)
                            return std::nullopt;
                        return defined == Active::Yes ? 1 : 0;
                    }
                    if (word == "__VERSION__")
                        return 300;
                    if (word == "GL_ES" || word == "GL_FRAGMENT_PRECISION_HIGH")
                        return 1;
                    // A macro the text defines with a value is replaced by that text, which is then read in its place
                    // (#define N 1 + 2 makes N * 3 seven); GLSL has no value for an undefined name
                    const auto it = mBranches.mMacros.find(word);
                    if (it == mBranches.mMacros.end() || it->second.mDefined != Active::Yes || ++mDepth > 64)
                        return std::nullopt;
                    mPos -= word.size();
                    mText.replace(mPos, word.size(), " " + it->second.mValue + " ");
                    return unary();
                }
            };

            std::vector<Group> mStack;
            std::map<std::string, Macro> mMacros;
        };

        // Removes #version and #extension lines (keeping the line, so that line numbers stay right) and the uniform
        // initialisers GLSL ES does not allow. Comments are blanked first; an initialiser counts only in text the
        // preprocessor keeps. skipped says, line by line, whether the preprocessor is known to skip the line.
        void convertLines(std::string_view original, GlslEsShader& result, std::string& out, std::vector<bool>& skipped)
        {
            static const std::regex version(R"(^\s*#\s*version\s+(\d+).*$)");
            static const std::regex extension(R"(^\s*#\s*extension\b.*$)");
            static const std::regex initialiser(
                R"(^(\s*uniform\s+([A-Za-z_]\w*)\s+([A-Za-z_]\w*))\s*=\s*([^;]*);(.*)$)");
            static const std::regex anyInitialiser(R"(^\s*uniform\b[^;]*=)");
            static const std::regex uniformDeclarators(
                R"(^\s*uniform\s+(?:(?:lowp|mediump|highp)\s+)?[A-Za-z_]\w*\s+([^;]*))");

            const std::string code = blankComments(original);
            const std::string_view source = code;
            Branches branches;
            std::map<std::string, UniformInitialiser> taken;

            std::size_t pos = 0;
            while (pos < source.size())
            {
                std::size_t end = source.find('\n', pos);
                const bool lastLine = end == std::string_view::npos;
                if (lastLine)
                    end = source.size();
                // Read without comments, written as it was
                const std::string line(source.substr(pos, end - pos));
                const std::string_view text = original.substr(pos, end - pos);
                skipped.push_back(branches.active() == Branches::Active::No);
                std::smatch match;
                // The compiler never sees a declaration the preprocessor skips
                if (branches.active() != Branches::Active::No && std::regex_search(line, match, uniformDeclarators))
                    for (const std::string& name : declaratorNames(match[1].str()))
                        if (isUnusableUniformName(name))
                            result.mErrors.push_back(
                                std::format("uniform {} has a name GLSL ES 3.00 keeps for itself", name));
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
                else if (branches.read(line))
                    out += text;
                else if (branches.active() == Branches::Active::No)
                    // The compiler skips these lines; an initialiser here is not this shader's
                    out += text;
                else if (branches.active() == Branches::Active::Unknown && std::regex_search(line, anyInitialiser))
                {
                    result.mErrors.push_back(
                        "uniform initialiser in a preprocessor branch that is decided only when compiling: "
                        + std::string(text));
                    out += text;
                }
                else if (std::regex_match(line, match, initialiser))
                {
                    UniformInitialiser value{ match[2].str(), match[3].str(), match[4].str() };
                    while (!value.mValue.empty() && std::isspace(static_cast<unsigned char>(value.mValue.back())))
                        value.mValue.pop_back();
                    if (const auto [it, added] = taken.emplace(value.mName, value); added)
                        result.mUniformInitialisers.push_back(std::move(value));
                    else if (it->second.mType != value.mType || it->second.mValue != value.mValue)
                        result.mErrors.push_back(std::format("uniform {} is given two initial values: {} {} and {} {}",
                            value.mName, it->second.mType, it->second.mValue, value.mType, value.mValue));
                    out += match[1].str() + ";";
                    out += text.substr(static_cast<std::size_t>(match.position(5)));
                }
                else
                {
                    if (std::regex_search(line, anyInitialiser))
                        result.mErrors.push_back("uniform initialiser not understood: " + std::string(text));
                    out += text;
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
            bool mGlobalInitialisers = false;
        };

        // Whether the line that starts at pos is a #define
        bool isDefineLine(const std::string& source, std::size_t pos)
        {
            std::size_t p = source.find_first_not_of(" \t", pos);
            if (p == std::string::npos || source[p] != '#')
                return false;
            p = source.find_first_not_of(" \t", p + 1);
            return p != std::string::npos && source.compare(p, 6, "define") == 0;
        }

        // Renames what GLSL ES 3.00 calls differently, word by word. Comments are dropped (their line breaks kept):
        // OpenSceneGraph's own pass over the text would otherwise turn a built-in name in a comment into a second
        // declaration. A built-in without an equivalent counts only on a line the preprocessor is not known to skip. A
        // #define body is text until the macro is used, and a header that both stages share (that of a post-processing
        // pass) names built-ins of the other stage there: such a name is left alone, and the GLSL ES compiler reports
        // it if the macro is ever used.
        std::string convertNames(
            const std::string& source, osg::Shader::Type stage, Uses& uses, const std::vector<bool>& skipped)
        {
            std::size_t lineNumber = 0;
            const bool isVertex = stage == osg::Shader::VERTEX;
            std::string out;
            out.reserve(source.size() + source.size() / 8);
            std::size_t i = 0;
            bool inDefine = isDefineLine(source, 0);
            while (i < source.size())
            {
                const char c = source[i];
                if (c == '/' && i + 1 < source.size() && (source[i + 1] == '/' || source[i + 1] == '*'))
                {
                    const bool line = source[i + 1] == '/';
                    std::size_t end = line ? source.find('\n', i) : source.find("*/", i + 2);
                    end = end == std::string::npos ? source.size() : (line ? end : end + 2);
                    const auto lineBreaks
                        = static_cast<std::size_t>(std::count(source.begin() + i, source.begin() + end, '\n'));
                    // A comment on one line stands for a space, which keeps the words around it apart
                    if (!line && lineBreaks == 0)
                        out += ' ';
                    // A comment over several lines is one space too, so a #define goes on after it: its line breaks
                    // are kept for the line numbers, and continue the #define
                    for (std::size_t n = 0; n < lineBreaks; ++n)
                        out += inDefine ? " \\\n" : "\n";
                    lineNumber += lineBreaks;
                    i = end;
                    continue;
                }
                if (c == '\n')
                {
                    const bool continued = i > 0 && source[i - 1] == '\\';
                    inDefine = (inDefine && continued) || isDefineLine(source, i + 1);
                    ++lineNumber;
                    out += c;
                    ++i;
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
                if (sReservedNames.contains(word))
                {
                    out += "of_";
                    out += word;
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
                    (isVertex || inDefine) && attribute != sAttributes.end())
                {
                    // In a #define of the fragment stage it is never expanded, and is only renamed: OpenSceneGraph,
                    // which renames these names in every stage as it compiles, would declare a vertex input for it in
                    // front of the default precision
                    if (isVertex)
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
                    if (!sKeptBuiltIns.contains(word) && !inDefine
                        && !(lineNumber < skipped.size() && skipped[lineNumber]))
                        uses.mUnknown.insert(std::string(word));
                    out += word;
                }
            }
            return out;
        }

        std::string preamble(osg::Shader::Type stage, const Uses& uses)
        {
            std::string out = "#version 300 es\n";
            // #extension comes before any declaration; a context without the extension draws unclipped
            if (uses.mClipVertex)
                out += std::format("#ifdef {0}\n#extension {0} : enable\n#endif\n", sClipDistanceExtension);
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
            {
                out += std::format("vec4 {};\n", sClipVertex);
                for (unsigned int i = 0; i < sClipPlaneCount; ++i)
                    out += std::format("uniform vec4 {};\n", clipPlaneUniformName(i));
            }
            for (const Helper& helper : sHelpers)
                if (uses.mHelpers.contains(helper.mName))
                    out += std::string(helper.mDefinition) + "\n";
            if (uses.mGlobalInitialisers)
                out += std::format("void {}();\n", sInitialiseGlobals);
            out += "#line 1 0\n";
            return out;
        }

        // Renames one identifier wherever it stands as a whole word (the text has no comments left)
        std::string renameWord(const std::string& source, std::string_view from, std::string_view to)
        {
            std::string out;
            out.reserve(source.size());
            std::size_t i = 0;
            while (i < source.size())
            {
                if (!isIdentifierStart(source[i]) || (i > 0 && isIdentifierPart(source[i - 1])))
                {
                    out += source[i++];
                    continue;
                }
                std::size_t end = i + 1;
                while (end < source.size() && isIdentifierPart(source[end]))
                    ++end;
                const std::string_view word = std::string_view(source).substr(i, end - i);
                out += word == from ? to : word;
                i = end;
            }
            return out;
        }

        // The main of a vertex shader that wrote gl_ClipVertex: the shader's own main, then each clip distance
        std::string clipDistanceMain()
        {
            std::string out
                = std::format("void main()\n{{\n    {}();\n#ifdef {}\n", sShaderMain, sClipDistanceExtension);
            for (unsigned int i = 0; i < sClipPlaneCount; ++i)
                out += std::format("    gl_ClipDistance[{}] = dot({}, {});\n", i, sClipVertex, clipPlaneUniformName(i));
            out += "#endif\n}\n";
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

        // An object-like macro: its text, and whether the converter knows that it holds at a line (it was defined
        // outside every preprocessor branch). A macro that takes arguments, or one defined in a branch, is not known.
        struct Macro
        {
            std::string mValue;
            bool mKnown = false;
        };

        using Macros = std::map<std::string, Macro, std::less<>>;

        // Whether an initialiser names nothing but numbers, constants, constructors, built-in functions and macros
        // known to stand for such text (followed to a depth, so that macros that name each other end)
        bool isConstantExpression(std::string_view value, const std::set<std::string, std::less<>>& constants,
            const Macros& macros, int depth = 0)
        {
            std::size_t i = 0;
            while (i < value.size())
            {
                const char c = value[i];
                if (std::isdigit(static_cast<unsigned char>(c))
                    || (c == '.' && i + 1 < value.size() && std::isdigit(static_cast<unsigned char>(value[i + 1]))))
                {
                    while (i < value.size() && (isIdentifierPart(value[i]) || value[i] == '.'))
                        ++i;
                    continue;
                }
                if (!isIdentifierStart(c))
                {
                    ++i;
                    continue;
                }
                std::size_t end = i + 1;
                while (end < value.size() && isIdentifierPart(value[end]))
                    ++end;
                const std::string_view word = value.substr(i, end - i);
                std::size_t before = i;
                while (before > 0 && (value[before - 1] == ' ' || value[before - 1] == '\t'))
                    --before;
                const bool field = before > 0 && value[before - 1] == '.';
                if (!field && !sConstantExpressionNames.contains(word) && !constants.contains(word))
                {
                    const auto macro = macros.find(word);
                    if (macro == macros.end() || !macro->second.mKnown || depth >= 16
                        || !isConstantExpression(macro->second.mValue, constants, macros, depth + 1))
                        return false;
                }
                i = end;
            }
            return true;
        }

        // The words of a text that are names (not numbers, not fields)
        std::set<std::string, std::less<>> namesIn(std::string_view value)
        {
            std::set<std::string, std::less<>> names;
            for (std::size_t i = 0; i < value.size();)
            {
                if (std::isdigit(static_cast<unsigned char>(value[i])))
                {
                    while (i < value.size() && (isIdentifierPart(value[i]) || value[i] == '.'))
                        ++i;
                    continue;
                }
                if (!isIdentifierStart(value[i]))
                {
                    ++i;
                    continue;
                }
                std::size_t end = i + 1;
                while (end < value.size() && isIdentifierPart(value[end]))
                    ++end;
                names.emplace(value.substr(i, end - i));
                i = end;
            }
            return names;
        }

        // A #define or #undef line: the macro's name, and for an object-like #define its text
        struct MacroDirective
        {
            std::string mName;
            bool mDefine;
            bool mTakesArguments;
            std::string mValue;
        };

        std::optional<MacroDirective> readMacroDirective(std::string_view line)
        {
            static const std::regex directive(R"(^\s*#\s*(define|undef)\s+([A-Za-z_]\w*)(\()?\s*(.*?)\s*$)");
            std::match_results<std::string_view::const_iterator> match;
            if (!std::regex_match(line.begin(), line.end(), match, directive))
                return std::nullopt;
            return MacroDirective{ match[2].str(), match[1].str() == "define", match[3].matched, match[4].str() };
        }

        bool hasTopLevelComma(std::string_view value)
        {
            int depth = 0;
            for (char c : value)
            {
                if (c == '(' || c == '[')
                    ++depth;
                else if (c == ')' || c == ']')
                    --depth;
                else if (c == ',' && depth == 0)
                    return true;
            }
            return false;
        }

        // GLSL 1.20 lets a global variable start from a uniform or another variable; GLSL ES 3.00 takes only a
        // constant expression there. Such an initialiser moves into a function that main calls before anything else,
        // which is when it ran before. The function goes at the end, after every declaration, a linked shader's
        // folded in after main included. The declaration stays where it was and defines a macro, so that the function
        // assigns only the variables whose declarations the preprocessor kept. #line directives keep line numbers.
        // A declaration of several variables, or one that runs over several lines, is left alone.
        //
        // A macro is expanded where its line stands. An initialiser whose macros are known to be constant stays (GLSL
        // ES 3.00 takes it); one that moves takes its macros' text at the end, so a macro it names that is defined
        // again or undefined further down makes the conversion fail rather than give another value. Lines the
        // preprocessor is known to skip are left as they are; main is the one of the lines it may keep, or each of them
        // where the converter cannot tell.
        std::string moveGlobalInitialisers(
            const std::string& source, GlslEsShader& result, Uses& uses, const std::vector<bool>& skipped)
        {
            static const std::regex global(R"(^(\s*(?:(?:lowp|mediump|highp)\s+)?[A-Za-z_]\w*\s+([A-Za-z_]\w*))"
                                           R"((?:\s*\[[^\]]*\])?)\s*=\s*([^;{}]*?)\s*;\s*$)");
            static const std::regex constant(
                R"(^\s*const\s+(?:(?:lowp|mediump|highp)\s+)?[A-Za-z_]\w*\s+([A-Za-z_]\w*))");

            std::set<std::string, std::less<>> constants;
            std::string initialisers;
            int moved = 0;
            std::vector<std::string> lines;
            int lineNumber = 1;
            int sourceNumber = 0;
            int depth = 0;
            bool awaitingMain = false;
            bool inDirective = false;
            std::vector<std::pair<std::size_t, std::size_t>> mainBodies;
            const std::vector<std::string_view> sourceLines = splitLines(source);
            const auto isSkipped = [&](std::size_t index) { return index < skipped.size() && skipped[index]; };

            // Where each macro is defined or undefined, line by line
            std::map<std::string, std::vector<std::size_t>, std::less<>> macroChanges;
            for (std::size_t index = 0; index < sourceLines.size(); ++index)
                if (!isSkipped(index))
                    if (const std::optional<MacroDirective> directive = readMacroDirective(sourceLines[index]))
                        macroChanges[directive->mName].push_back(index);
            const auto changedAfter = [&](std::string_view name, std::size_t index) {
                const auto it = macroChanges.find(name);
                return it != macroChanges.end() && it->second.back() > index;
            };

            Macros macros;
            int branchDepth = 0;
            for (std::size_t index = 0; index < sourceLines.size(); ++index)
            {
                const std::string_view line = sourceLines[index];
                if (const std::optional<LineDirective> directive = readLineDirective(line))
                {
                    lineNumber = directive->mLine;
                    sourceNumber = directive->mSource.value_or(sourceNumber);
                    lines.emplace_back(line);
                    continue;
                }
                const std::size_t first = line.find_first_not_of(" \t");
                if (inDirective || (first != std::string_view::npos && line[first] == '#'))
                {
                    const bool continued = !line.empty() && line.back() == '\\';
                    if (!inDirective)
                    {
                        static const std::regex conditional(R"(^\s*#\s*(if|ifdef|ifndef|endif)\b.*$)");
                        std::match_results<std::string_view::const_iterator> match;
                        if (std::regex_match(line.begin(), line.end(), match, conditional))
                            branchDepth += match[1].str() == "endif" ? -1 : 1;
                        else if (const std::optional<MacroDirective> directive = readMacroDirective(line);
                            directive && !isSkipped(index))
                        {
                            if (directive->mDefine)
                                macros[directive->mName] = Macro{ directive->mValue,
                                    !directive->mTakesArguments && !continued && branchDepth == 0 };
                            else
                                macros.erase(directive->mName);
                        }
                    }
                    inDirective = continued;
                    lines.emplace_back(line);
                    ++lineNumber;
                    continue;
                }
                if (isSkipped(index))
                {
                    lines.emplace_back(line);
                    ++lineNumber;
                    continue;
                }

                const std::string text(line);
                std::smatch match;
                if (depth == 0 && std::regex_search(text, match, constant))
                    constants.insert(match[1].str());
                else if (depth == 0 && std::regex_match(text, match, global) && !hasTopLevelComma(match[3].str())
                    && !isConstantExpression(match[3].str(), constants, macros))
                {
                    for (const std::string& name : namesIn(match[3].str()))
                        if (changedAfter(name, index))
                            result.mErrors.push_back(std::format(
                                "global {} starts from {}, which is defined again or undefined further down: its "
                                "initialiser cannot move to main",
                                match[2].str(), name));
                    lines.push_back(std::format("{};\n#define {}{}\n#line {} {}", match[1].str(),
                        sGlobalInitialiserMacro, moved, lineNumber + 1, sourceNumber));
                    initialisers += std::format("#ifdef {}{}\n#line {} {}\n    {} = {};\n#endif\n",
                        sGlobalInitialiserMacro, moved, lineNumber, sourceNumber, match[2].str(), match[3].str());
                    ++moved;
                    ++lineNumber;
                    continue;
                }

                for (std::size_t i = 0; i < line.size(); ++i)
                {
                    const char c = line[i];
                    if (isIdentifierStart(c) && (i == 0 || !isIdentifierPart(line[i - 1])))
                    {
                        std::size_t end = i + 1;
                        while (end < line.size() && isIdentifierPart(line[end]))
                            ++end;
                        if (depth == 0 && line.substr(i, end - i) == "main")
                            awaitingMain = true;
                        i = end - 1;
                    }
                    else if (c == ';' && depth == 0)
                        awaitingMain = false;
                    else if (c == '{')
                    {
                        if (depth == 0 && awaitingMain)
                            mainBodies.emplace_back(lines.size(), i + 1);
                        awaitingMain = false;
                        ++depth;
                    }
                    else if (c == '}')
                        --depth;
                }
                lines.push_back(text);
                ++lineNumber;
            }

            if (moved == 0)
                return source;
            if (mainBodies.empty())
            {
                result.mErrors.push_back("a global initialiser that is not constant, and no main to run it from");
                return source;
            }
            uses.mGlobalInitialisers = true;
            // From the last, so that two in one line keep their places
            for (auto it = mainBodies.rbegin(); it != mainBodies.rend(); ++it)
                lines[it->first].insert(it->second, std::format("{}();", sInitialiseGlobals));
            std::string out;
            out.reserve(source.size() + initialisers.size() + 256);
            for (const std::string& line : lines)
            {
                out += line;
                out += '\n';
            }
            out += std::format("void {}()\n{{\n{}}}\n", sInitialiseGlobals, initialisers);
            return out;
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
        std::vector<bool> skipped;
        convertLines(source, result, lines, skipped);

        Uses uses;
        std::string body = convertNames(lines, stage, uses, skipped);
        for (const std::string& name : uses.mUnknown)
            result.mErrors.push_back(name + " has no GLSL ES 3.00 equivalent");
        if (uses.mDynamicFragData)
            result.mErrors.push_back("gl_FragData is indexed by something other than a number");
        body = moveGlobalInitialisers(body, result, uses, skipped);
        if (uses.mClipVertex)
        {
            body = renameWord(body, "main", sShaderMain);
            if (!body.empty() && body.back() != '\n')
                body += '\n';
            body += clipDistanceMain();
        }

        result.mSource = preamble(stage, uses) + body;
        return result;
    }

    std::string clipPlaneUniformName(unsigned int index)
    {
        return std::format("of_ClipPlane{}", index);
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
