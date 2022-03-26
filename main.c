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
typedef char password_t[PASSWORD_SIZE];

struct task_t
{
    password_t password;
};

struct queue_t
{
    struct task_t tasks[8];
    int size, capacity;
    int head, tail;
    pthread_mutex_t head_mut, tail_mut;
    sem_t count, available;
};

void
queue_init(struct queue_t *queue)
{
    queue->size = 0;
    queue->capacity = sizeof(queue->tasks) / sizeof(struct task_t);
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
    ++queue->head;
    if (queue->head >= queue->capacity)
        queue->head = 0;

    *task = queue->tasks[queue->head];
    /* printf("dequed '%s'\n", task->password); */
    pthread_mutex_unlock(&queue->head_mut);

    sem_post(&queue->available);
}

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

struct st_context_t
{
    char *hash;
    struct crypt_data cd;
};

struct mt_context_t
{
    volatile int tasks_running;
    pthread_mutex_t tasks_mutex;
    pthread_cond_t tasks_cond;

    struct queue_t queue;
    password_t password;
    char *hash;
};

typedef bool (*password_handler_t)(void *, struct task_t *);

bool
bruteforce_rec(struct task_t *task,
               struct config_t *config,
               int pos,
               void *context,
               password_handler_t handler)
{
    if (config->length == pos)
    {
        if (handler(context, task)) return true;
    }
    else
    {
        for (int i = 0; config->alphabet[i] != '\0'; ++i)
        {
            task->password[pos] = config->alphabet[i];
            if (bruteforce_rec(task, config, pos + 1, context, handler))
                return true;
        }
    }
    return false;
} 

bool
bruteforce_iter(struct task_t *task,
                struct config_t *config,
                void *context,
                password_handler_t handler)
{
    size_t size = strlen(config->alphabet) - 1;
    int a[config->length];
    memset(a, 0, config->length * sizeof(int));

    while (true)
    {
        int k;
        for (k = 0; k < config->length; ++k)
            task->password[k] = config->alphabet[a[k]];

        if (handler(context, task)) return true;
    
        for (k = config->length - 1; (k >= 0) && (a[k] == size); --k)
            a[k] = 0;
        if (k < 0) break;
        a[k]++;
    }
    return false;
}

bool
check_password_single(struct task_t *task, char *hash)
{
    struct crypt_data data;
    data.initialized = 0; // ???
    char *hashed = crypt_r(task->password, hash, &data);
    return (strcmp(hashed, hash) == 0);
}

bool
st_password_handler(void *context, struct task_t *task)
{
    struct st_context_t *ctx = (struct st_context_t *) context;
    return check_password_single(task, ctx->hash);
}


bool
singlethreaded(struct task_t *task, struct config_t *config)
{
    struct st_context_t context;
    context.hash = config->hash;
    bool found = false;

    switch (config->brute_mode)
    {
    case M_ITERATIVE:
        found = bruteforce_iter(task, config, &context, st_password_handler);
        break;
    case M_RECURSIVE:
        found = bruteforce_rec(task, config, 0, &context, st_password_handler);
        break;
    }

    return found;
}

void *
mt_worker(void *arg)
{
    struct mt_context_t *context = (struct mt_context_t *) arg;
  
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

        pthread_mutex_lock(&context->tasks_mutex);
        --context->tasks_running;
        pthread_mutex_unlock(&context->tasks_mutex);

        /* printf("%i\n", context->tasks_running); */
        if (context->tasks_running == 0)
            pthread_cond_signal(&context->tasks_cond);
    }
    return NULL;
}


bool
mt_password_handler(void *context, struct task_t *task)
{
    struct mt_context_t *ctx = (struct mt_context_t *) context;

    pthread_mutex_lock(&ctx->tasks_mutex);
    ++ctx->tasks_running;
    pthread_mutex_unlock(&ctx->tasks_mutex);

    queue_push(&ctx->queue, task);
    return false;
}

bool
multithreaded(struct task_t *task, struct config_t *config)
{
    struct mt_context_t context;
    context.hash = config->hash;
    context.tasks_running = 0;
    pthread_mutex_init(&context.tasks_mutex, NULL);
    pthread_cond_init(&context.tasks_cond, NULL);

    queue_init(&context.queue);

    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    pthread_t threads[cpu_count];
    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_create(&threads[i], NULL, mt_worker, (void *) &context);
    }

    bool found = false;
    switch (config->brute_mode)
    {
    case M_ITERATIVE:
        found = bruteforce_iter(task, config, &context, mt_password_handler);
        break;
    case M_RECURSIVE:
        found = bruteforce_rec(task, config, 0, &context, mt_password_handler);
        break;
    }

    pthread_mutex_lock(&context.tasks_mutex);
    while (context.tasks_running != 0)
        pthread_cond_wait(&context.tasks_cond, &context.tasks_mutex);
    pthread_mutex_unlock(&context.tasks_mutex);

    for (int i = 0; i < cpu_count; ++i)
    {
        printf("Cancelling: %i/%i\n", i + 1, cpu_count);
        pthread_cancel(threads[i]);
        pthread_join(threads[i], NULL);
    }

    queue_destroy(&context.queue);
    pthread_mutex_destroy(&context.tasks_mutex);
    pthread_cond_destroy(&context.tasks_cond);

    return found;
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

    struct task_t task;
    task.password[config.length] = '\0';

    bool found;
    switch (config.run_mode)
    {
    case M_SINGLE:
        found = singlethreaded(&task, &config);
        break;
    case M_MULTI:
        found = multithreaded(&task, &config);
        break;
    }

    if (found)
        printf("Password found: '%s'\n", task.password);
    else
        printf("Password not found\n");

    return 0;
}
