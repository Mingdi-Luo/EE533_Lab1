/*
 * Concurrent TCP server (fork-per-connection) with per-connection read/write loop
 *
 * Build: gcc -Wall -Wextra -O2 server_fork_loop.c -o server
 * Run:   ./server <port>
 *
 * Behavior:
 * - Accepts connections forever (while(1))
 * - Each connection is handled in a child process (fork)
 * - Child reads multiple messages until client closes or sends "quit"/"exit"
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static void reap_children(int signo) {
    (void)signo;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        /* reap all finished children */
    }
}

static int is_quit_cmd(const char *s) {
    /* Treat lines starting with "quit" or "exit" (case-insensitive, ignoring leading spaces) as termination */
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s == '\0') return 0;

    char tmp[8];
    size_t i = 0;
    while (i < sizeof(tmp) - 1 && s[i] && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') {
        tmp[i] = (char)tolower((unsigned char)s[i]);
        i++;
    }
    tmp[i] = '\0';
    return (strcmp(tmp, "quit") == 0) || (strcmp(tmp, "exit") == 0);
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        sent += (size_t)n;
    }
    return 0;
}

static void handle_client_loop(int fd, const struct sockaddr_in *cli_addr) {
    char buffer[256];

    printf("[pid %ld] connected: %s:%u\n",
           (long)getpid(),
           inet_ntoa(cli_addr->sin_addr),
           ntohs(cli_addr->sin_port));
    fflush(stdout);

    while (1) {
        memset(buffer, 0, sizeof(buffer));

        ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            die("ERROR reading from socket");
        }
        if (n == 0) {
            /* client closed connection */
            printf("[pid %ld] client disconnected: %s:%u\n",
                   (long)getpid(),
                   inet_ntoa(cli_addr->sin_addr),
                   ntohs(cli_addr->sin_port));
            fflush(stdout);
            break;
        }

        /* Ensure buffer is NUL-terminated (read already left room) */
        buffer[n] = '\0';

        printf("[pid %ld] msg from %s:%u -> %s",
               (long)getpid(),
               inet_ntoa(cli_addr->sin_addr),
               ntohs(cli_addr->sin_port),
               buffer);
        if (buffer[n - 1] != '\n') printf("\n");
        fflush(stdout);

        if (is_quit_cmd(buffer)) {
            const char *bye = "Bye.\n";
            if (send_all(fd, bye, strlen(bye)) < 0) {
                perror("ERROR writing to socket");
            }
            printf("[pid %ld] client disconnected (quit/exit): %s:%u\n",
            (long)getpid(),
            inet_ntoa(cli_addr->sin_addr),
            ntohs(cli_addr->sin_port));
            fflush(stdout);
            break;
        }

        const char *reply = "I got your message\n";
        if (send_all(fd, reply, strlen(reply)) < 0) {
            die("ERROR writing to socket");
        }
    }
}

int main(int argc, char *argv[]) {
    int sockfd;
    int portno;
    struct sockaddr_in serv_addr;

    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }
    portno = atoi(argv[1]);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) die("ERROR opening socket");

    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        die("ERROR setsockopt(SO_REUSEADDR)");
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons((uint16_t)portno);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) die("ERROR on binding");
    if (listen(sockfd, 5) < 0) die("ERROR on listen");

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = reap_children;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGCHLD, &sa, NULL) < 0) die("ERROR sigaction(SIGCHLD)");

    printf("Server listening on port %d (pid %ld)\n", portno, (long)getpid());
    fflush(stdout);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t clilen = sizeof(cli_addr);

        int newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            if (errno == EINTR) continue;
            die("ERROR on accept");
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("ERROR on fork");
            close(newsockfd);
            continue;
        }

        if (pid == 0) {
            close(sockfd);
            handle_client_loop(newsockfd, &cli_addr);
            close(newsockfd);
            _exit(0);
        }

        close(newsockfd);
    }

    return 0;
}
