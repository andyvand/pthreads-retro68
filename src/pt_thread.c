/*
 * pt_thread.c - thread lifecycle, registry, blocking primitives and
 * cancellation for pthreads-retro68.
 *
 * Scheduling model
 * ----------------
 * All threads are Thread Manager *cooperative* threads.  Library code is
 * therefore never preempted between two Thread Manager calls, which is what
 * makes the (lock free) bookkeeping below safe.
 *
 * A thread created by pthread_create() blocks by putting itself into the
 * kStoppedThreadState; whoever wakes it puts it back into kReadyThreadState.
 * The main application thread (and any thread not created through this
 * library) never stops itself - it keeps calling YieldToAnyThread() while it
 * waits, so the application's event handling and the Process Manager are
 * never starved by a stopped main thread.  Timed waits also poll by
 * yielding, since nobody would wake a stopped thread when the timeout
 * expires.
 */
#include "pt_internal.h"

#include <stdlib.h>
#include <string.h>
#include <Gestalt.h>

static int        g_tm_state;          /* 0 unknown, 1 present, -1 absent */
static pt_thread *g_threads;           /* registry of live threads        */
static pt_thread *g_zombies;           /* finished, joinable, not joined  */
static pt_thread *g_cache;             /* last pt_self() result           */
static mt_ThreadID g_main_tid;
static int        g_live_created;      /* created threads not finished    */
static int        g_concurrency;

/* ------------------------------------------------------------------------
 * Thread Manager availability
 * --------------------------------------------------------------------- */
int pt_available(void)
{
    if (g_tm_state == 0) {
#if TARGET_API_MAC_CARBON
        g_tm_state = 1;
#else
        long resp = 0;
        if (Gestalt(MT_GESTALT_THREAD_MGR_ATTR, &resp) == noErr &&
            (resp & (1L << MT_GESTALT_THREAD_MGR_PRESENT)))
            g_tm_state = 1;
        else
            g_tm_state = -1;
#endif
    }
    return g_tm_state > 0;
}

/* Record the main thread's ID before main() runs. */
__attribute__((constructor)) static void pt_init_main(void)
{
    if (pt_available())
        mt_GetCurrentThread(&g_main_tid);
}

/* ------------------------------------------------------------------------
 * Registry
 * --------------------------------------------------------------------- */
static void registry_add(pt_thread *t)
{
    t->next = g_threads;
    g_threads = t;
}

static int list_remove(pt_thread **head, pt_thread *t);

static void registry_remove(pt_thread *t)
{
    list_remove(&g_threads, t);
    if (g_cache == t)
        g_cache = NULL;
}

static pt_thread *alloc_thread(void)
{
    pt_thread *t = (pt_thread *)calloc(1, sizeof(pt_thread));
    if (!t)
        return NULL;
    t->magic = PT_MAGIC_THREAD;
    t->cancel_state = PTHREAD_CANCEL_ENABLE;
    t->cancel_type = PTHREAD_CANCEL_DEFERRED;
    t->sched_policy = SCHED_OTHER;
    return t;
}

static void free_thread(pt_thread *t)
{
    t->magic = 0;
    free(t);
}

/* Static storage for the main thread so pthread_self() works even when the
 * heap is exhausted. */
static pt_thread g_main_thread;

