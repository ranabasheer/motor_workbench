/*
 * serial_log.c
 *
 *  Created on: Mar 12, 2021
 *      Author: RanaBasheer
 */
//#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <serial_log.h>
#include "serial_log_types.h"
#include "serial_log_compress.h"
#include "serial_log_stream.h"
#include <serial_log_interface.h>


log_t *logs[MAX_LOGS] = {0};
log_error_code_t error_code;
static void *memory_buffer;
uint32_t memory_buffer_size;
uint32_t memory_buffer_position;
static bool timer_initialized = false;
static uint16_t sampling_rate; //rate at which signal is sampled. Default is 1ms.
static uint16_t timer_ticks;
static uint32_t in_current_ms;
#define CHAR_STORAGE_FACTOR (SERIAL_LOG_BYTES_TO_BITS(1)>>3)

//#define SERIAL_LOG_DEBUG_PRINTF
int serial_log_str_length(char *str)
{
    return serial_log_read_8bit(str, 0);
}

/*
 * Source is in the native format with gaps between characters
 * destination has to be in the 8bit packed format
 */
static int log_str_copy(char *dest, char *src, int length)
{
    int i;
    //first 8 bits are used to store the length of the string
    for(i = 0; i < length-1 && src[i] != 0; ++i)
    {
        serial_log_store_8bit(dest, i+1, src[i]);
    }
    //i contains the length of the string
    serial_log_store_8bit(dest, 0, i);
    //we return one extra length to indicate that we used
    //index 0 to store the length of the string
    return (i+1);
}

/*
 * store value at the specified bit offset and number of bits
 */
static void store_data_bits(uint32_t value, uint32_t *data_ptr, uint32_t bit_offset, uint32_t bit_count)
{
    unsigned char lo,hi;
    uint32_t mask_length, src_index;
    src_index = bit_offset>>(uint32_t)5;  //this is the index into the data_ptr buffer
    lo = bit_offset&0x1F;       //this is the offset into the 32bit value;
    hi = lo+bit_count;
    if(hi > 32)
    {
        //bit copy is stradling between 2 32bit values so we first copy
        //the bits that fit within the current 32 bit data_ptr
        //and then fill the rest in the next one
        uint32_t bit_count_1 = hi-32;//32-lo;
        bit_count = 32-lo;

        //fill the first 32 bit with data that will fit in that
        mask_length = (uint32_t)((uint32_t)1<<bit_count_1) - (uint32_t)1;
        data_ptr[src_index + 1] &= ~(mask_length);
        data_ptr[src_index + 1] |= ((value & (mask_length << bit_count))>>bit_count);
    }

    // get the length of the mask
    //the conditional check is needed because
    //if have to right shift by 32 then there will be an overflow resulting i
    //in mask length being zero.
    if(bit_count < 32)
    {
        mask_length = (uint32_t)((uint32_t)1<<bit_count) - (uint32_t)1;
        value &= mask_length;
        data_ptr[src_index] &= ~(mask_length << lo);
    }
    else
    {
        data_ptr[src_index] = value;
    }


    data_ptr[src_index] |= (value << lo);
}

/*
 * Scans through the logs to find that is unassigned
 */
static int find_free_log_space_index()
{
    int i;
      for(i = 0; i < MAX_LOGS; ++i)
      {
        log_t *log_ptr = logs[i];
        if(log_ptr == NULL)
        {
          return i;
        }
      }
      return -1;
}


/*
 * Frees the dynamic memory allocated for the log stream data
 */
static void free_log_stream(log_stream_t *log_stream_ptr)
{
    log_stream_ptr->in_use = false; //unclaim this spot
}

/*
 * this function makes sure that the length used to adjust the memory is
 * correct and confirms to a void * boundary
 */
static uint32_t adjust_memory_length(uint32_t length)
{
    //make sure that the length is a multiple of sizeof(void *)
    //length = (uint32_t)((((float)length + 0.5)/(float)sizeof(void *))*(float)sizeof(void *));
    #define SIZEOF_VOID_PTR (sizeof(void *))
    length = ((uint32_t)((length + SIZEOF_VOID_PTR - 1)/SIZEOF_VOID_PTR))*SIZEOF_VOID_PTR;

    return length/sizeof(int);
}

static int *allocate_memory(uint32_t size)
{
    int *memory = ((int *)memory_buffer + memory_buffer_position);
    memory_buffer_position+=size;
    if(memory_buffer_position >= memory_buffer_size)
        return NULL;
    memset(memory, 0, size);
    return memory;
}

