#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <crypt.h>
#define _XOPEN_SOURCE
#include <ucontext.h>
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
    int from, to;
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
    M_ITERATIVE,
    M_REC_ITERATOR,
};

enum run_mode_t
{
    M_SINGLE,
    M_MULTI,
    M_GENERATOR,
};

struct config_t
{
    char *alphabet;
    int length;
    enum brute_mode_t brute_mode;
    enum run_mode_t run_mode;
    char *hash;
};

struct iter_state_t
{
    int idx[PASSWORD_SIZE];
    char *alphabet;
    size_t alph_size;
    struct task_t *task;
};

// MINSIGSTKSZ is not available (???)
#define STACK_SIZE 131000
struct rec_state_t
{
    ucontext_t main, worker;
    char stack[STACK_SIZE];
    bool done;
    struct task_t *task;
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
    volatile bool found;

    struct config_t *config;
};

struct gn_context_t
{
    union {
        struct iter_state_t iter_state;
        struct rec_state_t rec_state;
    };
    pthread_mutex_t mutex;
    password_t password;
    char *hash;
    volatile bool found;
    volatile bool done;

    struct config_t *config;
};

typedef bool (*password_handler_t)(void *, struct task_t *);

bool
bruteforce_rec_internal(struct task_t *task,
                        struct config_t *config,
                        void *context,
                        password_handler_t handler,
                        int pos)
{
    if (pos == task->to)
    {
        return handler(context, task);
    }
    else
    {
        for (int i = 0; config->alphabet[i] != '\0'; ++i)
        {
            task->password[pos] = config->alphabet[i];
            if (bruteforce_rec_internal(task, config, context, handler, pos + 1))
                return true;
        }
    }
    return false;
} 

bool
bruteforce_rec(struct task_t *task,
               struct config_t *config,
               void *context,
               password_handler_t handler)
{
    return bruteforce_rec_internal(task, config, context, handler, task->from);
}

bool
cooperative_handler(void *context, struct task_t *task)
{
    struct rec_state_t *state = (struct rec_state_t *) context;
    swapcontext(&state->worker, &state->main);
    return false;
}

void
bruteforce_rec_cooperative(struct config_t *config,
                           void *context,
                           password_handler_t handler)
{
    struct rec_state_t *state = (struct rec_state_t *) context;
    state->done = false;
    bruteforce_rec(state->task, config, context, cooperative_handler);
    state->done = true;
}

void
rec_init(struct rec_state_t *state, struct task_t *task, struct config_t *config)
{
    state->task = task;
    getcontext(&state->main);
    state->worker = state->main;
    state->worker.uc_stack.ss_sp = state->stack;
    state->worker.uc_stack.ss_size = sizeof(state->stack);
    state->worker.uc_link = &state->main;
    makecontext(&state->worker,
                (void (*) (void)) bruteforce_rec_cooperative,
                3, config, (void *) state, cooperative_handler);
    swapcontext(&state->main, &state->worker);
}

bool
rec_next(struct rec_state_t *state)
{
    /* printf("HELLO\n"); */
    swapcontext(&state->main, &state->worker);
    return !state->done;
}

bool
bruteforce_rec_iter(struct task_t *task,
                    struct config_t *config,
                    void *context,
                    password_handler_t handler)
{
    struct rec_state_t state;
    rec_init(&state, task, config);
    while (true)
    {
        if (handler(context, task))
            return true;
        if (!rec_next(&state))
            break;
    }
    return false;
}

void
iter_init(struct iter_state_t *state, struct task_t *task, char *alphabet)
{
    state->alphabet = alphabet;
    state->alph_size = strlen(alphabet) - 1;
    state->task = task;

    for (int i = task->from; i < task->to; ++i)
    {
        state->idx[i] = 0;
        task->password[i] = alphabet[0];
    }
}

bool
iter_next(struct iter_state_t *state)
{
    struct task_t *task = state->task;

    int k;
    for (k = task->to - 1; (k >= task->from) && (state->idx[k] == state->alph_size); --k)
    {
        state->idx[k] = 0;
        task->password[k] = state->alphabet[state->idx[k]];
    }
    if (k < task->from) return false;
    task->password[k] = state->alphabet[++state->idx[k]];
    return true;
}

bool
bruteforce_iter(struct task_t *task,
                struct config_t *config,
                void *context,
                password_handler_t handler)
{
    struct iter_state_t state;
    iter_init(&state, task, config->alphabet);
    while (true)
    {
        if (handler(context, task))
            return true;
        if (!iter_next(&state))
            break;
    }
    return false;
}

bool
st_password_handler(void *context, struct task_t *task)
{
    struct st_context_t *ctx = (struct st_context_t *) context;
    char *hashed = crypt_r(task->password, ctx->hash, &ctx->cd);
    return (strcmp(hashed, ctx->hash) == 0);
}

bool
process_task(struct task_t *task, struct config_t *config, struct st_context_t *context)
{
    bool found = false;
    switch (config->brute_mode)
    {
    case M_ITERATIVE:
        found = bruteforce_iter(task, config, context, st_password_handler);
        break;
    case M_RECURSIVE:
        found = bruteforce_rec(task, config, context, st_password_handler);
        break;
    case M_REC_ITERATOR:
        found = bruteforce_rec_iter(task, config, context, st_password_handler);
        break;
    }
    return found;
}

