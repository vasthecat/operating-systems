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
    while (!found)
    {
        recv(network_socket, task, sizeof(struct task_t), 0);
        found = process_task(task, config, &st_context, st_password_handler);
        if (found)
        {
            int msg = (int) sizeof(task->password);
            send(network_socket, &msg, sizeof(int), 0);
            send(network_socket, task->password, msg, 0);
        }
        else
        {
            int msg = 0;
            send(network_socket, &msg, sizeof(int), 0);
        }
    }

	close(network_socket);

	return found;
}
