#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

#define PASSWORD_SIZE 20
typedef char password_t[PASSWORD_SIZE];

struct task_t
{
    password_t password;
    int from, to;
};

enum brute_mode_t
{
    M_RECURSIVE,
    M_ITERATIVE,
    M_REC_ITERATOR,
};

enum run_mode_t
{
    M_SINGLE,
    M_MULTI,
    M_GENERATOR,
};

struct config_t
{
    char *alphabet;
    int length;
    enum brute_mode_t brute_mode;
    enum run_mode_t run_mode;
    char *hash;
};

typedef bool (*password_handler_t)(void *, struct task_t *);

#endif // COMMON_H