pt_thread *pt_self(void)
{
    mt_ThreadID tid = 0;
    pt_thread *t;

    if (!pt_available()) {
        /* No Thread Manager: behave as a single-threaded process. */
        if (g_main_thread.magic != PT_MAGIC_THREAD) {
            g_main_thread.magic = PT_MAGIC_THREAD;
            g_main_thread.is_main = 1;
            g_main_thread.detached = 1;
            g_main_thread.sched_policy = SCHED_OTHER;
        }
        return &g_main_thread;
    }

    mt_GetCurrentThread(&tid);
    if (g_cache && g_cache->tid == tid)
        return g_cache;

    for (t = g_threads; t; t = t->next) {
        if (t->tid == tid) {
            g_cache = t;
            return t;
        }
    }

    /* Not one of ours: adopt it (main thread or foreign Thread Manager
     * thread). */
    if (g_main_tid == 0)
        g_main_tid = tid;       /* constructor did not run (shouldn't happen) */
    if (tid == g_main_tid && g_main_thread.magic != PT_MAGIC_THREAD) {
        t = &g_main_thread;
        memset(t, 0, sizeof(*t));
        t->magic = PT_MAGIC_THREAD;
        t->cancel_state = PTHREAD_CANCEL_ENABLE;
        t->cancel_type = PTHREAD_CANCEL_DEFERRED;
        t->sched_policy = SCHED_OTHER;
        t->is_main = 1;
        t->detached = 1;
        t->tid = tid;
        strcpy(t->name, "main");
        registry_add(t);
        g_cache = t;
        return t;
    }

    t = alloc_thread();
    if (!t) {
        /* Out of memory - fall back to the main record rather than
         * returning NULL; this is a best effort for a foreign thread. */
        return &g_main_thread;
    }
    t->tid = tid;
    t->detached = 1;
    registry_add(t);
    g_cache = t;
    return t;
}

static int list_remove(pt_thread **head, pt_thread *t)
{
    pt_thread **pp;
    for (pp = head; *pp; pp = &(*pp)->next) {
        if (*pp == t) {
            *pp = t->next;
            t->next = NULL;
            return 1;
        }
    }
    return 0;
}

pt_thread *pt_validate(pthread_t th)
{
    pt_thread *want = (pt_thread *)(uintptr_t)th;
    pt_thread *t;

    if (!want)
        return NULL;
    for (t = g_threads; t; t = t->next)
        if (t == want)
            return t;
    for (t = g_zombies; t; t = t->next)
        if (t == want)
            return t;
    return NULL;
}

/* Release a finished thread record that nobody will join any more. */
static void reap_thread(pt_thread *t)
{
    list_remove(&g_zombies, t);
    free_thread(t);
}

void pt_yield(void)
{
    if (pt_available())
        YieldToAnyThread();
}

/* ------------------------------------------------------------------------
 * Wait queues
 * --------------------------------------------------------------------- */
void pt_queue_push(pt_queue *q, pt_waiter *w)
{
    w->next = NULL;
    if (q->tail)
        q->tail->next = w;
    else
        q->head = w;
    q->tail = w;
}

pt_waiter *pt_queue_pop(pt_queue *q)
{
    pt_waiter *w = q->head;
    if (w) {
        q->head = w->next;
        if (!q->head)
            q->tail = NULL;
        w->next = NULL;
    }
    return w;
}

int pt_queue_remove(pt_queue *q, pt_waiter *w)
{
    pt_waiter *prev = NULL, *cur;
    for (cur = q->head; cur; prev = cur, cur = cur->next) {
        if (cur == w) {
            if (prev)
                prev->next = cur->next;
            else
                q->head = cur->next;
            if (q->tail == cur)
                q->tail = prev;
            cur->next = NULL;
            return 1;
        }
    }
    return 0;
}

void pt_wake_thread(pt_thread *t)
{
    if (t && t->blocked) {
        t->blocked = 0;
        SetThreadState(t->tid, kReadyThreadState, kNoThreadID);
    }
}

void pt_wake(pt_waiter *w)
{
    w->woken = 1;
    pt_wake_thread(w->thread);
}

int pt_wake_one(pt_queue *q)
{
    pt_waiter *w = pt_queue_pop(q);
    if (!w)
        return 0;
    pt_wake(w);
    return 1;
}

void pt_wake_all(pt_queue *q)
{
    pt_waiter *w;
    while ((w = pt_queue_pop(q)) != NULL)
        pt_wake(w);
}

int pt_cancel_requested(pt_thread *self)
{
    return self->cancel_pending && !self->canceling &&
           self->cancel_state == PTHREAD_CANCEL_ENABLE;
}

