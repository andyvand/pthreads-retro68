/*
 * pt_mutex.c - mutexes and mutex attributes.
 *
 * pthread_mutex_t is newlib's 32-bit handle; it holds a pointer to a heap
 * allocated pt_mutex.  Statically initialised (or zeroed) handles are
 * allocated on first use.  That lazy initialisation cannot race because
 * nothing in between yields to another cooperative thread.
 */
#include "pt_internal.h"
#include <stdlib.h>

struct pt_mutex {
    int type;
    pt_thread *owner;
    int count;
    pt_queue waiters;
};

static int normalize_type(int type)
{
    return type == PTHREAD_MUTEX_DEFAULT ? PTHREAD_MUTEX_NORMAL : type;
}

static pt_mutex *mutex_alloc(int type)
{
    pt_mutex *m = (pt_mutex *)calloc(1, sizeof(pt_mutex));
    if (m)
        m->type = normalize_type(type);
    return m;
}

pt_mutex *pt_mutex_get(pthread_mutex_t *mp, int *err)
{
    uint32_t h;
    int type;
    pt_mutex *m;

    if (!mp) {
        *err = EINVAL;
        return NULL;
    }
    h = (uint32_t)*mp;
    if (!PT_HANDLE_IS_STATIC(h))
        return (pt_mutex *)(uintptr_t)h;

    if (h == PT_HANDLE_RECURSIVE)
        type = PTHREAD_MUTEX_RECURSIVE;
    else if (h == PT_HANDLE_ERRORCHECK)
        type = PTHREAD_MUTEX_ERRORCHECK;
    else
        type = PTHREAD_MUTEX_NORMAL;

    m = mutex_alloc(type);
    if (!m) {
        *err = ENOMEM;
        return NULL;
    }
    *mp = (pthread_mutex_t)(uintptr_t)m;
    return m;
}

/* ------------------------------------------------------------------------
 * Attributes.  newlib's pthread_mutexattr_t for this target only has the
 * fields 'is_initialized' and 'recursive'; the mutex type is kept in
 * 'recursive'.
 * --------------------------------------------------------------------- */
int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    if (!attr)
        return EINVAL;
    attr->is_initialized = 1;
    attr->recursive = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    if (!attr || !attr->is_initialized)
        return EINVAL;
    attr->is_initialized = 0;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
    if (!attr || !attr->is_initialized || !type)
        return EINVAL;
    *type = attr->recursive;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    if (!attr || !attr->is_initialized)
        return EINVAL;
    switch (type) {
    case PTHREAD_MUTEX_NORMAL:
    case PTHREAD_MUTEX_RECURSIVE:
    case PTHREAD_MUTEX_ERRORCHECK:
    case PTHREAD_MUTEX_DEFAULT:
        attr->recursive = type;
        return 0;
    default:
        return EINVAL;
    }
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr,
                                 int *pshared)
{
    if (!attr || !attr->is_initialized || !pshared)
        return EINVAL;
    *pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared)
{
    if (!attr || !attr->is_initialized)
        return EINVAL;
    if (pshared == PTHREAD_PROCESS_PRIVATE)
        return 0;
    return pshared == PTHREAD_PROCESS_SHARED ? ENOTSUP : EINVAL;
}

int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr,
                                  int *protocol)
{
    if (!attr || !attr->is_initialized || !protocol)
        return EINVAL;
    *protocol = PTHREAD_PRIO_NONE;
    return 0;
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int protocol)
{
    if (!attr || !attr->is_initialized)
        return EINVAL;
    if (protocol == PTHREAD_PRIO_NONE)
        return 0;
    return (protocol == PTHREAD_PRIO_INHERIT ||
            protocol == PTHREAD_PRIO_PROTECT) ? ENOTSUP : EINVAL;
}

