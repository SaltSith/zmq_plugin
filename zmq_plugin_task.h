#pragma once

#include <pthread.h>

#include <stdbool.h>

int zmq_plugin_task_init(void);
pthread_t zmq_plugin_task_handle_get(void);
void zmq_plugin_task_cleanup_handler(void *arg);
bool zmq_plugin_task_send_msg(void *msg);