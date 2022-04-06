#include "iterative.h"
#include "common.h"

#include <string.h>
#include <stdbool.h>

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
