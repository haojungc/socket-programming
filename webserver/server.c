#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_STATIC_DIR "static"
#define PORT "3333"
#define BACKLOG 16
#define MAX_CONTENT_LENGTH 8192
#define MAXBUFSIZE (MAX_CONTENT_LENGTH << 1)
#define MAXDATASIZE 256
#define MAXFILENAME 64
#define MAXPATHLEN 128

void sigchld_handler(int s)
{
    int tmp = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = tmp;
}

/**
 * Checks if @filename exists. If @filename exists, opens it with flags @flags
 * and returns the file descriptor.
 */
int open_regular_file(const char *filename, int flags)
{
    struct stat lstat_info, fstat_info;

    if (lstat(filename, &lstat_info) == -1) {
        perror("lstat");
        return -1;
    }

    if (!S_ISREG(lstat_info.st_mode)) {
        fprintf(stderr, "%s is not a regular file\n", filename);
        return -1;
    }

    int fd;
    if ((fd = open(filename, flags)) == -1) {
        perror("open");
        return -1;
    }

    if (fstat(fd, &fstat_info) == -1) {
        perror("fstat");
        return -1;
    }

    if (lstat_info.st_ino != fstat_info.st_ino ||
        lstat_info.st_dev != fstat_info.st_dev) {
        fprintf(stderr, "%s changed during open()", filename);
        return -1;
    }

    return fd;
}

int read_file(const char *filename, char *buf, size_t count)
{
    int fd;

    if ((fd = open_regular_file(filename, O_RDONLY)) == -1) {
        fprintf(stderr, "%s does not exists\n", filename);
        return -1;
    }

    int byte_count;
    if ((byte_count = read(fd, buf, count)) == -1) {
        perror("read");
        return -1;
    }

    return byte_count;
}

/**
 * Reads repeatedly until the entire message from client is received.
 * Note: @buf is always null-terminated
 */
int recv_all(const int sock_fd, char *buf)
{
    int len = 0;
    int remaining = MAXBUFSIZE - 1;
    for (;;) {
        int byte_count = 0;
        if ((byte_count = recv(
                 sock_fd, buf + len,
                 remaining >= MAXDATASIZE - 1 ? MAXDATASIZE - 1 : remaining,
                 0)) == -1) {
            return -1;
        }

        len += byte_count;
        remaining -= byte_count;
        buf[len] = '\0';

        if (len == 0) {
            return 0;
        }

        /* Checks if there is an empty line (end of HTTP request) */
        if (strcmp(&buf[len - 4], "\r\n\r\n") == 0) {
            break;
        }
    }
    return len;
}

int send_all(const int sock_fd, void *msg, size_t len, int flags)
{
    int total = 0;
    int byte_count = 0;
    while (total < len) {
        if ((byte_count = send(sock_fd, msg + total, len - total, flags)) ==
            -1) {
            perror("send");
            return -1;
        }
        total += byte_count;
    }

    return total;
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}

/**
 * Parses the requested filename from the HTTP request.
 * @request: an HTTP request, which looks like the sample below (index.html
 * should be stored in @filename):
 *     GET /index.html HTTP/1.1 Host: localhost:3333
 *     User-Agent: Mozilla/5.0 ...
 *     ...
 * @filename: name of the requested file
 */
void get_requested_filename(const char *request, char *filename)
{
    /* Locates the first character after the first '/' */
    char *begin;
    if ((begin = strchr(request, '/')) == NULL) {
        fprintf(stderr, "invalid request\n");
        return;
    }
    begin++;

    /* Stores filename into @filename */
    int len = 0;
    for (char *p = begin; *p != ' '; p++) {
        filename[len++] = *p;
    }
    filename[len] = '\0';
}

int main(int argc, char **argv)
{
    int sock_fd, newsock_fd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;

    const char *static_dir = DEFAULT_STATIC_DIR;
    /* Synopsis:
     *   $./server [-static <path-to-static-directory>]
     */
    if (argc >= 3) {
        static_dir = argv[2];
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int ret;
    if ((ret = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return 1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) ==
            -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) ==
            -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sock_fd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    char s[INET6_ADDRSTRLEN];
    for (;;) {
        sin_size = sizeof(client_addr);
        newsock_fd =
            accept(sock_fd, (struct sockaddr *) &client_addr, &sin_size);
        if (newsock_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(client_addr.ss_family,
                  get_in_addr((struct sockaddr *) &client_addr), s, sizeof(s));
        printf("server: get connection from %s\n", s);

        if (!fork()) {
            close(sock_fd);

            char filename[MAXFILENAME];
            char buf[MAXBUFSIZE];
            int byte_count;
            for (;;) {
                if ((byte_count = recv_all(newsock_fd, buf)) == -1) {
                    perror("recv");
                    exit(1);
                }

                if (byte_count == 0) {
                    fprintf(stderr, "server: client closed connection");
                    exit(0);
                }

                printf("message from client: %s\n\n", buf);

                get_requested_filename(buf, filename);

                printf("requested filename: %s\n\n", filename);

                char msg[MAXBUFSIZE];
                char body[MAX_CONTENT_LENGTH];
                char filepath[MAXPATHLEN];
                sprintf(filepath, "%s/%s", static_dir, filename);

                if (read_file(filepath, body, sizeof(body)) == -1) {
                    /* 404 */
                    const char *msg_404 = "Not Found";
                    sprintf(msg,
                            "HTTP/1.1 404 Not Found\r\n"
                            "Content-Length: %lu\r\n"
                            "Content-Type: text/plain; charset=utf-8\r\n"
                            "\r\n"
                            "%s\r\n",
                            strlen(msg_404) + 2, msg_404);
                } else {
                    sprintf(msg,
                            "HTTP/1.1 200 Ok\r\n"
                            "Connection: Keep-Alive\r\n"
                            "Content-Length: %lu\r\n"
                            "Content-Type: text/html; charset=utf-8\r\n"
                            "Keep-Alive: timeout=5, max=1000\r\n"
                            "\r\n"
                            "%s\r\n",
                            strlen(body) + 2, body);
                }

                if ((byte_count = send_all(newsock_fd, (void *) msg,
                                           strlen(msg), 0)) == -1) {
                    exit(1);
                }

                printf("msg len: %lu, sent %d bytes\n", strlen(msg),
                       byte_count);
            }

            close(newsock_fd);
            exit(0);
        }
        close(newsock_fd);
    }

    return 0;
}