/*
 * allocate a new log stream inside the log ptr
 */
static log_stream_t *allocate_new_log_stream(const char *name, float *data_ptr, int total_stream_count)
{
    int i, length;
    int *memory;
    log_stream_t *log_stream_ptr;

    //assign memory for this stream pointer;
    length = adjust_memory_length(sizeof(log_stream_t));
    memory = allocate_memory(length);
    if(memory == NULL)
    {
        return NULL;
    }
    log_stream_ptr = (log_stream_t *)memory;
    log_stream_ptr->in_use = true; //claim this spot

    length = strlen((char *)name)+1;
    length = adjust_memory_length((length + CHAR_STORAGE_FACTOR/2)/CHAR_STORAGE_FACTOR);
    memory = allocate_memory(length);
    if(memory == NULL)
    {
        return NULL;
    }
    //assign memory for storing the stream name
    log_stream_ptr->name = (char *)memory;
    //assign the data_ptr and the value;
    log_stream_ptr->data_ptr = data_ptr;
    log_stream_ptr->data_value = *data_ptr;
    //copy the title of this log into this space
    log_str_copy(log_stream_ptr->name, (char *)name, MAX_NAME_SIZE-1);

    //allocate memory for the log_stream_data_t objects
    for(i = 0; i < MAX_STREAM_DATA_BUFFERS; ++i)
    {
        //assign memory for this data stream here
        length = adjust_memory_length(sizeof(log_stream_data_t));
        memory = allocate_memory(length);
        if(memory == NULL)
        {
            return NULL;
        }
        log_stream_data_t *log_stream_data_ptr = (log_stream_data_t *)memory;

        log_stream_ptr->buffers[i] = log_stream_data_ptr;
        log_stream_data_ptr->state = SERIAL_LOG_DATA_NOT_SET; //indicate that this space is available

    }
    log_stream_ptr->active_stream_data_ptr = NULL;



    //check if this machine is big endian or not
    {
        uint32_t test_value = 0x12345678;
        log_stream_ptr->big_endian = ((*((char *)&test_value))&0xFF) == 0x12;
    }
    log_stream_ptr->type_length_in_bits = SERIAL_LOG_BYTES_TO_BITS(sizeof(float));


    return log_stream_ptr;
}

/*
 * scans through the log_data_streams under the log_stream to find one that
 * can be used to send the data out
 */
static log_stream_data_t *find_free_stream_data_buffer(log_stream_t *log_stream_ptr)
{
    int i;
    if(log_stream_ptr == NULL)
    {
        return  NULL;
    }

    for(i = 0; i < MAX_STREAM_DATA_BUFFERS; ++i)
    {
        log_stream_data_t *log_stream_data_ptr = log_stream_ptr->buffers[i];

        if(log_stream_data_ptr->state == SERIAL_LOG_DATA_NOT_SET)
            return log_stream_data_ptr;
    }
    return NULL;
}

static void init_active_stream_data_buffer(log_stream_t *log_stream_ptr)
{
    //uint32_t byte_count = BITS_TO_BYTES(log_stream_ptr->max_bit_count);
    //memset(log_stream_ptr->active_stream_data_ptr->data_ptr, 0, byte_count);
    log_stream_ptr->active_stream_data_ptr->data_bits = 0;
    log_stream_ptr->active_stream_data_ptr->state = SERIAL_LOG_DATA_FILLING;
}

