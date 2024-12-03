#ifndef _CONNECTOR_HPP_
#define _CONNECTOR_HPP_

#include <stdint.h>  //int8_t uint16_t
#include <fcntl.h>   //open  fcntl
//#include <string>    //string
#include <termios.h> //termios
#include <unistd.h>  //close

//#include <iostream> //cout
//#include <vector>   //verctor

#include <debug.h>

#define UART_DEVICE "/dev/ttyS1"

typedef struct UART_CONNECTOR_CONTEXT_S__ {
    uint8_t nUartDev[128];
    int nUartFd;

    int speed;

    int flow_ctrl;
    int databits;
    int stopbits;
    int parity;

} UART_CONNECTOR_CONTEXT_S;

int16_t InitUart(UART_CONNECTOR_CONTEXT_S** ctx, const char * path);
int16_t DeinitUart(UART_CONNECTOR_CONTEXT_S* ctx);
int16_t UartSendData(UART_CONNECTOR_CONTEXT_S* ctx, const uint8_t* data, uint32_t len);
int32_t UartRecvData(UART_CONNECTOR_CONTEXT_S* ctx, uint8_t* buff, int32_t len);
#endif /* _CONNECTOR_HPP_ */
