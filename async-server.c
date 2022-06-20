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

#include "sem.h"

#define MAX_CLIENTS 50
#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

enum command_t
{
    CMD_EXIT = 1,
    CMD_TASK,
};

struct handler_context_t;

struct node_t
{
    int socket_fd;
    pthread_t receiver_id, sender_id;
    struct handler_context_t *handler_context;
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
    set->data = calloc(set->capacity, sizeof(*set->data));
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
        set->data = realloc(set->data, set->capacity * sizeof(*set->data));
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

struct id_set_t
{
    size_t size, capacity;
    long *data;

    sem_t available;
};

static void
id_set_init(struct id_set_t *set, long size)
{
    sem_init(&set->available, 0, size);

    set->data = calloc(size, sizeof(*set->data));
    if (set->data == NULL)
        handle_error("Couldn't allocate space for id_set_t (free ids)");

    for (int i = 0; i < size; ++i)
    {
        set->data[i] = i;
    }
    set->capacity = size;
    set->size = size;
}

static long
borrow_id(struct id_set_t *set)
{
    sem_wait(&set->available);
    long id = set->data[0];
    --set->size;
    set->data[0] = set->data[set->size];
    return id;
}

static void
return_id(struct id_set_t *set, long id)
{
    set->data[set->size] = id;
    ++set->size;
    sem_post(&set->available);
}

static void
id_set_destroy(struct id_set_t *set)
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

    struct config_t *config;
};

struct handler_context_t
{
    int max_tasks;
    int current_tasks;
    struct task_t *tasks;
    struct id_set_t id_set;
    pthread_mutex_t id_set_mutex;

    int client_sfd;
    struct srv_context_t *srv_context;
};

static void
handler_context_destroy(struct handler_context_t *context)
{
    free(context->tasks);
    id_set_destroy(&context->id_set);
    pthread_mutex_destroy(&context->id_set_mutex);
}

struct params_t
{
    struct srv_context_t *context;
    int socket_fd;
};

static int
send_tag(int socket_fd, enum command_t tag)
{
    struct iovec vec[1];
    vec[0].iov_base = &tag;
    vec[0].iov_len = sizeof(tag);
    return sendall_vec(socket_fd, vec, 1);
}

static int
send_message(int socket_fd, enum command_t tag, void *data, int length)
{
    struct iovec vec[3];
    vec[0].iov_base = &tag;
    vec[0].iov_len = sizeof(tag);

    vec[1].iov_base = &length;
    vec[1].iov_len = sizeof(length);

    vec[2].iov_base = data;
    vec[2].iov_len = length;

    int status = sendall_vec(socket_fd, vec, sizeof(vec) / sizeof(vec[0]));
    if (status == -1) return -1;

    return 0;
}

static int
close_client(int client_sfd)
{
    int status;

    /* fprintf(stderr, "Sending EXIT\n"); */
    status = send_tag(client_sfd, CMD_EXIT);
    if (status == -1) return -1;

    // Client should close socket on its side and send EOF
    char res;
    status = recv(client_sfd, &res, sizeof(res), 0);

    shutdown(client_sfd, SHUT_RDWR);
    close(client_sfd);
    return 0;
}

static void *
task_sender(void *arg)
{
    struct handler_context_t *context = (struct handler_context_t *) arg;
    struct srv_context_t *srv_context = context->srv_context;
    int client_sfd = context->client_sfd;

    int status;

    while (true)
    {
        if (srv_context->tasks_running == 0)
        {
            fprintf(stderr, "No tasks left, exiting sender\n");
            fprintf(stderr, "Clients running: %li\n", srv_context->set.size);
            break;
        }
        else
        {
            pthread_mutex_lock(&context->id_set_mutex);
            long id = borrow_id(&context->id_set);
            pthread_mutex_unlock(&context->id_set_mutex);
            struct task_t *task = &context->tasks[id];
            queue_pop(&srv_context->queue, task);

            task->to = task->from;
            task->from = 0;
            task->id = id;
            task->correct = false;
            task->done = false;

            pthread_mutex_lock(&srv_context->tasks_mutex);
            --srv_context->tasks_running;
            ++context->current_tasks;
            pthread_mutex_unlock(&srv_context->tasks_mutex);

            status = send_message(client_sfd, CMD_TASK, task, sizeof(*task));
            if (status == -1) goto cleanup;
        }
    }

    goto exit_label;
cleanup:
    // NOTE: All tasks should be returned in task_receiver.
    // This label and comment will be removed when this workflow is tested.

exit_label:
    return NULL;
}

