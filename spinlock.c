#include "spinlock.h"
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdatomic.h>

#define SPINLOCK_DEFAULT_SLEEP_USECS 10

rc_t spinlock_attr_init(spinlock_attrs_t* attrs) {
    if (attrs == NULL)
        return InvalidArgument;

    attrs->sleep_usecs = SPINLOCK_DEFAULT_SLEEP_USECS;

    return Success;
}

rc_t spinlock_init(spinlock_t* handle, spinlock_obj_t* obj, spinlock_attrs_t* attrs) {
    spinlock_attrs_t default_attrs;
    rc_t rc;

    if (handle == NULL) {
        fprintf(stderr, "Invalid Handle\n");
        return InvalidArgument;
    }

    if (obj == NULL) {
        fprintf(stderr, "Invalid Object\n");
        return InvalidArgument;
    }

    if (attrs == NULL) {
        attrs = &default_attrs;
        rc = spinlock_attr_init(attrs);
        if (rc != Success) {
            fprintf(stderr, "Attr init failed\n");
            return rc;
        }
    }
        
    handle->obj = obj;
    handle->obj->sleep_usecs = attrs->sleep_usecs;
    atomic_init(&handle->obj->lock, 0);

    return Success;
}

rc_t spinlock_create(spinlock_t* handle, spinlock_attrs_t* attrs) {
    rc_t rc;
    if (handle == NULL) {
        fprintf(stderr, "Invalid Handle\n");
        return InvalidArgument;
    }

    spinlock_obj_t* obj = malloc(sizeof(spinlock_obj_t));

    if (obj == NULL) {
        fprintf(stderr, "Out of Memory\n");
        return OutOfMemory;
    }

    rc = spinlock_init(handle, obj, attrs);

    if (rc != Success) {
        fprintf(stderr, "Failure in create to create spinlock.\n");
        free(obj);
        return rc;
    }
}

rc_t spinlock_destroy(spinlock_t* handle) {
    if (handle == NULL)
        return InvalidArgument;

    free(handle->obj);

    return Success;
}

rc_t spinlock_acquire(spinlock_t* handle) {
    if (handle == NULL)
        return InvalidArgument;

    atomic_int expected = 0;

    while (!atomic_compare_exchange_strong(&handle->obj->lock, &expected, 1)) {
        expected = 0;
        usleep(handle->obj->sleep_usecs);
    }
    
    return Success;
}

rc_t spinlock_release(spinlock_t* handle) {
    atomic_int expected = 1;

    if (atomic_compare_exchange_strong(&handle->obj->lock, &expected, 0))
        return Success;

    fprintf(stderr, "Trying to release from thread that does not have lock acquired.\n");
    return InvalidOperation;
}