bool
singlethreaded(struct task_t *task, struct config_t *config)
{
    struct st_context_t context;
    context.hash = config->hash;
    context.cd.initialized = 0;

    task->from = 0;
    task->to = config->length;

    bool found = process_task(task, config, &context);
    return found;
}

void *
mt_worker(void *arg)
{
    struct mt_context_t *context = (struct mt_context_t *) arg;
    struct config_t *config = context->config;

    struct st_context_t st_context;
    st_context.hash = context->hash;
    st_context.cd.initialized = 0;

    while (true)
    {
        struct task_t task;
        queue_pop(&context->queue, &task);

        task.to = task.from;
        task.from = 0;
        if (process_task(&task, config, &st_context))
        {
            memcpy(context->password, task.password, sizeof(task.password));
            context->found = true;
        }

        pthread_mutex_lock(&context->tasks_mutex);
        --context->tasks_running;
        pthread_mutex_unlock(&context->tasks_mutex);

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
    return (ctx->password[0] != 0);
}

bool
multithreaded(struct task_t *task, struct config_t *config)
{
    struct mt_context_t context;
    context.hash = config->hash;
    context.tasks_running = 0;
    pthread_mutex_init(&context.tasks_mutex, NULL);
    pthread_cond_init(&context.tasks_cond, NULL);
    context.password[0] = 0;
    context.config = config;

    queue_init(&context.queue);

    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    pthread_t threads[cpu_count];
    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_create(&threads[i], NULL, mt_worker, (void *) &context);
    }

    task->from = 2;
    task->to = config->length;

    switch (config->brute_mode)
    {
    case M_ITERATIVE:
        bruteforce_iter(task, config, &context, mt_password_handler);
        break;
    case M_RECURSIVE:
    case M_REC_ITERATOR:
        bruteforce_rec(task, config, &context, mt_password_handler);
        break;
    }

    pthread_mutex_lock(&context.tasks_mutex);
    while (context.tasks_running != 0)
        pthread_cond_wait(&context.tasks_cond, &context.tasks_mutex);
    pthread_mutex_unlock(&context.tasks_mutex);

    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_cancel(threads[i]);
        pthread_join(threads[i], NULL);
    }

    memcpy(task->password, context.password, sizeof(context.password));

    queue_destroy(&context.queue);
    pthread_mutex_destroy(&context.tasks_mutex);
    pthread_cond_destroy(&context.tasks_cond);

    return context.found;
}

void *
gn_worker(void *arg)
{
    struct gn_context_t *context = (struct gn_context_t *) arg;
    struct config_t *config = context->config;

    struct st_context_t st_context;
    st_context.hash = context->hash;
    st_context.cd.initialized = 0;

    while (true)
    {
        struct task_t task;
        pthread_mutex_lock(&context->mutex);
        bool done = context->done;
        if (!done)
        {
            switch (config->brute_mode)
            {
            case M_ITERATIVE:
                task = *context->iter_state.task;
                context->done = !iter_next(&context->iter_state);
                break;
            case M_RECURSIVE:
            case M_REC_ITERATOR:
                task = *context->rec_state.task;
                context->done = !rec_next(&context->rec_state);
                break;
            }
        }
        pthread_mutex_unlock(&context->mutex);

        if (done || context->found) break;

        task.to = task.from;
        task.from = 0;
        if (process_task(&task, config, &st_context))
        {
            memcpy(context->password, task.password, sizeof(task.password));
            context->found = true;
            context->done = true;
        }
    }
    return NULL;
}

bool
generator(struct task_t *task, struct config_t *config)
{
    struct gn_context_t context;
    context.hash = config->hash;

    task->from = 2;
    task->to = config->length;
    switch (config->brute_mode)
    {
    case M_ITERATIVE:
        iter_init(&context.iter_state, task, config->alphabet);
        break;
    case M_RECURSIVE:
    case M_REC_ITERATOR:
        rec_init(&context.rec_state, task, config);
        break;
    }

    pthread_mutex_init(&context.mutex, NULL);
    context.password[0] = 0;
    context.config = config;
    context.done = false;
    context.found = false;

    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN) - 1;
    pthread_t threads[cpu_count];
    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_create(&threads[i], NULL, gn_worker, (void *) &context);
    }

    gn_worker(&context);

    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_join(threads[i], NULL);
    }

    memcpy(task->password, context.password, sizeof(context.password));

    return context.found;
}

void
parse_opts(struct config_t *config, int argc, char *argv[])
{
    int opt;
    opterr = 1;
    while ((opt = getopt(argc, argv, "irymsga:l:h:")) != -1)
    {
        switch (opt)
        {
        case 'i':
            config->brute_mode = M_ITERATIVE;
            break;
        case 'r':
            config->brute_mode = M_RECURSIVE;
            break;
        case 'y':
            config->brute_mode = M_REC_ITERATOR;
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
        case 'g':
            config->run_mode = M_GENERATOR;
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
    case M_GENERATOR:
        found = generator(&task, &config);
        break;
    }

    if (found)
        printf("Password found: '%s'\n", task.password);
    else
        printf("Password not found\n");

    return 0;
}
