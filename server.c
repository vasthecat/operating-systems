#include "server.h"

#include "singlethreaded.h"
#include "iterative.h"
#include "recursive.h"
#include "queue.h"
#include "common.h"

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

struct node_t
{
    int socket_fd;
    pthread_t thread_id;
};

struct set_t
{
    size_t size, capacity;
    struct node_t *data;
};

void
set_init(struct set_t *set)
{
    set->size = 0;
    set->capacity = 2;
    set->data = calloc(set->capacity, sizeof(struct node_t));
    if (set->data == NULL)
        handle_error("Couldn't allocate space for set_t");
}

void
set_insert(struct set_t *set, struct node_t node)
{
    set->data[set->size] = node;
    set->size++;
    if (set->size == set->capacity)
    {
        set->capacity *= 2;
        set->data = realloc(set->data, set->capacity);
        if (set->data == NULL)
            handle_error("Couldn't reallocate space for set_t");
    }
}

// More convenient than set_insert in this case
struct node_t *
set_take_last(struct set_t *set)
{
    set->size++;
    if (set->size == set->capacity)
    {
        set->capacity *= 2;
        set->data = realloc(set->data, set->capacity);
        if (set->data == NULL)
            handle_error("Couldn't reallocate space for set_t");
    }
    return &set->data[set->size - 1];
}

void
set_remove_sock(struct set_t *set, int socket_fd)
{
    int idx = 0;
    for (int i = 0; i < set->size; ++i)
    {
        if (set->data[i].socket_fd == socket_fd)
        {
            idx = i;
            break;
        }
    }
    set->data[idx] = set->data[set->size - 1];
    set->size--;
}

void
set_destroy(struct set_t *set)
{
    free(set->data);
}

struct srv_context_t
{
    volatile int tasks_running;
    pthread_mutex_t tasks_mutex;
    pthread_cond_t tasks_cond;

    struct set_t set;
    pthread_mutex_t set_mutex;
    struct queue_t queue;
    password_t password;
    char *hash;
    volatile bool found;
    volatile bool done;

    pthread_mutex_t thread_started;

    struct config_t *config;
};

struct params_t
{
    struct srv_context_t *context;
    int socket_fd;
};

static int
close_client(int client_sfd)
{
    int status;

    // Send tag
    enum command_t command = CMD_EXIT;
    status = sendall(client_sfd, &command, sizeof(command), 0);
    if (status == -1) return -1;

    // Send length
    int length = 0;
    status = sendall(client_sfd, &length, sizeof(length), 0);
    if (status == -1) return -1;

    // Client should close socket on its side and send EOF
    char res;
    status = recv(client_sfd, &res, sizeof(res), 0);

    shutdown(client_sfd, SHUT_RDWR);
    close(client_sfd);
    return 0;
}

static int
send_task(const int client_sfd, struct task_t *task, bool *result)
{
    int status;

    // Send tag
    enum command_t command = CMD_TASK;
    status = sendall(client_sfd, &command, sizeof(command), 0);
    if (status == -1) return -1;

    // Send length
    int length = sizeof(struct task_t);
    status = sendall(client_sfd, &length, sizeof(length), 0);
    if (status == -1) return -1;

    // Send value
    status = sendall(client_sfd, task, length, 0);
    if (status == -1) return -1;

    int size;
    status = recvall(client_sfd, &size, sizeof(size), 0);
    if (status == -1) return -1;

    if (size != 0)
    {
        status = recv(client_sfd, task->password, size, 0);
        if (status == -1) return -1;
    }

    *result = (size != 0);
    return 0;
}

