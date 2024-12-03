#include "Connector.h"

static UART_CONNECTOR_CONTEXT_S* g_uart_context_p;

static int OpenUart(UART_CONNECTOR_CONTEXT_S* ctx)
{
    ctx->nUartFd = open((char*)ctx->nUartDev, O_RDWR | O_NOCTTY);
    if (ctx->nUartFd < 0)
    {
        AUTOSOME_ERROR("failed to open output file");
        perror("failed to open output file");
        return -1;
    }

    return 0;
}

static int SetUart(UART_CONNECTOR_CONTEXT_S* ctx)
{
    struct termios options;
    int ret = 0;

    if (tcgetattr(ctx->nUartFd, &options) != 0)
    {
        AUTOSOME_DEBUG("Setup serial fialed!");
        return -1;
    }

    int speed_arr[] = {B1500000, B1152000, B1000000, B921600,
                       B576000, B500000, B460800, B230400, B115200, B57600,
                       B38400, B19200, B9600, B4800, B2400, B1800, B1200,
                       B600, B300, B200, B150, B134, B110, B75, B50, B0};
    int name_arr[] = {1500000, 1152000, 1000000, 921600,
                      576000, 500000, 460800, 230400, 115200, 57600,
                      38400, 19200, 9600, 4800, 2400, 1800, 1200,
                      600, 300, 200, 150, 134, 110, 75, 50, 0};

    if (tcgetattr(ctx->nUartFd, &options) != 0)
    {
        AUTOSOME_DEBUG("Setup serial fialed!");
        return -1;
    }

    AUTOSOME_DEBUG("speed = %d", ctx->speed);
    for (uint32_t i = 0; i < sizeof(speed_arr) / sizeof(int); i++)
    {
        if (ctx->speed == name_arr[i])
        {
            ret = cfsetispeed(&options, speed_arr[i]);
            if (ret)
            {
                perror("cfsetispeed");
                AUTOSOME_DEBUG("set in speed failed");
            }

            ret = cfsetospeed(&options, speed_arr[i]);
            if (ret)
            {
                perror("cfsetispeed");
                AUTOSOME_DEBUG("set out speed failed");
            }
            break;
        }
    }

    options.c_cflag |= CLOCAL;
    options.c_cflag |= CREAD;

    switch (ctx->flow_ctrl)
    {
    case 0: // no flow control
        options.c_cflag &= ~CRTSCTS;
        options.c_iflag &= ~(BRKINT | ICRNL | INPCK |ISTRIP | IXON);
        break;
    case 1: // hardware flow control
        options.c_cflag |= CRTSCTS;
        break;
    case 2: // software flow control
        options.c_cflag |= IXON | IXOFF | IXANY;
        break;
    default:
        AUTOSOME_DEBUG("Unsupported flow control");
        return -1;
    }

    options.c_cflag &= ~CSIZE;
    switch (ctx->databits)
    {
    case 5:
        options.c_cflag |= CS5;
        break;
    case 6:
        options.c_cflag |= CS6;
        break;
    case 7:
        options.c_cflag |= CS7;
        break;
    case 8:
        options.c_cflag |= CS8;
        break;
    default:
        AUTOSOME_DEBUG("Unsupported databits!");
        return -1;
    }

    switch (ctx->parity)
    {
    case 'n': // no parity
    case 'N':
        options.c_cflag &= ~PARENB;
        options.c_iflag &= ~INPCK;
        break;
    case 'o': // odd parity
    case 'O':
        options.c_cflag |= (PARODD | PARENB);
        options.c_iflag &= INPCK;
        break;
    case 'e': // even parity
    case 'E':
        options.c_cflag |= PARENB;
        options.c_cflag &= ~PARODD;
        options.c_iflag |= INPCK;
        break;
    case 's': // blank
    case 'S':
        options.c_cflag &= ~PARENB;
        options.c_iflag &= ~CSTOPB;
        break;
    default:
        AUTOSOME_DEBUG("Unsupported parity");
        return -1;
    }

    switch (ctx->stopbits)
    {
    case 1:
        options.c_cflag &= ~CSTOPB;
        break;
    case 2:
        options.c_cflag |= CSTOPB;
        break;
    default:
        AUTOSOME_DEBUG("Unsupported stop bits");
        return -1;
    }

    // mode
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); /*Input*/
    options.c_oflag &= ~OPOST;                          /*Output*/

    // wait_time: 0.1s; min char to read:1
    options.c_cc[VTIME] = 1;
    options.c_cc[VMIN] = 1;

    // if data overflow, receive data, but not read
    tcflush(ctx->nUartFd, TCIFLUSH);

    // save configuration
    if (tcsetattr(ctx->nUartFd, TCSANOW, &options) != 0)
    {
        AUTOSOME_DEBUG("set serial error!");
        return -1;
    }
    return 0;
}


int16_t InitUart(UART_CONNECTOR_CONTEXT_S** ctx, const char * path)
{
    g_uart_context_p = (UART_CONNECTOR_CONTEXT_S*)malloc(sizeof(UART_CONNECTOR_CONTEXT_S));

    if(g_uart_context_p == NULL)
    {
        AUTOSOME_ERROR("malloc failed!!!");
        return -1;
    }
    *ctx = g_uart_context_p;

    g_uart_context_p->speed = 115200;
    // int speed = 9600;
    g_uart_context_p->flow_ctrl = 0;
    g_uart_context_p->databits = 8;
    g_uart_context_p->stopbits = 1;
    g_uart_context_p->parity = 'n';

    g_uart_context_p->nUartFd = -1;


    int ret = 0;

    memset(g_uart_context_p->nUartDev, 0, 128);
    strcpy((char*)g_uart_context_p->nUartDev, path);

    ret = OpenUart(g_uart_context_p);
    if (ret < 0)
    {
        AUTOSOME_ERROR("OpenUart Failed !!! \n");
        return -1;
    }

    ret = SetUart(g_uart_context_p);
    if (ret < 0)
    {
        AUTOSOME_ERROR("SetUart Failed !!!\n");
        return -2;
    }

    return 0;
}

int16_t DeinitUart(UART_CONNECTOR_CONTEXT_S* ctx)
{
    if (ctx->nUartFd > 0)
    {
        // AUTOSOME_DEBUG();
        close(ctx->nUartFd);
        ctx->nUartFd = -1;

        free(ctx);
        ctx = NULL;
    }
    return 0;
}



int16_t UartSendData(UART_CONNECTOR_CONTEXT_S* ctx, const uint8_t* data, uint32_t len)
{
    int n = 0;
    uint32_t sendCount = 0;

    do
    {
        n = write(ctx->nUartFd, (data + sendCount), len - sendCount);
        if (n < 0)
        {
            perror("write data failed !!!");
            return n;
        }

        sendCount += n;
    } while (sendCount < len);
    // free(buff);
    return 0;
}

int32_t UartRecvData(UART_CONNECTOR_CONTEXT_S* ctx, uint8_t* buff, int32_t len)
{
    int ret = 0;
    ret = read(ctx->nUartFd, buff, len);
    // AUTOSOME_DEBUG("ret = %d", ret);
    return ret;
}
