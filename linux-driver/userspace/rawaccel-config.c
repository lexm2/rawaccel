#include "rawaccel-config.h"
#include "rawaccel-device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#define MAX_CLIENTS 8

struct rawaccel_config_server {
    int listen_fd;
    int client_fds[MAX_CLIENTS];
    char socket_path[256];
    rawaccel_config_callback_t callback;
    void *userdata;
};

struct rawaccel_config_server *rawaccel_config_server_create(
    const char *socket_path,
    rawaccel_config_callback_t callback,
    void *userdata
) {
    struct rawaccel_config_server *server = calloc(1, sizeof(*server));
    if (!server) {
        return NULL;
    }

    server->callback = callback;
    server->userdata = userdata;
    server->listen_fd = -1;
    strncpy(server->socket_path, socket_path, sizeof(server->socket_path) - 1);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        server->client_fds[i] = -1;
    }

    // Remove existing socket file
    unlink(socket_path);

    // Create socket
    server->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server->listen_fd < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        goto error;
    }

    // Bind
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to bind socket to %s: %s\n",
                socket_path, strerror(errno));
        goto error;
    }

    // Listen
    if (listen(server->listen_fd, 5) < 0) {
        fprintf(stderr, "Failed to listen on socket: %s\n", strerror(errno));
        goto error;
    }

    printf("Config server listening on %s\n", socket_path);

    return server;

error:
    rawaccel_config_server_destroy(server);
    return NULL;
}

void rawaccel_config_server_destroy(struct rawaccel_config_server *server) {
    if (!server) {
        return;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->client_fds[i] >= 0) {
            close(server->client_fds[i]);
        }
    }

    if (server->listen_fd >= 0) {
        close(server->listen_fd);
    }

    unlink(server->socket_path);
    free(server);
}

int rawaccel_config_server_get_fd(struct rawaccel_config_server *server) {
    return server->listen_fd;
}

static int handle_config_message(struct rawaccel_config_server *server,
                                 const struct rawaccel_ipc_message *msg,
                                 const void *payload) {
    if (msg->command == RAWACCEL_CMD_UPDATE_CONFIG) {
        const struct rawaccel_device_config *config = payload;

        // Call callback with config
        if (server->callback) {
            server->callback(server->userdata, config, server->userdata);
        }

        return 0;
    } else if (msg->command == RAWACCEL_CMD_DISABLE) {
        struct rawaccel_device_config config = {0};
        config.enabled = false;

        if (server->callback) {
            server->callback(server->userdata, &config, server->userdata);
        }

        return 0;
    }

    return -1;
}

int rawaccel_config_server_process(struct rawaccel_config_server *server) {
    // Accept new connections
    int client_fd = accept(server->listen_fd, NULL, NULL);
    if (client_fd >= 0) {
        // Find empty slot
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (server->client_fds[i] < 0) {
                server->client_fds[i] = client_fd;
                printf("Accepted new client connection (fd=%d)\n", client_fd);
                break;
            }
        }
    }

    // Process data from existing clients
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->client_fds[i] < 0) {
            continue;
        }

        int fd = server->client_fds[i];

        // Read message header
        struct rawaccel_ipc_message msg;
        ssize_t n = recv(fd, &msg, sizeof(msg), MSG_DONTWAIT);

        if (n == sizeof(msg)) {
            // Validate magic and version
            if (msg.magic != RAWACCEL_IPC_MAGIC) {
                fprintf(stderr, "Invalid magic: 0x%x\n", msg.magic);
                close(fd);
                server->client_fds[i] = -1;
                continue;
            }

            if (msg.version != RAWACCEL_IPC_VERSION) {
                fprintf(stderr, "Invalid version: %u\n", msg.version);
                close(fd);
                server->client_fds[i] = -1;
                continue;
            }

            // Read payload
            void *payload = NULL;
            if (msg.payload_size > 0) {
                payload = malloc(msg.payload_size);
                if (!payload) {
                    close(fd);
                    server->client_fds[i] = -1;
                    continue;
                }

                ssize_t total = 0;
                while (total < msg.payload_size) {
                    n = recv(fd, (char *)payload + total,
                            msg.payload_size - total, 0);
                    if (n <= 0) {
                        break;
                    }
                    total += n;
                }

                if (total != msg.payload_size) {
                    fprintf(stderr, "Failed to read full payload\n");
                    free(payload);
                    close(fd);
                    server->client_fds[i] = -1;
                    continue;
                }
            }

            // Handle message
            int rc = handle_config_message(server, &msg, payload);

            // Send ACK
            uint32_t ack = (rc == 0) ? 1 : 0;
            send(fd, &ack, sizeof(ack), 0);

            free(payload);
        } else if (n == 0) {
            // Client disconnected
            printf("Client disconnected (fd=%d)\n", fd);
            close(fd);
            server->client_fds[i] = -1;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // Error
            fprintf(stderr, "Error reading from client: %s\n", strerror(errno));
            close(fd);
            server->client_fds[i] = -1;
        }
    }

    return 0;
}
