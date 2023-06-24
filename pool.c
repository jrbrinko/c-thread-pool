#include "pool.h"
#include "rc.h"
#include "cqueue.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct thread_pool_args_st {
    cqueue_t* work_queue;
    cqueue_t* results_queue;
} thread_pool_args_t;


/**
 * @brief: The function for the pool thread.
 * 
 * The pool thread takes two queues: a work queue and a result queue. From that, it dequeues work requests from the work queue and does the function for the particular argument. Then, it puts the result in the result queue. Stops if there is an error (rc_t) or if there is a work_request with a null function pointer.
 * @param: arg the arguments (type thread_pool_args_st) which contains the queues.
 * @return: the rc_t value (Success, OutOfMemory etc.)
*/

void* pool_thread(void* arg) {

    thread_pool_args_t* queues = (thread_pool_args_t*) arg;

    cqueue_t* work_queue = queues->work_queue;
    cqueue_t* results_queue = queues->results_queue;

    rc_t rc;
    uint32_t size;     

    bool loop = true;
    while(loop) {

        pool_work_t* work_request;
        uint32_t size;

        // get work request
        rc = cqueue_dequeue(work_queue, sizeof(pool_work_t), (void**)&work_request, &size, NULL);
        if (rc != Success) {
            fprintf(stderr, "There was an error dequeueing, error value was %d\n", rc);
            return (rc_t*) rc;
        }

        // If function pointer is null EXIT
        if(work_request->function_ptr == NULL) {
            loop = false;
            return (rc_t*) rc;
        } else {
            // Call function on argument (get return code and result)
            void* fun_result;
            rc = work_request->function_ptr(work_request->arg, &fun_result);
            if (rc != Success) {
                fprintf(stderr, "There was an error with the user function, error value was %d\n", rc);
                return (rc_t*) rc;
            }

            // Make a pool result and enqueue on result queue
            pool_result_t* result_request = malloc(sizeof(pool_result_t));

            result_request->id = work_request->id;
            result_request->rc = Success;
            result_request->result = fun_result;

            rc = cqueue_enqueue(results_queue, result_request, sizeof(pool_result_t), NULL);
            if(rc != Success) {
                fprintf(stderr, "There was an error enqueueing to result queue, error value was %d\n", rc);
                return (rc_t*) rc;
            }

        }

    }

    return (rc_t*) rc;

}

/**
 * @brief: Creates the pool and its threads.
 * 
 * 
 * Creates the pool. Initalizes the work request queue and result queue. Also, creates the threads based on the given pool_size. 
 * 
 * @param: pool -- the pointer to the pool object declared outside the funciton.
 * @param: pool_size -- the size of the pool (or number of threads).
 * @return: the rc_t value (Success, OutOfMemory etc.)
*/
rc_t pool_create(thread_pool_t* pool, int pool_size) {

    rc_t rc;
    
    cqueue_attr_t work_attrs;
    work_attrs.block_size = sizeof(pool_work_t);
    cqueue_attr_t result_attrs;
    result_attrs.block_size = sizeof(pool_result_t);

    rc = cqueue_attr_init(&work_attrs);
    if (rc != Success) {
        fprintf(stderr, "Error calling attr init.\n");
        return -1;
    }
    rc = cqueue_create(&pool->work_queue, &work_attrs);
    if (rc != Success) {
        fprintf(stderr, "Error calling cqueue create.\n");
        return rc;
    }

    // Creating result queue
    rc = cqueue_attr_init(&result_attrs);
    if (rc != Success) {
        fprintf(stderr, "Error calling attr init.\n");
        return rc;
    }
    rc = cqueue_create(&pool->results_queue, &result_attrs);
    if (rc != Success) {
        fprintf(stderr, "Error calling cqueue create.\n");
        return rc;
    }

    if (pool_size <= 0) {
        fprintf(stderr, "Error: pool_size cannot be less than 0.");
        return InvalidArgument;
    }

    pool->threads = malloc(sizeof(pthread_t) * pool_size);
    pool->size = pool_size;

    thread_pool_args_t* thread_args = malloc(sizeof(thread_pool_args_t));
    thread_args->work_queue = &pool->work_queue;
    thread_args->results_queue = &pool->results_queue;

    for (int i = 0; i < pool_size; i++) {

        rc = pthread_create(&pool->threads[i], NULL, pool_thread, thread_args);
        if (rc != 0) {
            fprintf(stderr, "There was a problem during creation for pthread with error=%d\n", rc);
            return rc;
        }
    }

    return Success;

}

