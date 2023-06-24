#ifndef cqueue_h
#define cqueue_h

#include "rc.h"
#include "spinlock.h"
#include <stdint.h>
#include <stddef.h>

typedef struct timespec timespec_t;

typedef struct cqueue_attr_st {
    uint32_t block_size;
    uint32_t num_blocks;
    spinlock_attrs_t lock_attrs;
} cqueue_attr_t;

typedef struct cqueue_obj_st {
    uint32_t head;
    uint32_t tail;
    uint32_t available_msgs;
    uint32_t free_blocks;
    uint32_t num_blocks;
    uint32_t block_size;
    spinlock_obj_t lock_obj;
    uint64_t data[];
} cqueue_obj_t;

typedef struct cqueue_st {
    cqueue_obj_t* obj;
    spinlock_t lock;
} cqueue_t;

rc_t cqueue_attr_init(cqueue_attr_t* attrs);
rc_t cqueue_alloc_size(cqueue_attr_t* attrs, int* required_bytes);
rc_t cqueue_init(cqueue_t* queue, cqueue_obj_t* obj, cqueue_attr_t* attrs);
rc_t cqueue_create(cqueue_t* queue, cqueue_attr_t* attrs);
rc_t cqueue_destroy(cqueue_t* queue);
rc_t cqueue_enqueue(cqueue_t* queue, void* item, uint32_t size, timespec_t* timeout);
rc_t cqueue_dequeue(cqueue_t* handle, uint32_t max_bytes, void** item, uint32_t* size, timespec_t* timeout);
rc_t cqueue_size(cqueue_t* queue, uint32_t* size);

#endif