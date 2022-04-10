#include "common.h"
#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

int
sendall(const int socket_fd, const void *data, const int size, const int flags)
{
    const void *bytes = data;
    size_t bytes_to_write = size;
    while (bytes_to_write > 0)
    {
        int written = TEMP_FAILURE_RETRY(send(socket_fd, bytes, bytes_to_write, flags));
        if (written == -1) return -1;
        bytes_to_write -= written;
        bytes += written;
    }
    return size;
}

int
recvall(const int socket_fd, void *data, const int size, const int flags)
{
    void *bytes = data;
    size_t bytes_to_read = size;
    while (bytes_to_read > 0)
    {
        int nread = TEMP_FAILURE_RETRY(recv(socket_fd, bytes, bytes_to_read, flags));
        if (nread == -1) return -1;
        bytes_to_read -= nread;
        bytes += nread;
    }
    return size;
}
