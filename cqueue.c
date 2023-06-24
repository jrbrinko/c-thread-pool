#include "cqueue.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <linux/futex.h>      /* Definition of FUTEX_* constants */
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>
#include <sys/errno.h>
#include <stdbool.h>

#define DEFAULT_BLOCK_SIZE 128
#define DEFAULT_NUM_BLOCKS 32

typedef struct cqueue_item {
    uint32_t size;
    char data[];
} cqueue_item_t;

rc_t cqueue_attr_init(cqueue_attr_t* attrs) {
    rc_t rc;
    if (attrs == NULL) {
        fprintf(stderr, "On cqueue_attr_init the attrs cannot be NULL\n");
        return InvalidArgument;
    } 

    attrs->block_size = DEFAULT_BLOCK_SIZE;
    attrs->num_blocks = DEFAULT_NUM_BLOCKS;
    rc = spinlock_attr_init(&attrs->lock_attrs);
    if (rc != Success) {
        fprintf(stderr, "On cqueue_attr_init the lock attrs could not be initialized.\n");
        return InvalidArgument;
    }

    return Success;
}

rc_t cqueue_alloc_size(cqueue_attr_t* attrs, int* required_bytes) {

    if (attrs == NULL) {
        fprintf(stderr, "On cqueue_alloc_size the attrs cannot be NULL\n");
        return InvalidArgument;
    } 

    if (required_bytes == NULL) {
        fprintf(stderr, "On cqueue_alloc_size the required_bytes pointer cannot be NULL\n");
        return InvalidArgument;
    } 

    uint32_t sz = 0;
    sz += sizeof(cqueue_obj_t);
    sz += (attrs->block_size + sizeof(cqueue_item_t)) * attrs->num_blocks;

    *required_bytes = sz;

    return Success;
}

rc_t cqueue_init(cqueue_t* handle, cqueue_obj_t* obj, cqueue_attr_t* attrs) {
    cqueue_attr_t default_attrs; 
    rc_t rc;

    if (handle == NULL) {
        fprintf(stderr, "The queue pointer cannot be NULL.\n");
        return InvalidArgument;
    }

    if (obj == NULL) {
        fprintf(stderr, "The obj pointer cannot be NULL.\n");
        return InvalidArgument;
    }   

    if (attrs == NULL) {
        attrs = &default_attrs;
        rc = cqueue_attr_init(attrs);
        if (rc != Success) {
            fprintf(stderr, "Could not init default attrs\n");
            return rc;
        }
    }

    rc = spinlock_init(&handle->lock, &obj->lock_obj, &attrs->lock_attrs);
    if (rc != Success) {
        fprintf(stderr, "Could not init the queue lock.\n");
        return rc;        
    }

    handle->obj = obj;
    obj->block_size = attrs->block_size;
    obj->num_blocks = attrs->num_blocks;
    obj->head = 0;
    obj->tail = 0;
    obj->available_msgs = 0;
    obj->free_blocks = obj->num_blocks;

    return Success;
}


rc_t cqueue_create(cqueue_t* handle, cqueue_attr_t* attrs) {
    cqueue_attr_t default_attrs; 
    rc_t rc;

    if (handle == NULL) {
        fprintf(stderr, "The handle cannot be NULL.\n");
        return InvalidArgument;
    }  

    if (attrs == NULL) {
        attrs = &default_attrs;
        rc = cqueue_attr_init(attrs);
        if (rc != Success) {
            fprintf(stderr, "Could not init default attrs\n");
            return rc;
        }
    }

    uint32_t sz;
    rc = cqueue_alloc_size(attrs, &sz);
    if (rc != Success) {
        fprintf(stderr, "Could not get the cqueue allocation size\n");
        return rc;
    }

    void* obj = malloc(sz);

    rc = cqueue_init(handle, obj, attrs);
    if (rc != Success) {
        fprintf(stderr, "Could not init the cqueue in create.\n");
        return rc;        
    }

    return Success;
}

rc_t cqueue_destroy(cqueue_t* handle) {
    if (handle == NULL) {
       fprintf(stderr, "On destroy the handle cannot be NULL\n");
       return InvalidArgument;
    }    

    if (handle->obj == NULL) {
       fprintf(stderr, "The handle was empty. Could not destroy it.\n");
       return InvalidArgument;        
    }

    free(handle->obj);

    return Success;
}

