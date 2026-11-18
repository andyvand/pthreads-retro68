/*
 * pt_attr.c - thread creation attributes.
 */
#include "pt_internal.h"
#include <string.h>

#define CHECK_ATTR(a) do { if (!(a) || !(a)->is_initialized) return EINVAL; } while (0)

int pthread_attr_init(pthread_attr_t *attr)
{
    if (!attr)
        return EINVAL;
    memset(attr, 0, sizeof(*attr));
    attr->is_initialized = 1;
    attr->stacksize = 0;                       /* library default */
    attr->contentionscope = PTHREAD_SCOPE_PROCESS;
    attr->inheritsched = PTHREAD_INHERIT_SCHED;
    attr->schedpolicy = SCHED_OTHER;
    attr->detachstate = PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    CHECK_ATTR(attr);
    attr->is_initialized = 0;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state)
{
    CHECK_ATTR(attr);
    if (!state)
        return EINVAL;
    *state = attr->detachstate;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state)
{
    CHECK_ATTR(attr);
    if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED)
        return EINVAL;
    attr->detachstate = state;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *size)
{
    CHECK_ATTR(attr);
    if (!size)
        return EINVAL;
    *size = attr->stacksize > 0 ? (size_t)attr->stacksize
                                : (size_t)PTHREADS_RETRO68_DEFAULT_STACKSIZE;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size)
{
    CHECK_ATTR(attr);
    if (size < PTHREAD_STACK_MIN || size > 0x7FFFFFFFu)
        return EINVAL;
    attr->stacksize = (int)size;
    return 0;
}

/* The Thread Manager always allocates thread stacks itself. */
int pthread_attr_getstackaddr(const pthread_attr_t *attr, void **addr)
{
    CHECK_ATTR(attr);
    if (!addr)
        return EINVAL;
    *addr = attr->stackaddr;
    return 0;
}

int pthread_attr_setstackaddr(pthread_attr_t *attr, void *addr)
{
    CHECK_ATTR(attr);
    (void)addr;
    return ENOTSUP;
}

int pthread_attr_getstack(const pthread_attr_t *attr, void **addr,
                          size_t *size)
{
    CHECK_ATTR(attr);
    if (!addr || !size)
        return EINVAL;
    *addr = attr->stackaddr;
    return pthread_attr_getstacksize(attr, size);
}

int pthread_attr_setstack(pthread_attr_t *attr, void *addr, size_t size)
{
    CHECK_ATTR(attr);
    (void)addr;
    (void)size;
    return ENOTSUP;
}

int pthread_attr_getguardsize(const pthread_attr_t *attr, size_t *size)
{
    CHECK_ATTR(attr);
    if (!size)
        return EINVAL;
    *size = 0;                  /* no guard pages on classic Mac OS */
    return 0;
}

int pthread_attr_setguardsize(pthread_attr_t *attr, size_t size)
{
    CHECK_ATTR(attr);
    (void)size;                 /* accepted and ignored, as POSIX allows */
    return 0;
}

int pthread_attr_getscope(const pthread_attr_t *attr, int *scope)
{
    CHECK_ATTR(attr);
    if (!scope)
        return EINVAL;
    *scope = attr->contentionscope;
    return 0;
}

int pthread_attr_setscope(pthread_attr_t *attr, int scope)
{
    CHECK_ATTR(attr);
    if (scope == PTHREAD_SCOPE_PROCESS) {
        attr->contentionscope = scope;
        return 0;
    }
    return scope == PTHREAD_SCOPE_SYSTEM ? ENOTSUP : EINVAL;
}

int pthread_attr_getinheritsched(const pthread_attr_t *attr, int *inherit)
{
    CHECK_ATTR(attr);
    if (!inherit)
        return EINVAL;
    *inherit = attr->inheritsched;
    return 0;
}

int pthread_attr_setinheritsched(pthread_attr_t *attr, int inherit)
{
    CHECK_ATTR(attr);
    if (inherit != PTHREAD_INHERIT_SCHED && inherit != PTHREAD_EXPLICIT_SCHED)
        return EINVAL;
    attr->inheritsched = inherit;
    return 0;
}

int pthread_attr_getschedpolicy(const pthread_attr_t *attr, int *policy)
{
    CHECK_ATTR(attr);
    if (!policy)
        return EINVAL;
    *policy = attr->schedpolicy;
    return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy)
{
    CHECK_ATTR(attr);
    if (policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR)
        return EINVAL;
    attr->schedpolicy = policy;
    return 0;
}

int pthread_attr_getschedparam(const pthread_attr_t *attr,
                               struct sched_param *param)
{
    CHECK_ATTR(attr);
    if (!param)
        return EINVAL;
    *param = attr->schedparam;
    return 0;
}

int pthread_attr_setschedparam(pthread_attr_t *attr,
                               const struct sched_param *param)
{
    CHECK_ATTR(attr);
    if (!param)
        return EINVAL;
    if (param->sched_priority != 0)
        return ENOTSUP;         /* the Thread Manager has no priorities */
    attr->schedparam = *param;
    return 0;
}
