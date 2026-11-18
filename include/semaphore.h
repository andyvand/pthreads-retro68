/*
 * semaphore.h - POSIX unnamed semaphores for Retro68 (pthreads-retro68).
 * Named semaphores (sem_open & co.) are not supported and fail with ENOSYS.
 */
#ifndef PTHREADS_RETRO68_SEMAPHORE_H
#define PTHREADS_RETRO68_SEMAPHORE_H

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int __value;
    void *__head;      /* wait queue (library private) */
    void *__tail;
    int __magic;
} sem_t;

#define SEM_FAILED ((sem_t *) 0)
#ifndef SEM_VALUE_MAX
#define SEM_VALUE_MAX 0x7FFFFFFF
#endif

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_destroy(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_timedwait(sem_t *sem, const struct timespec *abstime);
int sem_clockwait(sem_t *sem, clockid_t clock,
                  const struct timespec *abstime);
int sem_post(sem_t *sem);
int sem_getvalue(sem_t *sem, int *sval);

sem_t *sem_open(const char *name, int oflag, ...);
int sem_close(sem_t *sem);
int sem_unlink(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PTHREADS_RETRO68_SEMAPHORE_H */
