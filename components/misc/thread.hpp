#ifndef OPENFALLOUT_COMPONENTS_MISC_THREAD_H
#define OPENFALLOUT_COMPONENTS_MISC_THREAD_H

namespace Misc
{
    void setCurrentThreadIdlePriority();

    /// Marks the calling thread as one the next frame waits for (the main thread, the draw thread). This is the
    /// user-interactive QoS class, which keeps the thread on the performance cores; threads the caller starts
    /// afterwards are not raised with it.
    void setCurrentThreadFrameCriticalPriority();
}

#endif