int pt_block(pt_waiter *w, clockid_t clock, const struct timespec *abstime,
             int cancelable)
{
    pt_thread *self = w->thread;

    for (;;) {
        if (w->woken)
            return 0;
        if (cancelable && pt_cancel_requested(self))
            return ECANCELED;
        if (abstime && pt_deadline_passed(clock, abstime))
            return ETIMEDOUT;

        if (!pt_available()) {
            /* Single threaded: nobody can ever wake us. */
            if (!abstime)
                return EDEADLK;
            continue;           /* poll the clock until the deadline */
        }

        if (abstime || !self->created) {
            YieldToAnyThread();
        } else {
            /* No yield happens between the checks above and this call, so
             * a wake-up cannot be lost (cooperative scheduling). */
            self->blocked = 1;
            SetThreadState(kCurrentThreadID, kStoppedThreadState,
                           kNoThreadID);
            self->blocked = 0;
        }
    }
}

/* ------------------------------------------------------------------------
 * Thread creation / termination
 * --------------------------------------------------------------------- */
static void finish_thread(pt_thread *self, void *value)
{
    self->result = value;
    pt_run_tsd_destructors(self);

    self->state = PT_STATE_FINISHED;
    registry_remove(self);
    self->tid = 0;
    if (self->created)
        g_live_created--;
    if (!self->detached) {
        self->next = g_zombies;      /* keep it around for pthread_join() */
        g_zombies = self;
    }

    pt_wake_all(&self->joiners);
}

static pascal void *thread_entry(void *param)
{
    pt_thread *self = (pt_thread *)param;
    void *value;

    if (setjmp(self->exit_jmp) == 0)
        value = self->start(self->arg);
    else
        value = self->result;           /* set by pt_do_exit() */

    finish_thread(self, value);
    if (self->detached)
        free_thread(self);              /* nothing touches it afterwards */

    /* Returning disposes of the Thread Manager thread. */
    return NULL;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
    pt_thread *self, *t;
    Size stack = PTHREADS_RETRO68_DEFAULT_STACKSIZE;
    mt_ThreadID tid = 0;
    OSErr err;

    if (!thread || !start_routine)
        return EINVAL;
    if (attr && !attr->is_initialized)
        return EINVAL;
    if (!pt_available())
        return EAGAIN;

    self = pt_self();               /* make sure the creator is known */

    t = alloc_thread();
    if (!t)
        return EAGAIN;

    t->start = start_routine;
    t->arg = arg;
    t->created = 1;
    t->state = PT_STATE_RUNNING;

    if (attr) {
        if (attr->stacksize > 0)
            stack = attr->stacksize;
        t->detached = (attr->detachstate == PTHREAD_CREATE_DETACHED);
        if (attr->inheritsched == PTHREAD_EXPLICIT_SCHED) {
            t->sched_policy = attr->schedpolicy;
            t->sched_param = attr->schedparam;
        } else {
            t->sched_policy = self->sched_policy;
            t->sched_param = self->sched_param;
        }
    }
    if (stack < PTHREAD_STACK_MIN)
        stack = PTHREAD_STACK_MIN;

    /* Register before NewThread: the new thread does not run until we
     * yield, but keep the record consistent regardless. */
    registry_add(t);
    g_live_created++;

    err = NewThread(kCooperativeThread, thread_entry, t, stack,
                    kCreateIfNeeded, NULL, &tid);
    if (err != noErr) {
        registry_remove(t);
        g_live_created--;
        free_thread(t);
        return EAGAIN;
    }
    t->tid = tid;
    *thread = (pthread_t)(uintptr_t)t;
    return 0;
}

static void run_cleanup_handlers(pt_thread *self)
{
    struct _pthread_cleanup_context *c;
    while ((c = self->cleanup) != NULL) {
        self->cleanup = c->_previous;
        c->_routine(c->_arg);
    }
}

void pt_do_exit(pt_thread *self, void *value)
{
    run_cleanup_handlers(self);

    if (self->created) {
        self->result = value;
        longjmp(self->exit_jmp, 1);
    }

    if (self->is_main) {
        /* POSIX: the process continues until the last thread exits. */
        pt_run_tsd_destructors(self);
        self->state = PT_STATE_FINISHED;
        while (g_live_created > 0 && pt_available())
            YieldToAnyThread();
        exit(0);
    }

    /* Adopted foreign Thread Manager thread. */
    pt_run_tsd_destructors(self);
    registry_remove(self);
    free_thread(self);
    DisposeThread(kCurrentThreadID, value, false);
    for (;;)            /* not reached */
        YieldToAnyThread();
}

