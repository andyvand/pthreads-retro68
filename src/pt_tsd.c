/*
 * pt_tsd.c - thread-specific data (pthread_key_*).
 */
#include "pt_internal.h"

typedef struct {
    int in_use;
    unsigned long seq;          /* bumped on delete to detect stale keys */
    void (*destructor)(void *);
} pt_key;

static pt_key g_keys[PTHREAD_KEYS_MAX];

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    int i;
    if (!key)
        return EINVAL;
    for (i = 0; i < PTHREAD_KEYS_MAX; i++) {
        if (!g_keys[i].in_use) {
            g_keys[i].in_use = 1;
            g_keys[i].seq++;
            g_keys[i].destructor = destructor;
            *key = (pthread_key_t)i;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key)
{
    if (key >= PTHREAD_KEYS_MAX || !g_keys[key].in_use)
        return EINVAL;
    /* POSIX: destructors are not called.  Values stored under the old key
     * are hidden by the sequence number check below. */
    g_keys[key].in_use = 0;
    g_keys[key].destructor = NULL;
    return 0;
}

/* Each thread stores, next to every value, the key sequence number that was
 * current when it was set, so a value from a deleted key never shows up in
 * a new key that reuses the same slot. */

int pthread_setspecific(pthread_key_t key, const void *value)
{
    pt_thread *self;
    if (key >= PTHREAD_KEYS_MAX || !g_keys[key].in_use)
        return EINVAL;
    self = pt_self();
    self->specific[key] = (void *)value;
    self->specific_seq[key] = g_keys[key].seq;
    if (value)
        self->has_specific = 1;
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    pt_thread *self;
    if (key >= PTHREAD_KEYS_MAX || !g_keys[key].in_use)
        return NULL;
    self = pt_self();
    if (self->specific_seq[key] != g_keys[key].seq)
        return NULL;
    return self->specific[key];
}

void pt_run_tsd_destructors(pt_thread *self)
{
    int iter, i, again;

    if (!self->has_specific)
        return;

    for (iter = 0; iter < PTHREAD_DESTRUCTOR_ITERATIONS; iter++) {
        again = 0;
        for (i = 0; i < PTHREAD_KEYS_MAX; i++) {
            void *v = self->specific[i];
            if (!v || !g_keys[i].in_use ||
                self->specific_seq[i] != g_keys[i].seq)
                continue;
            self->specific[i] = NULL;
            if (g_keys[i].destructor) {
                g_keys[i].destructor(v);
                again = 1;
            }
        }
        if (!again)
            break;
    }
    self->has_specific = 0;
}
