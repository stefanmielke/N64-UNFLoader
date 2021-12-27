/***************************************************************
                           network.cpp

Handles USB I/O for networking.
***************************************************************/
#pragma warning(push, 0)
    #include "Include/lodepng.h"
#pragma warning(pop)
#include "main.h"
#include "helper.h"
#include "device.h"
#include "network.h"

#include <curl/curl.h>
#include <enet/enet.h>


/*********************************
              Macros
*********************************/

#define VERBOSE     1
#define BUFFER_SIZE 512
#define HEADER_SIZE 16
#define BLINKRATE   0.5
#define PATH_SIZE   256
#define HISTORY_SIZE 100


/*********************************
       Function Prototypes
*********************************/

void network_decidedata(ftdi_context_t* cart, u32 info, char* buffer, u32* read);
void network_handle_udp_start_server(ftdi_context_t* cart, u32 size, char* buffer, u32* read);
void network_handle_udp_connect(ftdi_context_t* cart, u32 size, char* buffer, u32* read);
void network_handle_udp_disconnect(ftdi_context_t* cart, u32 size, char* buffer, u32* read);
void network_handle_udp_send(ftdi_context_t* cart, u32 size, char* buffer, u32* read);
void network_handle_url_fetch(ftdi_context_t* cart, u32 size, char* buffer, u32* read);
void network_handle_url_post(ftdi_context_t* cart, u32 size, char* buffer, u32* read);
void network_handle_text(ftdi_context_t* cart, u32 size, char* buffer, u32* read);
size_t network_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);

typedef struct MemoryWriteCallback {
    char* memory;
    size_t cur_size;
} MemoryWriteCallback;

typedef enum NetworkType {
    NT_NOTHING,
    NT_CLIENT,
    NT_SERVER,
} NetworkType;


/*********************************
         Global Variables
*********************************/

static int network_headerdata[HEADER_SIZE];
CURL* curl;

ENetAddress address;
ENetHost* host;
ENetPeer *peer;
NetworkType network_type;


/*==============================
    network_main
    The main debug loop for input/output
    @param A pointer to the cart context
==============================*/

