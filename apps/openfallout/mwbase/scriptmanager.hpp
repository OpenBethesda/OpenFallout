#ifndef GAME_MWBASE_SCRIPTMANAGER_H
#define GAME_MWBASE_SCRIPTMANAGER_H

#include <string_view>

namespace ESM
{
    class RefId;
}

namespace Compiler
{
    class Extensions;
    class Locals;
}

namespace OFBase
{
    /// \brief Interface for script manager (implemented in MWScript)
    class ScriptManager
    {
        ScriptManager(const ScriptManager&);
        ///< not implemented

        ScriptManager& operator=(const ScriptManager&);
        ///< not implemented

    public:
        ScriptManager() {}

        virtual ~ScriptManager() {}

        virtual const Compiler::Locals& getLocals(const ESM::RefId& name) = 0;
        ///< Return locals for script \a name.

        virtual const Compiler::Extensions& getExtensions() const = 0;
    };
}

#endif