static bool log_data(log_stream_t *log_stream_ptr, float data)
{
    uint32_t value;
    uint32_t value_le;
    if(log_stream_ptr->active_stream_data_ptr == NULL)
    {
        //we don't have an active data buffer. Try to see if one freed up because the serial
        //code had sent it out
        log_stream_ptr->active_stream_data_ptr = find_free_stream_data_buffer(log_stream_ptr);
        if(log_stream_ptr->active_stream_data_ptr == NULL)
        {
            //no free data buffer is yet available
            error_code = STREAM_LOG_ERR_MAX_STREAMS_REACHED;
            return false;
        }
        init_active_stream_data_buffer(log_stream_ptr);
    }

    
    
    //if we don't have space to add another 32 more bits of data then this buffer is
    //ready to go out. assign the active buffer as the next free one
    if(log_stream_ptr->active_stream_data_ptr->data_bits >= log_stream_ptr->max_bit_count)
    {
        log_stream_ptr->active_stream_data_ptr->state = SERIAL_LOG_DATA_READY;
        #ifdef COMPRESS_STREAM
          //let's compress this data stream
          compress_stream(log_stream_ptr);
        #endif
        //set_active_stream_data_inactive(log_stream_ptr);
        log_stream_ptr->active_stream_data_ptr = find_free_stream_data_buffer(log_stream_ptr);
        if(log_stream_ptr->active_stream_data_ptr == NULL)
        {
            #ifdef SERIAL_LOG_DEBUG_PRINTF
              printf("out of space\n");
            #endif
            //error_flag = true;
            //strcpy(error_str, "out of space");
            //we have no active space available. so return false
            error_code = STREAM_LOG_ERR_OUT_OF_MEMORY;
            return false;
        }
        
        //we have a new active stream data ptr so reset all the last data metric
        init_active_stream_data_buffer(log_stream_ptr);
    }
    
    value_le = *((uint32_t *)&data);
    if(log_stream_ptr->big_endian)
    {
        //this is a big endian processor. so we need to swap the bytes to little endian format
        value = ((value_le & 0xFF) << 24);
        value |= ((value_le & 0xFF00) << 16);
        value |= ((value_le & 0xFF0000) << 8);
        value |= ((value_le & 0xFF000000) << 0);
    }
    else
        value = value_le;

    store_data_bits(value, log_stream_ptr->active_stream_data_ptr->data_ptr, log_stream_ptr->active_stream_data_ptr->data_bits, log_stream_ptr->type_length_in_bits);
    log_stream_ptr->active_stream_data_ptr->data_bits+=log_stream_ptr->type_length_in_bits;
    return true;
}

static log_t *allocate_log_ptr(char *title)
{
    int *memory;
    int length;
    log_t *log_ptr;
    int i = find_free_log_space_index(); //find an index in logs where we will store this log info
    if(i == INVALID_LOG_INDEX)
    {
        //invalid pointer passed in
        error_code = STREAM_LOG_ERR_MAX_LOGS_REACHED;
        return NULL;
    }
    length = adjust_memory_length(sizeof(log_t));
    memory = allocate_memory(length);
    if(memory == NULL)
    {
        return NULL;
    }
    logs[i] = log_ptr = (log_t *)memory;
    log_ptr->direction = LOG_OUTPUT;

    length = strlen((char *)title)+1; //extra character to store the length of the string
    length = adjust_memory_length((length + CHAR_STORAGE_FACTOR/2)/CHAR_STORAGE_FACTOR);
    memory = allocate_memory(length);
    if(memory == NULL)
    {
        return NULL;
    }
    log_ptr->title = (char *)memory;
    //copy the title of this log into this space
    log_str_copy(log_ptr->title, (char *)title, MAX_NAME_SIZE-1);
    return log_ptr;
}

/*
 * Closes the serial log
 */
void serial_log_close(void *log_input_ptr)
{
    int i;
    log_t *log_ptr = (log_t *)log_input_ptr;
    if(log_ptr == NULL)
    {
        return;
    }
    for(i = 0; i < MAX_LOG_STREAM_COUNT; ++i)
    {
      free_log_stream(STREAMS(log_ptr)[i]); //log_ptr->type.output.streams[i]);
    }
    log_ptr->direction = LOG_UNUSED;
}

/*
 * returns true if all the data buffers were reset else it returns false
 */
static bool init_output_data_buffers(log_stream_t *log_stream_ptr)
{
    int i;
    bool in_transit = false;
    log_stream_ptr->active_stream_data_ptr = NULL;
    for(i = 0; i < MAX_STREAM_DATA_BUFFERS; ++i)
    {
        if(log_stream_ptr->buffers[i]->state != SERIAL_LOG_DATA_TRANSMITTING)
        {
            log_stream_ptr->buffers[i]->state = SERIAL_LOG_DATA_NOT_SET;
        }
        else
            in_transit = true;
    }
    return in_transit;
}

/*
 * this is the function that samples all the output data and is called
 * SAMPLING_RATE per second
 */
