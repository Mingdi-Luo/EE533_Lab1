/*
 * TCP client with interactive loop (send multiple messages)
 *
 * Build: gcc -Wall -Wextra -O2 client_loop.c -o client
 * Run:   ./client <server_ip_or_hostname> <port>
 *
 * Usage:
 * - Type a line and press Enter to send
 * - Type "quit" or "exit" to close the connection
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static int is_quit_cmd(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s == '\0') return 0;

    char tmp[8];
    size_t i = 0;
    while (i < sizeof(tmp) - 1 && s[i] && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        tmp[i] = c;
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

static ssize_t recv_some(int fd, char *buf, size_t cap) {
    while (1) {
        ssize_t n = read(fd, buf, cap);
        if (n < 0 && errno == EINTR) continue;
        return n;
    }
}

int main(int argc, char *argv[]) {
    int sockfd;
    int portno;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];
    char reply[256];

    if (argc < 3) {
        fprintf(stderr, "usage: %s hostname port\n", argv[0]);
        exit(1);
    }

    portno = atoi(argv[2]);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) die("ERROR opening socket");

    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host\n");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, (size_t)server->h_length);
    serv_addr.sin_port = htons((uint16_t)portno);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        die("ERROR connecting");
    }

    printf("Connected. Type messages; 'quit' or 'exit' to close.\n");

    while (1) {
        printf("> ");
        fflush(stdout);

        memset(buffer, 0, sizeof(buffer));
        if (fgets(buffer, (int)sizeof(buffer) - 1, stdin) == NULL) {
            /* EOF */
            break;
        }

        if (send_all(sockfd, buffer, strlen(buffer)) < 0) {
            die("ERROR writing to socket");
        }

        memset(reply, 0, sizeof(reply));
        ssize_t n = recv_some(sockfd, reply, sizeof(reply) - 1);
        if (n < 0) die("ERROR reading from socket");
        if (n == 0) {
            printf("(server closed connection)\n");
            break;
        }
        reply[n] = '\0';
        printf("%s", reply);
        if (reply[n - 1] != '\n') printf("\n");

        if (is_quit_cmd(buffer)) {
            break;
        }
    }

    close(sockfd);
    return 0;
}