static void *
serve_client(void *arg)
{
    struct params_t *params = (struct params_t *) arg;
    struct srv_context_t *context = (struct srv_context_t *) params->context;
    int client_sfd = params->socket_fd;
    pthread_mutex_unlock(&context->thread_started);

    while (true)
    {
        struct task_t task;
        queue_pop(&context->queue, &task);

        task.to = task.from;
        task.from = 0;
        bool found = false;
        int status = send_task(client_sfd, &task, &found);
        if (status == -1)
        {
            queue_push(&context->queue, &task);
            break;
        }
        if (found)
        {
            memcpy(context->password, task.password, sizeof(task.password));
            context->found = true;
            context->done = true;
        }

        pthread_mutex_lock(&context->tasks_mutex);
        --context->tasks_running;
        pthread_mutex_unlock(&context->tasks_mutex);

        if (context->tasks_running == 0 || context->found)
            pthread_cond_signal(&context->tasks_cond);
    }

    pthread_mutex_lock(&context->set_mutex);
    set_remove_sock(&context->set, client_sfd);
    close_client(client_sfd);
    pthread_mutex_unlock(&context->set_mutex);

    return NULL;
}

static bool
srv_password_handler(void *context, struct task_t *task)
{
    struct srv_context_t *ctx = (struct srv_context_t *) context;

    pthread_mutex_lock(&ctx->tasks_mutex);
    ++ctx->tasks_running;
    pthread_mutex_unlock(&ctx->tasks_mutex);

    queue_push(&ctx->queue, task);
    return (ctx->password[0] != 0);
}

static void *
srv_server(void *arg)
{
    struct params_t *params = (struct params_t *) arg;
    struct srv_context_t *context = (struct srv_context_t *) params->context;
    int server_sfd = params->socket_fd;

    while (true)
    {
        // Printing to stderr to be able to check output in tests
        fprintf(stderr, "Waiting for new connections...\n");
        int client_socket = accept(server_sfd, NULL, NULL);
        if (client_socket == -1)
            handle_error("accept");
        fprintf(stderr, "Got new connection...\n");

        pthread_mutex_lock(&context->set_mutex);
        pthread_cleanup_push(
            (void (*) (void*)) pthread_mutex_unlock,
            &context->set_mutex
        );

        struct node_t *node = set_take_last(&context->set);
        node->socket_fd = client_socket;
        int status = pthread_create(
            &node->thread_id, NULL, serve_client,
            &(struct params_t) { context, client_socket }
        );
        if (status == 0)
        {
            pthread_mutex_lock(&context->thread_started);
        }
        else
        {
            close_client(client_socket);
        }

        pthread_cleanup_pop(!0);
        pthread_mutex_unlock(&context->set_mutex);
    }

    return NULL;
}

bool
run_server(struct task_t *task, struct config_t *config)
{
    struct srv_context_t context;
    context.hash = config->hash;
    context.tasks_running = 0;
    pthread_mutex_init(&context.thread_started, NULL);
    pthread_mutex_init(&context.tasks_mutex, NULL);
    pthread_mutex_init(&context.set_mutex, NULL);
    pthread_cond_init(&context.tasks_cond, NULL);
    context.password[0] = 0;
    context.config = config;
    context.done = false;
    context.found = false;
    queue_init(&context.queue);
    set_init(&context.set);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
        handle_error("socket");

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(config->port);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*) &server_address, sizeof(server_address)))
        handle_error("bind");

    if (listen(server_socket, MAX_CLIENTS) == -1)
        handle_error("listen");

    pthread_t server_thread;
    pthread_create(&server_thread, NULL, srv_server, 
                   (void *) &(struct params_t) { &context, server_socket });

    task->from = 2;
    task->to = config->length;
    process_task(task, config, &context, srv_password_handler);
    context.done = true;

    pthread_mutex_lock(&context.tasks_mutex);
    pthread_cond_wait(&context.tasks_cond, &context.tasks_mutex);
    pthread_mutex_unlock(&context.tasks_mutex);

    memcpy(task->password, context.password, sizeof(context.password));

    pthread_mutex_lock(&context.set_mutex);
    for (int i = 0; i < context.set.size; ++i)
    {
        pthread_t thread = context.set.data[i].thread_id;
        pthread_cancel(thread);
        pthread_join(thread, NULL);
        close_client(context.set.data[i].socket_fd);
    }
    pthread_mutex_unlock(&context.set_mutex);

    pthread_cancel(server_thread);
    pthread_join(server_thread, NULL);

    queue_destroy(&context.queue);
    set_destroy(&context.set);
    close(server_socket);

    return context.found;
}
