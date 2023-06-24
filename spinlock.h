#ifndef spinlock_h
#define spinlock_h

#include "rc.h"
#include <stdatomic.h>

typedef struct spinlock_attrs_st {
    int sleep_usecs;
} spinlock_attrs_t;

typedef struct spinlock_obj_st {
    volatile atomic_int lock;
    int sleep_usecs;
} spinlock_obj_t;

typedef struct spinlock_st {
    spinlock_obj_t* obj;
} spinlock_t;

rc_t spinlock_attr_init(spinlock_attrs_t* attrs);
rc_t spinlock_init(spinlock_t* handle, spinlock_obj_t* obj, spinlock_attrs_t* attrs);
rc_t spinlock_create(spinlock_t* handle, spinlock_attrs_t* attrs);
rc_t spinlock_acquire(spinlock_t* handle);
rc_t spinlock_release(spinlock_t* handle);
rc_t spinlock_destroy(spinlock_t* handle);

#endif