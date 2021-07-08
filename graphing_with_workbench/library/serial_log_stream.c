/*
 * serial_log_uart.c
 *
 *  Created on: Mar 12, 2021
 *      Author: RanaBasheer
 */
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "serial_log.h"
#include "serial_log_stream.h"
#include "serial_log_packet.h"
#include <serial_log_interface.h>

#define STREAM_INFO_PERIOD  2000 //2secons interval for sending the stream info

typedef struct in_transit_buffer_info_t {
    uint8_t stream_index;
    uint8_t buffer_index;
}in_transit_buffer_info_t;

static uint8_t input_rx[6];

extern log_t *logs[MAX_LOGS];

static serial_log_stream_state_t serial_log_stream_state;
static uint32_t current_time;
static uint32_t last_stream_info_send_time;

static serial_log_stream_state_t uart_state_on_finish_sending_data;
static log_t *in_transit_log_ptr;
static uint8_t log_index, log_stream_index;
static in_transit_buffer_info_t log_streams[MAX_LOG_STREAM_COUNT];

static uint8_t stream_header[6];
static bool log_info_packet_in_transit; //indicates if we are currently sending a log info packet
static bool send_log_info_title; //indicates if we are sending title or name

static serial_log_packet_t tx_packet;
static serial_log_packet_t rx_packet;

extern int serial_log_str_length(char *str);

static void start_uart_packet(serial_log_stream_state_t next_state)
{
  serial_log_packet_start(&tx_packet);
  serial_log_stream_state = SERIAL_LOG_STREAM_SEND_ACK_WAIT_BYTE;
  uart_state_on_finish_sending_data = next_state;
}

static void stop_uart_packet(serial_log_stream_state_t next_state)
{
  serial_log_packet_done(&tx_packet);
  serial_log_stream_state = SERIAL_LOG_STREAM_SEND_ACK_WAIT_BYTE;
  uart_state_on_finish_sending_data = next_state;
}

static void send_uart_data(uint8_t *buffer, uint32_t data_size, serial_log_stream_state_t next_state)
{
    serial_log_packet_send(&tx_packet, buffer, data_size);
    serial_log_stream_state = SERIAL_LOG_STREAM_SEND_ACK_WAIT_BYTE;
    uart_state_on_finish_sending_data = next_state;
}


static log_t *find_ready_stream_data_buffer(uint8_t *log_index, in_transit_buffer_info_t *streams)
{
    int i, j, k;
    int count;
    //check through all active logs to see if it is ready to be sent out through the serial port
    for(i = 0; i < MAX_LOGS; ++i)
    {
        log_t *log_ptr = logs[i];
        if(log_ptr != NULL)
        {
            if(log_ptr->direction == LOG_OUTPUT)
            {
                count = 0;
                for(j = 0; j < MAX_LOG_STREAM_COUNT; ++j)
                {
                    log_stream_t *log_stream_ptr = STREAMS(log_ptr)[j];//log_ptr->type.output.streams[j];
                    if(log_stream_ptr != NULL)
                    {
                        if(log_stream_ptr->in_use)
                        {
                            for(k = 0; k < MAX_STREAM_DATA_BUFFERS; ++k)
                            {
                                log_stream_data_t *log_stream_data_ptr = log_stream_ptr->buffers[k];
                                if(log_stream_data_ptr->state == SERIAL_LOG_DATA_READY)
                                {
                                    //this buffer is not active and has data filled in it. that means this is ready to go out
                                    //log_stream_data_ptr->state = SERIAL_LOG_DATA_TRANSMITTING;
                                    *log_index = (uint8_t)i;
                                    streams[count].stream_index = (uint8_t)j;
                                    streams[count].buffer_index = (uint8_t)k;
                                    count++;
                                    //return log_stream_data_ptr;
                                }
                            }
                        }
                    }
                }
                if(count == STREAM_COUNT(log_ptr)) //log_ptr->type.output.stream_count)
                {
                    return log_ptr;
                }
            }
        }
    }
    return NULL;
}