/* ------------------------------------------------------------------------
 * Mutexes
 * --------------------------------------------------------------------- */
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    pt_mutex *m;
    int type = PTHREAD_MUTEX_DEFAULT;

    if (!mutex)
        return EINVAL;
    if (attr) {
        if (!attr->is_initialized)
            return EINVAL;
        type = attr->recursive;
    }
    m = mutex_alloc(type);
    if (!m)
        return ENOMEM;
    *mutex = (pthread_mutex_t)(uintptr_t)m;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    pt_mutex *m;

    if (!mutex)
        return EINVAL;
    if (PT_HANDLE_IS_STATIC((uint32_t)*mutex)) {
        *mutex = 0;             /* never used - nothing allocated */
        return 0;
    }
    m = (pt_mutex *)(uintptr_t)*mutex;
    if (m->owner || m->waiters.head)
        return EBUSY;
    free(m);
    *mutex = 0;
    return 0;
}

int pt_mutex_lock_internal(pt_mutex *m, clockid_t clock,
                           const struct timespec *abstime)
{
    pt_thread *self = pt_self();
    pt_waiter w;
    int rc;

    if (m->owner == self) {
        if (m->type == PTHREAD_MUTEX_RECURSIVE) {
            m->count++;
            return 0;
        }
        if (m->type == PTHREAD_MUTEX_ERRORCHECK)
            return EDEADLK;
        /* NORMAL: relocking deadlocks, as POSIX specifies (unless timed). */
    }

    while (m->owner) {
        w.thread = self;
        w.woken = 0;
        pt_queue_push(&m->waiters, &w);
        rc = pt_block(&w, clock, abstime, 0);
        if (rc != 0) {
            if (!pt_queue_remove(&m->waiters, &w) && w.woken)
                pt_wake_one(&m->waiters);   /* pass the wake-up on */
            return rc;
        }
    }
    m->owner = self;
    m->count = 1;
    return 0;
}

int pt_mutex_unlock_internal(pt_mutex *m)
{
    pt_thread *self = pt_self();

    if (m->owner != self) {
        /* For NORMAL mutexes this is undefined behaviour; report it. */
        return EPERM;
    }
    if (--m->count > 0)
        return 0;
    m->owner = NULL;
    pt_wake_one(&m->waiters);
    return 0;
}

int pt_mutex_release_all(pt_mutex *m, int *count)
{
    if (m->owner != pt_self())
        return EPERM;
    *count = m->count;
    m->count = 1;
    return pt_mutex_unlock_internal(m);
}

int pt_mutex_restore(pt_mutex *m, int count)
{
    int rc = pt_mutex_lock_internal(m, CLOCK_REALTIME, NULL);
    if (rc == 0)
        m->count = count;
    return rc;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    int err = 0;
    pt_mutex *m = pt_mutex_get(mutex, &err);
    if (!m)
        return err;
    return pt_mutex_lock_internal(m, CLOCK_REALTIME, NULL);
}

int pthread_mutex_clocklock(pthread_mutex_t *mutex, clockid_t clock,
                            const struct timespec *abstime)
{
    int err = 0;
    pt_mutex *m;

    if (!pt_clock_valid(clock) || !pt_timespec_valid(abstime))
        return EINVAL;
    m = pt_mutex_get(mutex, &err);
    if (!m)
        return err;
    return pt_mutex_lock_internal(m, clock, abstime);
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex,
                            const struct timespec *abstime)
{
    return pthread_mutex_clocklock(mutex, CLOCK_REALTIME, abstime);
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    int err = 0;
    pt_thread *self;
    pt_mutex *m = pt_mutex_get(mutex, &err);
    if (!m)
        return err;

    self = pt_self();
    if (m->owner) {
        if (m->owner == self && m->type == PTHREAD_MUTEX_RECURSIVE) {
            m->count++;
            return 0;
        }
        return EBUSY;
    }
    m->owner = self;
    m->count = 1;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (!mutex || PT_HANDLE_IS_STATIC((uint32_t)*mutex))
        return mutex ? EPERM : EINVAL;
    return pt_mutex_unlock_internal((pt_mutex *)(uintptr_t)*mutex);
}