void pthread_exit(void *value_ptr)
{
    pt_do_exit(pt_self(), value_ptr);
}

void pt_act_on_cancel(pt_thread *self)
{
    self->canceling = 1;
    self->cancel_state = PTHREAD_CANCEL_DISABLE;
    pt_do_exit(self, PTHREAD_CANCELED);
}

int pthread_join(pthread_t thread, void **value_ptr)
{
    pt_thread *self = pt_self();
    pt_thread *t = pt_validate(thread);
    pt_waiter w;
    int rc;

    if (pt_cancel_requested(self))
        pt_act_on_cancel(self);
    if (!t)
        return ESRCH;
    if (t == self)
        return EDEADLK;
    if (t->detached || t->has_joiner)
        return EINVAL;

    if (t->state != PT_STATE_FINISHED) {
        t->has_joiner = 1;
        w.thread = self;
        w.woken = 0;
        pt_queue_push(&t->joiners, &w);
        rc = pt_block(&w, CLOCK_REALTIME, NULL, 1);
        if (rc != 0) {
            pt_queue_remove(&t->joiners, &w);
            t->has_joiner = 0;
            if (rc == ECANCELED)
                pt_act_on_cancel(self);
            return rc;
        }
    }

    if (value_ptr)
        *value_ptr = t->result;
    reap_thread(t);
    return 0;
}

int pthread_detach(pthread_t thread)
{
    pt_thread *t = pt_validate(thread);
    if (!t)
        return ESRCH;
    if (t->detached)
        return EINVAL;
    if (t->state == PT_STATE_FINISHED) {
        reap_thread(t);
        return 0;
    }
    t->detached = 1;
    return 0;
}

pthread_t pthread_self(void)
{
    return (pthread_t)(uintptr_t)pt_self();
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
    return t1 == t2;
}

unsigned long pthread_mac_threadid_np(pthread_t thread)
{
    pt_thread *t = pt_validate(thread);
    return t ? (unsigned long)t->tid : 0;
}

void pthread_yield(void)
{
    pt_yield();
}

int sched_yield(void)
{
    pt_yield();
    return 0;
}

int pthread_getconcurrency(void)
{
    return g_concurrency;
}

int pthread_setconcurrency(int new_level)
{
    if (new_level < 0)
        return EINVAL;
    g_concurrency = new_level;
    return 0;
}

int pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
    (void)thread;
    (void)clock_id;
    return ENOENT;
}

int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                   void (*child)(void))
{
    /* There is no fork() on classic Mac OS; accept and ignore. */
    (void)prepare;
    (void)parent;
    (void)child;
    return 0;
}

/* ------------------------------------------------------------------------
 * Scheduling parameters (the Thread Manager has no priorities; values are
 * stored and reported back only)
 * --------------------------------------------------------------------- */
static int policy_valid(int policy)
{
    return policy == SCHED_OTHER || policy == SCHED_FIFO ||
           policy == SCHED_RR;
}

