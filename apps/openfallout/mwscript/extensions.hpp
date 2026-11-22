#ifndef GAME_SCRIPT_EXTENSIONS_H
#define GAME_SCRIPT_EXTENSIONS_H

namespace Interpreter
{
    class Interpreter;
}

namespace OFScript
{
    void installOpcodes(Interpreter::Interpreter& interpreter);
}

#endif
