#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

#include "sem.h"
#include <pthread.h>

struct queue_t
{
    struct task_t tasks[256];
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
