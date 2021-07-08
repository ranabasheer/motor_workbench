/*
 * serial_log_packet.h
 *
 *      Author: Rana
 */

#ifndef SERIAL_LOG_PACKET_H_
#define SERIAL_LOG_PACKET_H_

#include <stdbool.h>
#include <stdint.h>
#include "serial_log_types.h"

#define SERIAL_LOG_PACKET_MAX_RX_SIZE   64



typedef enum serial_log_packet_rx_state_t{
  RX_INACTIVE = 0,
  WAIT_FOR_ESC,
  WAIT_FOR_SOP,
  WAIT_FOR_DATA,
  WAIT_FOR_NEXT_DATA,
  
  RX_READY
} serial_log_packet_rx_state_t;

typedef enum serial_log_packet_tx_state_t{
  TX_INACTIVE = 0,
  SEND_SOP,
  SEND_SOP_NOW,
  SEND_DATA_ESC_NOW,
  SEND_EOP_ESC_NOW,
  SEND_CONTROL_ESC_NOW,
  SEND_EOP_NOW,
  SEND_DATA,
  SEND_EOP,
  SEND_CRC_H,
  //SEND_CRC_L,
  
  
  WAIT_FOR_ACK
} serial_log_packet_tx_state_t;

typedef struct {
  union {
    serial_log_packet_tx_state_t  tx;
    serial_log_packet_rx_state_t  rx;
  }state;

  serial_log_packet_tx_state_t  next_tx_state;
  
  uint8_t   *buffer;
  uint16_t  length;
  uint16_t  index;
  uint16_t  crc;
} serial_log_packet_t;


void serial_log_packet_start(serial_log_packet_t *packet_ptr);
void serial_log_packet_done(serial_log_packet_t *packet_ptr);
void serial_log_packet_send(serial_log_packet_t *packet_ptr, uint8_t *data, uint16_t length);
void serial_log_packet_recv(serial_log_packet_t *packet_ptr, uint8_t *data, uint16_t length);

void serial_log_packet_build_rx(serial_log_packet_t *packet_ptr, void (*rx_data_handler)(serial_log_packet_t *));
void serial_log_packet_build_tx(serial_log_packet_t *packet_ptr);

void serial_log_packet_reset_tx(serial_log_packet_t *packet_ptr);
void serial_log_packet_reset_rx(serial_log_packet_t *packet_ptr);

bool is_serial_log_packet_tx_busy(serial_log_packet_t *packet_ptr);
bool is_serial_log_packet_rx_ready(serial_log_packet_t *packet_ptr);


#endif /* SERIAL_LOG_PACKET_MAX_RX_SIZE */
