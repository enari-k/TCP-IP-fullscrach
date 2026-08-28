#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

/*
 * Array
 */

#define countof(x) ((sizeof(x) / sizeof(x[0])))
#define tailof(x) (x + countof(x))
#define indexof(x, y) (((uintptr_t)y - (uintptr_t)x) / sizeof(*y))

/*
 * Time
 */

#define timeval_add_usec(x, y)         \
    do {                               \
        (x)->tv_sec += (y) / 1000000;  \
        (x)->tv_usec += (y) % 1000000; \
        if ((x)->tv_usec >= 1000000) { \
            (x)->tv_sec += 1;          \
            (x)->tv_usec -= 1000000;   \
        }                              \
    } while (0);

#define timespec_add_nsec(x, y)           \
    do {                                  \
        (x)->tv_sec += (y) / 1000000000;  \
        (x)->tv_nsec += (y) % 1000000000; \
        if ((x)->tv_nsec >= 1000000000) { \
            (x)->tv_sec += 1;             \
            (x)->tv_nsec -= 1000000000;   \
        }                                 \
    } while (0);

/*
 * Logging
 */
#define logf(lv, fmt, ...) \
    lprintf(stderr, lv, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define errorf(fmt, ...) logf('E', fmt, ##__VA_ARGS__)
#define warnf(fmt, ...) logf('W', fmt, ##__VA_ARGS__)
#define infof(fmt, ...) logf('I', fmt, ##__VA_ARGS__)

#ifdef DISABLE_DEBUG_LOG
#define debugf(...) ((void)0)
#else
#define debugf(fmt, ...) logf('D', fmt, ##__VA_ARGS__)
#endif

#ifdef HEXDUMP
#define debugdump(...) hexdump(stderr, __VA_ARGS__)
#else
#define debugdump(...)
#endif

/*
 * Queue
 */

struct queue_entry {
    struct queue_entry *next;
    /* data bytes exists after this structure. */
};

struct queue {
    struct queue_entry *head;
    struct queue_entry *tail;
    size_t num;
};

typedef void (*queue_func_t)(void *arg, struct queue_entry *entry);

extern void
queue_init(struct queue *queue);
extern struct queue_entry *
queue_push(struct queue *queue, struct queue_entry *entry);
extern struct queue_entry *
queue_pop(struct queue *queue);
extern struct queue_entry *
queue_peek(struct queue *queue);
extern void
queue_foreach(struct queue *queue, queue_func_t func, void *arg);

/*
 * Byteorder
 */

extern uint16_t
hton16(uint16_t h);
extern uint16_t
ntoh16(uint16_t n);
extern uint32_t
hton32(uint32_t h);
extern uint32_t
ntoh32(uint32_t n);

/*
 * Checksum
 */

extern uint16_t
cksum16(uint16_t *addr, uint16_t count, uint32_t init);

#endif