rc_t cqueue_enqueue(cqueue_t* handle, void* item, uint32_t size, timespec_t* timeout) {
    rc_t rc;

    if (handle == NULL) {
        fprintf(stderr, "The handle cannot be NULL\n");
        return InvalidArgument;
    } 

    if (item == NULL) {
        fprintf(stderr, "The item cannot be NULL\n");
        return InvalidArgument;
    } 

    if (size == 0) {
        fprintf(stderr, "The byte_size cannot be zero\n");
        return InvalidArgument;
    } 

    if (size > handle->obj->block_size) {
        fprintf(stderr, "The item cannot fit in the queue\n");
        return InvalidArgument;
    }

    rc = spinlock_acquire(&handle->lock);
    if (rc != Success) {
        fprintf(stderr, "The spin lock was not acquired\n");
        return rc;
    }

    while (handle->obj->free_blocks == 0) {
        spinlock_release(&handle->lock);
        long frc = syscall(SYS_futex, &handle->obj->free_blocks, FUTEX_WAIT, 0, timeout, NULL, NULL);
        if (frc == ETIMEDOUT) 
            return Timeout;
        
        rc = spinlock_acquire(&handle->lock);
        if (rc !=Success) {
            fprintf(stderr, "The spin lock was not acquired\n");
            return rc;  
        }
    }

    void* dest = &handle->obj->data;
    dest += handle->obj->head * (handle->obj->block_size + sizeof(cqueue_item_t));

    cqueue_item_t* item_ptr = dest;
    item_ptr->size = size;
    memcpy(&item_ptr->data, item, size);
    handle->obj->head = (handle->obj->head + 1) % handle->obj->num_blocks;
    handle->obj->available_msgs += 1;
    handle->obj->free_blocks -= 1;

    rc = spinlock_release(&handle->lock);
    if (rc != Success) {
        fprintf(stderr, "The spin lock was not released\n");
        return rc;
    }   

    syscall(SYS_futex, &handle->obj->available_msgs, FUTEX_WAKE, 1, NULL, NULL, NULL);

    return Success;
}

rc_t cqueue_dequeue(cqueue_t* handle, uint32_t max_size, void** item, uint32_t* size, timespec_t* timeout) {
    rc_t rc;

    if (handle == NULL) {
        fprintf(stderr, "The handle cannot be NULL\n");
        return InvalidArgument;
    } 

    if (item == NULL) {
        fprintf(stderr, "The item cannot be NULL\n");
        return InvalidArgument;
    } 

    if (max_size == 0) {
        fprintf(stderr, "The max_bytes cannot be zero\n");
        return InvalidArgument;
    } 

    if (size == NULL) {
        fprintf(stderr, "The item size cannot be NULL\n");
        return InvalidArgument;
    }

    rc = spinlock_acquire(&handle->lock);
    if (rc != Success) {
        fprintf(stderr, "The spin lock was not acquired\n");
        return rc;
    }

    while (handle->obj->available_msgs == 0) {
        spinlock_release(&handle->lock);
        long frc = syscall(SYS_futex, &handle->obj->available_msgs, FUTEX_WAIT, 0, timeout, NULL, NULL);
        if (frc == ETIMEDOUT) 
            return Timeout;
        
        rc = spinlock_acquire(&handle->lock);
        if (rc !=Success) {
            fprintf(stderr, "The spin lock was not acquired\n");
            return rc;  
        }
    }

    void* src = &handle->obj->data;
    src += handle->obj->tail * (handle->obj->block_size + sizeof(cqueue_item_t));

    cqueue_item_t* item_ptr = src;
    if (item_ptr->size > max_size) {
        spinlock_release(&handle->lock);
        fprintf(stderr, "The item size cannot be NULL\n");
        return InvalidArgument;  
    }

    void* data = malloc(item_ptr->size);
    if (data == NULL) {
        fprintf(stderr, "Out of Memory.\n");
        spinlock_release(&handle->lock);
        return OutOfMemory;         
    }

    *size = item_ptr->size;
    memcpy(data, &item_ptr->data, *size);
    handle->obj->tail = (handle->obj->tail + 1) % handle->obj->num_blocks;
    handle->obj->available_msgs -= 1;
    handle->obj->free_blocks += 1;
    *item = data;

    rc = spinlock_release(&handle->lock);
    if (rc != Success) {
        fprintf(stderr, "The spin lock was not released\n");
        return rc;
    } 

    syscall(SYS_futex, &handle->obj->free_blocks, FUTEX_WAKE, 1, NULL, NULL, NULL);
 
    return Success;
}

rc_t cqueue_size(cqueue_t* handle, uint32_t* size) {
    rc_t rc;

    if (handle == NULL) {
        fprintf(stderr, "The handle cannot be NULL\n");
        return InvalidArgument;
    } 

    if (size == NULL) {
        fprintf(stderr, "The size cannot be NULL\n");
        return InvalidArgument;
    } 

    rc = spinlock_acquire(&handle->lock);
    if (rc != Success) {
        fprintf(stderr, "The spin lock was not acquired\n");
        return rc;
    }

    *size = handle->obj->available_msgs;

    rc = spinlock_release(&handle->lock);
    if (rc != Success) {
        fprintf(stderr, "The spin lock was not released\n");
        return rc;
    } 
        
    return Success;
}