void network_main(ftdi_context_t *cart)
{
    int i;
    int alignment;
    char *outbuff, *inbuff;
    u16 cursorpos = 0;
    DWORD pending = 0;
    WINDOW* inputwin = newwin(1, getmaxx(stdscr), getmaxy(stdscr)-1, 0);

    network_type = NT_NOTHING;

    // Initialize debug mode keyboard input
    if (global_timeout != 0)
        pdprint("Network mode started. Press ESC to stop or wait for timeout.\n\n", CRDEF_INPUT, global_timeout);
    else
        pdprint("Network mode started. Press ESC to stop.\n\n", CRDEF_INPUT);
    timeout(0);
    curs_set(0);
    keypad(stdscr, TRUE);

    // Initialize our buffers
    outbuff = (char*) malloc(BUFFER_SIZE);
    inbuff = (char*) malloc(BUFFER_SIZE);
    memset(inbuff, 0, BUFFER_SIZE);

    // Open file for debug output
    if (global_debugout != NULL)
    {
        global_debugoutptr = fopen(global_debugout, "w+");
        if (global_debugoutptr == NULL)
        {
            pdprint("\n", CRDEF_ERROR);
            terminate("Unable to open %s for writing debug output.", global_debugout);
        }
    }

    // Decide the alignment based off the cart that's connected
    switch (cart->carttype)
    {
        case CART_EVERDRIVE: alignment = 16; break;
        case CART_SC64: alignment = 4; break;
        default: alignment = 0;
    }

    // init cURL for URL fetch
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // init ENet
    if (enet_initialize () != 0)
        terminate("Error initializing ENet");

    // Start the network server loop
    for ( ; ; ) 
	{
        int ch = getch();

        // If ESC is pressed, stop the loop
		if (ch == 27 || (global_timeout != 0 && global_timeouttime < time(NULL)))
			break;

        // Check if we have pending data
        FT_GetQueueStatus(cart->handle, &pending);
        if (pending > 0)
        {
            u32 info, read = 0;
            #if VERBOSE
                pdprint("\nReceiving %d bytes\n", CRDEF_INFO, pending);
            #endif

            // Ensure we have valid data by reading the header
            FT_Read(cart->handle, outbuff, 4, &cart->bytes_read);
            read += cart->bytes_read;
            if (outbuff[0] != 'D' || outbuff[1] != 'M' || outbuff[2] != 'A' || outbuff[3] != '@')
                terminate("Unexpected DMA header: %c %c %c %c.", outbuff[0], outbuff[1], outbuff[2], outbuff[3]);

            // Get information about the incoming data
            FT_Read(cart->handle, outbuff, 4, &cart->bytes_read);
            read += cart->bytes_read;
            info = swap_endian(outbuff[3] << 24 | outbuff[2] << 16 | outbuff[1] << 8 | outbuff[0]);

            // Decide what to do with the received data
            network_decidedata(cart, info, outbuff, &read);

            // Read the completion signal
            FT_Read(cart->handle, outbuff, 4, &cart->bytes_read);
            read += cart->bytes_read;
            if (outbuff[0] != 'C' || outbuff[1] != 'M' || outbuff[2] != 'P' || outbuff[3] != 'H')
                terminate("Did not receive completion signal: %c %c %c %c.", outbuff[0], outbuff[1], outbuff[2], outbuff[3]);

            // Ensure byte alignment by reading X amount of bytes needed
            if (alignment != 0 && (read % alignment) != 0)
            {
                int left = alignment - (read % alignment);
                FT_Read(cart->handle, outbuff, left, &cart->bytes_read);
            }
        }

        // If we got no more data, sleep a bit to be kind to the CPU
        FT_GetQueueStatus(cart->handle, &pending);
        if (pending == 0)
        {
            if (network_type == NT_NOTHING)
            {
                #ifndef LINUX
                    Sleep(10);
                #else
                    usleep(10);
                #endif
            }
            else
            {
                ENetEvent event;
                if (enet_host_service(host, &event, 1))
                {
                    switch (event.type)
                    {
                    case ENET_EVENT_TYPE_CONNECT:
                        {
                            printf("\nA new client connected from %x:%u.\n",
                                event.peer->address.host,
                                event.peer->address.port);
                            std::string result("A new client connected");
                            device_senddata(NETTYPE_UDP_DISCONNECT, (char *)result.c_str(), result.length());
                        } break;
                    case ENET_EVENT_TYPE_DISCONNECT:
                        {
                            printf("\n%u disconnected.\n", event.peer->connectID);
                            std::string result("Disconnected");
                            device_senddata(NETTYPE_UDP_DISCONNECT, (char *)result.c_str(), result.length());
                        } break;
                    case ENET_EVENT_TYPE_RECEIVE:
                        {
                            printf("\nA packet of length %zu containing %s was received from %u on channel %u.\n",
                                event.packet->dataLength,
                                event.packet->data,
                                event.peer->connectID,
                                event.channelID);

                            device_senddata(NETTYPE_UDP_SEND, (char *)event.packet->data, event.packet->dataLength);
                            enet_packet_destroy(event.packet);
                        } break;
                    }
                }
            }
        }
    }

    curl_global_cleanup();

    enet_deinitialize();

    // Close the debug output file if it exists
    if (global_debugoutptr != NULL)
    {
        fclose(global_debugoutptr);
        global_debugoutptr = NULL;
    }

    // Clean up everything
    free(outbuff);
    free(inbuff);

    wclear(inputwin);
    wrefresh(inputwin);
    delwin(inputwin);
    curs_set(0);
}


/*==============================
    network_decidedata
    Decides what function to call based on the command type stored in the info
    @param A pointer to the cart context
    @param 4 bytes with the info and size (from the cartridge)
    @param The buffer to use
    @param A pointer to a variable that stores the number of bytes read
==============================*/

void network_decidedata(ftdi_context_t* cart, u32 info, char* buffer, u32* read)
{
    u8 command = (info >> 24) & 0xFF;
    u32 size = info & 0xFFFFFF;

    // Decide what to do with the data based off the command type
    switch (command)
    {
        case NETTYPE_TEXT:             network_handle_text(cart, size, buffer, read); break;
        case NETTYPE_UDP_START_SERVER: network_handle_udp_start_server(cart, size, buffer, read); break;
        case NETTYPE_UDP_CONNECT:      network_handle_udp_connect(cart, size, buffer, read); break;
        case NETTYPE_UDP_DISCONNECT:   network_handle_udp_disconnect(cart, size, buffer, read); break;
        case NETTYPE_UDP_SEND:         network_handle_udp_send(cart, size, buffer, read); break;
        case NETTYPE_URL_FETCH:        network_handle_url_fetch(cart, size, buffer, read); break;
        case NETTYPE_URL_POST:         network_handle_url_post(cart, size, buffer, read); break;
        default:                       printf("Unknown data type: %d", command);
    }
}


