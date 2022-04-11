#include "client.h"

#include "singlethreaded.h"
#include "common.h"
#include "iterative.h"
#include "recursive.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

static int
cl_process_task(int network_socket, struct task_t *task,
                struct st_context_t *context, struct config_t *config)
{
    int status;

    bool found = process_task(task, config, context, st_password_handler);
    if (found)
    {
        int msg = (int) sizeof(task->password);
        status = sendall(network_socket, &msg, sizeof(int), 0);
        if (status == -1) return -1;
        status = sendall(network_socket, task->password, msg, 0);
        if (status == -1) return -1;
    }
    else
    {
        int msg = 0;
        status = sendall(network_socket, &msg, sizeof(int), 0);
        if (status == -1) return -1;
    }
    return 0;
}

bool
run_client(struct task_t *task, struct config_t *config)
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

    struct st_context_t st_context;
    st_context.hash = config->hash;
    st_context.cd.initialized = 0;

    bool found = false;
    int status;
    while (!found)
    {
        enum command_t tag;
        status = recvall(network_socket, &tag, sizeof(tag), 0);
        if (status == -1) break;

        int length;
        status = recvall(network_socket, &length, sizeof(length), 0);
        if (status == -1) break;

        switch (tag)
        {
        case CMD_EXIT:
            goto exit_label;
            break;
        case CMD_TASK:
            status = recvall(network_socket, task, length, 0);
            if (status == -1) goto exit_label;

            status = cl_process_task(network_socket, task, &st_context, config);
            if (status == -1) goto exit_label;

            break;
        }
    }

exit_label:

    close(network_socket);

    return found;
}
