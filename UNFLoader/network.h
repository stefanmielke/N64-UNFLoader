#ifndef __NETWORKMODE_HEADER
#define __NETWORKMODE_HEADER

    #include "device.h"

    // Data types defintions
    #define NETTYPE_UDP_CONNECT  0x01
    #define NETTYPE_UDP_SEND     0x02
    #define NETTYPE_URL_FETCH    0x03
    #define NETTYPE_URL_DOWNLOAD 0x04
    #define NETTYPE_URL_REQUEST  0x05
    #define NETTYPE_TEXT         0x06

    void network_main(ftdi_context_t *cart);

#endif