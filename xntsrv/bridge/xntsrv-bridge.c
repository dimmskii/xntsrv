/*
 * xntsrv-bridge - guest-side socket forwarder for XNTsrv.
 *
 * X clients only speak Unix sockets and TCP.  XNTsrv listens on the
 * hypervisor socket (Hyper-V sockets for WSL2, VMCI for VMware), which a
 * Linux guest reaches as AF_VSOCK.  This program listens on one or more
 * Unix sockets inside the guest and forwards each connection to the host.
 *
 * It is deliberately generic (any number of LISTEN=TARGET mappings) so the
 * same binary can carry other per-session sockets later, such as audio.
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <linux/vm_sockets.h>

#define DEFAULT_MAPPING "/tmp/.X11-unix/X0=vsock:2:106000"
#define MAX_MAPPINGS 16
#define BUFSZ 65536

struct mapping {
    const char *listen_path;
    const char *target;
    int fd;
};

static const char *progname = "xntsrv-bridge";

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: %s [OPTION]... [LISTEN=TARGET]...\n"
        "Forward Unix socket connections in this guest to the XNTsrv host.\n\n"
        "  LISTEN   path of the Unix socket to create\n"
        "  TARGET   vsock:CID:PORT | unix:PATH | tcp:HOST:PORT\n\n"
        "Options:\n"
        "  -m, --mode MODE   octal permissions for created sockets (default 0666)\n"
        "  -h, --help        show this help\n\n"
        "With no mapping given, uses %s\n"
        "(vsock CID 2 is the host).\n",
        progname, DEFAULT_MAPPING);
}

static int set_addr_un(struct sockaddr_un *sa, const char *path)
{
    memset(sa, 0, sizeof *sa);
    sa->sun_family = AF_UNIX;
    if (strlen(path) >= sizeof sa->sun_path) {
        fprintf(stderr, "%s: path too long: %s\n", progname, path);
        return -1;
    }
    strcpy(sa->sun_path, path);
    return 0;
}

/* Check a target's syntax without connecting. */
static int target_valid(const char *t)
{
    unsigned cid, port;
    char tail;

    if (!strncmp(t, "vsock:", 6))
        return sscanf(t + 6, "%u:%u%c", &cid, &port, &tail) == 2;
    if (!strncmp(t, "unix:", 5))
        return t[5] != '\0';
    if (!strncmp(t, "tcp:", 4)) {
        const char *colon = strrchr(t + 4, ':');
        return colon && colon != t + 4 && colon[1] != '\0';
    }
    return 0;
}

static int connect_target(const char *t)
{
    int fd = -1;

    if (!strncmp(t, "vsock:", 6)) {
        struct sockaddr_vm sa = { .svm_family = AF_VSOCK };
        unsigned cid, port;

        sscanf(t + 6, "%u:%u", &cid, &port);
        sa.svm_cid = cid;
        sa.svm_port = port;
        fd = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0)
            return fd;
    } else if (!strncmp(t, "unix:", 5)) {
        struct sockaddr_un sa;

        if (set_addr_un(&sa, t + 5) < 0)
            return -1;
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0)
            return fd;
    } else {
        struct addrinfo hints = { .ai_socktype = SOCK_STREAM }, *res, *ai;
        char host[256];
        const char *colon = strrchr(t + 4, ':');
        size_t hlen = (size_t)(colon - (t + 4));

        if (hlen >= sizeof host)
            return -1;
        memcpy(host, t + 4, hlen);
        host[hlen] = '\0';
        if (getaddrinfo(host, colon + 1, &hints, &res) != 0)
            return -1;
        for (ai = res; ai; ai = ai->ai_next) {
            fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, 0);
            if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
                /* Send each write at once: X sends many small messages, and
                 * Nagle's algorithm would hold some back until the server
                 * acknowledges the previous ones, which Windows may delay. */
                int on = 1;

                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
                freeaddrinfo(res);
                return fd;
            }
            if (fd >= 0)
                close(fd);
            fd = -1;
        }
        freeaddrinfo(res);
        return -1;
    }

    if (fd >= 0) {
        int saved = errno;
        close(fd);
        errno = saved;
    }
    return -1;
}

static int write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

