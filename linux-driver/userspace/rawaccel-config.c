#include "rawaccel-config.h"
#include "rawaccel-device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <errno.h>

#define MAX_CLIENTS 8

// Forward declaration of daemon structure from rawaccel-daemon.c
struct rawaccel_daemon {
    int epoll_fd;
    struct rawaccel_device *devices[16];  // MAX_DEVICES
    int num_devices;
    struct rawaccel_config_server *config_server;
    char socket_path[256];
};

struct rawaccel_config_server {
    int listen_fd;
    int client_fds[MAX_CLIENTS];
    int epoll_fd;
    char socket_path[256];
    rawaccel_config_callback_t callback;
    void *userdata;
};

struct rawaccel_config_server *rawaccel_config_server_create(
    const char *socket_path,
    int epoll_fd,
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
    server->epoll_fd = epoll_fd;
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

    // Set socket permissions so non-root users can connect
    if (chmod(socket_path, 0666) < 0) {
        fprintf(stderr, "Failed to set socket permissions: %s\n", strerror(errno));
        // Continue anyway - this is not fatal
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

bool rawaccel_config_server_is_client(struct rawaccel_config_server *server, int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->client_fds[i] == fd) {
            return true;
        }
    }
    return false;
}

static int handle_config_message(struct rawaccel_config_server *server,
                                 const struct rawaccel_ipc_message *msg,
                                 const void *payload,
                                 int client_fd) {
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
    } else if (msg->command == RAWACCEL_CMD_GET_SPEED) {
        printf("[CONFIG] Handling GET_SPEED command\n");

        // Get daemon pointer to access devices
        struct rawaccel_daemon *daemon = server->userdata;
        struct rawaccel_speed_telemetry telem = {0};

        printf("[CONFIG] Daemon ptr: %p, num_devices: %d\n",
               (void*)daemon, daemon ? daemon->num_devices : -1);

        // Get speed from first active device (or aggregate from all)
        if (daemon && daemon->num_devices > 0) {
            struct rawaccel_device *dev = daemon->devices[0];
            printf("[CONFIG] Device ptr: %p\n", (void*)dev);
            if (dev) {
                telem.speed = dev->telemetry.last_speed;
                telem.speed_x = dev->telemetry.last_speed_x;
                telem.speed_y = dev->telemetry.last_speed_y;
                printf("[CONFIG] Telemetry: speed=%.2f, x=%.2f, y=%.2f\n",
                       telem.speed, telem.speed_x, telem.speed_y);
            }
        }

        // Send telemetry response (skip ACK)
        printf("[CONFIG] Sending telemetry response (%zu bytes) to fd=%d\n",
               sizeof(telem), client_fd);
        ssize_t sent = send(client_fd, &telem, sizeof(telem), 0);
        printf("[CONFIG] Sent %zd bytes\n", sent);

        return 1;  // Special return code to skip ACK
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

                // Add client to epoll
                struct epoll_event ev = {0};
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                    fprintf(stderr, "Failed to add client to epoll: %s\n", strerror(errno));
                    close(client_fd);
                    server->client_fds[i] = -1;
                }
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
        ssize_t n = recv(fd, &msg, sizeof(msg), 0);  // Blocking recv since epoll triggered

        printf("[CONFIG] Received %zd bytes from client fd=%d\n", n, fd);

        if (n == sizeof(msg)) {
            printf("[CONFIG] Message: magic=0x%x, version=%u, command=%u, payload_size=%u\n",
                   msg.magic, msg.version, msg.command, msg.payload_size);
            // Validate magic and version
            if (msg.magic != RAWACCEL_IPC_MAGIC) {
                fprintf(stderr, "Invalid magic: 0x%x\n", msg.magic);
                epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
                server->client_fds[i] = -1;
                continue;
            }

            if (msg.version != RAWACCEL_IPC_VERSION) {
                fprintf(stderr, "Invalid version: %u\n", msg.version);
                epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
                server->client_fds[i] = -1;
                continue;
            }

            // Read payload
            void *payload = NULL;
            if (msg.payload_size > 0) {
                payload = malloc(msg.payload_size);
                if (!payload) {
                    epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
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
                    epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    server->client_fds[i] = -1;
                    continue;
                }
            }

            // Handle message
            int rc = handle_config_message(server, &msg, payload, fd);

            // Send ACK (unless handler returned 1, meaning it sent its own response)
            if (rc != 1) {
                uint32_t ack = (rc == 0) ? 1 : 0;
                send(fd, &ack, sizeof(ack), 0);
            }

            free(payload);
        } else if (n == 0) {
            // Client disconnected
            printf("Client disconnected (fd=%d)\n", fd);
            epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            server->client_fds[i] = -1;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // Error
            fprintf(stderr, "Error reading from client: %s\n", strerror(errno));
            epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            server->client_fds[i] = -1;
        }
    }

    return 0;
}
