#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

#ifdef __APPLE__
#include "sem.h"
#else
#include <semaphore.h>
#endif

#include <pthread.h>

struct queue_t
{
    struct task_t tasks[8];
    int size, capacity;
    int head, tail;
    pthread_mutex_t head_mut, tail_mut;
    sem_t count, available;
};

void
queue_init(struct queue_t *queue);

void
queue_destroy(struct queue_t *queue);

void
queue_push(struct queue_t *queue, struct task_t *task);

void
queue_pop(struct queue_t *queue, struct task_t *task);

#endif // QUEUE_H
