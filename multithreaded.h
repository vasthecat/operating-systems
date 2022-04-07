#ifndef MULTITHREADED_H
#define MULTITHREADED_H

#include <stdbool.h>

struct task_t;
struct config_t;

bool
multithreaded(struct task_t *, struct config_t *);

#endif // MULTITHREADED_H
