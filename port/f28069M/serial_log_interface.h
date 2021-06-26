#ifndef SERIAL_LOG_INTERFACE_H_
#define SERIAL_LOG_INTERFACE_H_

#include <stdint.h>
#include <stdbool.h>

//expected functions from the platform

//returns the number of bits that forms a byte for this platform
int SERIAL_LOG_BYTES_TO_BITS(int byte_length);
int SERIAL_LOG_BITS_TO_BYTES(int bit_length);

//function to store and read 8 bit data from memory
void serial_log_store_8bit(void *dest_memory, int byte_index, unsigned char value);
unsigned char serial_log_read_8bit(void *src_memory, int byte_index);

//function to send a byte of data through UART
void serial_log_uart_tx(unsigned char data) __attribute__((weak)) ;

//function to read a byte of data through UART
unsigned char serial_log_uart_rx();

//returns true if it is ok to send more data through UART
bool is_serial_log_uart_tx_more();

//returns true if there is data to be read from UART
bool is_serial_log_uart_rx_ready();

//function to initialize UART
void serial_log_uart_init();

void serial_log_init_time();

uint32_t serial_log_get_time_ms();

#endif
