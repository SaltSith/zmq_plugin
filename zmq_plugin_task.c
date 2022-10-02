#include "zmq_plugin_task.h"

#include "zmq_plugin_socket/zmq_plugin_socket.h"

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
#define POLL_TIMEOUT            POLL_TIMEOUT_DEFAULT


typedef enum {
    PLUGIN_EVENT_SEND_MESSAGE = 0,
    PLUGIN_EVENT_LAST
} zmq_plugin_task_event_type_t;

typedef struct {
    zmq_plugin_task_event_type_t event;
    void *args;
} zmq_plugin_task_event_t;


static bool plugin_ready;
static pthread_t zmq_plugin_task_handle;
static int zmq_plugin_task_queue = -1;

static zmq_pollitem_t items[2];


typedef void(*zmq_plugin_task_event_handler)(void *args);

static void zmq_plugin_task_event_handler_send_msg(void *args);

zmq_plugin_task_event_handler zmq_plugin_task_event_handlers[PLUGIN_EVENT_LAST] = {
    zmq_plugin_task_event_handler_send_msg,
};

static void
zmq_plugin_task_event_handler_send_msg(void *args)
{

    printf("Sending message from handler\r\n");
}


static void *zmq_plugin_task(void *arg);

static int zmq_plugin_task_queue_init(void);

static void zmq_plugin_task_check_queues(void);

static void zmq_plugin_task_pop_message(void);


static void 
*zmq_plugin_task(void *arg)
{
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(zmq_plugin_task_queue, &rfds);

    pthread_cleanup_push(zmq_plugin_task_cleanup_handler, NULL);

    plugin_ready = true;

    zmq_plugin_socket_send_message(wakeup_message, strlen(wakeup_message));

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

		zmq_plugin_socket_send_message ("Kenobi Req\0", strlen("Kenobi Req\0"));

		sleep(1);

        printf("-------------------Plugin task loop--------------\r\n");
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

        printf("Data is available on plugin task queue.\r\n");
        zmq_plugin_task_pop_message();
    }

    if (items[1].revents & ZMQ_POLLIN) {
        items[1].revents &= ~ZMQ_POLLIN;

        char buffer [100];
        memset(buffer, '\0', sizeof(buffer));
        zmq_recv(zmq_plugin_socket_get(), buffer, 100, ZMQ_DONTWAIT);
        printf("GET: %s \r\n", buffer);

		if (memcmp(buffer, wakeup_message, strlen(wakeup_message)) == 0) {
			printf("------ PARTNER RESTART------------\r\n");
		}

		if (memcmp(buffer, "ACK\0", strlen("ACK\0")) == 0) {

		} else {
			zmq_plugin_socket_send_message ("ACK\0", strlen("ACK\0"));
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
zmq_plugin_task_send_msg(void *msg)
{
    if (msg == NULL) {
        return false;
    }

    zmq_plugin_task_event_t event = {
        .args  = msg,
        .event = PLUGIN_EVENT_SEND_MESSAGE
    };

    while (!plugin_ready);

    int result = mq_send(zmq_plugin_task_queue,
                         (char *)&event,
                         sizeof(zmq_plugin_task_event_t),
                         0);

    return result == 0 ? true : false;
}
