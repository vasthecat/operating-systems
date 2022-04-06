#ifndef SINGLETHREADED_H
#define SINGLETHREADED_H

#include <crypt.h>
#include <stdbool.h>

struct task_t;
struct config_t;

struct st_context_t
{
    char *hash;
    struct crypt_data cd;
};

bool
st_password_handler(void *context, struct task_t *);

bool
singlethreaded(struct task_t *, struct config_t *);

bool
process_task(struct task_t *, struct config_t *, struct st_context_t *);

#endif // SINGLETHREADED_H
