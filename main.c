#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <crypt.h>
#ifdef __APPLE__
#include "sem.h"
#else
#include <semaphore.h>
#endif

#define PASSWORD_SIZE 20

enum brute_mode_t
{
    M_RECURSIVE,
    M_ITERATIVE
};

enum run_mode_t
{
    M_SINGLE,
    M_MULTI,
};

struct config_t
{
    char *alphabet;
    int length;
    enum brute_mode_t brute_mode;
    enum run_mode_t run_mode;
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
    sem_t count, available;
    volatile int tasks_running;
    pthread_mutex_t tasks_mutex;
    pthread_cond_t tasks_cond;
};

struct context_t
{
    struct queue_t queue;
    char *hash;
};

typedef bool (*password_handler_t)(struct context_t *, struct task_t *);

void
queue_init(struct queue_t *queue)
{
    queue->size = 0;
    queue->capacity = 8;
    queue->head = queue->tail = 0;

    sem_init(&queue->count, 0, 0);
    sem_init(&queue->available, 0, queue->capacity);

    pthread_mutex_init(&queue->head_mut, NULL);
    pthread_mutex_init(&queue->tail_mut, NULL);

    queue->tasks_running = 0;
    pthread_mutex_init(&queue->tasks_mutex, NULL);
    pthread_cond_init(&queue->tasks_cond, NULL);
}

void
queue_destroy(struct queue_t *queue)
{
    sem_close(&queue->count);
    sem_close(&queue->available);

    pthread_mutex_destroy(&queue->tasks_mutex);
    pthread_cond_destroy(&queue->tasks_cond);
}

void
queue_push(struct queue_t *queue, struct task_t *task)
{
    sem_wait(&queue->available);

    pthread_mutex_lock(&queue->tail_mut);

    pthread_mutex_lock(&queue->tasks_mutex);
    ++queue->tasks_running;
    pthread_mutex_unlock(&queue->tasks_mutex);

    if (queue->tail + 1 >= queue->capacity)
        queue->tail = 0;
    else
        ++queue->tail;

    queue->tasks[queue->tail] = *task;
    /* printf("Enqued '%s'\n", task->password); */
    pthread_mutex_unlock(&queue->tail_mut);
  
    sem_post(&queue->count);
}

void
queue_pop(struct queue_t *queue, struct task_t *task)
{
    sem_wait(&queue->count);

    pthread_mutex_lock(&queue->head_mut);
    if (queue->head + 1 >= queue->capacity)
        queue->head = 0;
    else
        ++queue->head;

    *task = queue->tasks[queue->head];
    /* printf("Dequed '%s'\n", task->password); */
    pthread_mutex_unlock(&queue->head_mut);

    sem_post(&queue->available);
}

void *
check_password_multi(void *arg)
{
    struct context_t *context = (struct context_t *) arg;
  
    struct crypt_data data;
    while (true)
    {
        struct task_t task;
        queue_pop(&context->queue, &task);

        data.initialized = 0;
        char *hashed = crypt_r(task.password, context->hash, &data);
        if (strcmp(hashed, context->hash) == 0)
        {
            printf("Password found: '%s'\n", task.password);
        }

        pthread_mutex_lock(&context->queue.tasks_mutex);
        --context->queue.tasks_running;
        pthread_mutex_unlock(&context->queue.tasks_mutex);

        if (context->queue.tasks_running == 0)
            pthread_cond_signal(&context->queue.tasks_cond);
    }
    return NULL;
}

bool
check_password_single(struct task_t task, char *hash)
{
    struct crypt_data data;
    data.initialized = 0; // ???
    char *hashed = crypt_r(task.password, hash, &data);
    if (strcmp(hashed, hash) == 0)
    {
        printf("Password found: '%s'\n", task.password);
        return true;
    }
    return false;
}

bool
mt_password_handler(struct context_t *context, struct task_t *task)
{
    queue_push(&context->queue, task);
    return false;
}

bool
st_password_handler(struct context_t *context, struct task_t *task)
{
    return check_password_single(*task, context->hash);
}

void
bruteforce_rec(char *password, struct config_t *config, int pos, 
               struct context_t *context,
               password_handler_t handler)
{
    if (config->length == pos)
    {
        struct task_t task;
        strcpy(task.password, password);
        if (handler(context, &task)) return;
    }
    else
    {
        for (int i = 0; config->alphabet[i] != '\0'; ++i)
        {
            password[pos] = config->alphabet[i];
            bruteforce_rec(password, config, pos + 1, context, handler);
        }
    }
} 

void
bruteforce_iter(struct config_t *config,
                struct context_t *context,
                password_handler_t handler)
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

        if (handler(context, &task)) return;
    
        for (k = config->length - 1; (k >= 0) && (a[k] == size); --k)
            a[k] = 0;
        if (k < 0) break;
        a[k]++;
    }
}

void
singlethreaded(struct config_t config)
{
    struct context_t context;
    context.hash = config.hash;

    char password[config.length + 1];
    password[config.length] = '\0';
    switch (config.brute_mode)
    {
    case M_ITERATIVE:
        bruteforce_iter(&config, &context, st_password_handler);
        break;
    case M_RECURSIVE:
        bruteforce_rec(password, &config, 0, &context, mt_password_handler);
        break;
    }
}

void
multithreaded(struct config_t config)
{
    struct context_t context;
    context.hash = config.hash;
    queue_init(&context.queue);

    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    pthread_t threads[cpu_count];
    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_create(&threads[i], NULL, check_password_multi, (void *) config.hash);
    }

    char password[config.length + 1];
    password[config.length] = '\0';
    switch (config.brute_mode)
    {
    case M_ITERATIVE:
        bruteforce_iter(&config, &context, st_password_handler);
        break;
    case M_RECURSIVE:
        bruteforce_rec(password, &config, 0, &context, mt_password_handler);
        break;
    }

    pthread_mutex_lock(&context.queue.tasks_mutex);
    while (context.queue.tasks_running != 0)
        pthread_cond_wait(&context.queue.tasks_cond, &context.queue.tasks_mutex);
    pthread_mutex_unlock(&context.queue.tasks_mutex);

    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_cancel(threads[i]);
        pthread_join(threads[i], NULL);
    }

    queue_destroy(&context.queue);
}

void
parse_opts(struct config_t *config, int argc, char *argv[])
{
    int opt;
    opterr = 1;
    while ((opt = getopt(argc, argv, "irmsa:l:h:")) != -1)
    {
        switch (opt)
        {
        case 'i':
            config->brute_mode = M_ITERATIVE;
            break;
        case 'r':
            config->brute_mode = M_RECURSIVE;
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
        case 's':
            config->run_mode = M_SINGLE;
            break;
        case 'm':
            config->run_mode = M_MULTI;
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
        .alphabet = "abc",
        .length = 3,
        .brute_mode = M_ITERATIVE,
        .run_mode = M_SINGLE,
        .hash = "hiwMxUWeODzGE", // hi + ccc
    };
    parse_opts(&config, argc, argv);

    switch (config.run_mode)
    {
    case M_SINGLE:
        singlethreaded(config);
        break;
    case M_MULTI:
        multithreaded(config);
        break;
    }

    return 0;
}
