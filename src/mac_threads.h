/*
 * mac_threads.h - minimal Thread Manager / Toolbox declarations used by
 * pthreads-retro68.
 *
 * The Multiversal Interfaces shipped with Retro68 do not contain a
 * <Threads.h>, so the handful of calls we need are declared here.
 *
 *   68k        : A-trap _ThreadDispatch (0xABF2) with a selector in D0.
 *   PowerPC    : imported from ThreadsLib (CFM shared library).
 *   Carbon     : imported from CarbonLib.
 *
 * This header is private to the library and is not installed.
 */
#ifndef PTHREADS_RETRO68_MAC_THREADS_H
#define PTHREADS_RETRO68_MAC_THREADS_H

#include <stdint.h>
#include <MacTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t mt_ThreadState;
enum {
    kReadyThreadState   = 0,
    kStoppedThreadState = 1,
    kRunningThreadState = 2
};

typedef uint32_t mt_ThreadStyle;
enum {
    kCooperativeThread = 1L << 0,
    kPreemptiveThread  = 1L << 1
};

typedef uint32_t mt_ThreadID;
enum {
    kNoThreadID          = 0,
    kCurrentThreadID     = 1,
    kApplicationThreadID = 2
};

typedef uint32_t mt_ThreadOptions;
enum {
    kNewSuspend       = (1 << 0),
    kUsePremadeThread = (1 << 1),
    kCreateIfNeeded   = (1 << 2),
    kFPUNotNeeded     = (1 << 3),
    kExactMatchThread = (1 << 4)
};

/* Thread entry point.  On 68k the Thread Manager calls it with the Pascal
 * calling convention; on PowerPC 'pascal' is defined away by Multiverse.h
 * and a plain C function pointer (transition vector) is expected.         */
typedef pascal void *(*mt_ThreadEntryProcPtr)(void *threadParam);

/* Gestalt selector for the Thread Manager */
#define MT_GESTALT_THREAD_MGR_ATTR   0x74686473L   /* 'thds' */
#define MT_GESTALT_THREAD_MGR_PRESENT 0            /* bit number */

#if defined(__m68k__)

pascal OSErr NewThread(mt_ThreadStyle threadStyle,
                       mt_ThreadEntryProcPtr threadEntry,
                       void *threadParam,
                       Size stackSize,
                       mt_ThreadOptions options,
                       void **threadResult,
                       mt_ThreadID *threadMade)
    = { 0x303C, 0x0E03, 0xABF2 };

pascal OSErr DisposeThread(mt_ThreadID threadToDump,
                           void *threadResult,
                           Boolean recycleThread)
    = { 0x303C, 0x0504, 0xABF2 };

pascal OSErr YieldToThread(mt_ThreadID suggestedThread)
    = { 0x303C, 0x0205, 0xABF2 };

pascal OSErr YieldToAnyThread(void)
    = { 0x42A7, 0x303C, 0x0205, 0xABF2 };

pascal OSErr MacGetCurrentThread(mt_ThreadID *currentThreadID)
    = { 0x303C, 0x0206, 0xABF2 };

pascal OSErr GetThreadState(mt_ThreadID threadToGet,
                            mt_ThreadState *threadState)
    = { 0x303C, 0x0407, 0xABF2 };

pascal OSErr SetThreadState(mt_ThreadID threadToSet,
                            mt_ThreadState newState,
                            mt_ThreadID suggestedThread)
    = { 0x303C, 0x0508, 0xABF2 };

pascal OSErr GetDefaultThreadStackSize(mt_ThreadStyle threadStyle,
                                       Size *stackSize)
    = { 0x303C, 0x0413, 0xABF2 };

pascal OSErr ThreadBeginCritical(void)
    = { 0x303C, 0x000B, 0xABF2 };

pascal OSErr ThreadEndCritical(void)
    = { 0x303C, 0x000C, 0xABF2 };

#define mt_GetCurrentThread MacGetCurrentThread

/* _Microseconds (0xA193): returns hi in A0, lo in D0. */
static inline uint64_t mt_Microseconds(void)
{
    register uint32_t hi __asm__("a0");
    register uint32_t lo __asm__("d0");
    __asm__ __volatile__(".short 0xA193"
                         : "=r"(hi), "=r"(lo)
                         :
                         : "d1", "d2", "a1", "cc", "memory");
    return ((uint64_t)hi << 32) | lo;
}

#else /* PowerPC (classic CFM or Carbon) */

OSErr NewThread(mt_ThreadStyle threadStyle,
                mt_ThreadEntryProcPtr threadEntry,
                void *threadParam,
                Size stackSize,
                mt_ThreadOptions options,
                void **threadResult,
                mt_ThreadID *threadMade);
OSErr DisposeThread(mt_ThreadID threadToDump, void *threadResult,
                    Boolean recycleThread);
OSErr YieldToThread(mt_ThreadID suggestedThread);
OSErr YieldToAnyThread(void);
OSErr GetCurrentThread(mt_ThreadID *currentThreadID);
OSErr GetThreadState(mt_ThreadID threadToGet, mt_ThreadState *threadState);
OSErr SetThreadState(mt_ThreadID threadToSet, mt_ThreadState newState,
                     mt_ThreadID suggestedThread);
OSErr GetDefaultThreadStackSize(mt_ThreadStyle threadStyle, Size *stackSize);
OSErr ThreadBeginCritical(void);
OSErr ThreadEndCritical(void);

#define mt_GetCurrentThread GetCurrentThread

typedef struct { uint32_t hi, lo; } mt_UnsignedWide;
void Microseconds(mt_UnsignedWide *microTickCount);

static inline uint64_t mt_Microseconds(void)
{
    mt_UnsignedWide w;
    Microseconds(&w);
    return ((uint64_t)w.hi << 32) | w.lo;
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* PTHREADS_RETRO68_MAC_THREADS_H */
