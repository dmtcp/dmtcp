#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <assert.h>
#define SOCKET_NAME "/tmp/seqpacket_sock"
#define MAX_MSG_SIZE 256

// Use a pipe to signal the client to start.
int pipefd[2];
int start = 0;

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int server(void)
{
    struct sockaddr_un name;
    int connection_socket, data_socket;
    char buffer[MAX_MSG_SIZE];
    int ret;

    /* 1. Create socket */
    connection_socket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (connection_socket == -1) {
        error("socket");
    }

    /* Remove existing socket file if it exists */
    unlink(SOCKET_NAME);

    /* 2. Bind socket to a name (file path) */
    memset(&name, 0, sizeof(name));
    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, SOCKET_NAME, sizeof(name.sun_path) - 1);

    ret = bind(connection_socket, (const struct sockaddr *) &name, sizeof(name));
    if (ret == -1) {
        error("bind");
    }

    close(pipefd[0]);
    assert(write(pipefd[1], &start, 1) == 1);

    printf("Server: Listening at %s...\n", SOCKET_NAME);

    /* 3. Prepare for accepting connections (backlog size 5) */
    ret = listen(connection_socket, 5);
    if (ret == -1) {
        error("listen");
    }

    /* 4. Accept connection and receive message */
        printf("Server: Waiting for connection...\n");

        /* Accept a connection */
        data_socket = accept(connection_socket, NULL, NULL);
        if (data_socket == -1) {
            error("accept");
        }

        printf("Server: Client connected. Receiving data...\n");

    /* Main loop to receive and send messages */
    for (;;) {
        /* Receive a message */
        memset(buffer, 0, sizeof(buffer));
        ret = recv(data_socket, buffer, MAX_MSG_SIZE, 0);
        if (ret == -1) {
            error("Server: recv");
        } else if (ret == 0) {
            printf("Server: Client closed connection.\n");
            break;
        } else {
            printf("Server: Received message (%d bytes): '%s'\n", ret, buffer);
        }

        // sleep 1 second
        sleep(1);
        /* Send a response message */
        const char *response = "Server received message.";
        ret = send(data_socket, response, strlen(response) + 1, 0);
        if (ret == -1) {
            error("Server: send");
        } else {
            printf("Server: Sent response: '%s'\n", response);
        }
    }

    /* Close the connection socket (unreachable in this infinite loop) */
    close(connection_socket);
    unlink(SOCKET_NAME);
    return 0;
}

int client(void)
{
    struct sockaddr_un name;
    int data_socket;
    char buffer[MAX_MSG_SIZE];
    int ret;

    /* 1. Create socket */
    data_socket = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (data_socket == -1) {
        error("socket");
    }

    /* 2. Connect to server socket */
    memset(&name, 0, sizeof(name));
    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, SOCKET_NAME, sizeof(name.sun_path) - 1);

    // Wait for the server to start.
    assert(read(pipefd[0], &start, 1) == 1);
    close(pipefd[0]);
    close(pipefd[1]);

    printf("Client: Connecting to server at %s...\n", SOCKET_NAME);
    ret = connect(data_socket, (const struct sockaddr *) &name, sizeof(name));
    if (ret == -1) {
        error("Client: connect");
    }

    printf("Client: Connected. Sending messages...\n");

    /* 3. Send and receive messages in a loop */
    int count = 0;
    while (1) {
        char msg[MAX_MSG_SIZE];
        sprintf(msg, "This is message %d.", count);
        ret = send(data_socket, msg, strlen(msg) + 1, 0);
        if (ret == -1) {
            error("Client: send");
        }
        printf("Client: Sent message (%d bytes).\n", ret);

        /* 5. Wait for server response */
        memset(buffer, 0, sizeof(buffer));
        ret = recv(data_socket, buffer, MAX_MSG_SIZE, 0);
        if (ret == -1) {
            error("Client: recv response");
        } else if (ret == 0) {
            printf("Client: Server closed connection.\n");
        } else {
            printf("Client: Received response (%d bytes): '%s'\n", ret, buffer);
        }
        count++;
    }

    /* 6. Close the socket */
    close(data_socket);
    printf("Client: Connection closed.\n");

    return 0;
}

int main(void)
{
    assert(pipe(pipefd) == 0);
    pid_t pid = fork();
    assert(pid != -1);
    if (pid == 0) {
        printf("Server: Starting server\n");
        server();
    } else {
        printf("Client: Starting client\n");
        client();
    }
    return 0;
}