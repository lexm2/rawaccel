#ifndef RAWACCEL_CONFIG_H
#define RAWACCEL_CONFIG_H

#include "rawaccel-types.h"

struct rawaccel_config_server;
struct rawaccel_device;

// Callback when configuration is updated
typedef void (*rawaccel_config_callback_t)(struct rawaccel_device *dev,
                                           const struct rawaccel_device_config *config,
                                           void *userdata);

// Create Unix socket server
struct rawaccel_config_server *rawaccel_config_server_create(
    const char *socket_path,
    rawaccel_config_callback_t callback,
    void *userdata
);

// Destroy server
void rawaccel_config_server_destroy(struct rawaccel_config_server *server);

// Get server file descriptor for epoll
int rawaccel_config_server_get_fd(struct rawaccel_config_server *server);

// Process incoming data
int rawaccel_config_server_process(struct rawaccel_config_server *server);

#endif // RAWACCEL_CONFIG_H