/*==============================
    network_handle_url_fetch
    Handles NETTYPE_URL_FETCH
    @param A pointer to the cart context
    @param The size of the incoming data
    @param The buffer to use
    @param A pointer to a variable that stores the number of bytes read
==============================*/

void network_handle_url_fetch(ftdi_context_t* cart, u32 size, char* buffer, u32* read)
{
    int total = 0;
    int left = size;

    // Ensure the data fits within our buffer
    if (left > BUFFER_SIZE)
        left = BUFFER_SIZE;

    // Read bytes until we finished
    while (left != 0)
    {
        // Read from the USB and print it
        FT_Read(cart->handle, buffer, left, &cart->bytes_read);
        #if VERBOSE
        pdprint("%.*s", CRDEF_PRINT, cart->bytes_read, buffer);
        #endif

        // Store the amount of bytes read
        (*read) += cart->bytes_read;
        total += cart->bytes_read;

        // Ensure the data fits within our buffer
        left = size - total;
        if (left > BUFFER_SIZE)
            left = BUFFER_SIZE;
    }

    CURLcode res;
    MemoryWriteCallback data_buffer;
    data_buffer.cur_size = 0;
    data_buffer.memory = NULL;

    curl = curl_easy_init();
    if (!curl)
        terminate("Error loading cURL");

    std::string url(buffer, 0, size);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, network_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&data_buffer);

    #if VERBOSE
    pdprint("\ncalling URL: %s\n", CRDEF_INFO, url.c_str());
    #endif

    res = curl_easy_perform(curl);
    if(res != CURLE_OK)
      pdprint("\ncurl_easy_perform() failed: %s\n", CRDEF_ERROR, curl_easy_strerror(res));

    curl_easy_cleanup(curl);

    device_senddata(NETTYPE_TEXT, data_buffer.memory, data_buffer.cur_size);
}

size_t network_write_callback(char *contents, size_t size, size_t nmemb, void *userdata)
{
    size_t real_size = size * nmemb;
    MemoryWriteCallback* data_buffer = (MemoryWriteCallback *)userdata;
    if (data_buffer->memory)
        data_buffer->memory = (char *)realloc(data_buffer->memory, data_buffer->cur_size + real_size);
    else
        data_buffer->memory = (char *)malloc(real_size);

    memcpy(&(data_buffer->memory[data_buffer->cur_size]), contents, real_size);
    data_buffer->cur_size += real_size;

    return real_size;
}


/*==============================
    network_handle_url_post
    Handles NETTYPE_URL_POST
    @param A pointer to the cart context
    @param The size of the incoming data
    @param The buffer to use
    @param A pointer to a variable that stores the number of bytes read
==============================*/

void network_handle_url_post(ftdi_context_t* cart, u32 size, char* buffer, u32* read)
{
    int total = 0;
    int left = size;

    // Ensure the data fits within our buffer
    if (left > BUFFER_SIZE)
        left = BUFFER_SIZE;

    // Read bytes until we finished
    while (left != 0)
    {
        // Read from the USB and print it
        FT_Read(cart->handle, buffer, left, &cart->bytes_read);
        #if VERBOSE
        pdprint("%.*s", CRDEF_PRINT, cart->bytes_read, buffer);
        #endif

        // Store the amount of bytes read
        (*read) += cart->bytes_read;
        total += cart->bytes_read;

        // Ensure the data fits within our buffer
        left = size - total;
        if (left > BUFFER_SIZE)
            left = BUFFER_SIZE;
    }

    curl = curl_easy_init();
    if (!curl)
        terminate("Error loading cURL");

    std::string url(buffer, 0, size);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);

    #if VERBOSE
    pdprint("\nPosting to URL: %s\n", CRDEF_INFO, url.c_str());
    #endif

    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK)
    {
        std::string result(curl_easy_strerror(res));
        device_senddata(NETTYPE_URL_POST, (char*)result.c_str(), result.length());
        pdprint("\ncurl_easy_perform() failed: %s\n", CRDEF_ERROR, curl_easy_strerror(res));
        return;
    }

    curl_easy_cleanup(curl);

    std::string result("done");
    device_senddata(NETTYPE_URL_POST, (char*)result.c_str(), result.length() + 1);
}


/*==============================
    network_handle_text
    Handles NETTYPE_TEXT
    @param A pointer to the cart context
    @param The size of the incoming data
    @param The buffer to use
    @param A pointer to a variable that stores the number of bytes read
==============================*/

