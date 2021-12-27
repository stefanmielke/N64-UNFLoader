#ifndef UNFL_NETWORK_H
#define UNFL_NETWORK_H

    /*********************************
             Settings macros
    *********************************/

    // Settings
    #define NETWORK_MODE      1   // Enable/Disable debug mode
    #define NETWORK_INIT_MSG  1   // Print a message when debug mode has initialized
    #define USE_FAULTTHREAD   1   // Create a fault detection thread (libultra only)
    #define OVERWRITE_OSPRINT 1   // Replaces osSyncPrintf calls with network_printf (libultra only)
    #define MAX_COMMANDS      25  // The max amount of user defined commands possible
    
    // Fault thread definitions (libultra only)
    #define FAULT_THREAD_ID    13
    #define FAULT_THREAD_PRI   125
    #define FAULT_THREAD_STACK 0x2000
    
    // USB thread definitions (libultra only)
    #define USB_THREAD_ID    14
    #define USB_THREAD_PRI   126
    #define USB_THREAD_STACK 0x2000
    
    // Network types defintions
    #define NETTYPE_TEXT              0x01
    #define NETTYPE_UDP_START_SERVER  0x02
    #define NETTYPE_UDP_CONNECT       0x03
    #define NETTYPE_UDP_DISCONNECT    0x04
    #define NETTYPE_UDP_SEND          0x05
    #define NETTYPE_URL_FETCH         0x06
    #define NETTYPE_URL_DOWNLOAD      0x07
    #define NETTYPE_URL_POST          0x08
    
    
    /*********************************
             Debug Functions
    *********************************/
    
    #if NETWORK_MODE
        
        /*==============================
            network_initialize
            Initializes the debug and USB library.
            Returns '1' if communication is OK, '0' if not.
        ==============================*/
        
        extern char network_initialize();
        
        
        /*==============================
            network_printf
            Prints a formatted message to the developer's command prompt.
            Supports up to 256 characters.
            @param A string to print
            @param variadic arguments to print as well
        ==============================*/
        
        extern void network_printf(const char* message, ...);


        /*==============================
            network_url_fetch
            Send a request to get data from URL.
            Supports up to 256 characters.
            @param A URL
        ==============================*/

        extern void network_url_fetch(const char* url);


        /*==============================
            network_url_fetch
            Send a POST request to get data from URL (no DATA can be sent yet).
            Supports up to 256 characters.
            @param A URL
        ==============================*/

        extern void network_url_post(const char* url);


        /*==============================
            network_udp_start_server
            Starts a server that will accept incoming data from clients.
            Supports up to 256 characters.
            @param Port to accept connections from.
        ==============================*/

        extern void network_udp_start_server(const char* port);


        /*==============================
            network_udp_connect
            Send a request to connect to a server.
            Supports up to 256 characters.
            @param IP address and port to connect to (eg.: "127.0.0.1:1234").
        ==============================*/

        extern void network_udp_connect(const char* ip_address);


        /*==============================
            network_udp_disconnect
            Disconnects from the current server.
            Supports up to 256 characters.
        ==============================*/

        extern void network_udp_disconnect();


        /*==============================
            network_udp_send_data
            Send data to the connected IP address.
            @param Data to send
            @param Size of data in bytes
        ==============================*/

        extern void network_udp_send_data(void* data, int size);


        /*==============================
            network_assert
            Halts the program if the expression fails.
            @param The expression to test
        ==============================*/
        
        #define network_assert(expr) (expr) ? ((void)0) : _network_assert(#expr, __FILE__, __LINE__)
        
        
        /*==============================
            network_pollcommands
            Check the USB for incoming commands.
        ==============================*/
        
        extern void network_pollcommands();
        
        
        /*==============================
            network_addcommand
            Adds a command for the USB to read.
            @param The command name
            @param The command description
            @param The function pointer to execute                                                                                  
        ==============================*/
        
        extern void network_addcommand(char* command, char* description, char*(*execute)());

        
        /*==============================
            network_parsecommand
            Stores the next part of the incoming command into the provided buffer.
            Make sure the buffer can fit the amount of data from network_sizecommand!
            If you pass NULL, it skips this command.
            @param The buffer to store the data in
        ==============================*/
        
        extern void network_parsecommand(void* buffer);
                
        
        /*==============================
            network_sizecommand
            Returns the size of the data from this part of the command.
            @return The size of the data in bytes, or 0
        ==============================*/
        
        extern int network_sizecommand();
        
        
        /*==============================
            network_printcommands
            Prints a list of commands to the developer's command prompt.
        ==============================*/
        
        extern void network_printcommands();

        
        // Ignore this, use the macro instead
        extern void _network_assert(const char* expression, const char* file, int line);
        
        // Include usb.h automatically
        #include "usb.h"
        
    #else
        
        // Overwrite library functions with useless macros if debug mode is disabled
        #define network_initialize() 
        #define network_printf(__VA_ARGS__) 
        #define network_screenshot(a, b, c)
        #define network_assert(a)
        #define network_pollcommands()
        #define network_addcommand(a, b, c)
        #define network_parsecommand(a) NULL
        #define network_sizecommand() 0
        #define network_printcommands()
        #define usb_initialize() 0
        #define usb_getcart() 0
        #define usb_write(a, b, c)
        #define usb_poll() 0
        #define usb_read(a, b)
        #define usb_skip(a)
        #define usb_rewind(a)
        #define usb_purge()
        
    #endif
    
#endif