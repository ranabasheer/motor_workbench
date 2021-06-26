#include <stddef.h>
#include "serial_log_packet.h"
#include <serial_log_interface.h>


#define ESC_BYTE  0xA5
#define SOP_BYTE  0xAA
#define EOP_BYTE  0xBB

#define SERIAL_LOG_PACKET_PRINTF_ENABLED 0

//char buffer[64];
//int buffer_pos = 0;

/**@brief Function for updating the current CRC-16 value for a single byte input.
 *
 * @param[in] current_crc The current calculated CRC-16 value.
 * @param[in] byte        The input data byte for the computation.
 *
 * @return The updated CRC-16 value, based on the input supplied.
 */
static uint16_t crc16_get(uint16_t current_crc, uint8_t byte)
{
    static const uint16_t crc16_table[16] =
    {
        0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
        0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400
    };

    uint16_t temp;

    // Compute checksum of lower four bits of a byte.
    temp         = crc16_table[current_crc & 0xF];
    current_crc  = (current_crc >> 4u) & 0x0FFFu;
    current_crc  = current_crc ^ temp ^ crc16_table[byte & 0xF];

    // Now compute checksum of upper four bits of a byte.
    temp         = crc16_table[current_crc & 0xF];
    current_crc  = (current_crc >> 4u) & 0x0FFFu;
    current_crc  = current_crc ^ temp ^ crc16_table[(byte >> 4u) & 0xF];

    return current_crc;
}

static void recv_data_byte(serial_log_packet_t *packet_ptr, uint8_t data)
{
    //packet_ptr->buffer[packet_ptr->index++] = data;
    if(packet_ptr->index < packet_ptr->length)
    {
        serial_log_store_8bit(packet_ptr->buffer, packet_ptr->index++, data);
        //packet_ptr->buffer[packet_ptr->index++] = data;
        //packet_ptr->index++;
    }
    packet_ptr->crc = crc16_get(packet_ptr->crc, data);
}

static void send_control_byte(serial_log_packet_t *packet_ptr, uint8_t control)
{
    //send the packet
    serial_log_uart_tx(control);
    packet_ptr->state.tx = WAIT_FOR_ACK;
}

static void send_data_byte(serial_log_packet_t *packet_ptr, uint8_t data)
{
    //send the packet
    serial_log_uart_tx(data);
    packet_ptr->state.tx = WAIT_FOR_ACK;
    //update CRC
    packet_ptr->crc = crc16_get(packet_ptr->crc, data);
    packet_ptr->index++;
}


void serial_log_packet_start(serial_log_packet_t *packet_ptr)
{
    _nassert(packet_ptr->state.tx == TX_INACTIVE);
    packet_ptr->state.tx = SEND_SOP;
    packet_ptr->index = 0;
    packet_ptr->length = 2; //ESC SOP
}

void serial_log_packet_done(serial_log_packet_t *packet_ptr)
{
    _nassert(packet_ptr->state.tx == TX_INACTIVE);
    packet_ptr->state.tx = SEND_EOP;
    packet_ptr->index = 0;
    packet_ptr->length = 4; //CRC_L, CRC_H, ESC EOP
}


void serial_log_packet_reset_rx(serial_log_packet_t *packet_ptr)
{
  packet_ptr->state.rx  = WAIT_FOR_ESC; //check the checksum and call the higher layer
  packet_ptr->index     = 0;
}

void serial_log_packet_reset_tx(serial_log_packet_t *packet_ptr)
{
  packet_ptr->state.tx  = TX_INACTIVE; //check the checksum and call the higher layer
  packet_ptr->index     = 0;
}

void serial_log_packet_send(serial_log_packet_t *packet_ptr, uint8_t *data, uint16_t length)
{
  _nassert(packet_ptr->state.tx == TX_INACTIVE);

  packet_ptr->state.tx  = SEND_DATA;
  packet_ptr->buffer    = data;
  packet_ptr->length    = length;
  packet_ptr->index     = 0;
}

void serial_log_packet_recv(serial_log_packet_t *packet_ptr, uint8_t *data, uint16_t length)
{
  packet_ptr->state.rx  = WAIT_FOR_ESC;
  packet_ptr->buffer    = data;
  packet_ptr->length    = length;
  packet_ptr->index     = 0;
}

/*
 * This function should be called by the underlying hardware when data is available.
 */
