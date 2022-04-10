#ifndef CLIENT_H
#define CLIENT_H

#include <stdbool.h>

struct task_t;
struct config_t;

bool
run_client(struct task_t *, struct config_t *);

#endif // CLIENT_H
