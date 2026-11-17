#include "shadermanager.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>

#include <osg/Program>
#include <osg/Uniform>
#include <osg/buffered_value>
#include <osgViewer/Viewer>

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/sceneutil/gldialect.hpp>
#include <components/settings/settings.hpp>

#include "glsles.hpp"

namespace
{
    // The values of the uniform initialisers the GLSL ES conversion took out of a shader ride on the shader itself, so
    // that a shader rebuilt for new defines or by a hot reload brings its new values to every program that has it
    struct InitialValues : osg::Referenced
    {
        std::vector<osg::ref_ptr<osg::Uniform>> mValues;
    };

    void setInitialValues(osg::Shader& shader, std::vector<osg::ref_ptr<osg::Uniform>>&& values)
    {
        if (values.empty())
        {
            if (shader.getUserData() != nullptr)
                shader.setUserData(nullptr);
            return;
        }
        osg::ref_ptr<InitialValues> data = new InitialValues;
        data->mValues = std::move(values);
        shader.setUserData(data.get());
    }

    osg::Shader::Type getShaderType(const std::string& templateName)
    {
        std::string_view ext = Misc::getFileExtension(templateName);

        if (ext == "vert")
            return osg::Shader::VERTEX;
        if (ext == "frag")
            return osg::Shader::FRAGMENT;
        if (ext == "geom")
            return osg::Shader::GEOMETRY;
        if (ext == "comp")
            return osg::Shader::COMPUTE;
        if (ext == "tese")
            return osg::Shader::TESSEVALUATION;
        if (ext == "tesc")
            return osg::Shader::TESSCONTROL;

        throw std::runtime_error("unrecognized shader template name: " + templateName);
    }

    std::string_view getRootPrefix(std::string_view path)
    {
        if (path.starts_with("lib"))
            return "lib";
        else if (path.starts_with("compatibility"))
            return "compatibility";
        else if (path.starts_with("core"))
            return "core";
        return {};
    }

    int getLineNumber(std::string_view source, std::size_t foundPos, int lineNumber, int offset)
    {
        constexpr std::string_view tag = "#line";
        std::size_t lineDirectivePosition = source.rfind(tag, foundPos);
        if (lineDirectivePosition != std::string_view::npos)
        {
            std::size_t lineNumberStart = lineDirectivePosition + tag.size() + 1;
            std::size_t lineNumberEnd = source.find_first_not_of("0123456789", lineNumberStart);
            std::string_view lineNumberString = source.substr(lineNumberStart, lineNumberEnd - lineNumberStart);
            lineNumber = Misc::StringUtils::toNumeric<int>(lineNumberString, 2) + offset;
        }
        else
        {
            lineDirectivePosition = 0;
        }
        lineNumber
            += static_cast<int>(std::count(source.begin() + lineDirectivePosition, source.begin() + foundPos, '\n'));
        return lineNumber;
    }

    bool addLineDirectivesAfterConditionalBlocks(std::string& source)
    {
        for (size_t position = 0; position < source.length();)
        {
            size_t foundPos = source.find("#endif", position);
            foundPos = std::min(foundPos, source.find("#elif", position));
            foundPos = std::min(foundPos, source.find("#else", position));

            if (foundPos == std::string::npos)
                break;

            foundPos = source.find_first_of("\n\r", foundPos);
            foundPos = source.find_first_not_of("\n\r", foundPos);

            if (foundPos == std::string::npos)
                break;

            int lineNumber = getLineNumber(source, foundPos, 1, -1);

            source.replace(foundPos, 0, std::format("#line {}\n", lineNumber));

            position = foundPos;
        }

        return true;
    }

