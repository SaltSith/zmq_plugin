add_library(zmq_plugin zmq_plugin_task.h zmq_plugin_task.c)

include_directories ("${PROJECT_SOURCE_DIR}/")
target_link_libraries(zmq_plugin postman)
