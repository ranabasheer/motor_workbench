#ifdef COMPRESS_STREAM
#include "serial_log_compress.h"

static void init_stream_compression_state(log_stream_t *log_stream_ptr)
{
    log_stream_ptr->compress.last_log_data = 0;
    log_stream_ptr->compress.last_meaningful_bit_count = \
    log_stream_ptr->compress.last_leading_zero_count = \
    log_stream_ptr->compress.last_trailing_zero_count = 32;
}

/*
 * fill the data into the log stream buffer one by one
 */

static bool pack_stream_data(log_stream_t *log_stream_ptr, float data)
{
    #define TOTAL_BIT_COUNT (sizeof(uint32_t)<<3)

    int i;
    uint32_t int_data;
    uint32_t value;
    uint32_t xor_data;
    uint8_t trailing_zero_count;      //number of zeros preceding the first value bit
    uint8_t meaningful_bit_count;     //number of bits used for the value
    uint8_t leading_zero_count;       //number of zeros after the last value bit
    uint8_t last_value_bit_position;  //position of the last_value_bit

    if(log_stream_ptr == NULL)
    {
        //we don't have space for new stream
        return false;
    }

    if(log_stream_ptr->active_stream_data_ptr == NULL)
    {
        //we don't have an active data buffer. Try to see if one freed up because the serial
        //code had sent it out
        log_stream_ptr->active_stream_data_ptr = find_free_stream_data_buffer(log_stream_ptr);
        if(log_stream_ptr->active_stream_data_ptr == NULL)
        {
            //no free data buffer is yet available
            return false;
        }
        init_active_stream_data_buffer(log_stream_ptr);
    }

    //if we don't have space to add another 32 more bits of data then this buffer is
    //ready to go out. assign the active buffer as the next free one
    if(log_stream_ptr->active_stream_data_ptr->data_bits+32 >= log_stream_ptr->max_bit_count)
    {
        //set_active_stream_data_inactive(log_stream_ptr);
        log_stream_ptr->active_stream_data_ptr = find_free_stream_data_buffer(log_stream_ptr);
        if(log_stream_ptr->active_stream_data_ptr == NULL)
        {
            printf("out of space\n");
            //we have no active space available. so return false
            return false;
        }
        //we have a new active stream data ptr so reset all the last data metric
        init_active_stream_data_buffer(log_stream_ptr);
    }


    int_data = *((uint32_t *)&data);
    
    if(log_stream_ptr->active_stream_data_ptr->data_bits == 0)
    {
      log_stream_ptr->compress.last_log_data = int_data;
      memcpy(log_stream_ptr->active_stream_data_ptr->data_ptr, &int_data, sizeof(uint32_t));
      log_stream_ptr->active_stream_data_ptr->data_bits += 32;
      return (log_stream_ptr->active_stream_data_ptr->data_bits<log_stream_ptr->max_bit_count);
    }
    
    //xor the new int data with the previous one and store the result in int_data
    xor_data = log_stream_ptr->compress.last_log_data ^ int_data;
    //printf("last input: %08X %08X %08X\n", log_stream_ptr->last_log_data, int_data, xor_data);

    if(xor_data == 0)
    {
        //new data and the last data are same since the XOR resulted in zero.
        //so store the control bit of zero
        log_stream_ptr->active_stream_data_ptr->data_bits++;
        return (log_stream_ptr->active_stream_data_ptr->data_bits<log_stream_ptr->max_bit_count);
    }

    log_stream_ptr->compress.last_log_data = int_data;
    
    int_data = xor_data;
    
    trailing_zero_count = 0;
    last_value_bit_position = 0;
    value = 0;
    /*
     * scan through the data bit value and find the trailing zero count
     * and the last value bit position
     */
    for(i = 0; i < TOTAL_BIT_COUNT; ++i)
    {
        if(xor_data & 0x1)
        {
            if(value == 0)
            {
                //this is where the value data bit starts
                trailing_zero_count = i;
                value = xor_data;
            }
            last_value_bit_position = i+1;
        }

        xor_data >>= 1;
    }
    leading_zero_count = TOTAL_BIT_COUNT - last_value_bit_position;
    meaningful_bit_count = last_value_bit_position - trailing_zero_count;
    //printf("1");
    store_data_bits(0x1, log_stream_ptr->active_stream_data_ptr->data_ptr, log_stream_ptr->active_stream_data_ptr->data_bits, 1);
    log_stream_ptr->active_stream_data_ptr->data_bits+=1;
    if((log_stream_ptr->compress.last_leading_zero_count <= leading_zero_count) && \
            (log_stream_ptr->compress.last_trailing_zero_count <= trailing_zero_count)
            )
    {
        //this new value falls within the meaningful bit count of last value
        //store the control bit zero;
        log_stream_ptr->active_stream_data_ptr->data_bits++;
        meaningful_bit_count = log_stream_ptr->compress.last_meaningful_bit_count;
        trailing_zero_count = log_stream_ptr->compress.last_trailing_zero_count;
        leading_zero_count = log_stream_ptr->compress.last_leading_zero_count;
    }
    else
    {
        //printf("storing 11 bits\n");
        log_stream_ptr->compress.last_meaningful_bit_count = meaningful_bit_count;
        log_stream_ptr->compress.last_leading_zero_count = leading_zero_count;
        log_stream_ptr->compress.last_trailing_zero_count = trailing_zero_count;
        //store the control bit 1 followed by the number of leading zeros in 5 bits
        store_data_bits(0x1, log_stream_ptr->active_stream_data_ptr->data_ptr, log_stream_ptr->active_stream_data_ptr->data_bits, 1);
        log_stream_ptr->active_stream_data_ptr->data_bits+=1;
        store_data_bits(leading_zero_count, log_stream_ptr->active_stream_data_ptr->data_ptr, log_stream_ptr->active_stream_data_ptr->data_bits, 5);
        log_stream_ptr->active_stream_data_ptr->data_bits += 5;
        //length of the meaningful bits in the next 6 bits
        if(meaningful_bit_count > 31 || meaningful_bit_count == 0)
        {
          printf("outside bound %d %d %d %d\n",log_stream_ptr->active_stream_data_ptr->data_bits>>5, leading_zero_count, meaningful_bit_count, trailing_zero_count);
        }
        store_data_bits(meaningful_bit_count, log_stream_ptr->active_stream_data_ptr->data_ptr, log_stream_ptr->active_stream_data_ptr->data_bits, 5);
        log_stream_ptr->active_stream_data_ptr->data_bits += 5;
    }
    int_data>>=trailing_zero_count;
    //printf("input: %08X %08X %d %d %d\n", int_data, value, leading_zero_count, meaningful_bit_count, trailing_zero_count);
    store_data_bits(int_data, log_stream_ptr->active_stream_data_ptr->data_ptr, log_stream_ptr->active_stream_data_ptr->data_bits, meaningful_bit_count);
    log_stream_ptr->active_stream_data_ptr->data_bits+=meaningful_bit_count;
    
    //printf("dataptr: %08X\n", log_stream_ptr->active_stream_data_ptr->data_ptr[0]);


    return (log_stream_ptr->active_stream_data_ptr->data_bits<log_stream_ptr->max_bit_count);
}
#endif
