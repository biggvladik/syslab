#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define BUFFER_SIZE 65536


int parse_nonnegative(const char *str, long long *result)
{
    char *endptr;

    errno = 0;

    long long value = strtoll(str, &endptr, 10);

    if (endptr == str) {
        return -1;
    }

    if (*endptr != '\0') {
        return -1;
    }

 
    if (errno == ERANGE) {
        return -1;
    }

    if (value < 0) {
        return -1;
    }

    *result = value;

    return 0;
}

int main(int argc, char *argv[])
{
   
    if (argc != 5) {
        const char msg[] =
            "Usage: ./myslice source destination OFFSET LENGTH\n";

        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 1;
    }

    const char *source = argv[1];
    const char *destination = argv[2];

    long long offset;
    long long length;

    if (parse_nonnegative(argv[3], &offset) == -1) {
        const char msg[] =
            "Error: OFFSET must be a non-negative integer\n";

        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 1;
    }

    if (parse_nonnegative(argv[4], &length) == -1) {
        const char msg[] =
            "Error: LENGTH must be a non-negative integer\n";

        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return 1;
    }

    int src_fd = open(source, O_RDONLY);

    if (src_fd == -1) {
        perror("open source");
        return 1;
    }

    struct stat src_stat;

    if (fstat(src_fd, &src_stat) == -1) {
        perror("fstat source");
        close(src_fd);
        return 1;
    }

    off_t size = lseek(src_fd, 0, SEEK_END);

    if (size == (off_t)-1) {
        perror("lseek SEEK_END");
        close(src_fd);
        return 1;
    }

    off_t file_offset = (off_t)offset;

    if ((long long)file_offset != offset) {
        const char msg[] =
            "Error: OFFSET is too large\n";

        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        close(src_fd);
        return 1;
    }

    int dst_fd = open(
        destination,
        O_WRONLY | O_CREAT,
        src_stat.st_mode & 0777
    );

    if (dst_fd == -1) {
        perror("open destination");
        close(src_fd);
        return 1;
    }

    struct stat dst_stat;

    if (fstat(dst_fd, &dst_stat) == -1) {
        perror("fstat destination");
        close(src_fd);
        close(dst_fd);
        return 1;
    }

    if (src_stat.st_dev == dst_stat.st_dev &&
        src_stat.st_ino == dst_stat.st_ino) {

        const char msg[] =
            "Error: source and destination are the same file\n";

        write(STDERR_FILENO, msg, sizeof(msg) - 1);

        close(src_fd);
        close(dst_fd);

        return 1;
    }


    if (close(dst_fd) == -1) {
        perror("close destination");
        close(src_fd);
        return 1;
    }

    dst_fd = open(
        destination,
        O_WRONLY | O_CREAT | O_TRUNC,
        src_stat.st_mode & 0777
    );

    if (dst_fd == -1) {
        perror("open destination");
        close(src_fd);
        return 1;
    }

    if (file_offset > size) {

        if (close(src_fd) == -1) {
            perror("close source");
            close(dst_fd);
            return 1;
        }

        if (close(dst_fd) == -1) {
            perror("close destination");
            return 1;
        }

        return 0;
    }

    if (lseek(src_fd, file_offset, SEEK_SET) == (off_t)-1) {
        perror("lseek SEEK_SET");
        close(src_fd);
        close(dst_fd);
        return 1;
    }

    char buffer[BUFFER_SIZE];

    long long remaining = length;

    while (remaining > 0) {

        size_t to_read;

        if (remaining < BUFFER_SIZE) {
            to_read = (size_t)remaining;
        } else {
            to_read = BUFFER_SIZE;
        }

        ssize_t bytes_read =
            read(src_fd, buffer, to_read);

        if (bytes_read == 0) {
            break;
        }

        if (bytes_read == -1) {

            if (errno == EINTR) {
                continue;
            }

            perror("read");

            close(src_fd);
            close(dst_fd);

            return 1;
        }

        ssize_t total_written = 0;
        
        while (total_written < bytes_read) {

            ssize_t bytes_written = write(
                dst_fd,
                buffer + total_written,
                (size_t)(bytes_read - total_written)
            );

            if (bytes_written == -1) {

                if (errno == EINTR) {
                    continue;
                }

                perror("write");

                close(src_fd);
                close(dst_fd);

                return 1;
            }

            if (bytes_written == 0) {
                const char msg[] =
                    "Error: write returned 0\n";

                write(
                    STDERR_FILENO,
                    msg,
                    sizeof(msg) - 1
                );

                close(src_fd);
                close(dst_fd);

                return 1;
            }

            total_written += bytes_written;
        }

        remaining -= bytes_read;
    }

    if (close(src_fd) == -1) {
        perror("close source");
        close(dst_fd);
        return 1;
    }

    if (close(dst_fd) == -1) {
        perror("close destination");
        return 1;
    }

    return 0;
}
