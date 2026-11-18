/*
 * pthread_test.c - smoke test for pthreads-retro68.
 *
 * Built as a Retro68 console application; run it on a Mac (or emulator)
 * with the Thread Manager (System 7.5+, or the Thread Manager extension).
 */
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("  FAILED: %s (line %d)\n", #cond, __LINE__);        \
            failures++;                                                 \
        }                                                               \
    } while (0)

/* ----------------------------------------------------------------------- */
static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
static long counter;

static void *count_worker(void *arg)
{
    int i, n = (int)(long)arg;
    for (i = 0; i < n; i++) {
        long tmp;
        pthread_mutex_lock(&counter_lock);
        tmp = counter;
        sched_yield();              /* invite a race */
        counter = tmp + 1;
        pthread_mutex_unlock(&counter_lock);
    }
    return (void *)(long)n;
}

static void test_mutex(void)
{
    pthread_t t[4];
    void *ret;
    int i;

    printf("mutex + join...\n");
    for (i = 0; i < 4; i++)
        CHECK(pthread_create(&t[i], NULL, count_worker, (void *)100L) == 0);
    for (i = 0; i < 4; i++) {
        CHECK(pthread_join(t[i], &ret) == 0);
        CHECK(ret == (void *)100L);
    }
    CHECK(counter == 400);
}

/* ----------------------------------------------------------------------- */
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_cond = PTHREAD_COND_INITIALIZER;
static int q_items, q_done, q_consumed;

static void *consumer(void *arg)
{
    pthread_mutex_lock(&q_lock);
    for (;;) {
        while (q_items == 0 && !q_done)
            pthread_cond_wait(&q_cond, &q_lock);
        if (q_items == 0 && q_done)
            break;
        q_items--;
        q_consumed++;
    }
    pthread_mutex_unlock(&q_lock);
    return NULL;
}

static void test_cond(void)
{
    pthread_t c[3];
    int i;

    printf("condition variables...\n");
    for (i = 0; i < 3; i++)
        pthread_create(&c[i], NULL, consumer, NULL);
    for (i = 0; i < 50; i++) {
        pthread_mutex_lock(&q_lock);
        q_items++;
        pthread_cond_signal(&q_cond);
        pthread_mutex_unlock(&q_lock);
        if (i % 7 == 0)
            sched_yield();
    }
    pthread_mutex_lock(&q_lock);
    q_done = 1;
    pthread_cond_broadcast(&q_cond);
    pthread_mutex_unlock(&q_lock);
    for (i = 0; i < 3; i++)
        pthread_join(c[i], NULL);
    CHECK(q_consumed == 50);
}

/* ----------------------------------------------------------------------- */
static void test_timedwait(void)
{
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t c = PTHREAD_COND_INITIALIZER;
    struct timespec start, now, deadline;
    int rc;

    printf("timed wait (200 ms)...\n");
    clock_gettime(CLOCK_REALTIME, &start);
    deadline = start;
    deadline.tv_nsec += 200000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        deadline.tv_sec++;
    }
    pthread_mutex_lock(&m);
    rc = pthread_cond_timedwait(&c, &m, &deadline);
    pthread_mutex_unlock(&m);
    clock_gettime(CLOCK_REALTIME, &now);
    CHECK(rc == ETIMEDOUT);
    CHECK((now.tv_sec - start.tv_sec) * 1000 +
          (now.tv_nsec - start.tv_nsec) / 1000000 >= 190);
    pthread_cond_destroy(&c);
    pthread_mutex_destroy(&m);
}

/* ----------------------------------------------------------------------- */
static pthread_key_t key;
static int destructor_calls;

static void key_destructor(void *v)
{
    (void)v;
    destructor_calls++;
}

static void *tsd_worker(void *arg)
{
    pthread_setspecific(key, arg);
    sched_yield();
    return pthread_getspecific(key);
}

static void test_tsd(void)
{
    pthread_t a, b;
    void *ra, *rb;

    printf("thread-specific data...\n");
    CHECK(pthread_key_create(&key, key_destructor) == 0);
    pthread_create(&a, NULL, tsd_worker, (void *)1L);
    pthread_create(&b, NULL, tsd_worker, (void *)2L);
    pthread_join(a, &ra);
    pthread_join(b, &rb);
    CHECK(ra == (void *)1L && rb == (void *)2L);
    CHECK(destructor_calls == 2);
    CHECK(pthread_getspecific(key) == NULL);
    pthread_key_delete(key);
}

/* ----------------------------------------------------------------------- */
static int cleanup_ran;

static void cleanup(void *arg)
{
    *(int *)arg = 1;
}

