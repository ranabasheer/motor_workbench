#include "serial_log_interface.h"
//#include "F2806x_Sci.h"
#include "DSP28x_project.h"


#define __BYTESIZE                          16 //C28x processor defines a single byte as 16bits
#define __BYTESIZE_LOG_2                    4  //2^4=16 to make the operations faster


/*
 * transmits a byte of data
 */
void serial_log_uart_tx(unsigned char data)
{
    SciaRegs.SCITXBUF = data;
}
//#pragma WEAK ( serial_log_uart_tx )

/*
 * receives a byte of data
 */
unsigned char serial_log_uart_rx()
{
    return SciaRegs.SCIRXBUF.all;
}

/*
 * returns true if it is ready to transmit data through UART
 */
bool is_serial_log_uart_tx_more()
{
    return (SciaRegs.SCICTL2.bit.TXRDY == 1);
}

/*
 * returns true if there are bytes ready to be read from UART
 */
bool is_serial_log_uart_rx_ready()
{
    //return (SciaRegs.SCIRXST.bit.RXRDY == 1);
    return (SciaRegs.SCIFFRX.bit.RXFFST ==1);
}

/*
 * iniitializes UART as 115200, 1 stop bit, no parity, 8 char bits,
 */

void serial_log_uart_init()
{
    //setup the SCIA FIFO
    SciaRegs.SCIFFTX.all=0xE040;
    SciaRegs.SCIFFRX.all=0x2044;
    SciaRegs.SCIFFCT.all=0x0;
  
    //
    // Note: Clocks were turned on to the SCIA peripheral
    // in the InitSysCtrl() function
    //
    
    //
    // 1 stop bit,  No loopback, No parity,8 char bits, async mode,
    // idle-line protocol
    //
    SciaRegs.SCICCR.all =0x0007;
    
    //
    // enable TX, RX, internal SCICLK, Disable RX ERR, SLEEP, TXWAKE
    //
    SciaRegs.SCICTL1.all =0x0003;

    SciaRegs.SCICTL2.bit.TXINTENA = 1;
    SciaRegs.SCICTL2.bit.RXBKINTENA = 1;

    
    //115200 baud baud @LSPCLK = 22.5MHz (90 MHz SYSCLK)
    SciaRegs.SCIHBAUD    =  0x0000;
    SciaRegs.SCILBAUD    =  0x0017;

    SciaRegs.SCICTL1.all =0x0023;  // Relinquish SCI from Reset
}

/*
 * returns the number of bits for a given byte
 */
int SERIAL_LOG_BYTES_TO_BITS(int byte_length)
{
    return ((byte_length)<<(__BYTESIZE_LOG_2));
}

/*
 * returns the number of bits for a given byte
 */
int SERIAL_LOG_BITS_TO_BYTES(int bit_length)
{
    return ((bit_length)>>(__BYTESIZE_LOG_2));
}

/*
 * stores 8bits of data in memory
 */
void serial_log_store_8bit(void *dest_memory, int byte_index, unsigned char value)
{
    __byte(((int *)dest_memory),(byte_index)) = (value&0xFF);
}

uint32_t  serial_log_get_time_ms()
{
    #define CPU_CLK_IN_KHZ 90E3
    return (0xFFFFFFFF - CpuTimer0Regs.TIM.all)/CPU_CLK_IN_KHZ; //milli seconds
}

void  serial_log_init_time()
{

    //CpuTimer0Regs
    //configure the timer0 to generate a clock every ms
    //Configure the timer to count
    CpuTimer0Regs.PRD.all       = 0xFFFFFFFF;
    CpuTimer0Regs.TPR.all       = 0;
    CpuTimer0Regs.TPRH.all      = 0;
    CpuTimer0Regs.TCR.bit.TRB   = 1;
    CpuTimer0Regs.TCR.bit.TSS   = 1; //stop the timer;
    CpuTimer0Regs.TCR.bit.SOFT  = 0;
    CpuTimer0Regs.TCR.bit.TIE   = 0;  //disable interrupt
    CpuTimer0Regs.TCR.bit.FREE  = 1;
    CpuTimer0Regs.TCR.bit.TSS   = 0; //start the timer;
}
/*
 * Reads 8 bits of data from memeory
 */
unsigned char serial_log_read_8bit(void *src_memory, int byte_index)
{
    return __byte(((int *)src_memory),(byte_index))&0xFF;
}