/* Copy both directions until both sides have closed. */
static void relay(int a, int b)
{
    static char buf[BUFSZ];
    struct pollfd p[2] = { { a, POLLIN, 0 }, { b, POLLIN, 0 } };
    int open_dirs = 2;

    while (open_dirs > 0) {
        if (poll(p, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        for (int i = 0; i < 2; i++) {
            ssize_t n;

            if (p[i].fd < 0 || !(p[i].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
            n = read(p[i].fd, buf, sizeof buf);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0) {
                /* Pass the end-of-stream on and stop watching this side. */
                shutdown(i ? a : b, SHUT_WR);
                p[i].fd = -1;
                open_dirs--;
                continue;
            }
            if (write_all(i ? a : b, buf, (size_t)n) < 0)
                return;
        }
    }
}

static int make_listener(const char *path, mode_t mode)
{
    struct sockaddr_un sa;
    struct stat st;
    char dir[sizeof sa.sun_path];
    char *slash;
    int fd;

    if (set_addr_un(&sa, path) < 0)
        return -1;

    /* Create the parent directory the way X expects /tmp/.X11-unix to be. */
    strcpy(dir, path);
    slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        if (mkdir(dir, 01777) == 0)
            chmod(dir, 01777);
    }

    /* Replace a stale socket, but never remove anything that is not one. */
    if (lstat(path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) {
            fprintf(stderr, "%s: %s exists and is not a socket\n", progname, path);
            return -1;
        }
        unlink(path);
    }

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 ||
        chmod(path, mode) < 0 || listen(fd, 64) < 0) {
        fprintf(stderr, "%s: cannot listen on %s: %s\n", progname, path, strerror(errno));
        if (fd >= 0)
            close(fd);
        return -1;
    }
    return fd;
}

static struct mapping maps[MAX_MAPPINGS];
static int nmaps;

static void cleanup(int sig)
{
    for (int i = 0; i < nmaps; i++)
        if (maps[i].fd >= 0)
            unlink(maps[i].listen_path);
    _exit(sig == 0 ? 1 : 0);
}

int main(int argc, char **argv)
{
    struct pollfd pfds[MAX_MAPPINGS];
    struct sigaction sa_chld = { .sa_handler = SIG_IGN, .sa_flags = SA_NOCLDWAIT };
    struct sigaction sa_term = { .sa_handler = cleanup };
    mode_t mode = 0666;
    char *default_map = NULL;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i], *eq;

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            usage(stdout);
            return 0;
        }
        if (!strcmp(arg, "-m") || !strcmp(arg, "--mode")) {
            char *end;

            if (++i >= argc) {
                usage(stderr);
                return 2;
            }
            mode = (mode_t)strtoul(argv[i], &end, 8);
            if (*end != '\0' || mode > 0777) {
                fprintf(stderr, "%s: invalid mode: %s\n", progname, argv[i]);
                return 2;
            }
            continue;
        }
        eq = strchr(arg, '=');
        if (arg[0] == '-' || !eq || eq == arg || nmaps == MAX_MAPPINGS) {
            fprintf(stderr, "%s: invalid argument: %s\n", progname, arg);
            usage(stderr);
            return 2;
        }
        *eq = '\0';
        maps[nmaps].listen_path = arg;
        maps[nmaps].target = eq + 1;
        nmaps++;
    }

    if (nmaps == 0) {
        default_map = strdup(DEFAULT_MAPPING);
        *strchr(default_map, '=') = '\0';
        maps[0].listen_path = default_map;
        maps[0].target = default_map + strlen(default_map) + 1;
        nmaps = 1;
    }

    for (int i = 0; i < nmaps; i++) {
        maps[i].fd = -1;
        if (!target_valid(maps[i].target)) {
            fprintf(stderr, "%s: invalid target: %s\n", progname, maps[i].target);
            return 2;
        }
    }

    sigaction(SIGCHLD, &sa_chld, NULL);
    sigaction(SIGINT, &sa_term, NULL);
    sigaction(SIGTERM, &sa_term, NULL);
    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < nmaps; i++) {
        maps[i].fd = make_listener(maps[i].listen_path, mode);
        if (maps[i].fd < 0)
            cleanup(0);
        pfds[i].fd = maps[i].fd;
        pfds[i].events = POLLIN;
        fprintf(stderr, "%s: %s -> %s\n", progname, maps[i].listen_path, maps[i].target);
    }

    for (;;) {
        if (poll(pfds, (nfds_t)nmaps, -1) < 0) {
            if (errno == EINTR)
                continue;
            perror(progname);
            cleanup(0);
        }
        for (int i = 0; i < nmaps; i++) {
            int client;
            pid_t pid;

            if (!(pfds[i].revents & POLLIN))
                continue;
            client = accept4(maps[i].fd, NULL, NULL, SOCK_CLOEXEC);
            if (client < 0)
                continue;
            pid = fork();
            if (pid == 0) {
                int host;

                signal(SIGINT, SIG_DFL);
                signal(SIGTERM, SIG_DFL);
                for (int j = 0; j < nmaps; j++)
                    close(maps[j].fd);
                host = connect_target(maps[i].target);
                if (host < 0) {
                    fprintf(stderr, "%s: cannot reach %s: %s\n", progname,
                            maps[i].target, strerror(errno));
                    _exit(1);
                }
                relay(client, host);
                _exit(0);
            }
            close(client);
        }
    }
}