void serial_log_packet_build_tx(serial_log_packet_t *packet_ptr)
{
  uint8_t data;
  switch(packet_ptr->state.tx)
  {
    case TX_INACTIVE:
      //we are waiting for the next event
    break;
    
    case WAIT_FOR_ACK:
    if(is_serial_log_uart_tx_more())
    {
      packet_ptr->state.tx = packet_ptr->next_tx_state;
    }
    break;
    
    case SEND_SOP: //this is the start of packet
      send_control_byte(packet_ptr, ESC_BYTE);
      packet_ptr->next_tx_state = SEND_SOP_NOW;
    break;

    case SEND_SOP_NOW: //this is the start of packet
      send_control_byte(packet_ptr, SOP_BYTE);
      packet_ptr->crc = 0;
      packet_ptr->next_tx_state = TX_INACTIVE;
    break;

    case SEND_DATA_ESC_NOW:
      send_data_byte(packet_ptr, ESC_BYTE); //we are sending ESC as data
      packet_ptr->next_tx_state = (packet_ptr->index<packet_ptr->length)?SEND_DATA:TX_INACTIVE;
    break;

    case SEND_CONTROL_ESC_NOW:
      send_control_byte(packet_ptr, ESC_BYTE); //we are sending ESC as data
      packet_ptr->index++;
      switch(packet_ptr->index)
      {
      case 1:
          packet_ptr->next_tx_state = SEND_CRC_H;
          break;

      case 2:
          packet_ptr->next_tx_state = SEND_EOP_ESC_NOW;
          break;
      }
      //packet_ptr->next_tx_state = (packet_ptr->index<packet_ptr->length)?SEND_DATA:TX_INACTIVE;
    break;

    case SEND_DATA:
      data = serial_log_read_8bit(packet_ptr->buffer, packet_ptr->index);
      if(data == ESC_BYTE)
      {
        //send one more ESC packet
        send_control_byte(packet_ptr, ESC_BYTE);
        packet_ptr->next_tx_state = SEND_DATA_ESC_NOW;
      }
      else
      {
        //send one more ESC packet
        send_data_byte(packet_ptr, data);
        packet_ptr->next_tx_state = (packet_ptr->index<packet_ptr->length)?SEND_DATA:TX_INACTIVE;
      }
    break;

    case SEND_EOP: //before we send out EOP we have to send out the CRC
      data = packet_ptr->crc&0xFF;
      if(data == ESC_BYTE)
      {
          send_control_byte(packet_ptr, ESC_BYTE);
          packet_ptr->next_tx_state = SEND_CONTROL_ESC_NOW;
      }
      else
      {
          send_control_byte(packet_ptr, data);
          packet_ptr->next_tx_state = SEND_CRC_H;
          packet_ptr->index++;
      }
    break;
    
    case SEND_CRC_H:
      data = (uint8_t)(packet_ptr->crc>>8)&0xFF;
      if(data == ESC_BYTE)
      {
          send_control_byte(packet_ptr, ESC_BYTE);
          packet_ptr->next_tx_state = SEND_CONTROL_ESC_NOW;
      }
      else
      {
          send_control_byte(packet_ptr, data);
          packet_ptr->next_tx_state = SEND_EOP_ESC_NOW;
          packet_ptr->index++;
      }
    break;
    
    case SEND_EOP_ESC_NOW:
      send_control_byte(packet_ptr, ESC_BYTE);
      packet_ptr->next_tx_state = SEND_EOP_NOW;
    break;

    case SEND_EOP_NOW:
      send_control_byte(packet_ptr, EOP_BYTE);
      packet_ptr->next_tx_state = TX_INACTIVE;
    break;
  }
}
//uint8_t buffer[10];
//uint8_t index = 0;
void serial_log_packet_build_rx(serial_log_packet_t *packet_ptr, void (*rx_data_handler)(serial_log_packet_t *))
{
  uint8_t data;
  if(is_serial_log_uart_rx_ready())
  {
    data = serial_log_uart_rx();
    //buffer[index++] = data;
    if(packet_ptr->index >= packet_ptr->length)
    {
      packet_ptr->index = 0;
    }
    //data = 0;//buffer[i];
    
    switch(packet_ptr->state.rx)
    {
      case WAIT_FOR_ESC:
      {
        if(data == ESC_BYTE)
        {
          packet_ptr->state.rx = WAIT_FOR_SOP;
        }
        break;
      }
      
      case WAIT_FOR_SOP:
      {
        if(data == SOP_BYTE)
        {
          packet_ptr->state.rx = WAIT_FOR_DATA;
          packet_ptr->index = 0;
          packet_ptr->crc = 0;
        }
        else
        {
#if SERIAL_LOG_PACKET_PRINTF_ENABLED
          printf("bad 0\n");
#endif
          packet_ptr->state.rx = WAIT_FOR_ESC;
        }
        break;
      }
      
      case WAIT_FOR_DATA:
      {
        if(data == ESC_BYTE)
        {
          packet_ptr->state.rx = WAIT_FOR_NEXT_DATA;
        }
        else
        {
          recv_data_byte(packet_ptr, data);
        }
        break;
      }
      
      case WAIT_FOR_NEXT_DATA:
      {
        switch(data)
        {
          case ESC_BYTE:
            packet_ptr->state.rx = WAIT_FOR_DATA;
            recv_data_byte(packet_ptr, ESC_BYTE);
            break;
            
          case EOP_BYTE:
            packet_ptr->state.rx = WAIT_FOR_ESC; //check the checksum and call the higher layer
            if(packet_ptr->crc == 0)
            {
#if SERIAL_LOG_PACKET_PRINTF_ENABLED
              //printf("Good CRC\r\n");
#endif
              packet_ptr->state.rx = RX_READY;
              if(rx_data_handler != NULL)
              {
                  rx_data_handler(packet_ptr);
              }
              packet_ptr->state.rx = WAIT_FOR_ESC;
            }
            else
            {
#if SERIAL_LOG_PACKET_PRINTF_ENABLED
              printf("Bad CRC\r\n");
#endif
              packet_ptr->state.rx = WAIT_FOR_ESC;
            }
            break;

          default:
            //packet_ptr->state.rx = WAIT_FOR_ESC;
            break;
        }
        break;
      }
    }
  }
}

bool is_serial_log_packet_tx_busy(serial_log_packet_t *packet_ptr)
{
    return (packet_ptr->state.tx == TX_INACTIVE)?false:true;
}

bool is_serial_log_packet_rx_ready(serial_log_packet_t *packet_ptr)
{
    return (packet_ptr->state.rx == RX_READY)?true:false;
}
