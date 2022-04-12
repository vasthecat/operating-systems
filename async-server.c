#include "async-server.h"

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
#include <arpa/inet.h>

#ifdef __APPLE__
#include "sem.h"
#else
#include <semaphore.h>
#endif

#define MAX_CLIENTS 50
#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

enum command_t
{
    CMD_EXIT = 1,
    CMD_TASK,
    CMD_NO_TASKS,
    CMD_TASKS_LIST_START,
    CMD_TASKS_LIST_END,
};

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

static void
set_init(struct set_t *set)
{
    set->size = 0;
    set->capacity = 2;
    set->data = calloc(set->capacity, sizeof(struct node_t));
    if (set->data == NULL)
        handle_error("Couldn't allocate space for set_t");
}

/*
static void
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
*/

// More convenient than set_insert in this case
static struct node_t *
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

static void
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

static void
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
send_tag(int socket_fd, enum command_t tag)
{
    return sendall(socket_fd, &tag, sizeof(tag), 0);
}

static int
send_message(int socket_fd, enum command_t tag, void *data, int length)
{
    int status;

    status = send_tag(socket_fd, tag);
    if (status == -1) return -1;

    status = sendall(socket_fd, &length, sizeof(length), 0);
    if (status == -1) return -1;

    status = sendall(socket_fd, data, length, 0);
    if (status == -1) return -1;

    return 0;
}

static int
close_client(int client_sfd)
{
    int status;

    status = send_tag(client_sfd, CMD_EXIT);
    if (status == -1) return -1;

    // Client should close socket on its side and send EOF
    char res;
    status = recv(client_sfd, &res, sizeof(res), 0);

    shutdown(client_sfd, SHUT_RDWR);
    close(client_sfd);
    return 0;
}

static int
send_tasks(const int client_sfd, const int max_tasks,
           struct srv_context_t *context, struct task_t *tasks,
           bool *task_active, int *current_tasks)
{
    int status;

    status = send_tag(client_sfd, CMD_TASKS_LIST_START);
    if (status == -1) return -1;

    for (int i = 0; i < max_tasks; ++i)
    {
        if (!task_active[i])
        {
            queue_pop(&context->queue, &tasks[i]);

            tasks[i].to = tasks[i].from;
            tasks[i].from = 0;
            tasks[i].id = i;

            pthread_mutex_lock(&context->tasks_mutex);
            --context->tasks_running;
            pthread_mutex_unlock(&context->tasks_mutex);

            status = send_message(client_sfd, CMD_TASK, &tasks[i], sizeof(struct task_t));
            if (status == -1) return -1;

            (*current_tasks)++;
        }
    }

    status = send_tag(client_sfd, CMD_TASKS_LIST_END);
    if (status == -1) return -1;

    return 0;
}

