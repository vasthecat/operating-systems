#include "common.h"

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>

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

int
sendall_vec(int socket_fd, struct iovec *iov, int iovcnt)
{
    struct iovec *vec = iov;
    int field_count = iovcnt;
    int size = 0;
    while (field_count > 0)
    {
        int written = TEMP_FAILURE_RETRY(writev(socket_fd, vec, field_count));
        if (written == -1) return -1;
        size += written;

        while (written > 0)
        {
            if (written >= vec->iov_len)
            {
                written -= vec->iov_len;
                vec++;
                field_count--;
            }
            else
            {
                vec->iov_base += written;
                vec->iov_len -= written;
            }
        }
    }
    return size;
}

int
recvall_vec(int socket_fd, struct iovec *iov, int iovcnt)
{
    struct iovec *vec = iov;
    int field_count = iovcnt;
    int size = 0;
    while (field_count > 0)
    {
        int read = TEMP_FAILURE_RETRY(readv(socket_fd, vec, field_count));
        if (read == -1) return -1;
        size += read;

        while (read > 0)
        {
            if (read >= vec->iov_len)
            {
                read -= vec->iov_len;
                vec++;
                field_count--;
            }
            else
            {
                vec->iov_base += read;
                vec->iov_len -= read;
            }
        }
    }
    return size;
}
