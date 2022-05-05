#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

#define PASSWORD_SIZE 20
typedef char password_t[PASSWORD_SIZE];

struct task_t
{
    password_t password;
    int from, to;
    long id;
    bool correct;
    bool done;
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
    M_SERVER,
    M_CLIENT,
    M_ASYNC_SERVER,
    M_ASYNC_CLIENT,
};

struct config_t
{
    char *alphabet;
    int length;
    enum brute_mode_t brute_mode;
    enum run_mode_t run_mode;
    char *hash;
    char *address;
    int port;
};

typedef bool (*password_handler_t)(void *, struct task_t *);

int
sendall(const int socket_fd, const void *data, const int size, const int flags);

int
recvall(const int socket_fd, void *data, const int size, const int flags);

struct iovec;

int
sendall_vec(int socket_fd, struct iovec *iov, int iovcnt);

int
recvall_vec(int socket_fd, struct iovec *iov, int iovcnt);

#endif // COMMON_H
