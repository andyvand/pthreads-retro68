/*
 * pt_cond.c - condition variables.
 */
#include "pt_internal.h"
#include <stdlib.h>

typedef struct {
    clockid_t clock;
    pt_queue waiters;
} pt_cond;

static pt_cond *cond_alloc(clockid_t clock)
{
    pt_cond *c = (pt_cond *)calloc(1, sizeof(pt_cond));
    if (c)
        c->clock = clock;
    return c;
}

static pt_cond *cond_get(pthread_cond_t *cp, int *err)
{
    pt_cond *c;
    if (!cp) {
        *err = EINVAL;
        return NULL;
    }
    if (!PT_HANDLE_IS_STATIC((uint32_t)*cp))
        return (pt_cond *)(uintptr_t)*cp;
    c = cond_alloc(CLOCK_REALTIME);
    if (!c) {
        *err = ENOMEM;
        return NULL;
    }
    *cp = (pthread_cond_t)(uintptr_t)c;
    return c;
}

/* ------------------------------------------------------------------------
 * Attributes
 * --------------------------------------------------------------------- */
int pthread_condattr_init(pthread_condattr_t *attr)
{
    if (!attr)
        return EINVAL;
    attr->is_initialized = 1;
    attr->clock = CLOCK_REALTIME;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
    if (!attr || !attr->is_initialized)
        return EINVAL;
    attr->is_initialized = 0;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *attr,
                              clockid_t *clock_id)
{
    if (!attr || !attr->is_initialized || !clock_id)
        return EINVAL;
    *clock_id = (clockid_t)attr->clock;
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock_id)
{
    if (!attr || !attr->is_initialized || !pt_clock_valid(clock_id))
        return EINVAL;
    attr->clock = (clock_t)clock_id;
    return 0;
}

int pthread_condattr_getpshared(const pthread_condattr_t *attr, int *pshared)
{
    if (!attr || !attr->is_initialized || !pshared)
        return EINVAL;
    *pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared)
{
    if (!attr || !attr->is_initialized)
        return EINVAL;
    if (pshared == PTHREAD_PROCESS_PRIVATE)
        return 0;
    return pshared == PTHREAD_PROCESS_SHARED ? ENOTSUP : EINVAL;
}

/* ------------------------------------------------------------------------
 * Condition variables
 * --------------------------------------------------------------------- */
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    pt_cond *c;
    clockid_t clock = CLOCK_REALTIME;

    if (!cond)
        return EINVAL;
    if (attr) {
        if (!attr->is_initialized)
            return EINVAL;
        clock = (clockid_t)attr->clock;
    }
    c = cond_alloc(clock);
    if (!c)
        return ENOMEM;
    *cond = (pthread_cond_t)(uintptr_t)c;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    pt_cond *c;
    if (!cond)
        return EINVAL;
    if (PT_HANDLE_IS_STATIC((uint32_t)*cond)) {
        *cond = 0;
        return 0;
    }
    c = (pt_cond *)(uintptr_t)*cond;
    if (c->waiters.head)
        return EBUSY;
    free(c);
    *cond = 0;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    if (!cond)
        return EINVAL;
    if (PT_HANDLE_IS_STATIC((uint32_t)*cond))
        return 0;               /* nobody can be waiting yet */
    pt_wake_one(&((pt_cond *)(uintptr_t)*cond)->waiters);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    if (!cond)
        return EINVAL;
    if (PT_HANDLE_IS_STATIC((uint32_t)*cond))
        return 0;
    pt_wake_all(&((pt_cond *)(uintptr_t)*cond)->waiters);
    return 0;
}

static int cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                     int use_clock, clockid_t clock,
                     const struct timespec *abstime)
{
    int err = 0, rc, count, lrc;
    pt_thread *self = pt_self();
    pt_cond *c;
    pt_mutex *m;
    pt_waiter w;

    if (abstime && !pt_timespec_valid(abstime))
        return EINVAL;
    if (use_clock && !pt_clock_valid(clock))
        return EINVAL;

    c = cond_get(cond, &err);
    if (!c)
        return err;
    if (!mutex || PT_HANDLE_IS_STATIC((uint32_t)*mutex))
        return EPERM;           /* caller cannot own an unused mutex */
    m = pt_mutex_get(mutex, &err);
    if (!m)
        return err;
    if (!use_clock)
        clock = c->clock;

    if (pt_cancel_requested(self))
        pt_act_on_cancel(self);

    /* Enqueue before releasing the mutex so no signal can be missed. */
    w.thread = self;
    w.woken = 0;
    pt_queue_push(&c->waiters, &w);

    rc = pt_mutex_release_all(m, &count);
    if (rc != 0) {
        pt_queue_remove(&c->waiters, &w);
        return rc;
    }

    rc = pt_block(&w, clock, abstime, 1);
    if (rc != 0)
        pt_queue_remove(&c->waiters, &w);
    if (rc == ETIMEDOUT && w.woken)
        rc = 0;                 /* signalled just in time */

    lrc = pt_mutex_restore(m, count);

    if (rc == ECANCELED) {
        /* The mutex is re-acquired before cleanup handlers run (POSIX). */
        pt_act_on_cancel(self);
    }
    return rc ? rc : lrc;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return cond_wait(cond, mutex, 0, CLOCK_REALTIME, NULL);
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime)
{
    if (!abstime)
        return EINVAL;
    return cond_wait(cond, mutex, 0, CLOCK_REALTIME, abstime);
}

int pthread_cond_clockwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           clockid_t clock, const struct timespec *abstime)
{
    if (!abstime)
        return EINVAL;
    return cond_wait(cond, mutex, 1, clock, abstime);
}
