/*
 * serial_log_types.h
 *
 *  Created on: Apr 6, 2021
 *      Author: RanaBasheer
 */

#ifndef SERIAL_LOG_TYPES_H_
#define SERIAL_LOG_TYPES_H_
#include <stdbool.h>
#include <stdint.h>
#include <serial_log.h>

#define MAX_LOG_STREAM_COUNT    3   //cannot be changed
#define MAX_LOGS                16  //cannot be changed
#define MAX_STREAM_DATA_BUFFERS 4
#define MAX_NAME_SIZE           64  //max number of characters used for log names
#define STORAGE_TIME_IN_MS      1000 //no. of milliseconds for which data is stored before send to the host computer


#define INVALID_LOG_INDEX       -1

#ifndef uint8_t
  typedef unsigned char uint8_t;
#endif

typedef enum log_stream_direction_t
{
    LOG_UNUSED = 0,
    LOG_INPUT,
    LOG_OUTPUT
}log_stream_direction_t;

typedef enum log_stream_type_t
{
    SERIAL_LOG_STRING_TYPE,
    SERIAL_LOG_FLOAT_TYPE,
    SERIAL_LOG_BOOL_TYPE
}log_stream_type_t;

typedef enum log_stream_data_state_t
{
    SERIAL_LOG_DATA_NOT_SET = 0,
    SERIAL_LOG_DATA_FILLING,
    SERIAL_LOG_DATA_READY,
    SERIAL_LOG_DATA_TRANSMITTING,
    SERIAL_LOG_COMPRESSED_DATA_FILLING,
    SERIAL_LOG_COMPRESSED_DATA_READY

}log_stream_data_state_t;

typedef enum log_trigger_state_t
{
    TRIGGER_WAIT_FOR_NEGATIVE_TRANSITION,
    TRIGGER_WAIT_FOR_POSITIVE_TRANSITION,
    TRIGGER_ACTIVE,
    TRIGGER_WAIT_FOR_TX_BUFFER_EMPTY,
    TRIGGER_WAIT_FOR_TX_BUFFER_OVFLOW,
    TRIGGER_INVALID
}log_trigger_state_t;

typedef struct log_stream_data_t
{
    uint32_t *data_ptr; //stream of floating data
    uint32_t data_bits; //if zero then this stream is available for filling
    uint32_t data_offset;//indicate the start index of where this data will be written
    log_stream_data_state_t state;
} log_stream_data_t;

typedef struct log_stream_compress_t
{
    uint32_t last_log_data;
    uint8_t last_leading_zero_count;
    uint8_t last_trailing_zero_count;
    uint8_t last_meaningful_bit_count;
} log_stream_compress_t;

typedef struct log_stream_t
{
    bool in_use; //indicates if this stream is active or not
    //2 buffers with one acting as the main one and the other acting
    //as the one which is used by the serial code for sending data
    log_stream_data_t *buffers[MAX_STREAM_DATA_BUFFERS];

    log_stream_data_t *active_stream_data_ptr;
    uint32_t max_bit_count; //this is the maximum number of data bits that is fillable in a single buffer
    uint8_t type_length_in_bits; //number of bits in a single data type
    bool big_endian;
    log_stream_type_t type;
    log_stream_compress_t compress;

    char *name;         //name of the substream
    float *data_ptr;    //pointer to the floating point data that is sampled periodically
    float data_value;   //low pass filtered data value
    float dc_value;     //double low pass filtered to allow a static dc content used for centering the data along the y axis
} log_stream_t;

#define STREAMS(log_ptr) (log_ptr->type.output.streams)
#define STREAM_COUNT(log_ptr) (log_ptr->type.output.stream_count)

typedef struct log_input_t
{
    int value;
    log_input_handler_t func;
}log_input_t;

typedef struct log_output_t
{
    uint16_t sample_count; //incremented at each sampling tick
    uint16_t sample_index; //when the number of sample count reaches the sample index, data is written into the output stream
    uint16_t store_count; //number of data points stored
    float lpf; //this is the low pass filtering coefficient for output data
    log_trigger_state_t trigger_state;

    int stream_count;
    log_stream_t *streams[MAX_LOG_STREAM_COUNT];   //
} log_output_t;

typedef struct log_t
{
    log_stream_direction_t direction;
    union {
        log_output_t output;
        log_input_t input;
    } type;
    char *title;

} log_t;


#endif /* LIBRARY_SERIAL_LOG_TYPES_H_ */
