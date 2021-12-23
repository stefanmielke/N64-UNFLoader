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
void network_handle_url_fetch(ftdi_context_t* cart, u32 size, char* buffer, u32* read);
void network_handle_text(ftdi_context_t* cart, u32 size, char* buffer, u32* read);
void network_handle_rawbinary(ftdi_context_t* cart, u32 size, char* buffer, u32* read);
size_t network_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);

typedef struct MemoryWriteCallback {
    char* memory;
    size_t cur_size;
} MemoryWriteCallback;


/*********************************
         Global Variables
*********************************/

static int network_headerdata[HEADER_SIZE];
CURL *curl;


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
    curl = curl_easy_init();
    if (!curl){
        terminate("Error loading cURL");
    }

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
            #ifndef LINUX
                Sleep(10);
            #else
                usleep(10);
            #endif
        }
    }

    curl_global_cleanup();

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
        case NETTYPE_TEXT:        network_handle_text(cart, size, buffer, read); break;
        case NETTYPE_URL_FETCH:   network_handle_url_fetch(cart, size, buffer, read); break;
        default:                  printf("Unknown data type: %d", command);
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
    
    curl_easy_setopt(curl, CURLOPT_URL, buffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, network_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&data_buffer);

    #if VERBOSE
    pdprint("\ncalling URL: %s\n", CRDEF_INFO, buffer);
    #endif

    res = curl_easy_perform(curl);
    if(res != CURLE_OK)
      pdprint("\ncurl_easy_perform() failed: %s\n", CRDEF_ERROR, curl_easy_strerror(res));

    curl_easy_cleanup(curl);

    device_senddata(0x01, data_buffer.memory, data_buffer.cur_size);
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
    network_handle_rawbinary
    Handles DATATYPE_RAWBINARY
    @param A pointer to the cart context
    @param The size of the incoming data
    @param The buffer to use
    @param A pointer to a variable that stores the number of bytes read
==============================*/

void network_handle_rawbinary(ftdi_context_t* cart, u32 size, char* buffer, u32* read)
{
    int total = 0;
    int left = size;
    char* filename = (char*) malloc(PATH_SIZE);
    char* extraname = gen_filename();
    FILE* fp; 

    // Ensure we malloced successfully
    if (filename == NULL || extraname == NULL)
        terminate("Unable to allocate memory for binary file.");

    // Create the binary file to save data to
    memset(filename, 0, PATH_SIZE);
    #ifndef LINUX
        if (global_exportpath != NULL)
                strcat_s(filename, PATH_SIZE, global_exportpath);
        strcat_s(filename, PATH_SIZE, "binaryout-");
        strcat_s(filename, PATH_SIZE, extraname);
        strcat_s(filename, PATH_SIZE, ".bin");
        fopen_s(&fp, filename, "wb+");
    #else
        if (global_exportpath != NULL)
            strcat(filename, global_exportpath);
        strcat(filename, "binaryout-");
        strcat(filename, extraname);
        strcat(filename, ".bin");
        fp = fopen(filename, "wb+");
    #endif

    // Ensure the file was created
    if (fp == NULL)
        terminate("Unable to create binary file.");

    // Ensure the data fits within our buffer
    if (left > BUFFER_SIZE)
        left = BUFFER_SIZE;

    // Read bytes until we finished
    while (left != 0)
    {
        // Read from the USB and save it to our binary file
        FT_Read(cart->handle, buffer, left, &cart->bytes_read);
        fwrite(buffer, 1, left, fp);

        // Store the amount of bytes read
        (*read) += cart->bytes_read;
        total += cart->bytes_read;

        // Ensure the data fits within our buffer
        left = size - total;
        if (left > BUFFER_SIZE)
            left = BUFFER_SIZE;
    }

    // Close the file and free the memory used for the filename
    pdprint("Wrote %d bytes to %s.\n", CRDEF_INFO, size, filename);
    fclose(fp);
    free(filename);
    free(extraname);
}