static void handle_inactive_state()
{
    if(current_time - last_stream_info_send_time > STREAM_INFO_PERIOD)
    {
        last_stream_info_send_time = current_time;
        log_index = 0;
        log_stream_index = 0;
        serial_log_stream_state = SERIAL_LOG_STREAM_START_INFO;
    }
    else
    {
        in_transit_log_ptr = find_ready_stream_data_buffer(&log_index, log_streams);
        log_stream_index = 0;
        if(in_transit_log_ptr == NULL)
        {
            //we don't have any filled transit buffer. keep checking and wait for one
            return;
        }
        start_uart_packet(SERIAL_LOG_STREAM_SEND_DATA_HEADER);
    }
}

static void handle_stream_data_done_state()
{
    uint8_t stream_index = log_streams[log_stream_index].stream_index;
    uint8_t buffer_index = log_streams[log_stream_index].buffer_index;
    log_stream_data_t *in_transit_log_stream_data_ptr = STREAMS(in_transit_log_ptr)[stream_index]->buffers[buffer_index]; //in_transit_log_ptr->type.output.streams[stream_index]->buffers[buffer_index];
    in_transit_log_stream_data_ptr->state = SERIAL_LOG_DATA_NOT_SET; //indicates to the bit packing that this is now available for filling
    log_stream_index++;

    if(log_stream_index < STREAM_COUNT(in_transit_log_ptr)) //in_transit_log_ptr->type.output.stream_count)
    {
        //start_uart_packet(SERIAL_LOG_STREAM_SEND_DATA_HEADER);
        serial_log_stream_state = SERIAL_LOG_STREAM_SEND_DATA_HEADER;
    }
    else
    {
        stop_uart_packet(SERIAL_LOG_STREAM_INACTIVE);
    }
}


static void handle_send_stream_data_header_state()
{
    uint8_t stream_index = log_streams[log_stream_index].stream_index;
    uint8_t buffer_index = log_streams[log_stream_index].buffer_index;
    log_stream_data_t *in_transit_log_stream_data_ptr = STREAMS(in_transit_log_ptr)[stream_index]->buffers[buffer_index];  //in_transit_log_ptr->type.output.streams[stream_index]->buffers[buffer_index];
    uint32_t bytes = in_transit_log_stream_data_ptr->data_bits;

    bytes = (bytes + 8 - 1)>>3; //ceil operation

    serial_log_store_8bit(stream_header, 0, ((LOG_STREAM_DATA_PACKET_ID&0x3) << 6) | ((stream_index&0x3) << 4) | (log_index&0xF));
    serial_log_store_8bit(stream_header, 1, bytes&0xFF);
    serial_log_store_8bit(stream_header, 2, (bytes>>8)&0xFF);
    send_uart_data(stream_header, 3, SERIAL_LOG_STREAM_SEND_DATA);
}


static void handle_send_stream_data_state()
{
    uint8_t stream_index = log_streams[log_stream_index].stream_index;
    uint8_t buffer_index = log_streams[log_stream_index].buffer_index;
    log_stream_data_t *in_transit_log_stream_data_ptr = STREAMS(in_transit_log_ptr)[stream_index]->buffers[buffer_index]; //in_transit_log_ptr->type.output.streams[stream_index]->buffers[buffer_index];
    uint32_t bytes = in_transit_log_stream_data_ptr->data_bits;
    bytes = (bytes + 8 - 1)>>3; //ceil operation
    in_transit_log_stream_data_ptr->state = SERIAL_LOG_DATA_TRANSMITTING;
    send_uart_data((uint8_t *)in_transit_log_stream_data_ptr->data_ptr,
                  bytes,
                  SERIAL_LOG_STREAM_DATA_DONE);
}


