#include "common.h"
#include "generator.h"
#include "iterative.h"
#include "recursive.h"
#include "singlethreaded.h"

#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <alloca.h>
#include <unistd.h>

struct gn_context_t
{
    pthread_mutex_t mutex;
    password_t password;
    char *hash;
    volatile bool found;
    volatile bool done;

    struct config_t *config;

    union {
        struct iter_state_t iter_state[0];
        struct rec_state_t rec_state[0];
    };
};

static void *
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
                task = *context->iter_state->task;
                context->done = !iter_next(context->iter_state);
                break;
            case M_RECURSIVE:
            case M_REC_ITERATOR:
                task = *context->rec_state->task;
                context->done = !rec_next(context->rec_state);
                break;
            }
        }
        pthread_mutex_unlock(&context->mutex);

        if (done || context->found) break;

        task.to = task.from;
        task.from = 0;
        if (process_task(&task, config, &st_context, st_password_handler))
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
    struct gn_context_t *context = NULL;

    task->from = 2;
    task->to = config->length;
    switch (config->brute_mode)
    {
    case M_ITERATIVE:
        context = alloca(sizeof(struct gn_context_t)
                         + sizeof(struct iter_state_t));
        iter_init(context->iter_state, task, config->alphabet);
        break;
    case M_RECURSIVE:
    case M_REC_ITERATOR:
        context = alloca(sizeof(struct gn_context_t)
                         + sizeof(struct rec_state_t));
        rec_init(context->rec_state, task, config);
        break;
    }

    context->hash = config->hash;
    pthread_mutex_init(&context->mutex, NULL);
    context->password[0] = 0;
    context->config = config;
    context->done = false;
    context->found = false;

    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN) - 1;
    pthread_t threads[cpu_count];
    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_create(&threads[i], NULL, gn_worker, (void *) context);
    }

    gn_worker(context);

    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_join(threads[i], NULL);
    }

    memcpy(task->password, context->password, sizeof(context->password));

    return context->found;
}