/**
 * @brief: Ends threads and frees pool's memory.
 * 
 * This function ends the threads by sending blank work_requests. Then, it joins the threads together. Finally, it frees the memory stored by the threads.
 * 
 * @param: pool -- the pointer to the pool object declared outside the funciton. 
 * @return: the rc_t value (Success, OutOfMemory etc.) 
*/
rc_t pool_destroy(thread_pool_t* pool) {

    rc_t rc;

    if (pool == NULL) {
        fprintf(stderr, "On destroy the pool cannot be NULL.\n");
        return InvalidArgument;
    }

    if (pool->threads == NULL) {
        fprintf(stderr, "The pool's thread was empty cannot destroy it.\n");
        return InvalidArgument;
    }

    // Turning off threads
    for(int i = 0; i < pool->size; i++) {

        pool_work_t sentinel_wr;
        sentinel_wr.function_ptr = NULL;
        rc = cqueue_enqueue(&pool->work_queue, &sentinel_wr, sizeof(pool_work_t), NULL);
        if (rc != Success) {
            fprintf(stderr, "Error calling cqueue enqueue.\n");
            return rc;
        }
    }

    // Join threads
    for(int i = 0; i < pool->size; i++) {

        int prc = pthread_join(pool->threads[i], NULL);
        if (prc != 0) {
            printf("There was a problem during pthread join with error=%d\n", prc);
            return -1;  
        }

    }

    free(pool->threads);

    return Success;
}


typedef struct result_thread_args_st{
    cqueue_t* results_queue;
    int arg_count;
    void** results;
} result_thread_args_t;

/**
 * @brief: Dequeues work requests.
 * 
 * This function is a thread function that dequeues work requests and puts them into the results that are accessible outside this function.
 * 
 * @param: arg the arguments for thread (type result_thread_args_t)
 * @return: the rc_t value (Success, OutOfMemory etc.) 
 * 
*/
void* result_thread(void* arg){

    result_thread_args_t* queue = (result_thread_args_t*) arg;
    int arg_count = queue->arg_count;
    cqueue_t* results_queue = queue->results_queue;

    rc_t* rc = malloc(sizeof(rc_t));
    int args_dequeued = 0;
    uint32_t size;
    while(args_dequeued < arg_count) {

        pool_result_t* result_request;
        cqueue_size(results_queue, &size);

        if(size != 0) {
            *rc = cqueue_dequeue(results_queue, sizeof(pool_result_t), (void**)&result_request, &size, NULL);

            if (*rc != Success) {
                fprintf(stderr, "Error calling cqueue enqueue.\n");
                return rc;
            }

            queue->results[result_request->id] = result_request->result;
            args_dequeued++;
        }

    }

    free(rc);
}

/**
 * @brief: Maps the threads from the given argument and function.
 * 
 * From a given array of arguments and function, this function computes the user function on all of the arguments and puts it in the results parameter. This function also puts all of the work requests onto the queue.
 * 
 * @param: pool -- thread pool object.
 * @param: fun -- the user function.
 * @param: arg_count -- number of arguments.
 * @param: args -- the given arguments
 * @results: results -- the results from applyting the user function to the argumnents.
 * 
 * @return: the rc_t value (Success, OutOfMemory etc.) 

*/
rc_t pool_map(thread_pool_t* pool, pool_fun_t fun, int arg_count, void* args[], void* results[]) {

    rc_t rc;
    // Make a work request of each arg (the id should be idx of array)
    pool_work_t work_request[arg_count];
    int total_work_requests = 0;

    for (int i = 0; i < arg_count; i++) {
        work_request[i].id = i;
        work_request[i].arg = args[i];
        work_request[i].function_ptr = fun;
        total_work_requests++;
    }

    // Dequeuing results
    pthread_t result_request_thread;
    result_thread_args_t result_thread_args;
    result_thread_args.arg_count = arg_count;
    result_thread_args.results_queue = &pool->results_queue;
    result_thread_args.results = results;

    rc = pthread_create(&result_request_thread, NULL, result_thread, (void*) &result_thread_args);

    // Enqueue work requests to work queue
    for(int i = 0; i < total_work_requests; i++) {
        rc = cqueue_enqueue(&pool->work_queue, &work_request[i], sizeof(pool_work_t), NULL);

        if (rc != Success) {
            fprintf(stderr, "Error calling cqueue enqueue.\n");
            return rc;
        }
    }

    // Result Queue Threads
    int prc = pthread_join(result_request_thread, NULL);
    if (prc != 0) {
        printf("There was a problem during pthread join with error=%d\n", prc);
        return -1;  
    }

    uint32_t size;
    rc = cqueue_size(&pool->results_queue, &size);
    if (rc != Success) {
        fprintf(stderr, "Error calling cqueue size.\n");
        pthread_exit(NULL);
    } 

    return Success;

}