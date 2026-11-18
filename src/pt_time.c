/*
 * pt_time.c - clocks and thread-friendly sleeping.
 *
 * libc on Retro68 declares, but does not implement, clock_gettime() and
 * nanosleep().  This file provides them (as weak symbols, so a real
 * implementation elsewhere takes precedence).  Sleeping yields to the other
 * threads instead of busy-waiting the whole application.
 */
#include "pt_internal.h"
#include <sys/time.h>
#include <unistd.h>

#define NSEC_PER_SEC 1000000000L

int pt_clock_valid(clockid_t clock)
{
    return clock == CLOCK_REALTIME || clock == CLOCK_MONOTONIC;
}

int pt_timespec_valid(const struct timespec *ts)
{
    return ts && ts->tv_nsec >= 0 && ts->tv_nsec < NSEC_PER_SEC;
}

void pt_now(clockid_t clock, struct timespec *ts)
{
    if (clock == CLOCK_MONOTONIC) {
        uint64_t us = mt_Microseconds();
        ts->tv_sec = (time_t)(us / 1000000u);
        ts->tv_nsec = (long)(us % 1000000u) * 1000L;
    } else {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ts->tv_sec = tv.tv_sec;
        ts->tv_nsec = (long)tv.tv_usec * 1000L;
    }
}

static int ts_cmp(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec != b->tv_sec)
        return a->tv_sec < b->tv_sec ? -1 : 1;
    if (a->tv_nsec != b->tv_nsec)
        return a->tv_nsec < b->tv_nsec ? -1 : 1;
    return 0;
}

int pt_deadline_passed(clockid_t clock, const struct timespec *abstime)
{
    struct timespec now;
    pt_now(clock, &now);
    return ts_cmp(&now, abstime) >= 0;
}

__attribute__((weak))
int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    if (!pt_clock_valid(clock_id) || !tp) {
        errno = EINVAL;
        return -1;
    }
    pt_now(clock_id, tp);
    return 0;
}

__attribute__((weak))
int clock_getres(clockid_t clock_id, struct timespec *res)
{
    if (!pt_clock_valid(clock_id)) {
        errno = EINVAL;
        return -1;
    }
    if (res) {
        res->tv_sec = 0;
        res->tv_nsec = 1000;    /* both clocks tick in microseconds */
    }
    return 0;
}

/* Sleep until 'deadline' on 'clock'; cancellation point. */
static int sleep_until(clockid_t clock, const struct timespec *deadline)
{
    pt_thread *self = pt_self();
    pt_waiter w;
    int rc;

    if (pt_cancel_requested(self))
        pt_act_on_cancel(self);

    w.next = NULL;
    w.thread = self;
    w.woken = 0;                /* nobody wakes us: wait for the deadline */
    rc = pt_block(&w, clock, deadline, 1);
    if (rc == ECANCELED)
        pt_act_on_cancel(self);
    return 0;
}

static void ts_add(struct timespec *t, const struct timespec *d)
{
    t->tv_sec += d->tv_sec;
    t->tv_nsec += d->tv_nsec;
    if (t->tv_nsec >= NSEC_PER_SEC) {
        t->tv_nsec -= NSEC_PER_SEC;
        t->tv_sec++;
    }
}

__attribute__((weak))
int clock_nanosleep(clockid_t clock_id, int flags,
                    const struct timespec *rqtp, struct timespec *rmtp)
{
    struct timespec deadline;

    if (!pt_clock_valid(clock_id))
        return EINVAL;
    if (!pt_timespec_valid(rqtp) || rqtp->tv_sec < 0)
        return EINVAL;

    if (flags & TIMER_ABSTIME) {
        deadline = *rqtp;
    } else {
        pt_now(clock_id, &deadline);
        ts_add(&deadline, rqtp);
    }
    sleep_until(clock_id, &deadline);
    if (rmtp && !(flags & TIMER_ABSTIME)) {
        rmtp->tv_sec = 0;
        rmtp->tv_nsec = 0;
    }
    return 0;
}

__attribute__((weak))
int nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
    int rc = clock_nanosleep(CLOCK_MONOTONIC, 0, rqtp, rmtp);
    if (rc) {
        errno = rc;
        return -1;
    }
    return 0;
}

__attribute__((weak))
int usleep(useconds_t useconds)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(useconds / 1000000u);
    ts.tv_nsec = (long)(useconds % 1000000u) * 1000L;
    return nanosleep(&ts, NULL);
}

__attribute__((weak))
unsigned sleep(unsigned seconds)
{
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);
    return 0;
}