static void handle_start_stream_info_state()
{
    log_t *log_ptr;
    if(log_stream_index >= MAX_LOG_STREAM_COUNT)
    {
        //we reached the end of this log's stream so go to the next one's title
        log_stream_index = 0;
        send_log_info_title = true;
        ++log_index;
    }

    if(log_index >= MAX_LOGS)
    {
        //we scanned through the entire log info list now go back to the inactive state
        if(log_info_packet_in_transit)
        {
            //we send out an info packet so close it now
            stop_uart_packet(SERIAL_LOG_STREAM_INACTIVE);
        }
        log_info_packet_in_transit = false;
        log_index = 0;
        return;
    }

    if(logs[log_index] != NULL)
    {
        log_ptr = logs[log_index];
        if(log_ptr->direction == LOG_OUTPUT)
        {
            log_stream_t *log_stream_ptr = STREAMS(log_ptr)[log_stream_index]; //logs[log_index]->type.output.streams[log_stream_index];
            if(log_stream_ptr != NULL)
            {
                if(log_stream_ptr->in_use)
                {
                    if(!log_info_packet_in_transit)
                    {
                        log_info_packet_in_transit = true;
                        start_uart_packet(SERIAL_LOG_STREAM_SEND_INFO_TITLE_HEADER);
                    }
                    else
                    {
                        serial_log_stream_state = send_log_info_title?SERIAL_LOG_STREAM_SEND_INFO_TITLE_HEADER:\
                                                            SERIAL_LOG_STREAM_SEND_INFO_NAME_HEADER;
                    }
                    send_log_info_title = false; //next iteration has to be names
                    return;
                }
            }
            ++log_stream_index;
            return;
        }
        else if(log_ptr->direction == LOG_INPUT)
        {
            if(!log_info_packet_in_transit)
            {
                log_info_packet_in_transit = true;
                start_uart_packet(SERIAL_LOG_STREAM_SEND_INFO_TITLE_HEADER);
            }
            else
            {
                serial_log_stream_state = send_log_info_title?SERIAL_LOG_STREAM_SEND_INFO_TITLE_HEADER:\
                                                                                    SERIAL_LOG_STREAM_SEND_INPUT_HEADER;
            }
            send_log_info_title = false; //next iteration has to be names
            return;
        }
    }

    //go to the next log since this one was not in use
    ++log_index;

}

static void handle_send_stream_info_title_header_state()
{
    serial_log_store_8bit(stream_header, 0, ((LOG_STREAM_INFO_TITLE_PACKET_ID&0x3) << 6) | ((log_stream_index&0x3) << 4) | (log_index&0xF));
    send_uart_data(stream_header, 1, SERIAL_LOG_STREAM_SEND_INFO_TITLE);
}
static void handle_send_stream_info_title_state()
{
    char *title = (char *)logs[log_index]->title;
    //uint8_t length = strlen(title)+1;
    uint8_t length = serial_log_str_length(title);//logs[log_index].title_length+1;
    if(length > MAX_NAME_SIZE)
        length = MAX_NAME_SIZE;
    send_uart_data((uint8_t *)title, length+1, SERIAL_LOG_STREAM_START_INFO);
}

static void handle_stream_info_input_done_state()
{
    //go to the next stream
    ++log_index;
    send_log_info_title = true; //inputs only have title;
    serial_log_stream_state = SERIAL_LOG_STREAM_START_INFO;
}

static void handle_send_stream_info_input_header_state()
{
    log_t *log_ptr = logs[log_index];
    int value = log_ptr->type.input.value;
    serial_log_store_8bit(stream_header, 0, ((LOG_STREAM_INFO_INPUT_PACKET_ID&0x3) << 6) );
    serial_log_store_8bit(stream_header, 1, (uint8_t)value);
    serial_log_store_8bit(stream_header, 2, (uint8_t)(value>>8));
    send_uart_data(stream_header, 3, SERIAL_LOG_STREAM_INFO_INPUT_DONE);
}

static void handle_send_stream_info_name_header_state()
{
    serial_log_store_8bit(stream_header, 0, ((LOG_STREAM_INFO_NAME_PACKET_ID&0x3) << 6) | ((log_stream_index&0x3) << 4) | (log_index&0xF));
    serial_log_store_8bit(stream_header, 1, STREAMS(logs[log_index])[log_stream_index]->type_length_in_bits);

    send_uart_data(stream_header, 2, SERIAL_LOG_STREAM_SEND_INFO_NAME);
}

static void handle_send_stream_info_name_state()
{
    char *name= (char *)STREAMS(logs[log_index])[log_stream_index]->name; //(char *)logs[log_index]->type.output.streams[log_stream_index]->name;
    uint8_t length = serial_log_str_length(name);
    if(length > MAX_NAME_SIZE)
        length = MAX_NAME_SIZE;
    send_uart_data((uint8_t *)name, length+1, SERIAL_LOG_STREAM_INFO_NAME_DONE);
}

