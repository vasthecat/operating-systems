#include "singlethreaded.h"
#include "common.h"
#include "iterative.h"
#include "recursive.h"

#include <string.h>
#include <stdbool.h>

bool
st_password_handler(void *context, struct task_t *task)
{
    struct st_context_t *ctx = (struct st_context_t *) context;
    char *hashed = crypt_r(task->password, ctx->hash, &ctx->cd);
    return (strcmp(hashed, ctx->hash) == 0);
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
