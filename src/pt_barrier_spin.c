/*
 * pt_barrier_spin.c - barriers and spin locks.
 */
#include "pt_internal.h"
#include <stdlib.h>

/* ------------------------------------------------------------------------
 * Barriers
 * --------------------------------------------------------------------- */
typedef struct {
    unsigned count;
    unsigned arrived;
    unsigned long generation;
    pt_queue waiters;
} pt_barrier;

int pthread_barrierattr_init(pthread_barrierattr_t *attr)
{
    if (!attr)
        return EINVAL;
    attr->is_initialized = 1;
    return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t *attr)
{
    if (!attr || !attr->is_initialized)
        return EINVAL;
    attr->is_initialized = 0;
    return 0;
}

int pthread_barrierattr_getpshared(const pthread_barrierattr_t *attr,
                                   int *pshared)
{
    if (!attr || !attr->is_initialized || !pshared)
        return EINVAL;
    *pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_barrierattr_setpshared(pthread_barrierattr_t *attr, int pshared)
{
    if (!attr || !attr->is_initialized)
        return EINVAL;
    if (pshared == PTHREAD_PROCESS_PRIVATE)
        return 0;
    return pshared == PTHREAD_PROCESS_SHARED ? ENOTSUP : EINVAL;
}

int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned count)
{
    pt_barrier *b;
    if (!barrier || count == 0 || (attr && !attr->is_initialized))
        return EINVAL;
    b = (pt_barrier *)calloc(1, sizeof(pt_barrier));
    if (!b)
        return ENOMEM;
    b->count = count;
    *barrier = (pthread_barrier_t)(uintptr_t)b;
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    pt_barrier *b;
    if (!barrier || !*barrier)
        return EINVAL;
    b = (pt_barrier *)(uintptr_t)*barrier;
    if (b->arrived || b->waiters.head)
        return EBUSY;
    free(b);
    *barrier = 0;
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
    pt_barrier *b;
    pt_waiter w;
    unsigned long gen;

    if (!barrier || !*barrier)
        return EINVAL;
    b = (pt_barrier *)(uintptr_t)*barrier;

    gen = b->generation;
    if (++b->arrived == b->count) {
        b->arrived = 0;
        b->generation++;
        pt_wake_all(&b->waiters);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }

    w.thread = pt_self();
    w.woken = 0;
    pt_queue_push(&b->waiters, &w);
    while (b->generation == gen) {
        if (pt_block(&w, CLOCK_REALTIME, NULL, 0) != 0) {
            /* Only possible without a Thread Manager (deadlock). */
            pt_queue_remove(&b->waiters, &w);
            b->arrived--;
            return EDEADLK;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * Spin locks.  Spinning without yielding would hang forever under
 * cooperative scheduling, so a contended lock yields to other threads.
 * --------------------------------------------------------------------- */
int pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
    if (!lock)
        return EINVAL;
    if (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED)
        return EINVAL;
    *lock = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock)
{
    if (!lock)
        return EINVAL;
    if (*lock)
        return EBUSY;
    return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock)
{
    pthread_spinlock_t self;
    if (!lock)
        return EINVAL;
    self = (pthread_spinlock_t)pthread_self();
    if (*lock == self)
        return EDEADLK;
    while (*lock) {
        if (!pt_available())
            return EDEADLK;
        pt_yield();
    }
    *lock = self;
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock)
{
    if (!lock)
        return EINVAL;
    if (*lock)
        return EBUSY;
    *lock = (pthread_spinlock_t)pthread_self();
    return 0;
}

int pthread_spin_unlock(pthread_spinlock_t *lock)
{
    if (!lock)
        return EINVAL;
    if (*lock != (pthread_spinlock_t)pthread_self())
        return EPERM;
    *lock = 0;
    return 0;
}