void network_handle_text(ftdi_context_t* cart, u32 size, char* buffer, u32* read)
{
    int total = 0;
    int left = size;

    // Ensure the data fits within our buffer
    if (left > BUFFER_SIZE)
        left = BUFFER_SIZE;

    // Read bytes until we finished
    while (left != 0)
    {
        // Read from the USB and print it
        FT_Read(cart->handle, buffer, left, &cart->bytes_read);
        pdprint("%.*s", CRDEF_PRINT, cart->bytes_read, buffer);

        // Store the amount of bytes read
        (*read) += cart->bytes_read;
        total += cart->bytes_read;

        // Ensure the data fits within our buffer
        left = size - total;
        if (left > BUFFER_SIZE)
            left = BUFFER_SIZE;
    }
}


/*==============================
    network_handle_udp_start_server
    Handles NETTYPE_UDP_START_SERVER
    @param A pointer to the cart context
    @param The size of the incoming data
    @param The buffer to use
    @param A pointer to a variable that stores the number of bytes read
==============================*/

void network_handle_udp_start_server(ftdi_context_t* cart, u32 size, char* buffer, u32* read)
{
    int total = 0;
    int left = size;

    // Ensure the data fits within our buffer
    if (left > BUFFER_SIZE)
        left = BUFFER_SIZE;

    // Read bytes until we finished
    while (left != 0)
    {
        // Read from the USB and print it
        FT_Read(cart->handle, buffer, left, &cart->bytes_read);
        #if VERBOSE
        pdprint("%.*s", CRDEF_PRINT, cart->bytes_read, buffer);
        #endif

        // Store the amount of bytes read
        (*read) += cart->bytes_read;
        total += cart->bytes_read;

        // Ensure the data fits within our buffer
        left = size - total;
        if (left > BUFFER_SIZE)
            left = BUFFER_SIZE;
    }

    if (network_type != NT_NOTHING)
    {
        pdprint("Network Type already set. Please call 'network_handle_udp_disconnect' first.", CRDEF_ERROR);
        return;
    }

    address.host = ENET_HOST_ANY;
    address.port = std::stoi(std::string(buffer));
    network_type = NT_SERVER;

    #if VERBOSE
    pdprint("\nCreating server on port '%d'\n", CRDEF_INFO, address.port);
    #endif

    host = enet_host_create(&address /* the address to bind the server host to */, 
                            3        /* allow up to 3 clients and/or outgoing connections */,
                            2        /* allow up to 2 channels to be used, 0 and 1 */,
                            0        /* assume any amount of incoming bandwidth */,
                            0        /* assume any amount of outgoing bandwidth */);
    if (host)
        pdprint("Server created.\n", CRDEF_INFO);
    else
        terminate("Server could not be created.");
}


/*==============================
    network_handle_udp_connect
    Handles NETTYPE_UDP_CONNECT
    @param A pointer to the cart context
    @param The size of the incoming data
    @param The buffer to use
    @param A pointer to a variable that stores the number of bytes read
==============================*/

