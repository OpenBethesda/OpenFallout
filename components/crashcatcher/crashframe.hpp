#ifndef OPENFALLOUT_COMPONENTS_CRASHCATCHER_CRASHFRAME_H
#define OPENFALLOUT_COMPONENTS_CRASHCATCHER_CRASHFRAME_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace CrashCatcher
{
    /// One line of the frame list that the crash log carries when no debugger could be started:
    /// "<label> 0x<address> <image file name>+0x<offset in the image> <symbol>+0x<offset in the symbol>\n".
    /// The symbol part is left out when the symbol is not known, and "(no image)" stands for the image when no loaded
    /// image holds the address (a call through a null function pointer). The line is cut to the room in `out` and
    /// ends with a null character when there is room for one. Gives the characters written, the null not counted.
    /// Called from a signal handler: no allocation.
    std::size_t formatFrame(std::span<char> out, const char* label, std::uintptr_t address, const char* image,
        std::uintptr_t imageBase, const char* symbol, std::uintptr_t symbolAddress);

    /// The label "#<number>" of a frame of the frame pointer chain, cut to the room in `out` and ended with a null
    /// character when there is room for one. Gives the characters written, the null not counted. Called from a signal
    /// handler: no allocation, no formatting function of the C library.
    std::size_t formatFrameNumber(std::span<char> out, unsigned int number);
}

#endif
