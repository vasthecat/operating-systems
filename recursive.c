#include "recursive.h"

#include <unistd.h>

static bool
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

static bool
cooperative_handler(void *context, struct task_t *task)
{
    struct rec_state_t *state = (struct rec_state_t *) context;
    swapcontext(&state->worker, &state->main);
    return false;
}

static void
bruteforce_rec_cooperative(struct config_t *config,
                           struct rec_state_t *state)
{
    state->done = false;
    bruteforce_rec(state->task, config, state, cooperative_handler);
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
