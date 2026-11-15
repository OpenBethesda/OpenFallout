#include "platform.hpp"

namespace Platform
{

    static void increaseFileHandleLimit()
    {
    }

    void init()
    {
        increaseFileHandleLimit();
    }

}
