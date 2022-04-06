#include "multithreaded.h"

#include "singlethreaded.h"
#include "iterative.h"
#include "recursive.h"
#include "queue.h"

#include <string.h>
#include <unistd.h>
#include <pthread.h>

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

static void *
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

static bool
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
