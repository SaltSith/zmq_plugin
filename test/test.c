#include "../../acutest.h"

#include "../zmq_plugin_task.h"

#include "string.h"

void test_zmq_plugin_task_init(void) {
    TEST_ASSERT(zmq_plugin_task_init() == 0);
}

void test_zmq_plugin_task_task_handle_get(void) {
    int result = zmq_plugin_task_init();
    TEST_ASSERT(result == 0);

    pthread_t pthread_ptr = zmq_plugin_task_handle_get();
    TEST_ASSERT(pthread_ptr != (pthread_t)NULL);
}

void test_zmq_plugin_task_send_message_NULL_MESSAGE_PTR(void) {
    int result = zmq_plugin_task_init();
    TEST_ASSERT(result == 0);

    pthread_t pthread_ptr = zmq_plugin_task_handle_get();
    TEST_ASSERT(pthread_ptr != (pthread_t)NULL);

    TEST_ASSERT(zmq_plugin_task_send_msg(NULL, 10) == false);
}

void test_zmq_plugin_task_send_message(void) {
    int result = zmq_plugin_task_init();
    TEST_ASSERT(result == 0);

    pthread_t pthread_ptr = zmq_plugin_task_handle_get();
    TEST_ASSERT(pthread_ptr != (pthread_t)NULL);

    TEST_ASSERT(zmq_plugin_task_send_msg("Hello\0", strlen("Hello\0")) == true);
}


TEST_LIST = {
	{"test_zmq_plugin_task_init", test_zmq_plugin_task_init},
	{"test_zmq_plugin_task_task_handle_get", test_zmq_plugin_task_task_handle_get},
	{"test_zmq_plugin_task_send_message_NULL_MESSAGE_PTR", test_zmq_plugin_task_send_message_NULL_MESSAGE_PTR},
	{"test_zmq_plugin_task_send_message", test_zmq_plugin_task_send_message},
	{0} /* must be terminated with 0 */
};