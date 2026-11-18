/*
 * pthread.h - POSIX threads for classic Mac OS (Retro68), implemented on
 * top of the Thread Manager.
 *
 * This header shadows newlib's <pthread.h> (which is compiled out for the
 * Retro68 targets).  The opaque object types (pthread_t, pthread_mutex_t,
 * ...) are the ones newlib already defines in <sys/_pthreadtypes.h>, so
 * code that only includes <sys/types.h> stays ABI compatible.
 *
 * Threads are COOPERATIVE: a thread runs until it blocks in one of the
 * functions of this library, calls sched_yield()/pthread_yield(), or
 * sleeps.  See README.md for details.
 */
#ifndef PTHREADS_RETRO68_PTHREAD_H
#define PTHREADS_RETRO68_PTHREAD_H

#define __PTHREAD_h            /* keep newlib's pthread.h out */
#define PTHREADS_RETRO68 1
#define PTHREADS_RETRO68_VERSION_MAJOR 1
#define PTHREADS_RETRO68_VERSION_MINOR 0
#define PTHREADS_RETRO68_VERSION_PATCH 0

#include <sys/types.h>
#include <sys/sched.h>
#include <stddef.h>
#include <time.h>
#include <sched.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
 * Base types.  Normally provided by newlib's <sys/_pthreadtypes.h> via
 * <sys/types.h>.  In strict ISO mode (e.g. -std=c99 without
 * _POSIX_C_SOURCE) newlib hides them, so provide identical definitions.
 * --------------------------------------------------------------------- */
#if !defined(_POSIX_THREADS) && !(__POSIX_VISIBLE >= 199506)
typedef __uint32_t pthread_t;
typedef struct {
    int is_initialized;
    void *stackaddr;
    int stacksize;
    int contentionscope;
    int inheritsched;
    int schedpolicy;
    struct sched_param schedparam;
    int detachstate;
} pthread_attr_t;
typedef __uint32_t pthread_mutex_t;
typedef struct {
    int is_initialized;
    int recursive;              /* holds the mutex type */
} pthread_mutexattr_t;
typedef __uint32_t pthread_cond_t;
typedef struct {
    int is_initialized;
    clock_t clock;
} pthread_condattr_t;
typedef __uint32_t pthread_key_t;
typedef struct {
    int is_initialized;
    int init_executed;
} pthread_once_t;
#define PTHREAD_SCOPE_PROCESS   0
#define PTHREAD_SCOPE_SYSTEM    1
#define PTHREAD_INHERIT_SCHED   1
#define PTHREAD_EXPLICIT_SCHED  2
#define PTHREAD_CREATE_DETACHED 0
#define PTHREAD_CREATE_JOINABLE 1
#define _PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) 0xFFFFFFFF)
#define _PTHREAD_COND_INITIALIZER  ((pthread_cond_t) 0xFFFFFFFF)
#define _PTHREAD_ONCE_INIT  { 1, 0 }
#endif

/* Types newlib only defines for RTOS targets. */
#if !defined(_POSIX_READER_WRITER_LOCKS)
typedef __uint32_t pthread_rwlock_t;
typedef struct {
    int is_initialized;
} pthread_rwlockattr_t;
#define _PTHREAD_RWLOCK_INITIALIZER ((pthread_rwlock_t) 0xFFFFFFFF)
#endif

#if !defined(_POSIX_BARRIERS)
typedef __uint32_t pthread_barrier_t;
typedef struct {
    int is_initialized;
} pthread_barrierattr_t;
#endif

#if !defined(_POSIX_SPIN_LOCKS)
typedef __uint32_t pthread_spinlock_t;
#endif

/* ------------------------------------------------------------------------
 * Feature macros describing this implementation.
 * --------------------------------------------------------------------- */
#ifndef _POSIX_THREADS
#define _POSIX_THREADS 200809L
#endif
#ifndef _POSIX_TIMEOUTS
#define _POSIX_TIMEOUTS 200809L
#endif
#ifndef _POSIX_READER_WRITER_LOCKS
#define _POSIX_READER_WRITER_LOCKS 200809L
#endif
#ifndef _POSIX_BARRIERS
#define _POSIX_BARRIERS 200809L
#endif
#ifndef _POSIX_SPIN_LOCKS
#define _POSIX_SPIN_LOCKS 200809L
#endif
#ifndef _POSIX_THREAD_ATTR_STACKSIZE
#define _POSIX_THREAD_ATTR_STACKSIZE 200809L
#endif
#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
#define _POSIX_THREAD_SAFE_FUNCTIONS 200809L
#endif
#ifndef _UNIX98_THREAD_MUTEX_ATTRIBUTES
#define _UNIX98_THREAD_MUTEX_ATTRIBUTES 1
#endif

