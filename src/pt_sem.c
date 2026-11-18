/*
 * pt_sem.c - unnamed POSIX semaphores.
 */
#include "pt_internal.h"
#include <semaphore.h>

static pt_queue *sem_queue(sem_t *s)
{
    return (pt_queue *)(void *)&s->__head;
}

/* pt_queue must overlay the two pointer fields of sem_t. */
typedef char pt_sem_layout_check[
    (sizeof(pt_queue) == 2 * sizeof(void *)) ? 1 : -1];

static int sem_ok(sem_t *s)
{
    return s && s->__magic == PT_MAGIC_SEM;
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
    if (!sem || value > SEM_VALUE_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (pshared) {
        errno = ENOSYS;
        return -1;
    }
    sem->__value = (int)value;
    sem->__head = NULL;
    sem->__tail = NULL;
    sem->__magic = PT_MAGIC_SEM;
    return 0;
}

int sem_destroy(sem_t *sem)
{
    if (!sem_ok(sem)) {
        errno = EINVAL;
        return -1;
    }
    if (sem->__head) {
        errno = EBUSY;
        return -1;
    }
    sem->__magic = 0;
    return 0;
}

static int sem_wait_common(sem_t *sem, clockid_t clock,
                           const struct timespec *abstime)
{
    pt_thread *self;
    pt_waiter w;
    int rc;

    if (!sem_ok(sem)) {
        errno = EINVAL;
        return -1;
    }
    self = pt_self();
    if (pt_cancel_requested(self))
        pt_act_on_cancel(self);

    while (sem->__value <= 0) {
        if (abstime && (!pt_clock_valid(clock) ||
                        !pt_timespec_valid(abstime))) {
            errno = EINVAL;
            return -1;
        }
        w.thread = self;
        w.woken = 0;
        pt_queue_push(sem_queue(sem), &w);
        rc = pt_block(&w, clock, abstime, 1);
        if (rc != 0) {
            pt_queue_remove(sem_queue(sem), &w);
            if (rc == ECANCELED)
                pt_act_on_cancel(self);
            errno = rc;
            return -1;
        }
    }
    sem->__value--;
    return 0;
}

int sem_wait(sem_t *sem)
{
    return sem_wait_common(sem, CLOCK_REALTIME, NULL);
}

int sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
    if (!abstime) {
        errno = EINVAL;
        return -1;
    }
    return sem_wait_common(sem, CLOCK_REALTIME, abstime);
}

int sem_clockwait(sem_t *sem, clockid_t clock, const struct timespec *abstime)
{
    if (!abstime) {
        errno = EINVAL;
        return -1;
    }
    return sem_wait_common(sem, clock, abstime);
}

int sem_trywait(sem_t *sem)
{
    if (!sem_ok(sem)) {
        errno = EINVAL;
        return -1;
    }
    if (sem->__value <= 0) {
        errno = EAGAIN;
        return -1;
    }
    sem->__value--;
    return 0;
}

int sem_post(sem_t *sem)
{
    if (!sem_ok(sem)) {
        errno = EINVAL;
        return -1;
    }
    if (sem->__value == SEM_VALUE_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    sem->__value++;
    pt_wake_one(sem_queue(sem));
    return 0;
}

int sem_getvalue(sem_t *sem, int *sval)
{
    if (!sem_ok(sem) || !sval) {
        errno = EINVAL;
        return -1;
    }
    *sval = sem->__value;
    return 0;
}

sem_t *sem_open(const char *name, int oflag, ...)
{
    (void)name;
    (void)oflag;
    errno = ENOSYS;
    return SEM_FAILED;
}

int sem_close(sem_t *sem)
{
    (void)sem;
    errno = ENOSYS;
    return -1;
}

int sem_unlink(const char *name)
{
    (void)name;
    errno = ENOSYS;
    return -1;
}
