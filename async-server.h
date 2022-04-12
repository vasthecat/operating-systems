#ifndef ASYNC_SERVER_H
#define ASYNC_SERVER_H

#include <stdbool.h>

struct task_t;
struct config_t;

bool
run_async_server(struct task_t *, struct config_t *);

bool
run_async_client(struct task_t *, struct config_t *);

#endif // ASYNC_SERVER_H