static void *
serve_client(void *arg)
{
    struct params_t *params = (struct params_t *) arg;
    struct srv_context_t *context = (struct srv_context_t *) params->context;
    int client_sfd = params->socket_fd;
    pthread_mutex_unlock(&context->thread_started);

    int status;

    int max_tasks;
    status = recvall(client_sfd, &max_tasks, sizeof(max_tasks), 0);
    if (status == -1) goto exit_label;

    int current_tasks = 0;
    struct task_t *tasks = alloca(max_tasks * sizeof(struct task_t));
    bool *task_active = alloca(max_tasks * sizeof(bool));
    memset(task_active, 0, max_tasks);

    while (true)
    {
        if (context->tasks_running == 0 && current_tasks != 0)
        {
            printf("Sending NO_TASKS\n");
            status = send_tag(client_sfd, CMD_NO_TASKS);
        }
        else
        {
            printf("Sending tasks, %i\n", context->tasks_running);
            status = send_tasks(client_sfd, max_tasks, context, tasks, task_active, &current_tasks);
        }

        if (status == -1) goto cleanup;

        printf("Receiving result\n");
        enum command_t tag;
        status = recvall(client_sfd, &tag, sizeof(tag), 0);
        if (status == -1) goto cleanup;

        if (tag == CMD_NO_TASKS) break;

        struct task_t task;
        status = recvall(client_sfd, &task, sizeof(task), 0);
        if (status == -1) goto cleanup;
        --current_tasks;

        printf("Got result:\n");
        printf("  password = '%s'\n", task.password);
        printf("  id = %li\n", task.id);
        printf("  correct = %i\n", task.correct);

        if (task.correct)
        {
            memcpy(context->password, task.password, sizeof(task.password));
            context->found = true;
            context->done = true;
        }

        if ((context->tasks_running == 0 && current_tasks == 0) || context->found)
        {
            /* pthread_cond_signal(&context->tasks_cond); */
            break;
        }
    }

    goto exit_label;
cleanup:
    // TODO: return all tasks to queue

exit_label:
    pthread_mutex_lock(&context->set_mutex);
    set_remove_sock(&context->set, client_sfd);
    close_client(client_sfd);
    pthread_mutex_unlock(&context->set_mutex);

    printf("Exiting: %i %i %i\n", context->tasks_running, current_tasks, context->found);
    if ((context->tasks_running == 0 && current_tasks == 0) || context->found)
    {
        pthread_cond_signal(&context->tasks_cond);
    }

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
run_async_server(struct task_t *task, struct config_t *config)
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

struct cl_context_t
{
    struct queue_t queue;
    struct queue_t queue_done;

    pthread_mutex_t done_mutex;
    pthread_cond_t task_done_cond;

    volatile int tasks_done;
    volatile int tasks_running;
    pthread_mutex_t tasks_mutex;

    struct config_t *config;
};

static int
read_message(int socket_fd, void *data)
{
    int status;

    int length;
    status = recvall(socket_fd, &length, sizeof(length), 0);
    if (status == -1) return -1;

    status = recvall(socket_fd, data, length, 0);
    if (status == -1) return -1;

    return 0;
}

static void *
cl_worker(void *arg)
{
    struct cl_context_t *context = (struct cl_context_t *) arg;
    struct config_t *config = context->config;

    struct st_context_t st_context;
    st_context.hash = config->hash;
    st_context.cd.initialized = 0;

    while (true)
    {
        struct task_t task;
        queue_pop(&context->queue, &task);

        bool found = process_task(&task, config, &st_context, st_password_handler);
        task.correct = found;
        queue_push(&context->queue_done, &task);

        pthread_mutex_lock(&context->tasks_mutex);
        --context->tasks_running;
        ++context->tasks_done;
        pthread_mutex_unlock(&context->tasks_mutex);

        pthread_cond_signal(&context->task_done_cond);
    }
    return NULL;
}

static int
read_task_list(const int socket_fd, struct cl_context_t *context)
{
    int status;

    while (true)
    {
        enum command_t tag;
        status = recvall(socket_fd, &tag, sizeof(tag), 0);
        if (status == -1) return -1;

        if (tag == CMD_TASKS_LIST_END)
        {
            break;
        }
        else
        {
            struct task_t task;
            status = read_message(socket_fd, &task);
            if (status == -1) return -1;
            queue_push(&context->queue, &task);

            pthread_mutex_lock(&context->tasks_mutex);
            ++context->tasks_running;
            pthread_mutex_unlock(&context->tasks_mutex);
        }
    }
    return 0;
}

bool
run_async_client(struct task_t *task, struct config_t *config)
{
    int network_socket = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(config->port);

    struct in_addr address;
    inet_pton(AF_INET, config->address, &address);
    server_address.sin_addr.s_addr = address.s_addr;

    if (connect(network_socket,
                (struct sockaddr *) &server_address,
                sizeof(server_address)))
    {
        handle_error("connection");
    }
    printf("Connected to server\n");

    struct cl_context_t context;
    queue_init(&context.queue);
    queue_init(&context.queue_done);
    pthread_mutex_init(&context.done_mutex, NULL);
    pthread_cond_init(&context.task_done_cond, NULL);
    context.tasks_running = 0;
    context.tasks_done = 0;
    pthread_mutex_init(&context.tasks_mutex, NULL);
    context.config = config;

    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN) - 1;
    cpu_count = 1;
    pthread_t threads[cpu_count];
    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_create(&threads[i], NULL, cl_worker, (void *) &context);
    }

    int status;
    status = sendall(network_socket, &cpu_count, sizeof(cpu_count), 0);
    if (status == -1) goto exit_label;

    bool found = false;
    while (!found)
    {
        printf("Waiting for new command\n");
        enum command_t tag;
        status = recvall(network_socket, &tag, sizeof(tag), 0);
        if (status == -1) break;

        switch (tag)
        {
        case CMD_TASKS_LIST_START:
            read_task_list(network_socket, &context);
            break;
        case CMD_NO_TASKS:
            printf("Received NO_TASKS\n");
            break;
        case CMD_EXIT:
            printf("Received EXIT\n");
            goto exit_label;
            break;
        default:
            fprintf(stderr, "This shouldn't happen\n");
            goto exit_label;
            break;
        }

        if (context.tasks_running == 0 && context.tasks_done == 0)
        {
            printf("Sending NO_TASKS\n");
            status = send_tag(network_socket, CMD_NO_TASKS);
            if (status == -1) break;
            continue;
        }

        if (context.tasks_running != 0)
        {
            printf("Waiting for task to complete, %i\n", context.tasks_running);
            pthread_mutex_lock(&context.done_mutex);
            pthread_cond_wait(&context.task_done_cond, &context.done_mutex);
            pthread_mutex_unlock(&context.done_mutex);
        }

        printf("Some task is done, sending to server\n");
        status = send_tag(network_socket, CMD_TASK);
        if (status == -1) break;
        struct task_t task;
        queue_pop(&context.queue_done, &task);
        status = sendall(network_socket, &task, sizeof(task), 0);
        if (status == -1) break;

        pthread_mutex_lock(&context.tasks_mutex);
        --context.tasks_done;
        pthread_mutex_unlock(&context.tasks_mutex);
        printf("%i tasks left\n", context.tasks_done);
    }

exit_label:
    queue_destroy(&context.queue);
    queue_destroy(&context.queue_done);
    pthread_mutex_destroy(&context.done_mutex);
    pthread_cond_destroy(&context.task_done_cond);
    pthread_mutex_destroy(&context.tasks_mutex);
    close(network_socket);

    return found;
}