static void log_all_output_data()
{
    int i, j;
    bool store_data = false;
    bool tx_buffer_active = true;
    //check through all active logs to find output logs
    for(i = 0; i < MAX_LOGS; ++i)
    {
        log_t *log_ptr = logs[i];
        if(log_ptr == NULL)
            continue;
        if(log_ptr->direction != LOG_OUTPUT)
            continue;
        float lpf = log_ptr->lpf;
        float dc_lpf  = lpf/100.0;
        log_trigger_state_t log_state = log_ptr->trigger_state;
        if(log_state == TRIGGER_ACTIVE)
        {
            //store data if the sample count reached the store count
            if(++log_ptr->sample_count > log_ptr->store_count)
            {
                store_data = true;
                log_ptr->sample_count = 0;
            }
        }

        for(j = 0; j < MAX_LOG_STREAM_COUNT; ++j)
        {
            log_stream_t *log_stream_ptr = STREAMS(log_ptr)[j];//log_ptr->type.output.streams[j];
            if(log_stream_ptr == NULL)
                continue;

            //apply low pass filtering based on their bandwidth
            log_stream_ptr->data_value = lpf*(*log_stream_ptr->data_ptr)+(1-lpf)*log_stream_ptr->data_value;
            log_stream_ptr->dc_value = dc_lpf*(*log_stream_ptr->data_ptr)+(1-dc_lpf)*log_stream_ptr->data_value;
            if(j == 0)
            {
                //we trigger the system
                if(log_state == TRIGGER_WAIT_FOR_POSITIVE_TRANSITION)
                {
                    if(log_stream_ptr->data_value > log_stream_ptr->dc_value)
                    {
                        log_state = TRIGGER_ACTIVE;
                        log_ptr->sample_count = 0;
                        store_data = true;
                        init_output_data_buffer(log_ptr);
                    }
                }
                else if(log_state == TRIGGER_WAIT_FOR_NEGATIVE_TRANSITION)
                {
                    if(log_stream_ptr->data_value <= log_stream_ptr->dc_value)
                    {
                        log_stream_ptr->state = TRIGGER_WAIT_FOR_POSITIVE_TRANSITION;
                    }
                }
            }
            if(store_data)
            {
                if(!log_data(log_stream_ptr, log_stream_ptr->data_value))
                {
                    //we ran out of space to send the data. So we have to drop this capture entirely
                    //and send a new set of data.
                    log_state = TRIGGER_WAIT_FOR_TX_BUFFER_EMPTY;
                    //if there is a data buffer already in transit then we wait for that buffer to go out.
                    //else we wait for the next trigger;
                    log_state = init_output_data_buffers(log_stream_ptr)?TRIGGER_WAIT_FOR_TX_BUFFER_EMPTY:TRIGGER_WAIT_FOR_NEGATIVE_TRANSITION;
                    break;
                }
            }

            if(log_state == TRIGGER_WAIT_FOR_TX_BUFFER_EMPTY)
            {
                //we are waiting for all the buffers to be empty
                //check to see if any data buffer is not in SERIAL_LOG_DATA_NOT_SET
                //tx_buffer_active&=log_stream_ptr->
                log_state = init_output_data_buffers(log_stream_ptr)?TRIGGER_WAIT_FOR_TX_BUFFER_EMPTY:TRIGGER_WAIT_FOR_NEGATIVE_TRANSITION;
            }
        }

        log_ptr->trigger_state = log_state;
    }
}

/*
 * This function is expected to be called once every sampling tick.
 */
void serial_log_handler()
{
    log_all_output_data();
    in_current_ms += timer_ticks;
    serial_log_stream_handler(in_current_ms);
}

#if 0
/*
 * array of data to be stored, one for each stream
 */
bool serial_log_data(void *log_input_ptr,...)
{
    int i;
    va_list stream_data_list;
    bool ret = true;
    log_t *log_ptr = (log_t *)log_input_ptr;
    if(log_ptr == NULL)
    {
        error_code = STREAM_LOG_ERR_OUT_OF_MEMORY;
        //we don't have space for new stream
        return false;
    }
    va_start(stream_data_list, log_input_ptr);
    //for(i = 0; i < log_ptr->type.output.stream_count&&ret; ++i)
    for(i = 0; i < STREAM_COUNT(log_ptr)&&ret; ++i)
    {
        float data;
        log_stream_t *log_stream_ptr = STREAMS(log_ptr)[i];//log_ptr->type.output.streams[i];
        data = va_arg(stream_data_list, float);
        ret = log_data(log_stream_ptr, data);
    }
    va_end(stream_data_list);
    return ret;
}
#endif

/*
 * This is the total memory available for logging data
 */
void serial_log_init(void *buffer, uint32_t buffer_size, uint16_t sampling_rate_in_hz)
{
    int i;
    memory_buffer = buffer;
    memory_buffer_size = buffer_size;
    memory_buffer_position = 0;
    sampling_rate = sampling_rate_in_hz;
    timer_ticks = (1000 + sampling_rate/2)/sampling_rate;
    in_current_ms = 0;
    for(i = 0; i < MAX_LOGS; ++i)
    {
        logs[i] = NULL;
    }
    serial_log_stream_handler_init();
    serial_log_uart_init();
}


