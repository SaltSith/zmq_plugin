#include "zmq_plugin_task.h"

#include "zmq_plugin_socket/zmq_plugin_socket.h"
#include "postman/postman.h"

#include "../config.h"

#include <mqueue.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <zmq.h>
#include <czmq.h>


#define POLL_TIMEOUT_DEFAULT    (50)
#define POLL_TIMEOUT_INFINITY   (-1)
#define POLL_TIMEOUT            POLL_TIMEOUT_INFINITY

#define ZMQ_RECEIVE_BUFFER_SIZE 1024


typedef enum {
    PLUGIN_EVENT_SEND_MESSAGE = 0,
    PLUGIN_EVENT_LAST
} zmq_plugin_task_event_type_t;

typedef struct {
    uint8_t *msg;
    size_t len;
} msg_info;

typedef struct {
    zmq_plugin_task_event_type_t event;
    msg_info args;
} zmq_plugin_task_event_t;

typedef struct {
    uint64_t restarts;
    bool is_alive;
    bool connected_before;
} partner_info_t;

static partner_info_t partner_info = {.restarts = 0, .is_alive = false, .connected_before = false};

static uint8_t *in_message;

static bool plugin_ready;
static pthread_t zmq_plugin_task_handle;
static int zmq_plugin_task_queue = -1;

static zmq_pollitem_t items[2];


typedef void(*zmq_plugin_task_event_handler)(msg_info args);

static void zmq_plugin_task_event_handler_send_msg(msg_info args);

zmq_plugin_task_event_handler zmq_plugin_task_event_handlers[PLUGIN_EVENT_LAST] = {
    zmq_plugin_task_event_handler_send_msg,
};

static void
zmq_plugin_task_event_handler_send_msg(msg_info args)
{

    zmq_plugin_socket_send_message(args.msg, args.len);

    free(args.msg);
}


static void *zmq_plugin_task(void *arg);

static int zmq_plugin_task_queue_init(void);

static void zmq_plugin_task_check_queues(void);

static void zmq_plugin_task_pop_message(void);

bool parter_live = false;

static void 
*zmq_plugin_task(void *arg)
{
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(zmq_plugin_task_queue, &rfds);

    pthread_cleanup_push(zmq_plugin_task_cleanup_handler, NULL);

    plugin_ready = true;

    sleep(1);
    uint8_t *hello_msg_buff = malloc(sizeof(uint8_t) * ZMQ_RECEIVE_BUFFER_SIZE);
    assert(hello_msg_buff != NULL);

    size_t msg_len = postman_hello_message(hello_msg_buff, ZMQ_RECEIVE_BUFFER_SIZE);
    int res = zmq_plugin_socket_send_message(hello_msg_buff, msg_len);
    (void)res;

    items[0].socket = NULL;
    items[0].fd     = zmq_plugin_task_queue;
    items[0].events = ZMQ_POLLIN;

    items[1].socket = zmq_plugin_socket_get();
    items[1].events = ZMQ_POLLIN;

    while (1) {
    	int rc;
    	do {
			rc = zmq_poll(items, sizeof(items) / sizeof(items[0]), POLL_TIMEOUT);

			if (rc > 0) {
				zmq_plugin_task_check_queues();
			}
    	} while(rc > 0);
    }
    
    pthread_cleanup_pop(NULL);
    
    return NULL;
}

static int
zmq_plugin_task_queue_init(void)
{
    struct mq_attr attr;

    attr.mq_flags   = 0;
    attr.mq_maxmsg  = 10;
    attr.mq_msgsize = sizeof(zmq_plugin_task_event_t);
    attr.mq_curmsgs = 0;

    zmq_plugin_task_queue = mq_open(zmq_plugin_task_QUEUE_NAME,
                                    O_CREAT | O_NONBLOCK | O_RDWR,
                                    0777,
                                    &attr);

    return zmq_plugin_task_queue;
}

static void
zmq_plugin_task_check_queues(void)
{
    if (items[0].revents & ZMQ_POLLIN) {
        items[0].revents &= ~ZMQ_POLLIN;

        zmq_plugin_task_pop_message();
    }

    if (items[1].revents & ZMQ_POLLIN) {
        items[1].revents &= ~ZMQ_POLLIN;
        parter_live = true;
        const size_t message_buffer_size = sizeof(uint8_t) * ZMQ_RECEIVE_BUFFER_SIZE;

        in_message = malloc(message_buffer_size);

        assert(in_message != NULL);
        memset(in_message, '\0', message_buffer_size);

        size_t msg_len = zmq_recv(zmq_plugin_socket_get(), in_message, message_buffer_size, ZMQ_DONTWAIT);
        if (postman_send_message(in_message, msg_len) == 1) {
            partner_info.is_alive = true;
            if (!partner_info.connected_before) {
                partner_info.connected_before = true;
                printf("Partner first connection.\r\n");
                return;
            }

            partner_info.restarts++;
            printf("Partner %ld restart.\r\n", partner_info.restarts);
        }
    }
}

static void
zmq_plugin_task_pop_message(void)
{
    unsigned int msg_prio;

    zmq_plugin_task_event_t event;

    int msg_len = sizeof(zmq_plugin_task_event_t);

    int result = mq_receive(zmq_plugin_task_queue,
                            (char *)&event,
                            msg_len,
                            &msg_prio);

    if (result > 0) {
        zmq_plugin_task_event_handlers[event.event](event.args);
    }
}

int
zmq_plugin_task_init(void)
{

    if (zmq_plugin_socket_init(PLUGIN_ROLE, PLUGIN_ADDR)) {
        return -1;
    }

    int result = zmq_plugin_task_queue_init();

    if (result == -1) {
        return result;
    }

    result = pthread_create(&zmq_plugin_task_handle,
                            NULL,
                            zmq_plugin_task,
                            "zmq_plugin_task");

    return result;
}

pthread_t 
zmq_plugin_task_handle_get(void)
{

    return zmq_plugin_task_handle;
}

void
zmq_plugin_task_cleanup_handler(void *arg)
{
    mq_unlink(zmq_plugin_task_QUEUE_NAME);

    zmq_plugin_socket_destroy();
}

bool
zmq_plugin_task_send_msg(uint8_t *msg, size_t len)
{
    if (msg == NULL) {
        return false;
    }

    zmq_plugin_task_event_t event = {
        .args = {.msg = msg,
                   .len = len},
        .event = PLUGIN_EVENT_SEND_MESSAGE
    };

    while (!plugin_ready);

    int result = mq_send(zmq_plugin_task_queue,
                         (char *)&event,
                         sizeof(zmq_plugin_task_event_t),
                         0);

    return result == 0 ? true : false;
}