/* ------------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------- */
#ifndef PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    3
#endif

#ifndef PTHREAD_PROCESS_PRIVATE
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED  1
#endif

#ifndef PTHREAD_PRIO_NONE
#define PTHREAD_PRIO_NONE    0
#define PTHREAD_PRIO_INHERIT 1
#define PTHREAD_PRIO_PROTECT 2
#endif

#define PTHREAD_MUTEX_INITIALIZER  _PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_COND_INITIALIZER   _PTHREAD_COND_INITIALIZER
#define PTHREAD_RWLOCK_INITIALIZER _PTHREAD_RWLOCK_INITIALIZER
#define PTHREAD_ONCE_INIT          _PTHREAD_ONCE_INIT

/* Non-standard but widely used initializers */
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP  ((pthread_mutex_t) 0xFFFFFFFE)
#define PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP ((pthread_mutex_t) 0xFFFFFFFD)

#define PTHREAD_CANCEL_ENABLE       0
#define PTHREAD_CANCEL_DISABLE      1
#define PTHREAD_CANCEL_DEFERRED     0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#define PTHREAD_CANCELED            ((void *) -1)

#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

/* Limits (newlib does not define them for this target) */
#ifdef PTHREAD_STACK_MIN
#undef PTHREAD_STACK_MIN           /* newlib's value (200) is too small */
#endif
#define PTHREAD_STACK_MIN            8192
#ifndef PTHREAD_KEYS_MAX
#define PTHREAD_KEYS_MAX             64
#endif
#ifndef PTHREAD_DESTRUCTOR_ITERATIONS
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#endif
#ifndef PTHREAD_THREADS_MAX
#define PTHREAD_THREADS_MAX          (-1) /* limited by memory only */
#endif
/* Stack size used when the attribute does not specify one. */
#define PTHREADS_RETRO68_DEFAULT_STACKSIZE (64L * 1024L)

/* ------------------------------------------------------------------------
 * Thread attributes
 * --------------------------------------------------------------------- */
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int state);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *size);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size);
int pthread_attr_getstackaddr(const pthread_attr_t *attr, void **addr);
int pthread_attr_setstackaddr(pthread_attr_t *attr, void *addr);
int pthread_attr_getstack(const pthread_attr_t *attr, void **addr,
                          size_t *size);
int pthread_attr_setstack(pthread_attr_t *attr, void *addr, size_t size);
int pthread_attr_getguardsize(const pthread_attr_t *attr, size_t *size);
int pthread_attr_setguardsize(pthread_attr_t *attr, size_t size);
int pthread_attr_getscope(const pthread_attr_t *attr, int *scope);
int pthread_attr_setscope(pthread_attr_t *attr, int scope);
int pthread_attr_getinheritsched(const pthread_attr_t *attr, int *inherit);
int pthread_attr_setinheritsched(pthread_attr_t *attr, int inherit);
int pthread_attr_getschedpolicy(const pthread_attr_t *attr, int *policy);
int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy);
int pthread_attr_getschedparam(const pthread_attr_t *attr,
                               struct sched_param *param);
int pthread_attr_setschedparam(pthread_attr_t *attr,
                               const struct sched_param *param);

/* ------------------------------------------------------------------------
 * Thread management
 * --------------------------------------------------------------------- */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **value_ptr);
int pthread_detach(pthread_t thread);
void pthread_exit(void *value_ptr) __attribute__((__noreturn__));
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));
void pthread_yield(void);
int pthread_getconcurrency(void);
int pthread_setconcurrency(int new_level);
int pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id);
int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                   void (*child)(void));

int pthread_getschedparam(pthread_t thread, int *policy,
                          struct sched_param *param);
int pthread_setschedparam(pthread_t thread, int policy,
                          const struct sched_param *param);
int pthread_setschedprio(pthread_t thread, int prio);

int pthread_setname_np(pthread_t thread, const char *name);
int pthread_getname_np(pthread_t thread, char *name, size_t len);

/* Non-portable: the Thread Manager ThreadID backing a pthread. */
unsigned long pthread_mac_threadid_np(pthread_t thread);

/* ------------------------------------------------------------------------
 * Cancellation
 * --------------------------------------------------------------------- */
int pthread_cancel(pthread_t thread);
int pthread_setcancelstate(int state, int *oldstate);
int pthread_setcanceltype(int type, int *oldtype);
void pthread_testcancel(void);

#ifndef _PTHREAD_CLEANUP_CONTEXT_DEFINED
#define _PTHREAD_CLEANUP_CONTEXT_DEFINED
struct _pthread_cleanup_context {
    void (*_routine)(void *);
    void *_arg;
    int _canceltype;
    struct _pthread_cleanup_context *_previous;
};
#endif