void network_handle_udp_connect(ftdi_context_t* cart, u32 size, char* buffer, u32* read)
{
    int total = 0;
    int left = size;

    // Ensure the data fits within our buffer
    if (left > BUFFER_SIZE)
        left = BUFFER_SIZE;

    // Read bytes until we finished
    while (left != 0)
    {
        // Read from the USB and print it
        FT_Read(cart->handle, buffer, left, &cart->bytes_read);
        #if VERBOSE
        pdprint("%.*s", CRDEF_PRINT, cart->bytes_read, buffer);
        #endif

        // Store the amount of bytes read
        (*read) += cart->bytes_read;
        total += cart->bytes_read;

        // Ensure the data fits within our buffer
        left = size - total;
        if (left > BUFFER_SIZE)
            left = BUFFER_SIZE;
    }

    if (network_type != NT_NOTHING)
    {
        pdprint("Network Type already set. Please call 'network_handle_udp_disconnect' first.", CRDEF_ERROR);
        return;
    }

    std::string full_address(buffer);
    int colon_pos = full_address.find(":");

    enet_address_set_host(&address, full_address.substr(0, colon_pos).c_str());
    address.port = std::stoi(full_address.substr(colon_pos + 1));
    
    #if VERBOSE
    pdprint("\nConnecting to IP '%d', Port '%d'\n", CRDEF_INFO, address.host, address.port);
    #endif

    host = enet_host_create(NULL /* create a client host */,
                            1    /* only allow 1 outgoing connection */,
                            2    /* allow up 2 channels to be used, 0 and 1 */,
                            0    /* assume any amount of incoming bandwidth */,
                            0    /* assume any amount of outgoing bandwidth */);
    if (!host)
        terminate("Client could not be created.");

    peer = enet_host_connect(host, &address, 2, 0);
    if (!peer)
        terminate("No available peers for initiating an ENet connection.");

    /* Wait up to 5 seconds for the connection attempt to succeed. */
    ENetEvent event;
    if (enet_host_service(host, &event, 5000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT)
    {
        pdprint("\nConnected to server.\n", CRDEF_INFO);
        std::string result("Connected");
        device_senddata(NETTYPE_UDP_CONNECT, (char *)result.c_str(), result.length());

        network_type = NT_CLIENT;
    }
    else
    {
        /* Either the 5 seconds are up or a disconnect event was */
        /* received. Reset the peer in the event the 5 seconds   */
        /* had run out without any significant event.            */
        enet_peer_reset(peer);
        std::string result("Failed to connect to server");
        pdprint("\n%s.\n", CRDEF_ERROR, result.c_str());
        device_senddata(NETTYPE_UDP_DISCONNECT, (char *)result.c_str(), result.length());
    }
}


/*==============================
    network_handle_udp_disconnect
    Handles NETTYPE_UDP_DISCONNECT
    @param A pointer to the cart context
    @param The size of the incoming data
    @param The buffer to use
    @param A pointer to a variable that stores the number of bytes read
==============================*/

void network_handle_udp_disconnect(ftdi_context_t* cart, u32 size, char* buffer, u32* read)
{
    if (network_type == NT_SERVER)
    {
        pdprint("\nDisconnecting server\n", CRDEF_INFO);

        enet_host_destroy(host);

        pdprint("\nServer disconnected\n", CRDEF_INFO);
    }
    else if (network_type == NT_CLIENT)
    {
        pdprint("\nDisconnecting client\n", CRDEF_INFO);

        enet_peer_disconnect(peer, 0);

        ENetEvent event;
        /* Allow up to 3 seconds for the disconnect to succeed
        * and drop any packets received packets.
        */
        bool done = false;
        while (enet_host_service(host, &event, 3000) > 0 && !done)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_RECEIVE:
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                pdprint("\nDisconnection succeeded\n", CRDEF_INFO);
                done = true;
                break;
            }
        }
        // if timeout occured above, force disconnect
        if (!done)
            enet_peer_reset(peer);

        std::string result("Disconnection succeeded");
        device_senddata(NETTYPE_UDP_DISCONNECT, (char *)result.c_str(), result.length());
    }
    else
    {
        pdprint("\nNot connected\n", CRDEF_INFO);
    }

    network_type = NT_NOTHING;
}


/*==============================
    network_handle_udp_send
    Handles NETTYPE_UDP_SEND
    @param A pointer to the cart context
    @param The size of the incoming data
    @param The buffer to use
    @param A pointer to a variable that stores the number of bytes read
==============================*/

void network_handle_udp_send(ftdi_context_t* cart, u32 size, char* buffer, u32* read)
{
    int total = 0;
    int left = size;

    // Ensure the data fits within our buffer
    if (left > BUFFER_SIZE)
        left = BUFFER_SIZE;

    // Read bytes until we finished
    while (left != 0)
    {
        // Read from the USB and print it
        FT_Read(cart->handle, buffer, left, &cart->bytes_read);
        #if VERBOSE
        pdprint("%.*s", CRDEF_PRINT, cart->bytes_read, buffer);
        #endif

        // Store the amount of bytes read
        (*read) += cart->bytes_read;
        total += cart->bytes_read;

        // Ensure the data fits within our buffer
        left = size - total;
        if (left > BUFFER_SIZE)
            left = BUFFER_SIZE;
    }

    if (network_type == NT_NOTHING)
    {
        pdprint("Network Type not set. Please call 'network_handle_udp_start_server' or 'network_handle_udp_connect' first.", CRDEF_ERROR);
        return;
    }

    ENetPacket* packet = enet_packet_create(buffer, size, 0);
    if (network_type == NT_SERVER)
        enet_host_broadcast(host, 0, packet);
    else
        enet_peer_send(peer, 0, packet);
        
    #if VERBOSE
    pdprint("Data sent", CRDEF_INFO);
    #endif
}
