#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "threadpool.h"

// Global thread pool
threadpool* thread_pool = NULL;

//  Private implementation of queue //------------------------------------------------------------------//
void enqueue(work_t** qhead, work_t** qtail, int (*routine)(void*), void* arg) {
    work_t* new_work = (work_t*)malloc(sizeof(work_t));
    if (new_work == NULL) {
        perror("error: malloc\n");
        exit(EXIT_FAILURE);
    }
    new_work->routine = routine;
    new_work->arg = arg;
    new_work->next = NULL;

    if (*qhead == NULL) {
        *qhead = *qtail = new_work;
    } else {
        (*qtail)->next = new_work;
        *qtail = new_work;
    }
}

work_t* dequeue(work_t** qhead, work_t** qtail) {
    if (*qhead == NULL)
        return NULL;

    work_t* front_work = *qhead;
    *qhead = (*qhead)->next;
    if (*qhead == NULL) {
        *qtail = NULL; // Update qtail when the queue becomes empty
    }
    return front_work;
}

void queue_free(work_t** qhead, work_t** qtail) {
    if (*qhead == NULL)
        return;

    while (*qhead != NULL) {
        work_t* temp = *qhead;
        *qhead = (*qhead)->next;
        free(temp);
    }
    *qtail = NULL; // Update qtail to indicate an empty queue
}
// --------------------------------------------------------------------------------------//

threadpool* create_threadpool(int num_threads_in_pool) {

    // check correct size:
    if (num_threads_in_pool > MAXT_IN_POOL)
        return NULL;

    // Create the pool
    thread_pool = malloc(sizeof(threadpool));

    // Check for correct allocation.
    if (thread_pool == NULL)
        return NULL;

    // set the number of threads in the pool
    thread_pool->num_threads = num_threads_in_pool;

    // size of queue is zero since i we dont have tasks yet,
    thread_pool->qsize = 0;

    // Create array of threads
    thread_pool->threads = malloc(sizeof(pthread_t*) * num_threads_in_pool);
    // initilize threads here:

    // Check for correct allocation.
    if (thread_pool->threads == NULL) {
        free(thread_pool);
        return NULL;
    }

    // Initialize the Queue;
    thread_pool->qhead = NULL;
    thread_pool->qtail = NULL;

    // Initialize the mutex lock and conditionals
    pthread_mutex_init(&(thread_pool->qlock), NULL);
    pthread_cond_init(&(thread_pool->q_not_empty), NULL);
    pthread_cond_init(&(thread_pool->q_empty), NULL);

    // Initialize destruction flags
    thread_pool->dont_accept = 0;
    thread_pool->shutdown = 0;

    // Initialize threads
    for (int i = 0; i < num_threads_in_pool; i++) {
        if (pthread_create(&(thread_pool->threads[i]), NULL, do_work, NULL) != 0) {
            perror("error: pthread_create");
            // Clean up resources and return NULL if thread creation fails
            destroy_threadpool(thread_pool);
            return NULL;
        }
    }

    return thread_pool;
}

// Function to safely destroy a threadpool
void destroy_threadpool(threadpool* destroyme) {
    // Lock the queue
    pthread_mutex_lock(&destroyme->qlock);
    // Set flag to stop accepting new tasks
    destroyme->dont_accept = 1;

    // Wait until the queue is empty
    while (destroyme->qsize > 0)
        pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock);

    // Signal shutdown
    destroyme->shutdown = 1;
    pthread_cond_broadcast(&destroyme->q_not_empty);

    // Unlock the queue
    pthread_mutex_unlock(&destroyme->qlock);

    // Join all threads
    for (int i = 0; i < destroyme->num_threads; i++)
        pthread_join(destroyme->threads[i], NULL);

    // Destroy mutex and condition variables
    pthread_mutex_destroy(&(destroyme->qlock));
    pthread_cond_destroy(&(destroyme->q_not_empty));
    pthread_cond_destroy(&(destroyme->q_empty));

    // Free memory
    free(destroyme->threads);
    queue_free(&destroyme->qhead, &destroyme->qtail);
    free(destroyme);
}


void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg) {
    // critical section properties:
    // reading dont_accept flag
    // adding a job to the queue
    // setting qsize
    pthread_mutex_lock(&from_me->qlock);

    // if we want to start the destruction process
    // then set dont_accept flag
    if (from_me->dont_accept == 1) {
        pthread_mutex_unlock(&from_me->qlock);
        return;
    }

    // create new job and append it to the queue
    enqueue(&from_me->qhead,&from_me->qtail,dispatch_to_here,arg);

    from_me->qsize++;

    pthread_cond_signal(&from_me->q_not_empty);

    pthread_mutex_unlock(&from_me->qlock);
    // end of critical section
}

// Function to execute tasks in the threadpool
void* do_work(void* p) {
    (void) p;
    while (1) {
        pthread_mutex_lock(&thread_pool->qlock);

        // Wait until there is work to do or shutdown is initiated
        while (thread_pool->qsize == 0 && !thread_pool->shutdown)
            pthread_cond_wait(&thread_pool->q_not_empty, &thread_pool->qlock);

        // Exit if shutdown is initiated or no more tasks are accepted
        if (thread_pool->shutdown || (thread_pool->qsize == 0 && thread_pool->dont_accept)) {
            pthread_mutex_unlock(&thread_pool->qlock);
            pthread_exit(NULL);
        }

        // Dequeue and process the task
        work_t* work = dequeue(&thread_pool->qhead, &thread_pool->qtail);
        if (work == NULL) {
            pthread_mutex_unlock(&thread_pool->qlock);
            continue;
        }
        thread_pool->qsize--;

        // Signal if the queue is empty and no more tasks are accepted
        if (thread_pool->qsize == 0 && thread_pool->dont_accept)
            pthread_cond_signal(&thread_pool->q_empty);

        pthread_mutex_unlock(&thread_pool->qlock);

        // Execute the task routine and free the work
        work->routine(work->arg);
        free(work);
    }
}