static void handle_stream_info_name_done_state()
{
    //go to the next stream
    ++log_stream_index;
    serial_log_stream_state = SERIAL_LOG_STREAM_START_INFO;
}

static void handle_send_byte_ack_wait_state()
{
    if(!is_serial_log_packet_tx_busy(&tx_packet))
      serial_log_stream_state = uart_state_on_finish_sending_data;
}

static void rx_packet_handler(serial_log_packet_t *serial_log_packet_ptr)
{
    log_t *log_ptr;
    uint8_t log_index = serial_log_read_8bit(serial_log_packet_ptr->buffer, 0);

    if(log_index < MAX_LOGS)
    {
        log_ptr = logs[log_index];
        if(log_ptr != NULL)
        {
            if(log_ptr->direction == LOG_INPUT)
            {
                uint16_t value = serial_log_read_8bit(serial_log_packet_ptr->buffer, 1);
                value |= (serial_log_read_8bit(serial_log_packet_ptr->buffer, 2) << 8);

                //uint16_t value = serial_log_packet_ptr->buffer[1] | (serial_log_packet_ptr->buffer[2] << 8);
                log_ptr->type.input.value = value;
                if(log_ptr->type.input.func != NULL)
                    log_ptr->type.input.func(value);
            }
        }
    }
}

void serial_log_stream_handler(uint32_t in_current_time)
{
    //current_time = serial_log_get_time_ms();
    current_time = in_current_time;
    switch(serial_log_stream_state)
    {
    case SERIAL_LOG_STREAM_INACTIVE:
        handle_inactive_state();
        break;

    case SERIAL_LOG_STREAM_SEND_DATA_HEADER:
        handle_send_stream_data_header_state();
        break;
    case SERIAL_LOG_STREAM_SEND_DATA:
        handle_send_stream_data_state();
        break;

    case SERIAL_LOG_STREAM_DATA_DONE:
        handle_stream_data_done_state();
        break;


    case SERIAL_LOG_STREAM_START_INFO:
        handle_start_stream_info_state();
        break;
    case SERIAL_LOG_STREAM_SEND_INFO_TITLE_HEADER:
        handle_send_stream_info_title_header_state();
        break;
    case SERIAL_LOG_STREAM_SEND_INFO_TITLE:
        handle_send_stream_info_title_state();
        break;
    case SERIAL_LOG_STREAM_SEND_INPUT_HEADER:
        handle_send_stream_info_input_header_state();
        break;
    case SERIAL_LOG_STREAM_INFO_INPUT_DONE:
        handle_stream_info_input_done_state();
        break;

    case SERIAL_LOG_STREAM_SEND_INFO_NAME_HEADER:
        handle_send_stream_info_name_header_state();
        break;
    case SERIAL_LOG_STREAM_SEND_INFO_NAME:
        handle_send_stream_info_name_state();
        break;
    case SERIAL_LOG_STREAM_INFO_NAME_DONE:
        handle_stream_info_name_done_state();
       break;
    
    case SERIAL_LOG_STREAM_SEND_ACK_WAIT_BYTE:
        handle_send_byte_ack_wait_state();
        break;
    
    default:
        break;
    }
    
    serial_log_packet_build_rx(&rx_packet, rx_packet_handler);
    serial_log_packet_build_tx(&tx_packet);

}

void serial_log_stream_handler_init()
{
    serial_log_stream_state = SERIAL_LOG_STREAM_INACTIVE;
    current_time = 0;
    log_info_packet_in_transit = false;
    send_log_info_title = true;
    last_stream_info_send_time = 0;
    in_transit_log_ptr = NULL;
    memset(log_streams, 0, sizeof(log_streams));
    memset(stream_header, 0, sizeof(stream_header));
    serial_log_packet_reset_tx(&tx_packet);
    serial_log_packet_reset_rx(&rx_packet);
    rx_packet.buffer = input_rx;
    rx_packet.length = sizeof(input_rx);
    uart_state_on_finish_sending_data = SERIAL_LOG_STREAM_INACTIVE;
    log_index = log_stream_index = 0;
}
