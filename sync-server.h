#ifndef SYNC_SERVER_H
#define SYNC_SERVER_H

#include <stdbool.h>

struct task_t;
struct config_t;

bool
run_server(struct task_t *, struct config_t *);

bool
run_client(struct task_t *, struct config_t *);

#endif // SYNC_SERVER_H
