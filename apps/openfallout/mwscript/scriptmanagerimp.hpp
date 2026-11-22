#ifndef GAME_SCRIPT_SCRIPTMANAGER_H
#define GAME_SCRIPT_SCRIPTMANAGER_H

#include <unordered_map>

#include <components/compiler/locals.hpp>
#include <components/compiler/streamerrorhandler.hpp>

#include <components/esm/refid.hpp>

#include "../mwbase/scriptmanager.hpp"

namespace OFWorld
{
    class ESMStore;
}

namespace Compiler
{
    class Context;
}

namespace OFScript
{
    /// The variables that a Morrowind script declares, for the dialogue that still reads them (Morrowind scripts are
    /// no longer run).
    class ScriptManager : public OFBase::ScriptManager
    {
        Compiler::StreamErrorHandler mErrorHandler;
        const OFWorld::ESMStore& mStore;
        Compiler::Context& mCompilerContext;

        std::unordered_map<ESM::RefId, Compiler::Locals> mOtherLocals;

    public:
        ScriptManager(const OFWorld::ESMStore& store, Compiler::Context& compilerContext, int warningsMode);

        const Compiler::Locals& getLocals(const ESM::RefId& name) override;
        ///< Return locals for script \a name.

        const Compiler::Extensions& getExtensions() const override;
    };
}

#endif
