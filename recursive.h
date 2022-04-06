#ifndef RECURSIVE_H
#define RECURSIVE_H

#include "common.h"

#include <stdbool.h>
#include <ucontext.h>


// MINSIGSTKSZ is not available (???)
#define STACK_SIZE 131000
struct rec_state_t
{
    ucontext_t main, worker;
    char stack[STACK_SIZE];
    bool done;
    struct task_t *task;
};

bool
bruteforce_rec(struct task_t *task,
               struct config_t *config,
               void *context,
               password_handler_t handler);

void
rec_init(struct rec_state_t *state, struct task_t *task, struct config_t *config);

bool
rec_next(struct rec_state_t *state);

bool
bruteforce_rec_iter(struct task_t *task,
                    struct config_t *config,
                    void *context,
                    password_handler_t handler);

#endif // RECURSIVE_H