    // Recursively replaces include statements with the actual source of the included files.
    // Adjusts #line statements accordingly and detects cyclic includes.
    // cycleIncludeChecker is the set of files that include this file directly or indirectly, and is intentionally not a
    // reference to allow automatic cleanup.
    bool parseIncludes(const std::filesystem::path& shaderPath, std::string& source, const std::string& fileName,
        int& fileNumber, std::set<std::filesystem::path> cycleIncludeChecker,
        std::set<std::filesystem::path>& includedFiles)
    {
        includedFiles.insert(shaderPath / fileName);
        // An include is cyclic if it is being included by itself
        if (cycleIncludeChecker.insert(shaderPath / fileName).second == false)
        {
            Log(Debug::Error) << "Shader " << fileName << " error: Detected cyclic #includes";
            return false;
        }

        Misc::StringUtils::replaceAll(source, "\r\n", "\n");

        size_t foundPos = 0;
        while ((foundPos = source.find("#include")) != std::string::npos)
        {
            size_t start = source.find('"', foundPos);
            if (start == std::string::npos || start == source.size() - 1)
            {
                Log(Debug::Error) << "Shader " << fileName << " error: Invalid #include";
                return false;
            }
            size_t end = source.find('"', start + 1);
            if (end == std::string::npos)
            {
                Log(Debug::Error) << "Shader " << fileName << " error: Invalid #include";
                return false;
            }
            std::string includeFilename = source.substr(start + 1, end - (start + 1));

            // Check if this include is a relative path
            // TODO: We shouldn't be relying on soft-coded root prefixes, just check if the path exists and fallback to
            // searching root if it doesn't
            if (getRootPrefix(includeFilename).empty())
                includeFilename
                    = Files::pathToUnicodeString(std::filesystem::path(fileName).parent_path() / includeFilename);

            std::filesystem::path includePath = shaderPath / includeFilename;

            // Determine the line number that will be used for the #line directive following the included source
            int lineNumber = getLineNumber(source, foundPos, 0, -1);

            // Include the file recursively
            std::ifstream includeFstream;
            includeFstream.open(includePath);
            if (includeFstream.fail())
            {
                Log(Debug::Error) << "Shader " << fileName << " error: Failed to open include " << includePath << ": "
                                  << std::generic_category().message(errno);
                return false;
            }
            int includedFileNumber = fileNumber++;

            std::stringstream buffer;
            buffer << includeFstream.rdbuf();
            std::string stringRepresentation = buffer.str();
            if (!addLineDirectivesAfterConditionalBlocks(stringRepresentation)
                || !parseIncludes(
                    shaderPath, stringRepresentation, includeFilename, fileNumber, cycleIncludeChecker, includedFiles))
            {
                Log(Debug::Error) << "In file included from " << fileName << "." << lineNumber;
                return false;
            }

            std::stringstream toInsert;
            toInsert << "#line 0 " << includedFileNumber << "\n"
                     << stringRepresentation << "\n#line " << lineNumber << " 0\n";

            source.replace(foundPos, (end - foundPos + 1), toInsert.str());
        }
        return true;
    }
}

namespace Shader
{
    struct HotReloadManager
    {
        using KeysHolder = std::set<ShaderManager::MapKey>;

        std::unordered_map<std::string, KeysHolder> mShaderFiles;
        std::unordered_map<std::string, std::set<std::filesystem::path>> templateIncludedFiles;
        std::filesystem::file_time_type mLastAutoRecompileTime;
        bool mHotReloadEnabled;
        bool mTriggerReload;

        HotReloadManager()
        {
            mTriggerReload = false;
            mHotReloadEnabled = false;
            mLastAutoRecompileTime = std::filesystem::file_time_type::clock::now();
        }

        void addShaderFiles(const std::string& templateName, const ShaderManager::DefineMap& defines)
        {
            const std::set<std::filesystem::path>& shaderFiles = templateIncludedFiles[templateName];
            for (const std::filesystem::path& file : shaderFiles)
            {
                mShaderFiles[Files::pathToUnicodeString(file)].insert(std::make_pair(templateName, defines));
            }
        }

        void update(ShaderManager& manager, osgViewer::Viewer& viewer)
        {
            auto timeSinceLastCheckMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::filesystem::file_time_type::clock::now() - mLastAutoRecompileTime);
            if ((mHotReloadEnabled && timeSinceLastCheckMillis.count() > 200) || mTriggerReload == true)
            {
                reloadTouchedShaders(manager, viewer);
            }
            mTriggerReload = false;
        }

