#include "extensions.hpp"

#include <components/interpreter/installopcodes.hpp>
#include <components/interpreter/interpreter.hpp>

namespace OFScript
{
    void installOpcodes(Interpreter::Interpreter& interpreter)
    {
        // Only the opcodes of the language itself (arithmetic, flow control, messages) are left: the Morrowind
        // functions (the opcode segments that hold them) were removed with the running of Morrowind scripts.
        Interpreter::installOpcodes(interpreter);
    }
}
