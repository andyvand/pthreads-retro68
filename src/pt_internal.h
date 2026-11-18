/*
 * pt_internal.h - private definitions of pthreads-retro68.
 */
#ifndef PTHREADS_RETRO68_INTERNAL_H
#define PTHREADS_RETRO68_INTERNAL_H

#include <pthread.h>
#include <setjmp.h>
#include <errno.h>
#include "mac_threads.h"

#ifndef ENOTSUP
#define ENOTSUP 134
#endif

/* Values a lazily-initialised handle may hold before first use. */
#define PT_HANDLE_STATIC     ((uint32_t) 0xFFFFFFFFu) /* *_INITIALIZER   */
#define PT_HANDLE_RECURSIVE  ((uint32_t) 0xFFFFFFFEu) /* recursive mutex */
#define PT_HANDLE_ERRORCHECK ((uint32_t) 0xFFFFFFFDu) /* errorcheck mutex*/
#define PT_HANDLE_IS_STATIC(h) ((h) == 0 || (h) >= PT_HANDLE_ERRORCHECK)

#define PT_MAGIC_THREAD  0x50544844 /* 'PTHD' */
#define PT_MAGIC_SEM     0x5053454D /* 'PSEM' */

typedef struct pt_thread pt_thread;

/* A node of a FIFO wait queue.  Lives on the stack of the waiting thread. */
typedef struct pt_waiter {
    struct pt_waiter *next;
    pt_thread *thread;
    volatile int woken;
} pt_waiter;

typedef struct {
    pt_waiter *head;
    pt_waiter *tail;
} pt_queue;

enum {
    PT_STATE_RUNNING  = 0,
    PT_STATE_FINISHED = 1
};

struct pt_thread {
    uint32_t magic;
    pt_thread *next;               /* registry link                      */
    mt_ThreadID tid;               /* Thread Manager ID (0 when gone)    */

    void *(*start)(void *);
    void *arg;
    void *result;

    int state;
    int detached;
    int created;                   /* 1: pthread_create, 0: adopted      */
    int is_main;
    volatile int blocked;          /* stopped in pt_block()              */

    pt_queue joiners;
    int has_joiner;

    volatile int cancel_pending;
    int cancel_state;
    int cancel_type;
    int canceling;                 /* already acting on a cancellation   */

    struct _pthread_cleanup_context *cleanup;

    void *specific[PTHREAD_KEYS_MAX];
    unsigned long specific_seq[PTHREAD_KEYS_MAX];
    int has_specific;

    int sched_policy;
    struct sched_param sched_param;
    char name[32];

    jmp_buf exit_jmp;
};

/* --- core (pt_thread.c) ------------------------------------------------ */
int        pt_available(void);         /* Thread Manager present?        */
pt_thread *pt_self(void);              /* never NULL once available      */
pt_thread *pt_validate(pthread_t t);   /* NULL if not a live thread      */
void       pt_yield(void);
void       pt_do_exit(pt_thread *self, void *value) __attribute__((noreturn));

/* --- blocking (pt_thread.c) -------------------------------------------- */
void pt_queue_push(pt_queue *q, pt_waiter *w);
int  pt_queue_remove(pt_queue *q, pt_waiter *w);   /* 1 if it was queued */
pt_waiter *pt_queue_pop(pt_queue *q);
void pt_wake(pt_waiter *w);                        /* mark + make ready  */
int  pt_wake_one(pt_queue *q);                     /* 1 if one was woken */
void pt_wake_all(pt_queue *q);
void pt_wake_thread(pt_thread *t);

/* Waits until w->woken, the absolute deadline passes (ETIMEDOUT) or, if
 * cancelable, a cancellation request arrives (ECANCELED).  Returns 0 when
 * woken.  The waiter is NOT removed from any queue by this function.      */
int  pt_block(pt_waiter *w, clockid_t clock, const struct timespec *abstime,
              int cancelable);

/* Cancellation helpers */
int  pt_cancel_requested(pt_thread *self);
void pt_act_on_cancel(pt_thread *self) __attribute__((noreturn));

/* --- time (pt_time.c) --------------------------------------------------- */
int  pt_clock_valid(clockid_t clock);
void pt_now(clockid_t clock, struct timespec *ts);
int  pt_deadline_passed(clockid_t clock, const struct timespec *abstime);
int  pt_timespec_valid(const struct timespec *ts);

/* --- TSD (pt_tsd.c) ----------------------------------------------------- */
void pt_run_tsd_destructors(pt_thread *self);

/* --- mutex internals used by condvars (pt_mutex.c) ---------------------- */
typedef struct pt_mutex pt_mutex;
pt_mutex *pt_mutex_get(pthread_mutex_t *m, int *err);
int  pt_mutex_lock_internal(pt_mutex *m, clockid_t clock,
                            const struct timespec *abstime);
int  pt_mutex_unlock_internal(pt_mutex *m);
/* Fully releases a (possibly recursive) mutex owned by the caller and
 * returns the recursion count so it can be restored. */
int  pt_mutex_release_all(pt_mutex *m, int *count);
int  pt_mutex_restore(pt_mutex *m, int count);

#endif /* PTHREADS_RETRO68_INTERNAL_H */