static void *
task_receiver(void *arg)
{
    struct handler_context_t *context = (struct handler_context_t *) arg;
    struct srv_context_t *srv_context = context->srv_context;
    int client_sfd = context->client_sfd;

    int status;

    while (true)
    {
        enum command_t tag;
        status = recvall(client_sfd, &tag, sizeof(tag), 0);
        if (status == -1) goto cleanup;

        int length;
        status = recvall(client_sfd, &length, sizeof(length), 0);
        if (status == -1) goto cleanup;

        struct task_t task;
        status = recvall(client_sfd, &task, sizeof(task), 0);
        if (status == -1) goto cleanup;
        --context->current_tasks;
        return_id(&context->id_set, task.id);
        context->tasks[task.id].done = true;

        if (task.correct)
        {
            memcpy(srv_context->password, task.password, sizeof(task.password));
            srv_context->found = true;
            break;
        }

        if (context->current_tasks == 0 && srv_context->tasks_running == 0)
        {
            break;
        }
    }

    goto exit_label;
cleanup:
    /* printf("[fd=%i] Starting cleanup\n", client_sfd); */
    // Return all tasks to queue
    for (int i = 0; i < context->max_tasks; ++i)
    {
        struct task_t *task = &context->tasks[i];
        if (!task->done)
        {
            task->from = task->to;
            task->to = srv_context->config->length;
            queue_push(&srv_context->queue, task);
        }
    }

exit_label:
    close_client(client_sfd);
    pthread_mutex_lock(&srv_context->set_mutex);
    set_remove_sock(&srv_context->set, client_sfd);
    pthread_mutex_unlock(&srv_context->set_mutex);
    handler_context_destroy(context);

    if (srv_context->tasks_running == 0 || srv_context->found)
    {
        pthread_cond_signal(&srv_context->tasks_cond);
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
    int status;

    // Printing to stderr to be able to check output in tests
    fprintf(stderr, "Server started successfully\n");
    while (true)
    {
        int client_sfd = accept(server_sfd, NULL, NULL);
        if (client_sfd == -1)
            handle_error("accept");
        fprintf(stderr, "Got new connection...\n");

        int max_tasks;
        status = recvall(client_sfd, &max_tasks, sizeof(max_tasks), 0);
        if (status == -1)
        {
            close_client(client_sfd);
            continue;
        }
        max_tasks *= 2;

        pthread_mutex_lock(&context->set_mutex);
        pthread_cleanup_push(
            (void (*) (void*)) pthread_mutex_unlock,
            &context->set_mutex
        );

        struct node_t *node = set_take_last(&context->set);
        node->socket_fd = client_sfd;

        node->handler_context = malloc(sizeof(*node->handler_context));
        node->handler_context->max_tasks = max_tasks;
        node->handler_context->current_tasks = 0;
        node->handler_context->tasks =
            malloc(max_tasks * sizeof(*node->handler_context->tasks));
        id_set_init(&node->handler_context->id_set, max_tasks);
        pthread_mutex_init(&node->handler_context->id_set_mutex, NULL);
        node->handler_context->client_sfd = client_sfd;
        node->handler_context->srv_context = context;

        status = pthread_create(
            &node->sender_id, NULL, task_sender, node->handler_context
        );
        if (status != 0)
        {
            perror("Couldn't start sender thread");
            pthread_cancel(node->sender_id);

            handler_context_destroy(node->handler_context);
            free(node->handler_context);
            set_remove_sock(&context->set, node->socket_fd);
            close_client(client_sfd);
        }
        else
        {
            status = pthread_create(
                &node->receiver_id, NULL, task_receiver, node->handler_context
            );
            if (status != 0)
            {
                perror("Couldn't start receiver thread");
                pthread_cancel(node->sender_id);
                pthread_cancel(node->receiver_id);

                handler_context_destroy(node->handler_context);
                free(node->handler_context);
                set_remove_sock(&context->set, node->socket_fd);
                close_client(client_sfd);
            }
        }

        pthread_cleanup_pop(!0);
    }

    return NULL;
}

bool
run_async_server(struct task_t *task, struct config_t *config)
{
    struct srv_context_t context;
    context.hash = config->hash;
    context.tasks_running = 0;
    pthread_mutex_init(&context.tasks_mutex, NULL);
    pthread_mutex_init(&context.set_mutex, NULL);
    pthread_cond_init(&context.tasks_cond, NULL);
    context.password[0] = 0;
    context.config = config;
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

    if (bind(server_socket, 
             (struct sockaddr*) &server_address, sizeof(server_address)))
        handle_error("bind");

    if (listen(server_socket, MAX_CLIENTS) == -1)
        handle_error("listen");

    pthread_t server_thread;
    struct params_t params = { &context, server_socket };
    pthread_create(&server_thread, NULL, srv_server, (void *) &params);

    task->from = 2;
    task->to = config->length;
    process_task(task, config, &context, srv_password_handler);

    pthread_mutex_lock(&context.tasks_mutex);
    pthread_cond_wait(&context.tasks_cond, &context.tasks_mutex);
    pthread_mutex_unlock(&context.tasks_mutex);

    memcpy(task->password, context.password, sizeof(context.password));

    while (true)
    {
        pthread_mutex_lock(&context.set_mutex);
        int set_size = context.set.size;
        pthread_t sender_thread = context.set.data[0].sender_id;
        pthread_t receiver_thread = context.set.data[0].receiver_id;
        pthread_mutex_unlock(&context.set_mutex);
        if (set_size == 0) break;

        pthread_join(receiver_thread, NULL);
        pthread_cancel(sender_thread);
        pthread_join(sender_thread, NULL);
    }
    printf("All clients disconnected\n");

    pthread_cancel(server_thread);
    pthread_join(server_thread, NULL);

    queue_destroy(&context.queue);
    set_destroy(&context.set);
    close(server_socket);

    return context.found;
}

/***************
 * CLIENT CODE *
 ***************/

struct cl_context_t
{
    struct queue_t queue;
    struct queue_t queue_done;

    volatile int tasks_done;
    volatile int tasks_running;
    pthread_mutex_t tasks_mutex;

    volatile bool server_done;

    int server_sfd;
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

        bool found = process_task(&task, config,
                                  &st_context, st_password_handler);
        task.correct = found;

        pthread_mutex_lock(&context->tasks_mutex);
        --context->tasks_running;
        ++context->tasks_done;
        pthread_mutex_unlock(&context->tasks_mutex);

        queue_push(&context->queue_done, &task);
    }
    return NULL;
}

