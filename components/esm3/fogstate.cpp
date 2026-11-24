#include "fogstate.hpp"

#include "esmreader.hpp"

namespace ESM
{
    void skipFogState(ESMReader& esm)
    {
        if (esm.isNextSub("BOUN"))
            esm.skipHSub();
        if (esm.isNextSub("ANGL"))
            esm.skipHSub();
        if (esm.isNextSub("CNTR"))
            esm.skipHSub();
        while (esm.isNextSub("FTEX"))
            esm.skipHSub();
    }
}
