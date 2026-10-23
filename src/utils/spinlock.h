#ifndef NYANOS_SPINLOCK_H
#define NYANOS_SPINLOCK_H

#include "asm_instrs.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INIT {0}

static inline void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
}

static inline void spinlock_acquire(spinlock_t *lock) {
    uint32_t value = 1;
    while (1) {
        __asm__ volatile (
            "xchgl %0, %1"
            : "=r" (value), "=m" (lock->locked)
            : "0" (value), "m" (lock->locked)
            : "memory"
        );
        if (value == 0) break;
        
        __asm__ volatile ("pause" ::: "memory");
    }
}

static inline void spinlock_release(spinlock_t *lock) {
    __asm__ volatile (
        "movl $0, %0"
        : "=m" (lock->locked)
        :
        : "memory"
    );
}

#define SPIN_LOCK_IRQSAVE(lock, flags) \
    do { \
        flags = read_rflags(); \
        cli(); \
        spinlock_acquire(lock); \
    } while (0)

#define SPIN_UNLOCK_IRQRESTORE(lock, flags) \
    do { \
        spinlock_release(lock); \
        if (flags & (1 << 9)) { \
            sti(); \
        } \
    } while (0)

#endif