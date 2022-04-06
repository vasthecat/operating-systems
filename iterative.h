#ifndef ITERATIVE_H
#define ITERATIVE_H

#include "common.h"

#include <stdlib.h>
#include <stdbool.h>

struct task_t;

struct iter_state_t
{
    int idx[PASSWORD_SIZE];
    char *alphabet;
    size_t alph_size;
    struct task_t *task;
};

void
iter_init(struct iter_state_t *, struct task_t *, char *alphabet);

bool
iter_next(struct iter_state_t *);

bool
bruteforce_iter(struct task_t *, struct config_t *, void *context,
                password_handler_t);

#endif // ITERATIVE_H
