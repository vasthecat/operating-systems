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
    set->data = malloc(size * sizeof(long));
    if (set->data == NULL)
        handle_error("Couldn't allocate space for id_set_t");
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
    sem_post(&set->available);
    set->data[set->size] = id;
    ++set->size;
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
    volatile bool done;

    pthread_mutex_t thread_started;

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
}

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

    fprintf(stderr, "Sending EXIT\n");
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
        if (srv_context->tasks_running == 0 && context->current_tasks != 0)
        {
            status = send_tag(client_sfd, CMD_NO_TASKS);
            fprintf(stderr, "No tasks left, exiting sender\n");
            break;
        }
        else
        {
            long id = borrow_id(&context->id_set);
            struct task_t *task = &context->tasks[id];
            queue_pop(&srv_context->queue, task);

            task->to = task->from;
            task->from = 0;
            task->id = id;
            task->correct = false;

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
    // TODO: return all tasks to queue

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

        if (tag == CMD_NO_TASKS) break;

        struct task_t task;
        status = recvall(client_sfd, &task, sizeof(task), 0);
        if (status == -1) goto cleanup;
        --context->current_tasks;
        return_id(&context->id_set, task.id);

        /*
        printf("Got result:\n");
        printf("  password = '%s'\n", task.password);
        printf("  id = %li\n", task.id);
        printf("  correct = %i\n", task.correct);
        */

        if (task.correct)
        {
            memcpy(srv_context->password, task.password, sizeof(task.password));
            srv_context->found = true;
            srv_context->done = true;
            break;
        }
    }

    goto exit_label;
cleanup:
    // TODO: return all tasks to queue

exit_label:
    pthread_mutex_lock(&srv_context->set_mutex);
    set_remove_sock(&srv_context->set, client_sfd);
    close_client(client_sfd);
    pthread_mutex_unlock(&srv_context->set_mutex);

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

    while (true)
    {
        // Printing to stderr to be able to check output in tests
        fprintf(stderr, "Waiting for new connections...\n");
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
            handler_context_destroy(node->handler_context);
            free(node->handler_context);
            close_client(client_sfd);
        }

        status = pthread_create(
            &node->receiver_id, NULL, task_receiver, node->handler_context
        );
        if (status != 0)
        {
            perror("Couldn't start receiver thread");
            handler_context_destroy(node->handler_context);
            free(node->handler_context);
            close_client(client_sfd);
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
        pthread_t receiver_thread = context.set.data[i].receiver_id;
        pthread_cancel(receiver_thread);
        pthread_join(receiver_thread, NULL);

        pthread_t sender_thread = context.set.data[i].sender_id;
        pthread_cancel(sender_thread);
        pthread_join(sender_thread, NULL);

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

    volatile bool receiver_done;

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

pid_t syscall(int);

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
    while (true)
    {
        enum command_t tag;
        status = recvall(server_sfd, &tag, sizeof(tag), 0);
        if (status == -1) break;

        switch (tag)
        {
        case CMD_TASK:
            struct task_t task;
            status = read_message(server_sfd, &task);
            if (status == -1) goto exit_label;

            pthread_mutex_lock(&context->tasks_mutex);
            queue_push(&context->queue, &task);
            ++context->tasks_running;
            pthread_mutex_unlock(&context->tasks_mutex);
            break;
        case CMD_NO_TASKS:
            context->receiver_done = true;
            break;
        case CMD_EXIT:
            fprintf(stderr, "Received EXIT\n");
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
        if (context->receiver_done &&
            context->tasks_running == 0 &&
            context->tasks_done == 0)
        {
            status = send_tag(server_sfd, CMD_NO_TASKS);
            break;
        }

        struct task_t task;
        queue_pop(&context->queue_done, &task);
        status = send_tag(server_sfd, CMD_TASK);
        if (status == -1) break;
        status = sendall(server_sfd, &task, sizeof(task), 0);
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
    context.receiver_done = false;
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
    pthread_create(&receiver_thread, NULL, cl_task_receiver, (void *) &context);

    pthread_t sender_thread;
    pthread_create(&sender_thread, NULL, cl_task_sender, (void *) &context);

    pthread_join(receiver_thread, NULL);

    for (int i = 0; i < cpu_count; ++i)
    {
        pthread_cancel(threads[i]);
        pthread_join(threads[i], NULL);
    }

    pthread_cancel(sender_thread);
    pthread_join(sender_thread, NULL);

exit_label:
    queue_destroy(&context.queue);
    queue_destroy(&context.queue_done);
    pthread_mutex_destroy(&context.tasks_mutex);
    close(server_sfd);

    return false;
}