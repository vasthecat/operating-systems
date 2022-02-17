#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <crypt.h>
#include <fcntl.h> // O_* constants for named semaphores
#include <semaphore.h>

#define PASSWORD_SIZE 20

enum brute_mode_t
{
    M_RECURSIVE,
    M_ITERATIVE
};

struct config_t
{
    char *alphabet;
    int length;
    enum brute_mode_t mode;
    char *hash;
};

struct task_t
{
    char password[PASSWORD_SIZE];
};

struct queue_t
{
    struct task_t tasks[8];
    int size, capacity;
    int head, tail;
    pthread_mutex_t head_mut, tail_mut;
    sem_t *count, *available;
};
struct queue_t queue;
volatile int tasks = 0;
pthread_mutex_t tasks_mutex;
pthread_cond_t tasks_cond;

void
queue_init(struct queue_t *queue)
{
    queue->size = 0;
    queue->capacity = 8;
    queue->head = queue->tail = 0;

    // MacOS doesn't support unnamed semaphores
    if ((queue->count = sem_open("/brute-sem_count", O_CREAT, 0644, 0)) == SEM_FAILED)
    {
        printf("Could not initialize 'count' semaphore\n");
        exit(EXIT_FAILURE);
    }
    sem_unlink("/brute-sem_count");
    if ((queue->available = sem_open("/brute-sem_available", O_CREAT, 0644, queue->capacity)) == SEM_FAILED)
    {
        sem_close(queue->count);
        printf("Could not initialize 'available' semaphore\n");
        exit(EXIT_FAILURE);
    }
    sem_unlink("/brute-sem_available");

    pthread_mutex_init(&queue->head_mut, NULL);
    pthread_mutex_init(&queue->tail_mut, NULL);
}

void
queue_destroy(struct queue_t *queue)
{
    sem_close(queue->count);
    sem_close(queue->available);
}

void
queue_push(struct queue_t *queue, struct task_t *task)
{
    sem_wait(queue->available);

    pthread_mutex_lock(&queue->tail_mut);

    pthread_mutex_lock(&tasks_mutex);
    ++tasks;
    pthread_mutex_unlock(&tasks_mutex);

    if (queue->tail + 1 >= queue->capacity)
        queue->tail = 0;
    else
        ++queue->tail;

    queue->tasks[queue->tail] = *task;
    printf("Enqued '%s'\n", task->password);
    pthread_mutex_unlock(&queue->tail_mut);
  
    sem_post(queue->count);
}

void
queue_pop(struct queue_t *queue, struct task_t *task)
{
    sem_wait(queue->count);

    pthread_mutex_lock(&tasks_mutex);
    --tasks;
    pthread_mutex_unlock(&tasks_mutex);

    pthread_mutex_lock(&queue->head_mut);
    if (queue->head + 1 >= queue->capacity)
        queue->head = 0;
    else
        ++queue->head;

    *task = queue->tasks[queue->head];
    printf("Dequed '%s'\n", task->password);
    pthread_mutex_unlock(&queue->head_mut);

    sem_post(queue->available);
}

void *
check_password(void *arg)
{
    char *hash = (char *) arg;
  
    while (true)
    {
        struct task_t task;
        queue_pop(&queue, &task);

        struct crypt_data data;
        char *hashed = crypt_r(task.password, hash, &data);
        if (strcmp(hashed, hash) == 0)
        {
            printf("Password found: '%s'\n", task.password);
        }
        if (tasks == 0)
            pthread_cond_signal(&tasks_cond);
    }
    return NULL;
}

void
bruteforce_rec(char *password, struct config_t *config, int pos)
{
    if (config->length == pos)
    {
        struct task_t task;
        memset(task.password, 0, PASSWORD_SIZE);
        strcpy(task.password, password);
        queue_push(&queue, &task);
    }
    else
    {
        for (int i = 0; config->alphabet[i] != '\0'; ++i)
        {
            password[pos] = config->alphabet[i];
            bruteforce_rec(password, config, pos + 1);
        }
    }
} 

void
bruteforce_iter(struct config_t *config)
{
    size_t size = strlen(config->alphabet) - 1;
    int a[config->length];
    memset(a, 0, config->length * sizeof(int));

    while (true)
    {
        int k;
        struct task_t task;
        memset(task.password, 0, PASSWORD_SIZE);
        for (k = 0; k < config->length; ++k)
            task.password[k] = config->alphabet[a[k]];

        queue_push(&queue, &task);
    
        for (k = config->length - 1; (k >= 0) && (a[k] == size); --k)
            a[k] = 0;
        if (k < 0) break;
        a[k]++;
    }
}

void
parse_opts(struct config_t *config, int argc, char *argv[])
{
    int opt;
    opterr = 1;
    while ((opt = getopt(argc, argv, "ira:l:h:")) != -1)
    {
        switch (opt)
        {
        case 'i':
            config->mode = M_ITERATIVE;
            break;
        case 'r':
            config->mode = M_RECURSIVE;
            break;
        case 'a':
            config->alphabet = optarg;
            break;
        case 'l':
            config->length = atoi(optarg);
            break;
        case 'h':
            config->hash = optarg;
            break;
        default:
            exit(1);
            break;
        }
    }
}

int
main(int argc, char *argv[])
{
    struct config_t config = {
        .alphabet = "abcd",
        .length = 3,
        .mode = M_ITERATIVE,
        .hash = "hiN3t5mIZ/ytk", // hi + abcd
    };
    parse_opts(&config, argc, argv);
    queue_init(&queue);

    pthread_mutex_init(&tasks_mutex, NULL);
    pthread_cond_init(&tasks_cond, NULL);


    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    pthread_t threads[cpu_count];
    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_create(&threads[i], NULL, check_password, (void *) config.hash);
    }

    char password[config.length + 1];
    password[config.length] = '\0';
    switch (config.mode)
    {
    case M_ITERATIVE:
        bruteforce_iter(&config);
        break;
    case M_RECURSIVE:
        bruteforce_rec(password, &config, 0);
        break;
    }

    pthread_mutex_lock(&tasks_mutex);
    while (tasks != 0)
        pthread_cond_wait(&tasks_cond, &tasks_mutex);
    pthread_mutex_unlock(&tasks_mutex);

    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_cancel(threads[i]);
        pthread_join(threads[i], NULL);
    }

    queue_destroy(&queue);

    return 0;
}