void *serial_log_output(const char * title, uint16_t bandwidth_in_hz, int stream_count,...)
{
    int i, j, length,memory_size_per_buffer;
    log_t *log_ptr;
    va_list stream_list;

    log_ptr = allocate_log_ptr((char *)title);
    if(log_ptr == NULL)
    {
        //we ran out of memory
        error_code = STREAM_LOG_ERR_OUT_OF_MEMORY;
        return NULL;
    }

    //assign all the streams as NULL
    for(i = 0; i < MAX_LOG_STREAM_COUNT; ++i)
    {
        STREAMS(log_ptr)[i] = NULL;
    }
    log_ptr->direction = LOG_OUTPUT;
    stream_count = STREAM_COUNT(log_ptr) = (stream_count < MAX_LOG_STREAM_COUNT)?stream_count:MAX_LOG_STREAM_COUNT;
    //stream_count = STREAM_COUNT(log_ptr);

    va_start( stream_list, stream_count );
    for(i = 0; i < stream_count; ++i)
    {
        const char *stream_name = va_arg( stream_list, const char *);
        float *data_ptr = va_arg( stream_list, float *);
        STREAMS(log_ptr)[i] = allocate_new_log_stream(stream_name, data_ptr, stream_count);
        if(STREAMS(log_ptr)[i] == NULL)
        {
            //we ran out of memory
            error_code = STREAM_LOG_ERR_OUT_OF_MEMORY;
            return NULL;
        }
        //STREAMS(log_ptr)[i] = log_stream_ptr;
    }
    va_end(stream_list);

    log_ptr->lpf = (float)bandwidth_in_hz/(2*3.14*sampling_rate);
    log_ptr->sample_count = 0;
    log_ptr->store_count = (float)sampling_rate/(float)bandwidth_in_hz;
    //Now allocate space for storing the data. We will allocate enough space to
    //store data for 100ms. So at 1000Hz sampling rate that will be 100 float units of space
    uint32_t buffer_size = ((float)bandwidth_in_hz/(float)STORAGE_TIME_IN_MS)
    //buffer size has to be divided among the MAX_STREAM_DATA_BUFFERS
    memory_size_per_buffer = ((buffer_size + MAX_STREAM_DATA_BUFFERS - 1)/MAX_STREAM_DATA_BUFFERS);
    memory_size_per_buffer&=(~(uint32_t)(sizeof(uint32_t)-1)); //make sure that the buffer_size is divisible by uint32_t data type

    for(i = 0; i < STREAM_COUNT(log_ptr); ++i)
    {
        log_stream_t *log_stream_ptr = STREAMS(log_ptr)[i];
        log_stream_ptr->max_bit_count = log_stream_ptr->type_length_in_bits*memory_size_per_buffer;//SERIAL_LOG_BYTES_TO_BITS(memory_size_per_buffer); //number of bits that we can store
        for(j = 0; j < MAX_STREAM_DATA_BUFFERS; ++j)
        {
            int *memory;
            log_stream_data_t *log_stream_data_ptr = log_stream_ptr->buffers[j];
            length = SERIAL_LOG_BITS_TO_BYTES(log_stream_ptr->max_bit_count);
            length = adjust_memory_length(length);
            memory = allocate_memory(length);
            if(memory == NULL)
            {
                error_code = STREAM_LOG_ERR_OUT_OF_MEMORY;
                return NULL;
            }
            log_stream_data_ptr->data_ptr = (uint32_t *)memory;
        }
    }

    return log_ptr;
}

void *serial_log_input(const char * title, int init_value, log_input_handler_t handler_func)
{
    log_t *log_ptr = allocate_log_ptr((char *)title);
    if(log_ptr == NULL)
    {
        //we ran out of memory
        error_code = STREAM_LOG_ERR_OUT_OF_MEMORY;
        return NULL;
    }

    //claim this spot
    log_ptr->direction = LOG_INPUT;
    log_ptr->type.input.value = init_value;
    log_ptr->type.input.func = handler_func;

    if(handler_func != NULL)
        handler_func(init_value); //call the handler function with the init value
    return log_ptr;
}

int serial_log_get_input_value(void *log_input_ptr)
{
    log_t *log_ptr = (log_t *)log_input_ptr;
    return log_ptr->type.input.value;
}