int sched_get_priority_min(int policy)
{
    if (!policy_valid(policy)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int sched_get_priority_max(int policy)
{
    if (!policy_valid(policy)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pthread_getschedparam(pthread_t thread, int *policy,
                          struct sched_param *param)
{
    pt_thread *t = pt_validate(thread);
    if (!t)
        return ESRCH;
    if (policy)
        *policy = t->sched_policy;
    if (param)
        *param = t->sched_param;
    return 0;
}

int pthread_setschedparam(pthread_t thread, int policy,
                          const struct sched_param *param)
{
    pt_thread *t = pt_validate(thread);
    if (!t)
        return ESRCH;
    if (!param || !policy_valid(policy))
        return EINVAL;
    if (param->sched_priority != 0)
        return ENOTSUP;
    t->sched_policy = policy;
    t->sched_param = *param;
    return 0;
}

int pthread_setschedprio(pthread_t thread, int prio)
{
    pt_thread *t = pt_validate(thread);
    if (!t)
        return ESRCH;
    if (prio != 0)
        return EINVAL;
    t->sched_param.sched_priority = prio;
    return 0;
}

int pthread_setname_np(pthread_t thread, const char *name)
{
    pt_thread *t = pt_validate(thread);
    if (!t)
        return ESRCH;
    if (!name)
        return EINVAL;
    if (strlen(name) >= sizeof(t->name))
        return ERANGE;
    strcpy(t->name, name);
    return 0;
}

int pthread_getname_np(pthread_t thread, char *name, size_t len)
{
    pt_thread *t = pt_validate(thread);
    if (!t)
        return ESRCH;
    if (!name || len == 0)
        return EINVAL;
    if (strlen(t->name) >= len)
        return ERANGE;
    strcpy(name, t->name);
    return 0;
}

/* ------------------------------------------------------------------------
 * Cancellation
 * --------------------------------------------------------------------- */
int pthread_cancel(pthread_t thread)
{
    pt_thread *t = pt_validate(thread);
    if (!t || t->state == PT_STATE_FINISHED)
        return ESRCH;

    t->cancel_pending = 1;
    if (t == pt_self()) {
        if (t->cancel_state == PTHREAD_CANCEL_ENABLE &&
            t->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS && !t->canceling)
            pt_act_on_cancel(t);
        return 0;
    }
    /* Let it notice the request if it is blocked at a cancellation point.
     * The waiter's 'woken' flag stays clear, so pt_block() will report
     * ECANCELED (or simply re-block if cancellation is disabled). */
    pt_wake_thread(t);
    return 0;
}

int pthread_setcancelstate(int state, int *oldstate)
{
    pt_thread *self = pt_self();
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
        return EINVAL;
    if (oldstate)
        *oldstate = self->cancel_state;
    if (!self->canceling)
        self->cancel_state = state;
    return 0;
}

int pthread_setcanceltype(int type, int *oldtype)
{
    pt_thread *self = pt_self();
    if (type != PTHREAD_CANCEL_DEFERRED &&
        type != PTHREAD_CANCEL_ASYNCHRONOUS)
        return EINVAL;
    if (oldtype)
        *oldtype = self->cancel_type;
    self->cancel_type = type;
    if (type == PTHREAD_CANCEL_ASYNCHRONOUS && pt_cancel_requested(self))
        pt_act_on_cancel(self);
    return 0;
}

void pthread_testcancel(void)
{
    pt_thread *self = pt_self();
    if (pt_cancel_requested(self))
        pt_act_on_cancel(self);
}

void _pthread_cleanup_push(struct _pthread_cleanup_context *context,
                           void (*routine)(void *), void *arg)
{
    pt_thread *self = pt_self();
    context->_routine = routine;
    context->_arg = arg;
    context->_canceltype = self->cancel_type;
    context->_previous = self->cleanup;
    self->cleanup = context;
}

void _pthread_cleanup_pop(struct _pthread_cleanup_context *context,
                          int execute)
{
    pt_thread *self = pt_self();
    self->cleanup = context->_previous;
    if (execute)
        context->_routine(context->_arg);
}

/* ------------------------------------------------------------------------
 * pthread_once
 * --------------------------------------------------------------------- */
enum { ONCE_NOT_RUN = 0, ONCE_DONE = 1, ONCE_RUNNING = 2 };

static void once_cancel_cleanup(void *arg)
{
    ((pthread_once_t *)arg)->init_executed = ONCE_NOT_RUN;
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    if (!once_control || !init_routine)
        return EINVAL;

    for (;;) {
        switch (once_control->init_executed) {
        case ONCE_DONE:
            return 0;
        case ONCE_NOT_RUN:
            once_control->init_executed = ONCE_RUNNING;
            pthread_cleanup_push(once_cancel_cleanup, once_control);
            init_routine();
            pthread_cleanup_pop(0);
            once_control->init_executed = ONCE_DONE;
            return 0;
        default:
            /* Another thread is running the initialiser and yielded. */
            pt_yield();
            break;
        }
    }
}
