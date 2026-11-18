/*
 * pt_rwlock.c - read/write locks (writer preferring).
 */
#include "pt_internal.h"
#include <stdlib.h>

typedef struct {
    int readers;               /* number of read locks held   */
    pt_thread *writer;         /* owner of the write lock     */
    pt_queue read_waiters;
    pt_queue write_waiters;
} pt_rwlock;

static pt_rwlock *rw_get(pthread_rwlock_t *rp, int *err)
{
    pt_rwlock *rw;
    if (!rp) {
        *err = EINVAL;
        return NULL;
    }
    if (!PT_HANDLE_IS_STATIC((uint32_t)*rp))
        return (pt_rwlock *)(uintptr_t)*rp;
    rw = (pt_rwlock *)calloc(1, sizeof(pt_rwlock));
    if (!rw) {
        *err = ENOMEM;
        return NULL;
    }
    *rp = (pthread_rwlock_t)(uintptr_t)rw;
    return rw;
}

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr)
{
    if (!attr)
        return EINVAL;
    attr->is_initialized = 1;
    return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr)
{
    if (!attr || !attr->is_initialized)
        return EINVAL;
    attr->is_initialized = 0;
    return 0;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attr,
                                  int *pshared)
{
    if (!attr || !attr->is_initialized || !pshared)
        return EINVAL;
    *pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attr, int pshared)
{
    if (!attr || !attr->is_initialized)
        return EINVAL;
    if (pshared == PTHREAD_PROCESS_PRIVATE)
        return 0;
    return pshared == PTHREAD_PROCESS_SHARED ? ENOTSUP : EINVAL;
}

int pthread_rwlock_init(pthread_rwlock_t *rwlock,
                        const pthread_rwlockattr_t *attr)
{
    pt_rwlock *rw;
    if (!rwlock || (attr && !attr->is_initialized))
        return EINVAL;
    rw = (pt_rwlock *)calloc(1, sizeof(pt_rwlock));
    if (!rw)
        return ENOMEM;
    *rwlock = (pthread_rwlock_t)(uintptr_t)rw;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
    pt_rwlock *rw;
    if (!rwlock)
        return EINVAL;
    if (PT_HANDLE_IS_STATIC((uint32_t)*rwlock)) {
        *rwlock = 0;
        return 0;
    }
    rw = (pt_rwlock *)(uintptr_t)*rwlock;
    if (rw->readers || rw->writer || rw->read_waiters.head ||
        rw->write_waiters.head)
        return EBUSY;
    free(rw);
    *rwlock = 0;
    return 0;
}

static int can_read(pt_rwlock *rw)
{
    return !rw->writer && !rw->write_waiters.head;
}

static int rd_lock(pthread_rwlock_t *rwlock, clockid_t clock,
                   const struct timespec *abstime, int try_only)
{
    int err = 0, rc;
    pt_thread *self;
    pt_rwlock *rw;
    pt_waiter w;

    if (abstime && (!pt_clock_valid(clock) || !pt_timespec_valid(abstime)))
        return EINVAL;
    rw = rw_get(rwlock, &err);
    if (!rw)
        return err;
    self = pt_self();
    if (rw->writer == self)
        return EDEADLK;

    while (!can_read(rw)) {
        if (try_only)
            return EBUSY;
        w.thread = self;
        w.woken = 0;
        pt_queue_push(&rw->read_waiters, &w);
        rc = pt_block(&w, clock, abstime, 0);
        if (rc != 0) {
            pt_queue_remove(&rw->read_waiters, &w);
            return rc;
        }
        /* Readers woken by a releasing writer may proceed even if new
         * writers queued up in the meantime. */
        if (!rw->writer)
            break;
    }
    rw->readers++;
    return 0;
}

static int wr_lock(pthread_rwlock_t *rwlock, clockid_t clock,
                   const struct timespec *abstime, int try_only)
{
    int err = 0, rc;
    pt_thread *self;
    pt_rwlock *rw;
    pt_waiter w;

    if (abstime && (!pt_clock_valid(clock) || !pt_timespec_valid(abstime)))
        return EINVAL;
    rw = rw_get(rwlock, &err);
    if (!rw)
        return err;
    self = pt_self();
    if (rw->writer == self)
        return EDEADLK;

    while (rw->writer || rw->readers) {
        if (try_only)
            return EBUSY;
        w.thread = self;
        w.woken = 0;
        pt_queue_push(&rw->write_waiters, &w);
        rc = pt_block(&w, clock, abstime, 0);
        if (rc != 0) {
            pt_queue_remove(&rw->write_waiters, &w);
            /* Readers may have been held back only because we waited. */
            if (!rw->writer && !rw->write_waiters.head)
                pt_wake_all(&rw->read_waiters);
            return rc;
        }
    }
    rw->writer = self;
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    return rd_lock(rwlock, CLOCK_REALTIME, NULL, 0);
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
    return rd_lock(rwlock, CLOCK_REALTIME, NULL, 1);
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t *rwlock,
                               const struct timespec *abstime)
{
    if (!abstime)
        return EINVAL;
    return rd_lock(rwlock, CLOCK_REALTIME, abstime, 0);
}

int pthread_rwlock_clockrdlock(pthread_rwlock_t *rwlock, clockid_t clock,
                               const struct timespec *abstime)
{
    if (!abstime)
        return EINVAL;
    return rd_lock(rwlock, clock, abstime, 0);
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    return wr_lock(rwlock, CLOCK_REALTIME, NULL, 0);
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
    return wr_lock(rwlock, CLOCK_REALTIME, NULL, 1);
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *rwlock,
                               const struct timespec *abstime)
{
    if (!abstime)
        return EINVAL;
    return wr_lock(rwlock, CLOCK_REALTIME, abstime, 0);
}

int pthread_rwlock_clockwrlock(pthread_rwlock_t *rwlock, clockid_t clock,
                               const struct timespec *abstime)
{
    if (!abstime)
        return EINVAL;
    return wr_lock(rwlock, clock, abstime, 0);
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    pt_rwlock *rw;

    if (!rwlock || PT_HANDLE_IS_STATIC((uint32_t)*rwlock))
        return rwlock ? EPERM : EINVAL;
    rw = (pt_rwlock *)(uintptr_t)*rwlock;

    if (rw->writer) {
        if (rw->writer != pt_self())
            return EPERM;
        rw->writer = NULL;
        /* Hand over to queued readers first, then a writer. */
        if (rw->read_waiters.head)
            pt_wake_all(&rw->read_waiters);
        else
            pt_wake_one(&rw->write_waiters);
        return 0;
    }
    if (rw->readers > 0) {
        if (--rw->readers == 0)
            pt_wake_one(&rw->write_waiters);
        return 0;
    }
    return EPERM;
}