static void *cancel_worker(void *arg)
{
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t c = PTHREAD_COND_INITIALIZER;
    (void)arg;
    pthread_cleanup_push(cleanup, &cleanup_ran);
    pthread_mutex_lock(&m);
    for (;;)
        pthread_cond_wait(&c, &m);     /* cancellation point */
    pthread_cleanup_pop(0);
    return NULL;
}

static void test_cancel(void)
{
    pthread_t t;
    void *ret;

    printf("cancellation...\n");
    pthread_create(&t, NULL, cancel_worker, NULL);
    sched_yield();                      /* let it block */
    CHECK(pthread_cancel(t) == 0);
    CHECK(pthread_join(t, &ret) == 0);
    CHECK(ret == PTHREAD_CANCELED);
    CHECK(cleanup_ran == 1);
}

/* ----------------------------------------------------------------------- */
static sem_t sem;
static int sem_got;

static void *sem_worker(void *arg)
{
    (void)arg;
    sem_wait(&sem);
    sem_got++;
    return NULL;
}

static void test_sem_barrier_rwlock(void)
{
    pthread_t t[3];
    pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
    pthread_once_t once = PTHREAD_ONCE_INIT;
    int i;

    printf("semaphores, rwlocks...\n");
    sem_init(&sem, 0, 0);
    for (i = 0; i < 3; i++)
        pthread_create(&t[i], NULL, sem_worker, NULL);
    sched_yield();
    CHECK(sem_got == 0);
    for (i = 0; i < 3; i++)
        sem_post(&sem);
    for (i = 0; i < 3; i++)
        pthread_join(t[i], NULL);
    CHECK(sem_got == 3);
    sem_destroy(&sem);

    CHECK(pthread_rwlock_rdlock(&rw) == 0);
    CHECK(pthread_rwlock_rdlock(&rw) == 0);
    CHECK(pthread_rwlock_trywrlock(&rw) == EBUSY);
    pthread_rwlock_unlock(&rw);
    pthread_rwlock_unlock(&rw);
    CHECK(pthread_rwlock_trywrlock(&rw) == 0);
    CHECK(pthread_rwlock_tryrdlock(&rw) == EBUSY);
    pthread_rwlock_unlock(&rw);
    pthread_rwlock_destroy(&rw);
    (void)once;
}

static pthread_barrier_t barrier;
static int serial_count;

static void *barrier_worker(void *arg)
{
    (void)arg;
    if (pthread_barrier_wait(&barrier) == PTHREAD_BARRIER_SERIAL_THREAD)
        serial_count++;
    return NULL;
}

static void test_barrier(void)
{
    pthread_t t[3];
    int i;

    printf("barriers...\n");
    pthread_barrier_init(&barrier, NULL, 4);
    for (i = 0; i < 3; i++)
        pthread_create(&t[i], NULL, barrier_worker, NULL);
    barrier_worker(NULL);
    for (i = 0; i < 3; i++)
        pthread_join(t[i], NULL);
    CHECK(serial_count == 1);
    pthread_barrier_destroy(&barrier);
}

/* ----------------------------------------------------------------------- */
static int once_calls;
static pthread_once_t once_ctl = PTHREAD_ONCE_INIT;
static void once_fn(void) { once_calls++; }

static void *detached_worker(void *arg)
{
    pthread_once(&once_ctl, once_fn);
    usleep(1000);
    *(int *)arg = 1;
    return NULL;
}

static void test_detached_once(void)
{
    pthread_attr_t attr;
    pthread_t t;
    int flags[2] = {0, 0};
    int i;

    printf("detached threads, once, sleep...\n");
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 32 * 1024);
    for (i = 0; i < 2; i++)
        CHECK(pthread_create(&t, &attr, detached_worker, &flags[i]) == 0);
    pthread_attr_destroy(&attr);
    CHECK(pthread_join(t, NULL) == EINVAL);
    for (i = 0; i < 200 && !(flags[0] && flags[1]); i++)
        usleep(1000);
    CHECK(flags[0] && flags[1]);
    CHECK(once_calls == 1);
}

int main(void)
{
    printf("pthreads-retro68 self test\n");
    printf("main thread: Thread Manager ID %lu\n",
           pthread_mac_threadid_np(pthread_self()));

    test_mutex();
    test_cond();
    test_timedwait();
    test_tsd();
    test_cancel();
    test_sem_barrier_rwlock();
    test_barrier();
    test_detached_once();

    if (failures)
        printf("\n%d check(s) FAILED\n", failures);
    else
        printf("\nall tests passed\n");
    printf("(press return to quit)\n");
    getchar();
    return failures != 0;
}
