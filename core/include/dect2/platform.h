// Small OS abstractions used by the core: thread priority.
#pragma once

#if defined(__APPLE__)
#include <pthread.h>
#include <pthread/qos.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace dect2 {

enum class ThreadPriority {
    Realtime,    // the sample path: must never wait for decoders or the UI
    High,        // playback threads
    Background   // heavy decoding that can yield to the above
};

// Best effort: failures (for example missing privileges for a raised priority) are ignored.
inline void setThreadPriority(ThreadPriority p) {
#if defined(__APPLE__)
    switch (p) {
    case ThreadPriority::Realtime: pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); break;
    case ThreadPriority::High: pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0); break;
    case ThreadPriority::Background: pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0); break;
    }
#elif defined(__linux__)
    // nice values are per thread on Linux; lowering the value needs privileges, raising it never does
    const int prio = p == ThreadPriority::Realtime ? -5 : p == ThreadPriority::High ? -2 : 5;
    (void)setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), prio);
#else
    (void)p;
#endif
}

} // namespace dect2
