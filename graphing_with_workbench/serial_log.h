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

/*
 * This function allows plotting data on workbench. If you have multiple streams
 * that need to be displayed on a single oscilloscope frame then include those as
 * multiple streams in it. Each stream will need a name and the variable that is used
 *
 * For e.g. To log Clarke Transformation where the back EMF has a maximum back EMF frequency of 300Hz
 * with two streams alpha and beta in alpha_current and beta current. The variables have to be volatile
 * serial_log_output("Clarke Transformation", 300, 2, "alpha", &alpha_current, "beta", &beta_current);
 *
 */
void *serial_log_output(const char * title, uint16_t signal_bandwidth_in_hz, int stream_count,...);
void *serial_log_input(const char * title, int init_value, log_input_handler_t handler_func);

bool serial_log_data(void *log_input_ptr,...);
int serial_log_get_input_value(void *log_input_ptr);

void serial_log_close(void *log_input_ptr);
void serial_log_handler(uint32_t timer_ms);
void serial_log_sample_data();
void serial_log_init(void *log_memory, uint32_t log_memory_size, uint16_t sampling_rate_in_hz);


#endif /* SERIAL_LOG_H_ */
