#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>
#include <dirent.h>

#include "rawaccel-device.h"
#include "rawaccel-config.h"
#include "rawaccel-types.h"

#define MAX_DEVICES 16
#define MAX_EVENTS 32
#define DEFAULT_SOCKET_PATH "/tmp/rawaccel.sock"
#define INPUT_DIR "/dev/input"

static volatile sig_atomic_t running = 1;

struct rawaccel_daemon {
    int epoll_fd;
    struct rawaccel_device *devices[MAX_DEVICES];
    int num_devices;
    struct rawaccel_config_server *config_server;
    char socket_path[256];
};

static void signal_handler(int signo) {
    (void)signo;
    running = 0;
}

static void config_update_callback(struct rawaccel_device *dev,
                                   const struct rawaccel_device_config *config,
                                   void *userdata) {
    (void)dev;  // Unused
    struct rawaccel_daemon *daemon = userdata;

    printf("Received config update: enabled=%d, num_points=%u\n",
           config->enabled, config->lut_x.num_points);

    // Apply to all devices
    for (int i = 0; i < daemon->num_devices; i++) {
        if (daemon->devices[i]) {
            rawaccel_device_update_config(daemon->devices[i], config);
        }
    }
}

static int scan_and_add_devices(struct rawaccel_daemon *daemon) {
    DIR *dir = opendir(INPUT_DIR);
    if (!dir) {
        fprintf(stderr, "Failed to open %s: %s\n", INPUT_DIR, strerror(errno));
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, entry->d_name);

        // Check if it's a mouse
        if (!rawaccel_device_is_mouse(path)) {
            continue;
        }

        // Check if already added
        bool already_added = false;
        for (int i = 0; i < daemon->num_devices; i++) {
            if (daemon->devices[i] &&
                strcmp(daemon->devices[i]->path, path) == 0) {
                already_added = true;
                break;
            }
        }

        if (already_added) {
            continue;
        }

        // Create device
        struct rawaccel_device *dev = rawaccel_device_create(path);
        if (!dev) {
            continue;
        }

        // Add to daemon
        if (daemon->num_devices >= MAX_DEVICES) {
            fprintf(stderr, "Maximum devices reached\n");
            rawaccel_device_destroy(dev);
            continue;
        }

        daemon->devices[daemon->num_devices++] = dev;

        // Add to epoll
        struct epoll_event ev = {0};
        ev.events = EPOLLIN;
        ev.data.fd = dev->fd;

        if (epoll_ctl(daemon->epoll_fd, EPOLL_CTL_ADD, dev->fd, &ev) < 0) {
            fprintf(stderr, "Failed to add device to epoll: %s\n",
                    strerror(errno));
            rawaccel_device_destroy(dev);
            daemon->num_devices--;
        }
    }

    closedir(dir);
    printf("Found %d mouse device(s)\n", daemon->num_devices);

    return 0;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Options:\n");
    printf("  --socket PATH    Unix socket path (default: %s)\n",
           DEFAULT_SOCKET_PATH);
    printf("  --help           Show this help\n");
}

int main(int argc, char **argv) {
    struct rawaccel_daemon daemon = {0};
    strncpy(daemon.socket_path, DEFAULT_SOCKET_PATH,
            sizeof(daemon.socket_path) - 1);

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            strncpy(daemon.socket_path, argv[++i],
                   sizeof(daemon.socket_path) - 1);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("RawAccel Userspace Daemon\n");
    printf("Socket: %s\n", daemon.socket_path);

    // Create epoll
    daemon.epoll_fd = epoll_create1(0);
    if (daemon.epoll_fd < 0) {
        fprintf(stderr, "Failed to create epoll: %s\n", strerror(errno));
        return 1;
    }

    // Create config server
    daemon.config_server = rawaccel_config_server_create(
        daemon.socket_path,
        daemon.epoll_fd,
        config_update_callback,
        &daemon
    );

    if (!daemon.config_server) {
        fprintf(stderr, "Failed to create config server\n");
        close(daemon.epoll_fd);
        return 1;
    }

    // Add config server to epoll
    struct epoll_event ev = {0};
    ev.events = EPOLLIN;
    ev.data.fd = rawaccel_config_server_get_fd(daemon.config_server);

    if (epoll_ctl(daemon.epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0) {
        fprintf(stderr, "Failed to add config server to epoll: %s\n",
                strerror(errno));
        goto cleanup;
    }

    // Scan for devices
    if (scan_and_add_devices(&daemon) < 0) {
        fprintf(stderr, "Failed to scan devices\n");
        goto cleanup;
    }

    if (daemon.num_devices == 0) {
        fprintf(stderr, "No mouse devices found\n");
        goto cleanup;
    }

    printf("Daemon started, processing events...\n");

    // Main event loop
    struct epoll_event events[MAX_EVENTS];
    while (running) {
        int nfds = epoll_wait(daemon.epoll_fd, events, MAX_EVENTS, 1000);

        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "epoll_wait failed: %s\n", strerror(errno));
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            // Check if it's the config server listen socket
            if (fd == rawaccel_config_server_get_fd(daemon.config_server)) {
                rawaccel_config_server_process(daemon.config_server);
            } else if (rawaccel_config_server_is_client(daemon.config_server, fd)) {
                // It's a client connection
                rawaccel_config_server_process(daemon.config_server);
            } else {
                // It's a device - find it in our device list
                struct rawaccel_device *dev = NULL;
                for (int j = 0; j < daemon.num_devices; j++) {
                    if (daemon.devices[j] && daemon.devices[j]->fd == fd) {
                        dev = daemon.devices[j];
                        break;
                    }
                }

                if (dev) {
                    if (rawaccel_device_process_event(dev) < 0) {
                        fprintf(stderr, "Error processing event from %s\n",
                                dev->name);
                    }
                }
            }
        }
    }

    printf("Shutting down...\n");

cleanup:
    // Cleanup devices
    for (int i = 0; i < daemon.num_devices; i++) {
        if (daemon.devices[i]) {
            rawaccel_device_destroy(daemon.devices[i]);
        }
    }

    // Cleanup config server
    if (daemon.config_server) {
        rawaccel_config_server_destroy(daemon.config_server);
    }

    close(daemon.epoll_fd);

    return 0;
}
