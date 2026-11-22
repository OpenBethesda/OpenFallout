#include "scriptmanagerimp.hpp"

#include <exception>
#include <sstream>
#include <stdexcept>

#include <components/debug/debuglog.hpp>

#include <components/esm/refid.hpp>
#include <components/esm3/loadscpt.hpp>

#include <components/misc/strings/lower.hpp>

#include <components/compiler/context.hpp>
#include <components/compiler/exception.hpp>
#include <components/compiler/quickfileparser.hpp>
#include <components/compiler/scanner.hpp>

#include "../mwworld/esmstore.hpp"

namespace OFScript
{
    ScriptManager::ScriptManager(const OFWorld::ESMStore& store, Compiler::Context& compilerContext, int warningsMode)
        : mErrorHandler()
        , mStore(store)
        , mCompilerContext(compilerContext)
    {
        mErrorHandler.setWarningsMode(warningsMode);
    }

    const Compiler::Locals& ScriptManager::getLocals(const ESM::RefId& name)
    {
        {
            auto iter = mOtherLocals.find(name);

            if (iter != mOtherLocals.end())
                return iter->second;
        }

        if (const ESM::Script* script = mStore.get<ESM::Script>().search(name))
        {
            Compiler::Locals locals;

            const Compiler::ContextOverride override(mErrorHandler, name.getRefIdString() + "[local variables]");

            std::istringstream stream(script->mScriptText);
            Compiler::QuickFileParser parser(mErrorHandler, mCompilerContext, locals);
            Compiler::Scanner scanner(mErrorHandler, stream, mCompilerContext.getExtensions());
            try
            {
                scanner.scan(parser);
            }
            catch (const Compiler::SourceException&)
            {
                // error has already been reported via error handler
                locals.clear();
            }
            catch (const std::exception& error)
            {
                Log(Debug::Error) << "Error: An exception has been thrown: " << error.what();
                locals.clear();
            }

            auto iter = mOtherLocals.emplace(name, locals).first;

            return iter->second;
        }

        throw std::logic_error("script " + name.toDebugString() + " does not exist");
    }

    const Compiler::Extensions& ScriptManager::getExtensions() const
    {
        return *mCompilerContext.getExtensions();
    }
}