        void reloadTouchedShaders(ShaderManager& manager, osgViewer::Viewer& viewer)
        {
            bool threadsRunningToStop = false;
            std::vector<ShaderManager::MapKey> rebuilt;
            for (auto& [pathShaderToTest, shaderKeys] : mShaderFiles)
            {
                const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(pathShaderToTest);
                if (writeTime.time_since_epoch() > mLastAutoRecompileTime.time_since_epoch())
                {
                    if (!threadsRunningToStop)
                    {
                        threadsRunningToStop = viewer.areThreadsRunning();
                        if (threadsRunningToStop)
                            viewer.stopThreading();
                    }

                    // The cell preloading threads read the same maps through getShader
                    std::lock_guard<std::mutex> lock(manager.mMutex);

                    // Every template that includes the file is read again: those of the shaders below, and in the
                    // Metal build those of the library shaders folded into them. A template that cannot be read keeps
                    // its previous text, and the shaders of this file keep theirs.
                    const std::filesystem::path touched(pathShaderToTest);
                    bool readAgain = true;
                    for (auto& [name, text] : manager.mShaderTemplates)
                    {
                        const auto included = templateIncludedFiles.find(name);
                        if (included == templateIncludedFiles.end() || !included->second.contains(touched))
                            continue;
                        std::string source;
                        if (manager.readTemplate(name, source))
                            text = std::move(source);
                        else
                            readAgain = false;
                    }
                    if (!readAgain)
                        continue;

                    for (const auto& [templateName, shaderDefines] : shaderKeys)
                    {
                        ShaderManager::ShaderMap::iterator shaderIt
                            = manager.mShaders.find(std::make_pair(templateName, shaderDefines));
                        if (shaderIt == manager.mShaders.end())
                        {
                            Log(Debug::Error) << "Failed to find shader " << templateName;
                            continue;
                        }

                        ShaderManager::TemplateMap::iterator templateIt = manager.mShaderTemplates.find(templateName);
                        if (templateIt == manager.mShaderTemplates.end())
                            break;
                        std::string shaderSource = templateIt->second;

                        std::vector<std::string> linkedShaderNames;
                        ShaderManager::UniformList initialValues;
                        if (!manager.createSourceFromTemplate(
                                shaderSource, linkedShaderNames, templateName, shaderDefines)
                            || !manager.finishSource(shaderSource, linkedShaderNames, templateName,
                                shaderIt->second->getType(), shaderDefines, initialValues))
                        {
                            break;
                        }
                        shaderIt->second->setShaderSource(shaderSource);
                        setInitialValues(*shaderIt->second, std::move(initialValues));
                        rebuilt.emplace_back(templateName, shaderDefines);
                    }
                }
            }
            // In the Metal build a rebuilt shader may fold in library shaders it did not before: their files are
            // watched from now on (added here, as the loop above walks the same map)
            if (manager.mConvertToGlslEs)
            {
                std::lock_guard<std::mutex> lock(manager.mMutex);
                for (const auto& [templateName, shaderDefines] : rebuilt)
                    addShaderFiles(templateName, shaderDefines);
            }
            if (threadsRunningToStop)
                viewer.startThreading();
            mLastAutoRecompileTime = std::filesystem::file_time_type::clock::now();
        }
    };

    ShaderManager::ShaderManager()
        : mConvertToGlslEs(SceneUtil::GLDialect::sConvertsShadersToGlslEs)
    {
        mHotReloadManager = std::make_unique<HotReloadManager>();
    }

    ShaderManager::~ShaderManager() = default;

    void ShaderManager::setShaderPath(const std::filesystem::path& path)
    {
        mPath = path;
    }

    bool parseForeachDirective(std::string& source, const std::string& templateName, size_t foundPos)
    {
        constexpr std::string_view directiveStart = "$foreach";
        size_t iterNameStart = foundPos + directiveStart.size() + 1;
        size_t iterNameEnd = source.find_first_of(" \n\r()[].;,", iterNameStart);
        if (iterNameEnd == std::string::npos)
        {
            Log(Debug::Error) << "Shader " << templateName << " error: Unexpected EOF";
            return false;
        }
        std::string iteratorName = "$" + source.substr(iterNameStart, iterNameEnd - iterNameStart);

        size_t listStart = iterNameEnd + 1;
        size_t listEnd = source.find_first_of("\n\r", listStart);
        if (listEnd == std::string::npos)
        {
            Log(Debug::Error) << "Shader " << templateName << " error: Unexpected EOF";
            return false;
        }
        std::string_view list = std::string_view(source).substr(listStart, listEnd - listStart);
        std::vector<std::string> listElements;
        if (!list.empty())
            Misc::StringUtils::split(list, listElements, ",");

        size_t contentStart = source.find_first_not_of("\n\r", listEnd);
        constexpr std::string_view directiveEnd = "$endforeach";
        size_t contentEnd = source.find(directiveEnd, contentStart);
        if (contentEnd == std::string::npos)
        {
            Log(Debug::Error) << "Shader " << templateName << " error: Unexpected EOF";
            return false;
        }
        std::string_view content = std::string_view(source).substr(contentStart, contentEnd - contentStart);

        size_t overallEnd = contentEnd + directiveEnd.size();

        int lineNumber = getLineNumber(source, overallEnd, 2, 0);

        std::string replacement;
        for (const std::string& element : listElements)
        {
            std::string contentInstance(content);
            size_t foundIterator;
            while ((foundIterator = contentInstance.find(iteratorName)) != std::string::npos)
                contentInstance.replace(foundIterator, iteratorName.length(), element);
            replacement += contentInstance;
        }
        replacement += "\n#line " + std::to_string(lineNumber);
        source.replace(foundPos, overallEnd - foundPos, replacement);
        return true;
    }

    bool parseLinkDirective(
        std::string& source, std::string& linkTarget, const std::string& templateName, size_t foundPos)
    {
        size_t endPos = foundPos + 5;
        size_t lineEnd = source.find_first_of('\n', endPos);
        // If lineEnd = npos, this is the last line, so no need to check
        std::string linkStatement = source.substr(endPos, lineEnd - endPos);
        std::regex linkRegex(R"r(\s*"([^"]+)"\s*)r" // Find any quoted string as the link name -> match[1]
                             R"r((if\s+)r" // Begin optional condition -> match[2]
                             R"r((!)?\s*)r" // Optional ! -> match[3]
                             R"r(([_a-zA-Z0-9]+)?)r" // The condition -> match[4]
                             R"r()?\s*)r" // End optional condition -> match[2]
        );
        std::smatch linkMatch;
        bool hasCondition = false;
        std::string linkConditionExpression;
        if (std::regex_match(linkStatement, linkMatch, linkRegex))
        {
            linkTarget = linkMatch[1].str();
            hasCondition = !linkMatch[2].str().empty();
            linkConditionExpression = linkMatch[4].str();
        }
        else
        {
            Log(Debug::Error) << "Shader " << templateName << " error: Expected a shader filename to link";
            return false;
        }
        if (linkTarget.empty())
        {
            Log(Debug::Error) << "Shader " << templateName << " error: Empty link name";
            return false;
        }

        if (hasCondition)
        {
            bool condition = !(linkConditionExpression.empty() || linkConditionExpression == "0");
            if (linkMatch[3].str() == "!")
                condition = !condition;

            if (!condition)
                linkTarget.clear();
        }

        source.replace(foundPos, lineEnd - foundPos, "");
        return true;
    }

    bool parseDirectives(std::string& source, std::vector<std::string>& linkedShaderTemplateNames,
        const ShaderManager::DefineMap& defines, const ShaderManager::DefineMap& globalDefines,
        const std::string& templateName)
    {
        const char escapeCharacter = '$';
        size_t foundPos = 0;

        while ((foundPos = source.find(escapeCharacter, foundPos)) != std::string::npos)
        {
            size_t endPos = source.find_first_of(" \n\r()[].;,", foundPos);
            if (endPos == std::string::npos)
            {
                Log(Debug::Error) << "Shader " << templateName << " error: Unexpected EOF";
                return false;
            }
            std::string_view directive = std::string_view(source).substr(foundPos + 1, endPos - (foundPos + 1));
            if (directive == "foreach")
            {
                if (!parseForeachDirective(source, templateName, foundPos))
                    return false;
            }
            else if (directive == "link")
            {
                std::string linkTarget;
                if (!parseLinkDirective(source, linkTarget, templateName, foundPos))
                    return false;
                if (!linkTarget.empty() && linkTarget != templateName)
                    linkedShaderTemplateNames.push_back(std::move(linkTarget));
            }
            else
            {
                Log(Debug::Error) << "Shader " << templateName << " error: Unknown shader directive: $" << directive;
                return false;
            }
        }

        return true;
    }

    bool parseDefines(std::string& source, const ShaderManager::DefineMap& defines,
        const ShaderManager::DefineMap& globalDefines, const std::string& templateName)
    {
        const char escapeCharacter = '@';
        size_t foundPos = 0;
        std::vector<std::string> forIterators;
        while ((foundPos = source.find(escapeCharacter)) != std::string::npos)
        {
            size_t endPos = source.find_first_of(" \n\r()[].;,", foundPos);
            if (endPos == std::string::npos)
            {
                Log(Debug::Error) << "Shader " << templateName << " error: Unexpected EOF";
                return false;
            }
            std::string define = source.substr(foundPos + 1, endPos - (foundPos + 1));
            ShaderManager::DefineMap::const_iterator defineFound = defines.find(define);
            ShaderManager::DefineMap::const_iterator globalDefineFound = globalDefines.find(define);
            if (define == "foreach")
            {
                source.replace(foundPos, 1, "$");
                size_t iterNameStart = endPos + 1;
                size_t iterNameEnd = source.find_first_of(" \n\r()[].;,", iterNameStart);
                if (iterNameEnd == std::string::npos)
                {
                    Log(Debug::Error) << "Shader " << templateName << " error: Unexpected EOF";
                    return false;
                }
                forIterators.push_back(source.substr(iterNameStart, iterNameEnd - iterNameStart));
            }
            else if (define == "endforeach")
            {
                source.replace(foundPos, 1, "$");
                if (forIterators.empty())
                {
                    Log(Debug::Error) << "Shader " << templateName << " error: endforeach without foreach";
                    return false;
                }
                else
                    forIterators.pop_back();
            }
            else if (define == "link")
            {
                source.replace(foundPos, 1, "$");
            }
            else if (std::find(forIterators.begin(), forIterators.end(), define) != forIterators.end())
            {
                source.replace(foundPos, 1, "$");
            }
            else if (defineFound != defines.end())
            {
                source.replace(foundPos, endPos - foundPos, defineFound->second);
            }
            else if (globalDefineFound != globalDefines.end())
            {
                source.replace(foundPos, endPos - foundPos, globalDefineFound->second);
            }
            else
            {
                Log(Debug::Error) << "Shader " << templateName << " error: Undefined " << define;
                return false;
            }
        }
        return true;
    }

    osg::ref_ptr<osg::Shader> ShaderManager::getShader(
        std::string templateName, const ShaderManager::DefineMap& defines, std::optional<osg::Shader::Type> type)
    {
        std::unique_lock<std::mutex> lock(mMutex);

        return getShaderInternal(std::move(templateName), defines, type);
    }

    osg::ref_ptr<osg::Shader> ShaderManager::getShaderInternal(
        std::string templateName, const ShaderManager::DefineMap& defines, std::optional<osg::Shader::Type> type)
    {
        // TODO: Implement mechanism to switch to core or compatibility profile shaders.
        // This logic is temporary until core support is supported.
        if (getRootPrefix(templateName).empty())
            templateName = "compatibility/" + templateName;

        // read the template if we haven't already
        TemplateMap::iterator templateIt = loadTemplate(templateName);
        if (templateIt == mShaderTemplates.end())
            return nullptr;

        ShaderMap::iterator shaderIt = mShaders.find(std::make_pair(templateName, defines));
        if (shaderIt == mShaders.end())
        {
            const osg::Shader::Type shaderType = type ? *type : getShaderType(templateName);
            std::string shaderSource = templateIt->second;
            std::vector<std::string> linkedShaderNames;
            UniformList initialValues;
            if (!createSourceFromTemplate(shaderSource, linkedShaderNames, templateName, defines)
                || !finishSource(shaderSource, linkedShaderNames, templateName, shaderType, defines, initialValues))
            {
                // Add to the cache anyway to avoid logging the same error over and over.
                mShaders.insert(std::make_pair(std::make_pair(templateName, defines), nullptr));
                return nullptr;
            }

            osg::ref_ptr<osg::Shader> shader(new osg::Shader(shaderType));
            shader->setShaderSource(shaderSource);
            setInitialValues(*shader, std::move(initialValues));
            // Assign a unique prefix to allow the SharedStateManager to compare shaders efficiently.
            // Append shader source filename for debugging.
            static unsigned int counter = 0;
            shader->setName(std::format("{} {}", counter++, templateName));

            mHotReloadManager->addShaderFiles(templateName, defines);

            getLinkedShaders(shader, linkedShaderNames, defines);

            shaderIt = mShaders.insert(std::make_pair(std::make_pair(templateName, defines), shader)).first;
        }
        return shaderIt->second;
    }

    ShaderManager::TemplateMap::iterator ShaderManager::loadTemplate(const std::string& templateName)
    {
        TemplateMap::iterator templateIt = mShaderTemplates.find(templateName);
        if (templateIt != mShaderTemplates.end())
            return templateIt;

        std::string source;
        if (!readTemplate(templateName, source))
            return mShaderTemplates.end();
        return mShaderTemplates.insert(std::make_pair(templateName, std::move(source))).first;
    }

    bool ShaderManager::readTemplate(const std::string& templateName, std::string& source)
    {
        std::filesystem::path path = mPath / templateName;
        std::ifstream stream;
        stream.open(path);
        if (stream.fail())
        {
            Log(Debug::Error) << "Failed to open shader " << path << ": " << std::generic_category().message(errno);
            return false;
        }
        std::stringstream buffer;
        buffer << stream.rdbuf();

        // parse includes
        int fileNumber = 1;
        std::string text = buffer.str();
        std::set<std::filesystem::path> insertedPaths;
        if (!addLineDirectivesAfterConditionalBlocks(text)
            || !parseIncludes(mPath, text, templateName, fileNumber, {}, insertedPaths))
            return false;
        mHotReloadManager->templateIncludedFiles[templateName] = std::move(insertedPaths);
        source = std::move(text);
        return true;
    }

    bool ShaderManager::finishSource(std::string& source, std::vector<std::string>& linkedShaderNames,
        const std::string& templateName, osg::Shader::Type type, const DefineMap& defines, UniformList& initialValues)
    {
        if (!mConvertToGlslEs)
            return true;

        // OpenGL ES links one shader object per stage: the library shaders this one links are folded into its text.
        // Their files count as files of this shader for the hot reload.
        std::vector<std::string> linkedSources;
        std::set<std::string> seen{ templateName };
        std::deque<std::string> todo(linkedShaderNames.begin(), linkedShaderNames.end());
        linkedShaderNames.clear();
        while (!todo.empty())
        {
            const std::string name = std::move(todo.front());
            todo.pop_front();
            if (!seen.insert(name).second)
                continue;
            const TemplateMap::iterator templateIt = loadTemplate(name);
            if (templateIt == mShaderTemplates.end())
                return false;
            std::string linkedSource = templateIt->second;
            std::vector<std::string> moreLinked;
            if (!createSourceFromTemplate(linkedSource, moreLinked, name, defines))
                return false;
            linkedSources.push_back(std::move(linkedSource));
            todo.insert(todo.end(), moreLinked.begin(), moreLinked.end());
            const std::set<std::filesystem::path> linkedFiles = mHotReloadManager->templateIncludedFiles[name];
            mHotReloadManager->templateIncludedFiles[templateName].insert(linkedFiles.begin(), linkedFiles.end());
        }

        GlslEsShader converted = convertToGlslEs(foldLinkedShaders(source, linkedSources), type);
        for (const std::string& error : converted.mErrors)
            Log(Debug::Error) << "Shader " << templateName << " error: cannot be converted to GLSL ES 3.00: " << error;
        if (!converted.mErrors.empty())
            return false;

        static std::set<std::pair<std::string, std::string>> reported;
        for (const std::string& dropped : converted.mDropped)
            if (reported.emplace(templateName, dropped).second)
                Log(Debug::Warning) << "Shader " << templateName << " goes without " << dropped
                                    << ": OpenGL ES 3.0 has no equivalent";

        for (const UniformInitialiser& initialiser : converted.mUniformInitialisers)
        {
            if (osg::ref_ptr<osg::Uniform> uniform = makeUniform(initialiser))
                initialValues.push_back(std::move(uniform));
            else
                Log(Debug::Error) << "Shader " << templateName << " error: uniform " << initialiser.mName
                                  << " loses its initial value " << initialiser.mValue
                                  << ": the shader manager cannot set a " << initialiser.mType << " to it";
        }

        source = std::move(converted.mSource);
        return true;
    }

    std::vector<osg::ref_ptr<osg::Uniform>> getInitialValues(const osg::Program& program)
    {
        std::vector<osg::ref_ptr<osg::Uniform>> values;
        for (unsigned int i = 0; i < program.getNumShaders(); ++i)
            if (const auto* data = dynamic_cast<const InitialValues*>(program.getShader(i)->getUserData()))
                values.insert(values.end(), data->mValues.begin(), data->mValues.end());
        return values;
    }

    class SamplerProgram : public osg::Program
    {
    public:
        SamplerProgram() = default;
        explicit SamplerProgram(const ShaderManager::SamplerBindingMap& samplers);
        SamplerProgram(
            const ShaderManager::SamplerBindingMap& samplers, const osg::Program& other, const osg::CopyOp& copyop);
        SamplerProgram(const SamplerProgram& other, const osg::CopyOp& copyop = osg::CopyOp::SHALLOW_COPY);

        META_Object(Shader, SamplerProgram)

        bool hasSamplers(const ShaderManager::SamplerBindingMap& samplers) const;

        void apply(osg::State& state) const override;

        void compileGLObjects(osg::State& state) const override;

        void resizeGLObjectBuffers(unsigned int maxSize) override;

    private:
        std::vector<osg::ref_ptr<osg::Uniform>> mSamplers;
        mutable osg::buffered_value<const PerContextProgram*> mApplied;
    };

    SamplerProgram::SamplerProgram(const ShaderManager::SamplerBindingMap& samplers)
    {
        for (const auto& [name, unit] : samplers)
            mSamplers.emplace_back(new osg::Uniform(name.c_str(), unit));
    }

    SamplerProgram::SamplerProgram(
        const ShaderManager::SamplerBindingMap& samplers, const osg::Program& other, const osg::CopyOp& copyop)
        : osg::Program(other, copyop)
    {
        for (const auto& [name, index] : other.getUniformBlockBindingList())
            addBindUniformBlock(name, index);
        for (const auto& [name, unit] : samplers)
            mSamplers.emplace_back(new osg::Uniform(name.c_str(), unit));
    }

    SamplerProgram::SamplerProgram(const SamplerProgram& other, const osg::CopyOp& copyop)
        : SamplerProgram({}, other, copyop)
    {
        mSamplers = other.mSamplers;
    }

    bool SamplerProgram::hasSamplers(const ShaderManager::SamplerBindingMap& samplers) const
    {
        if (mSamplers.size() != samplers.size())
            return false;
        for (const osg::ref_ptr<osg::Uniform>& sampler : mSamplers)
        {
            int unit = -1;
            sampler->get(unit);
            const auto it = samplers.find(sampler->getName());
            if (it == samplers.end() || it->second != unit)
                return false;
        }
        return true;
    }

    void SamplerProgram::apply(osg::State& state) const
    {
        const PerContextProgram* pcp = getPCP(state);
        const bool relink = pcp->needsLink();

        osg::Program::apply(state);

        if (state.getLastAppliedProgramObject() != pcp)
            return;

        const PerContextProgram*& applied = mApplied[state.getContextID()];
        if (applied == pcp && !relink)
            return;

        // Set before the state set's own values, as GLSL does with initialisers
        for (const osg::ref_ptr<osg::Uniform>& value : getInitialValues(*this))
            pcp->apply(*value);
        for (const osg::ref_ptr<osg::Uniform>& sampler : mSamplers)
            pcp->apply(*sampler);
        applied = pcp;
    }

    // A state set compiled ahead of drawing may link the program again (after a hot reload or new defines), which
    // resets its uniforms: the next apply sets the initial values and samplers again
    void SamplerProgram::compileGLObjects(osg::State& state) const
    {
        const bool relink = getNumShaders() != 0 && getPCP(state)->needsLink();
        osg::Program::compileGLObjects(state);
        if (relink)
            mApplied[state.getContextID()] = nullptr;
    }

    void SamplerProgram::resizeGLObjectBuffers(unsigned int maxSize)
    {
        osg::Program::resizeGLObjectBuffers(maxSize);
        mApplied.resize(maxSize);
    }

    osg::ref_ptr<osg::Program> ShaderManager::getProgram(const std::string& templateName, const DefineMap& defines,
        const osg::Program* programTemplate, const SamplerBindingMap& samplers)
    {
        auto vert = getShader(templateName + ".vert", defines);
        auto frag = getShader(templateName + ".frag", defines);

        if (!vert || !frag)
            throw std::runtime_error("failed initializing shader: " + templateName);

        return getProgram(std::move(vert), std::move(frag), programTemplate, samplers);
    }

    osg::ref_ptr<osg::Program> ShaderManager::getProgram(osg::ref_ptr<osg::Shader> vertexShader,
        osg::ref_ptr<osg::Shader> fragmentShader, const osg::Program* programTemplate,
        const SamplerBindingMap& samplers)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        ProgramMap::iterator found = mPrograms.find(std::make_pair(vertexShader, fragmentShader));
        if (found == mPrograms.end())
        {
            if (!programTemplate)
                programTemplate = mProgramTemplate;
            osg::ref_ptr<SamplerProgram> program = programTemplate
                ? new SamplerProgram(samplers, *programTemplate, osg::CopyOp::SHALLOW_COPY)
                : new SamplerProgram(samplers);
            program->addShader(vertexShader);
            program->addShader(fragmentShader);
            addLinkedShaders(vertexShader, program);
            addLinkedShaders(fragmentShader, program);
            found = mPrograms.insert(std::make_pair(std::make_pair(vertexShader, fragmentShader), program)).first;
        }
        else
            assert(static_cast<SamplerProgram*>(found->second.get())->hasSamplers(samplers));
        return found->second;
    }

    osg::ref_ptr<osg::Program> ShaderManager::cloneProgram(const osg::Program* src)
    {
        osg::ref_ptr<osg::Program> program = static_cast<osg::Program*>(src->clone(osg::CopyOp::SHALLOW_COPY));
        for (auto& [name, idx] : src->getUniformBlockBindingList())
            program->addBindUniformBlock(name, idx);
        return program;
    }

    ShaderManager::DefineMap ShaderManager::getGlobalDefines()
    {
        return DefineMap(mGlobalDefines);
    }

    void ShaderManager::setGlobalDefines(DefineMap& globalDefines)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mGlobalDefines = globalDefines;
        // clear out linked dependencies - changing defines may make them obsolete
        for (const auto& [pair, program] : mPrograms)
        {
            for (unsigned int i = 0; i < program->getNumShaders();)
            {
                if (program->getShader(i) != pair.first && program->getShader(i) != pair.second)
                    program->removeShader(program->getShader(i));
                else
                    ++i;
            }
        }
        for (const auto& [key, shader] : mShaders)
        {
            std::string templateId = key.first;
            ShaderManager::DefineMap defines = key.second;
            if (shader == nullptr)
                // I'm not sure how to handle a shader that was already broken as there's no way to get a potential
                // replacement to the nodes that need it.
                continue;
            const TemplateMap::iterator templateIt = mShaderTemplates.find(templateId);
            if (templateIt == mShaderTemplates.end())
            {
                Log(Debug::Error) << "Shader " << templateId << " keeps its text: its template is not loaded";
                continue;
            }
            std::string shaderSource = templateIt->second;
            std::vector<std::string> linkedShaderNames;
            UniformList initialValues;
            if (!createSourceFromTemplate(shaderSource, linkedShaderNames, templateId, defines)
                || !finishSource(
                    shaderSource, linkedShaderNames, templateId, shader->getType(), defines, initialValues))
                // We just broke the shader and there's no way to force existing objects back to fixed-function mode as
                // we would when creating the shader. If we put a nullptr in the shader map, we just lose the ability to
                // put a working one in later.
                continue;
            shader->setShaderSource(shaderSource);
            setInitialValues(*shader, std::move(initialValues));
            // The library shaders folded in for the new defines are watched by the hot reload
            if (mConvertToGlslEs)
                mHotReloadManager->addShaderFiles(templateId, defines);

            getLinkedShaders(shader, linkedShaderNames, defines);
        }
        for (const auto& [pair, program] : mPrograms)
        {
            addLinkedShaders(pair.first, program);
            addLinkedShaders(pair.second, program);
        }
    }

    void ShaderManager::releaseGLObjects(osg::State* state)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& [_, shader] : mShaders)
        {
            if (shader != nullptr)
                shader->releaseGLObjects(state);
        }
        for (const auto& [_, program] : mPrograms)
            program->releaseGLObjects(state);
    }

    bool ShaderManager::createSourceFromTemplate(std::string& source,
        std::vector<std::string>& linkedShaderTemplateNames, const std::string& templateName,
        const ShaderManager::DefineMap& defines)
    {
        if (!parseDefines(source, defines, mGlobalDefines, templateName))
            return false;
        if (!parseDirectives(source, linkedShaderTemplateNames, defines, mGlobalDefines, templateName))
            return false;
        return true;
    }

    void ShaderManager::getLinkedShaders(
        osg::ref_ptr<osg::Shader> shader, const std::vector<std::string>& linkedShaderNames, const DefineMap& defines)
    {
        mLinkedShaders.erase(shader);
        if (linkedShaderNames.empty())
            return;

        for (auto& linkedShaderName : linkedShaderNames)
        {
            auto linkedShader = getShaderInternal(linkedShaderName, defines, shader->getType());
            if (linkedShader)
                mLinkedShaders[shader].emplace_back(linkedShader);
        }
    }

    void ShaderManager::addLinkedShaders(osg::ref_ptr<osg::Shader> shader, osg::ref_ptr<osg::Program> program)
    {
        auto linkedIt = mLinkedShaders.find(shader);
        if (linkedIt != mLinkedShaders.end())
            for (const auto& linkedShader : linkedIt->second)
                program->addShader(linkedShader);
    }

    int ShaderManager::reserveGlobalTextureUnits(Slot slot, int count)
    {
        // TODO: Reuse units when count increase forces reallocation
        // TODO: Warn if trampling on the ~8 units needed by model textures
        auto unit = mReservedTextureUnitsBySlot[static_cast<int>(slot)];
        if (unit.index >= 0 && unit.count >= count)
            return unit.index;

        if (getAvailableTextureUnits() < count + 1)
            throw std::runtime_error("Can't reserve texture unit; no available units");
        mReservedTextureUnits += count;

        unit.index = mMaxTextureUnits - mReservedTextureUnits;
        unit.count = count;

        mReservedTextureUnitsBySlot[static_cast<int>(slot)] = unit;

        std::string_view slotDescr;
        switch (slot)
        {
            case Slot::OpaqueDepthTexture:
                slotDescr = "opaque depth texture";
                break;
            case Slot::OpaqueColorTexture:
                slotDescr = "opaque color texture";
                break;
            case Slot::SkyTexture:
                slotDescr = "sky RTT";
                break;
            case Slot::ShadowMaps:
                slotDescr = "shadow maps";
                break;
            default:
                slotDescr = "UNKNOWN";
        }
        if (unit.count == 1)
            Log(Debug::Info) << "Reserving texture unit for " << slotDescr << ": " << unit.index;
        else
            Log(Debug::Info) << "Reserving texture units for " << slotDescr << ": " << unit.index << ".."
                             << (unit.index + count - 1);

        return unit.index;
    }

    void ShaderManager::update(osgViewer::Viewer& viewer)
    {
        mHotReloadManager->update(*this, viewer);
    }

    void ShaderManager::setHotReloadEnabled(bool value)
    {
        mHotReloadManager->mHotReloadEnabled = value;
    }

    void ShaderManager::triggerShaderReload()
    {
        mHotReloadManager->mTriggerReload = true;
    }

    ShaderManager::DefineMap getDefaultDefines()
    {
        return {
            { "forcePPL", "0" },
            { "clamp", "1" },
            { "preLightEnv", "0" },
            { "radialFog", "0" },
            { "exponentialFog", "0" },
            { "reverseZ", "0" },
            { "waterRefraction", "0" },
            { "classicFalloff", "1" },
            { "skyBlending", "0" },
            { "disableNormals", "1" },
            { "useGPUShader4", "0" },
            { "useOVR_multiview", "0" },
            { "distorionRTRatio", "0" },
            { "numViews", "1" },
            { "particle", "0" },
            { "particlePointLighting", "1" },
        };
    }
}
