#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>

struct task_t;
struct config_t;

bool
run_server(struct task_t *, struct config_t *);

#endif // SERVER_H
