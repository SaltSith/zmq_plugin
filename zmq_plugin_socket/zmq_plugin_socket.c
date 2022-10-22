#include "zmq_plugin_socket.h"

#include <czmq.h>
#include <zmq.h>

#include <stdio.h>

#include <string.h>
#include <unistd.h>

typedef struct {
    void *socket_context;
    void *socket;
    zpoller_t *socket_poller;
    socket_role_t role;
} plugin_ctx_type_t;

static plugin_ctx_type_t plugin_ctx;

int zmq_plugin_socket_init(const socket_role_t role, const char *addr)
{
    if ((addr == NULL) || (role == SOCKET_ROLE_LAST)) {
        return -1;
    }

    plugin_ctx.role = role;

    plugin_ctx.socket_context = zmq_ctx_new();
    if (plugin_ctx.socket_context == NULL) {
        return -2;
    }

    plugin_ctx.socket = zmq_socket(plugin_ctx.socket_context, ZMQ_PAIR);
    if (plugin_ctx.socket == NULL) {
        return -3;
    }

    int result = -1;

    if (plugin_ctx.role == REQUESTER) {
        result = zmq_connect(plugin_ctx.socket, addr);
    } else {
        result = zmq_bind(plugin_ctx.socket, addr);
    }

    if (result) {
    	return -4;
    }

    plugin_ctx.socket_poller = zpoller_new(plugin_ctx.socket, NULL);

    if (plugin_ctx.socket_poller == NULL) {
        return -5;
    }

    return 0;
}

int zmq_plugin_socket_destroy(void)
{
    int result = zmq_close(plugin_ctx.socket);

    result += zmq_ctx_destroy(plugin_ctx.socket_context);
    zpoller_destroy((zpoller_t **)&plugin_ctx.socket_poller);

    return result;
}

int zmq_plugin_socket_send_message(const uint8_t *message, const int len)
{
    if (message == NULL) {
        return -1;
    }

    return zmq_send (plugin_ctx.socket, message, len, ZMQ_DONTWAIT);
}

void *zmq_plugin_socket_get(void)
{
    return plugin_ctx.socket;
}
