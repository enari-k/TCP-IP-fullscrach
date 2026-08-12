#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#include "platform.h"

#include "util.h"
#include "net.h"

int platform_init(void)
{
    srandom(time(NULL));
    if (intr_init() == -1)
    {
        return -1;
    }
    intr_register(INTR_IRQ_SOFT, net_softirq_handler, 0, NULL);
    if (timer_init() == -1)
    {
        return -1;
    }
    if (sched_init() == -1)
    {
        return -1;
    }
    return 0;
}

int platform_run(void)
{
    if (intr_run() == -1)
    {
        return -1;
    }
    if (timer_run() == -1)
    {
        return -1;
    }
    if (sched_run() == -1)
    {
        return -1;
    }
    return 0;
}

int platform_shutdown(void)
{
    if (intr_shutdown() == -1)
    {
        return -1;
    }
    if (timer_shutdown() == -1)
    {
        return -1;
    }
    if (sched_shutdown() == -1)
    {
        return -1;
    }
    return 0;
}

/*
 * Memory
 */

void *
memory_alloc(size_t size)
{
    return calloc(1, size);
}

void memory_free(void *ptr)
{
    free(ptr);
}

/*
 * Lock
 */

int lock_init(lock_t *lock)
{
    return pthread_mutex_init(lock, NULL);
}

int lock_acquire(lock_t *lock)
{
    return pthread_mutex_lock(lock);
}

int lock_release(lock_t *lock)
{
    return pthread_mutex_unlock(lock);
}

/*
 * Random
 */

uint16_t
random16(void)
{
    return random() % (UINT16_MAX + 1);
}
