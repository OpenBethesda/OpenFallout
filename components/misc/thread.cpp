#include "thread.hpp"

#include <components/debug/debuglog.hpp>

#include <cstring>
#include <thread>

#include <pthread/qos.h>

namespace Misc
{
    void setCurrentThreadIdlePriority()
    {
        // Background QoS also throttles I/O.
        const int result = pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
        if (result == 0)
            Log(Debug::Verbose) << "Using idle priority for thread=" << std::this_thread::get_id();
        else
            Log(Debug::Warning) << "Failed to set idle priority for thread=" << std::this_thread::get_id() << ": "
                                << std::generic_category().message(result);
    }

    void setCurrentThreadFrameCriticalPriority()
    {
        const int result = pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        if (result == 0)
            Log(Debug::Verbose) << "Using frame-critical priority for thread=" << std::this_thread::get_id();
        else
            Log(Debug::Warning) << "Failed to set frame-critical priority for thread=" << std::this_thread::get_id()
                                << ": " << std::generic_category().message(result);
    }
}