static void *
cl_task_receiver(void *arg)
{
    struct cl_context_t *context = arg;
    int server_sfd = context->server_sfd;

    int status;
    struct task_t task;
    while (true)
    {
        enum command_t tag;
        status = recvall(server_sfd, &tag, sizeof(tag), 0);
        if (status == -1) break;

        switch (tag)
        {
        case CMD_TASK:
            status = read_message(server_sfd, &task);
            if (status == -1) goto exit_label;

            queue_push(&context->queue, &task);
            pthread_mutex_lock(&context->tasks_mutex);
            ++context->tasks_running;
            pthread_mutex_unlock(&context->tasks_mutex);
            break;
        case CMD_EXIT:
            fprintf(stderr, "Received EXIT\n");
            context->server_done = true;
            goto exit_label;
            break;
        default:
            fprintf(stderr, "This shouldn't happen\n");
            goto exit_label;
            break;
        }
    }

exit_label:
    return NULL;
}

static void *
cl_task_sender(void *arg)
{
    struct cl_context_t *context = arg;
    int server_sfd = context->server_sfd;

    int status;
    while (true)
    {
        if (context->server_done &&
            context->tasks_running == 0 &&
            context->tasks_done == 0)
        {
            printf("Exiting sender\n");
            break;
        }

        struct task_t task;
        queue_pop(&context->queue_done, &task);

        status = send_message(server_sfd, CMD_TASK, &task, sizeof(task));
        if (status == -1) break;

        pthread_mutex_lock(&context->tasks_mutex);
        --context->tasks_done;
        pthread_mutex_unlock(&context->tasks_mutex);
    }

    return NULL;
}

bool
run_async_client(struct task_t *task, struct config_t *config)
{
    int server_sfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(config->port);

    struct in_addr address;
    inet_pton(AF_INET, config->address, &address);
    server_address.sin_addr.s_addr = address.s_addr;

    if (connect(server_sfd,
                (struct sockaddr *) &server_address,
                sizeof(server_address)))
    {
        handle_error("connection");
    }
    fprintf(stderr, "Connected to server\n");

    struct cl_context_t context;
    queue_init(&context.queue);
    queue_init(&context.queue_done);
    context.tasks_running = 0;
    context.tasks_done = 0;
    pthread_mutex_init(&context.tasks_mutex, NULL);
    context.server_done = false;
    context.server_sfd = server_sfd;
    context.config = config;

    int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    pthread_t threads[cpu_count];

    int status = sendall(server_sfd, &cpu_count, sizeof(cpu_count), 0);
    if (status == -1) goto exit_label;

    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_create(&threads[i], NULL, cl_worker, (void *) &context);
    }

    pthread_t receiver_thread;
    status = pthread_create(
            &receiver_thread, NULL, cl_task_receiver, (void *) &context);
    if (status != 0)
    {
        perror("Couldn't start receiver thread");
        pthread_cancel(receiver_thread);
        goto cancel_label;
    }

    pthread_t sender_thread;
    status = pthread_create(
            &sender_thread, NULL, cl_task_sender, (void *) &context);
    if (status != 0)
    {
        perror("Couldn't start sender thread");
        pthread_cancel(receiver_thread);
        pthread_cancel(sender_thread);
        goto cancel_label;
    }

    pthread_join(receiver_thread, NULL);

    pthread_cancel(sender_thread);
    pthread_join(sender_thread, NULL);

cancel_label:
    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_cancel(threads[i]);
        pthread_join(threads[i], NULL);
    }

exit_label:
    queue_destroy(&context.queue);
    queue_destroy(&context.queue_done);
    pthread_mutex_destroy(&context.tasks_mutex);
    close(server_sfd);

    return false;
}
