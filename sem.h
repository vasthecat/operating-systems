#ifndef SEM_H
#define SEM_H

typedef struct
{
    pthread_cond_t sem_cond;
    pthread_mutex_t value_mutex;
    int value;
} sem_t;


void
sem_init(sem_t *sem, int pshared, int value)
{
    pthread_mutex_init(&sem->value_mutex, NULL);
    pthread_cond_init(&sem->sem_cond, NULL);
    sem->value = value;
}

void
sem_wait(sem_t *sem)
{
    pthread_mutex_lock(&sem->value_mutex);
    while (sem->value == 0)
        pthread_cond_wait(&sem->sem_cond, &sem->value_mutex);
    --sem->value;
    pthread_mutex_unlock(&sem->value_mutex);
}

void
sem_post(sem_t *sem)
{
    pthread_mutex_lock(&sem->value_mutex);
    ++sem->value;
    pthread_cond_signal(&sem->sem_cond);
    pthread_mutex_unlock(&sem->value_mutex);

}

void
sem_close(sem_t *sem)
{
    pthread_mutex_destroy(&sem->value_mutex);
    pthread_cond_destroy(&sem->sem_cond);
}

#endif // SEM_H