void _pthread_cleanup_push(struct _pthread_cleanup_context *context,
                           void (*routine)(void *), void *arg);
void _pthread_cleanup_pop(struct _pthread_cleanup_context *context,
                          int execute);

#define pthread_cleanup_push(_routine, _arg)                          \
    do {                                                              \
        struct _pthread_cleanup_context _pthread_clup_ctx;            \
        _pthread_cleanup_push(&_pthread_clup_ctx, (_routine), (_arg))

#define pthread_cleanup_pop(_execute)                                 \
        _pthread_cleanup_pop(&_pthread_clup_ctx, (_execute));         \
    } while (0)

/* ------------------------------------------------------------------------
 * Mutexes
 * --------------------------------------------------------------------- */
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr,
                                 int *pshared);
int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared);
int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
                                  int *protocol);
int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int protocol);

int pthread_mutex_init(pthread_mutex_t *mutex,
                       const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_timedlock(pthread_mutex_t *mutex,
                            const struct timespec *abstime);
int pthread_mutex_clocklock(pthread_mutex_t *mutex, clockid_t clock,
                            const struct timespec *abstime);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

/* ------------------------------------------------------------------------
 * Condition variables
 * --------------------------------------------------------------------- */
int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_destroy(pthread_condattr_t *attr);
int pthread_condattr_getclock(const pthread_condattr_t *attr,
                              clockid_t *clock_id);
int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock_id);
int pthread_condattr_getpshared(const pthread_condattr_t *attr,
                                int *pshared);
int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared);

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime);
int pthread_cond_clockwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           clockid_t clock, const struct timespec *abstime);

/* ------------------------------------------------------------------------
 * Read/write locks
 * --------------------------------------------------------------------- */
int pthread_rwlockattr_init(pthread_rwlockattr_t *attr);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr);
int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attr,
                                  int *pshared);
int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attr, int pshared);

int pthread_rwlock_init(pthread_rwlock_t *rwlock,
                        const pthread_rwlockattr_t *attr);
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_timedrdlock(pthread_rwlock_t *rwlock,
                               const struct timespec *abstime);
int pthread_rwlock_clockrdlock(pthread_rwlock_t *rwlock, clockid_t clock,
                               const struct timespec *abstime);
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_timedwrlock(pthread_rwlock_t *rwlock,
                               const struct timespec *abstime);
int pthread_rwlock_clockwrlock(pthread_rwlock_t *rwlock, clockid_t clock,
                               const struct timespec *abstime);
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

/* ------------------------------------------------------------------------
 * Barriers
 * --------------------------------------------------------------------- */
int pthread_barrierattr_init(pthread_barrierattr_t *attr);
int pthread_barrierattr_destroy(pthread_barrierattr_t *attr);
int pthread_barrierattr_getpshared(const pthread_barrierattr_t *attr,
                                   int *pshared);
int pthread_barrierattr_setpshared(pthread_barrierattr_t *attr, int pshared);

int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);

/* ------------------------------------------------------------------------
 * Spin locks (yield while contended - spinning alone would never let the
 * owner run under cooperative scheduling)
 * --------------------------------------------------------------------- */
int pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int pthread_spin_destroy(pthread_spinlock_t *lock);
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_spin_trylock(pthread_spinlock_t *lock);
int pthread_spin_unlock(pthread_spinlock_t *lock);

/* ------------------------------------------------------------------------
 * Thread-specific data
 * --------------------------------------------------------------------- */
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);
void *pthread_getspecific(pthread_key_t key);

/* ------------------------------------------------------------------------
 * Related functions implemented by this library (newlib declares them in
 * <sched.h>/<time.h>/<unistd.h> only for some targets).
 * --------------------------------------------------------------------- */
int sched_yield(void);
int sched_get_priority_min(int policy);
int sched_get_priority_max(int policy);

/* Clocks.  newlib has the prototypes behind _POSIX_TIMERS, which is not
 * defined for Retro68, and libc does not implement them.  These versions
 * yield to other threads while sleeping.
 *   CLOCK_REALTIME  : gettimeofday()
 *   CLOCK_MONOTONIC : Microseconds() since boot                          */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC ((clockid_t) 4)
#endif
#if !defined(_POSIX_TIMERS)
int clock_gettime(clockid_t clock_id, struct timespec *tp);
int clock_getres(clockid_t clock_id, struct timespec *res);
int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);
#endif
#if !defined(_POSIX_CLOCK_SELECTION)
#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 4
#endif
int clock_nanosleep(clockid_t clock_id, int flags,
                    const struct timespec *rqtp, struct timespec *rmtp);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PTHREADS_RETRO68_PTHREAD_H */
