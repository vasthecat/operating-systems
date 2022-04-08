/* #include "server.h" */

#include "singlethreaded.h"
#include "iterative.h"
#include "recursive.h"
#include "queue.h"

#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <alloca.h>
#include <stdio.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef __APPLE__
#include "sem.h"
#else
#include <semaphore.h>
#endif

#define MAX_CLIENTS 50
#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)


struct srv_context_t
{
    sem_t slots_available;

    volatile int tasks_running;
    pthread_mutex_t tasks_mutex;
    pthread_cond_t tasks_cond;

    pthread_mutex_t mutex;
    password_t password;
    char *hash;
    volatile bool found;
    volatile bool done;

    struct config_t *config;

    union {
        struct iter_state_t iter_state[0];
        struct rec_state_t rec_state[0];
    };
};

struct params_t
{
    struct srv_context_t *context;
    int socket_fd;
};

static bool
send_task(struct task_t *task, int client_sfd)
{
    printf("Sending task\n");
    send(client_sfd, (char *) task, sizeof(struct task_t), 0);

    printf("Receiving response\n");
    char size;
    recv(client_sfd, &size, sizeof(size), 0);
    printf("Got size: %i\n", size);

    if (size != 0)
        recv(client_sfd, task->password, sizeof(task->password), 0);

    return size != 0;
}

static void *
serve_client(void *arg)
{
    printf("Serving client\n");
    struct params_t *params = (struct params_t *) arg;
    struct srv_context_t *context = (struct srv_context_t *) params->context;
    struct config_t *config = context->config;
    int client_sfd = params->socket_fd;

    while (true)
    {
        struct task_t task;
        pthread_mutex_lock(&context->mutex);
        bool done = context->done;
        if (!done)
        {
            switch (config->brute_mode)
            {
            case M_ITERATIVE:
                task = *context->iter_state->task;
                context->done = !iter_next(context->iter_state);
                break;
            case M_RECURSIVE:
            case M_REC_ITERATOR:
                task = *context->rec_state->task;
                context->done = !rec_next(context->rec_state);
                break;
            default:
                done = true;
                break;
            }
        }
        pthread_mutex_unlock(&context->mutex);

        if (done || context->found) break;

        task.to = task.from;
        task.from = 0;

        if (send_task(&task, client_sfd))
        {
            printf("ae: %s\n", task.password);
            memcpy(context->password, task.password, sizeof(task.password));
            context->found = true;
            context->done = true;
        }
    }

    printf("Exiting serve_client\n");

    pthread_mutex_lock(&context->tasks_mutex);
    --context->tasks_running;
    pthread_mutex_unlock(&context->tasks_mutex);

    if (context->tasks_running == 0)
        pthread_cond_signal(&context->tasks_cond);

    return NULL;
}

static void *
srv_server(void *arg)
{
    struct params_t *params = (struct params_t *) arg;
    struct srv_context_t *context = (struct srv_context_t *) params->context;
    int server_sfd = params->socket_fd;

    printf("Waiting for new connections...\n");
    while (true)
    {
        int client_socket = accept(server_sfd, NULL, NULL);
        if (client_socket == -1)
            handle_error("accept");
        printf("Got new connection...\n");

        /* sem_wait(&context->slots_available); */

        pthread_mutex_lock(&context->tasks_mutex);
        ++context->tasks_running;
        pthread_mutex_unlock(&context->tasks_mutex);

        printf("Starting client thread\n");

        pthread_t thread; // Not joined
        pthread_create(&thread, NULL, serve_client,
                       (void *) &(struct params_t) { context, client_socket });
    }

    return NULL;
}

bool
run_server(struct task_t *task, struct config_t *config)
{
    struct srv_context_t *context = NULL;


    task->from = 2;
    task->to = config->length;
    switch (config->brute_mode)
    {
    case M_ITERATIVE:
        context = alloca(sizeof(struct srv_context_t)
                         + sizeof(struct iter_state_t));
        iter_init(context->iter_state, task, config->alphabet);
        break;
    case M_RECURSIVE:
    case M_REC_ITERATOR:
        context = alloca(sizeof(struct srv_context_t)
                         + sizeof(struct rec_state_t));
        rec_init(context->rec_state, task, config);
        break;
    }

    context->tasks_running = 0;
    pthread_mutex_init(&context->tasks_mutex, NULL);
    pthread_cond_init(&context->tasks_cond, NULL);

    context->hash = config->hash;
    pthread_mutex_init(&context->mutex, NULL);
    context->password[0] = 0;
    context->config = config;
    context->done = false;
    context->found = false;

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
        handle_error("socket");

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9000);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*) &server_address, sizeof(server_address)))
        handle_error("bind");

    if (listen(server_socket, MAX_CLIENTS) == -1)
        handle_error("listen");

    /* Now we can accept incoming connections one
    at a time using accept(2) */


    pthread_t server_thread;
    pthread_create(&server_thread, NULL, srv_server, 
                   (void *) &(struct params_t) { context, server_socket });

    pthread_mutex_lock(&context->tasks_mutex);
    /* while (context->tasks_running != 0) */
        pthread_cond_wait(&context->tasks_cond, &context->tasks_mutex);
    pthread_mutex_unlock(&context->tasks_mutex);

    printf("Canceling threads...\n");

    pthread_cancel(server_thread);
    pthread_join(server_thread, NULL);

    close(server_socket);

    return false;
}
