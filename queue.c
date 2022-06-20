#include "queue.h"

void
queue_init(struct queue_t *queue)
{
    queue->size = 0;
    queue->capacity = sizeof(queue->tasks) / sizeof(queue->tasks[0]);
    queue->head = queue->tail = 0;

    sem_init(&queue->count, 0, 0);
    sem_init(&queue->available, 0, queue->capacity);

    pthread_mutex_init(&queue->head_mut, NULL);
    pthread_mutex_init(&queue->tail_mut, NULL);
}

void
queue_destroy(struct queue_t *queue)
{
    sem_close(&queue->count);
    sem_close(&queue->available);
    pthread_mutex_destroy(&queue->head_mut);
    pthread_mutex_destroy(&queue->tail_mut);
}

void
queue_push(struct queue_t *queue, struct task_t *task)
{
    sem_wait(&queue->available);

    pthread_mutex_lock(&queue->tail_mut);
    ++queue->tail;
    if (queue->tail >= queue->capacity)
        queue->tail = 0;

    ++queue->size;
    queue->tasks[queue->tail] = *task;
    pthread_mutex_unlock(&queue->tail_mut);
  
    sem_post(&queue->count);
}

void
queue_pop(struct queue_t *queue, struct task_t *task)
{
    sem_wait(&queue->count);

    pthread_mutex_lock(&queue->head_mut);
    ++queue->head;
    if (queue->head >= queue->capacity)
        queue->head = 0;

    --queue->size;
    *task = queue->tasks[queue->head];
    pthread_mutex_unlock(&queue->head_mut);

    sem_post(&queue->available);
}
