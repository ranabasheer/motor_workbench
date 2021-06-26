/*
 * serial_log.h
 *
 *  Created on: Mar 12, 2021
 *      Author: RanaBasheer
 */

#ifndef SERIAL_LOG_H_
#define SERIAL_LOG_H_
#include <stdbool.h>
#include <stdint.h>
typedef enum log_error_code_t {
    STREAM_LOG_ERR_OUT_OF_MEMORY = 1,
    STREAM_LOG_ERR_MAX_LOGS_REACHED,
    STREAM_LOG_ERR_MAX_STREAMS_REACHED
} log_error_code_t;

typedef void (*log_input_handler_t)(int);

void *serial_log_output(const char * title, uint32_t buffer_size, int stream_count,...);
void *serial_log_input(const char * title, int init_value, log_input_handler_t handler_func);

bool serial_log_data(void *log_input_ptr,...);
int serial_log_get_input_value(void *log_input_ptr);

void serial_log_close(void *log_input_ptr);
void serial_log_handler(uint32_t in_current_ms);
void serial_log_init(void *log_memory, uint32_t log_memory_size);


#endif /* SERIAL_LOG_H_ */
