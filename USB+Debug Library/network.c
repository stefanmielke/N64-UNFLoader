/***************************************************************
							debug.c

A basic debug library that makes use of the USB library for N64
flashcarts.
https://github.com/buu342/N64-UNFLoader
***************************************************************/
#include "network.h"
#ifndef LIBDRAGON
#include <ultra64.h>
#include <PR/os_internal.h>	 // Needed for Crash's Linux toolchain
#else
#include <libdragon.h>
#include <stdio.h>
#include <math.h>
#endif
#include <stdarg.h>
#include <string.h>

#if NETWORK_MODE

/*********************************
		   Definitions
*********************************/

#define MSG_FAULT 0x10
#define MSG_READ 0x11
#define MSG_WRITE 0x12

#define USBERROR_NONE 0
#define USBERROR_NOTTEXT 1
#define USBERROR_UNKNOWN 2
#define USBERROR_TOOMUCH 3
#define USBERROR_CUSTOM 4

#define HASHTABLE_SIZE 7
#define COMMAND_TOKENS 10
#define BUFFER_SIZE 256

/*********************************
Libultra types (for libdragon)
*********************************/

#ifdef LIBDRAGON
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;

typedef signed char s8;
typedef short s16;
typedef long s32;
typedef long long s64;

typedef volatile unsigned char vu8;
typedef volatile unsigned short vu16;
typedef volatile unsigned long vu32;
typedef volatile unsigned long long vu64;

typedef volatile signed char vs8;
typedef volatile short vs16;
typedef volatile long vs32;
typedef volatile long long vs64;

typedef float f32;
typedef double f64;
#endif

/*********************************
			 Structs
*********************************/

// Register struct
typedef struct {
	u32 mask;
	u32 value;
	char *string;
} regDesc;

// Thread message struct
typedef struct {
	int msgtype;
	int datatype;
	void *buff;
	int size;
} usbMesg;

// Network command struct
typedef struct {
	char *command;
	char *description;
	char *(*execute)();
	void *next;
} networkCommand;

/*********************************
		Function Prototypes
*********************************/

#ifndef LIBDRAGON
// Threads
#if USE_FAULTTHREAD
static void network_thread_fault(void *arg);
#endif
static void network_thread_usb(void *arg);

// Other
#if OVERWRITE_OSPRINT
static void *network_osSyncPrintf_implementation(void *unused, const char *str, size_t len);
#endif
#else
static void network_thread_usb(void *arg);
#endif

/*********************************
			 Globals
*********************************/

// Function pointers
#ifndef LIBDRAGON
extern int _Printf(void *(*copyfunc)(void *, const char *, size_t), void *, const char *, va_list);
#if OVERWRITE_OSPRINT
extern void *__printfunc;
#endif
#endif

// Debug globals
static char network_initialized = 0;
static char network_buffer[BUFFER_SIZE];

// Commands hashtable related
static networkCommand *network_commands_hashtable[HASHTABLE_SIZE];
static networkCommand network_commands_elements[MAX_COMMANDS];
static int network_commands_count = 0;

// Command parsing related
static int network_command_current = 0;
static int network_command_totaltokens = 0;
static int network_command_incoming_start[COMMAND_TOKENS];
static int network_command_incoming_size[COMMAND_TOKENS];
static char *network_command_error;

#ifndef LIBDRAGON
// Fault thread globals
#if USE_FAULTTHREAD
static OSMesgQueue faultMessageQ;
static OSMesg faultMessageBuf;
static OSThread faultThread;
static u64 faultThreadStack[FAULT_THREAD_STACK / sizeof(u64)];
#endif

// USB thread globals
static OSMesgQueue usbMessageQ;
static OSMesg usbMessageBuf;
static OSThread usbThread;
static u64 usbThreadStack[USB_THREAD_STACK / sizeof(u64)];

// List of error causes
static regDesc causeDesc[] = {
	{CAUSE_BD, CAUSE_BD, "BD"},
	{CAUSE_IP8, CAUSE_IP8, "IP8"},
	{CAUSE_IP7, CAUSE_IP7, "IP7"},
	{CAUSE_IP6, CAUSE_IP6, "IP6"},
	{CAUSE_IP5, CAUSE_IP5, "IP5"},
	{CAUSE_IP4, CAUSE_IP4, "IP4"},
	{CAUSE_IP3, CAUSE_IP3, "IP3"},
	{CAUSE_SW2, CAUSE_SW2, "IP2"},
	{CAUSE_SW1, CAUSE_SW1, "IP1"},
	{CAUSE_EXCMASK, EXC_INT, "Interrupt"},
	{CAUSE_EXCMASK, EXC_MOD, "TLB modification exception"},
	{CAUSE_EXCMASK, EXC_RMISS, "TLB exception on load or instruction fetch"},
	{CAUSE_EXCMASK, EXC_WMISS, "TLB exception on store"},
	{CAUSE_EXCMASK, EXC_RADE, "Address error on load or instruction fetch"},
	{CAUSE_EXCMASK, EXC_WADE, "Address error on store"},
	{CAUSE_EXCMASK, EXC_IBE, "Bus error exception on instruction fetch"},
	{CAUSE_EXCMASK, EXC_DBE, "Bus error exception on data reference"},
	{CAUSE_EXCMASK, EXC_SYSCALL, "System call exception"},
	{CAUSE_EXCMASK, EXC_BREAK, "Breakpoint exception"},
	{CAUSE_EXCMASK, EXC_II, "Reserved instruction exception"},
	{CAUSE_EXCMASK, EXC_CPU, "Coprocessor unusable exception"},
	{CAUSE_EXCMASK, EXC_OV, "Arithmetic overflow exception"},
	{CAUSE_EXCMASK, EXC_TRAP, "Trap exception"},
	{CAUSE_EXCMASK, EXC_VCEI, "Virtual coherency exception on intruction fetch"},
	{CAUSE_EXCMASK, EXC_FPE, "Floating point exception (see fpcsr)"},
	{CAUSE_EXCMASK, EXC_WATCH, "Watchpoint exception"},
	{CAUSE_EXCMASK, EXC_VCED, "Virtual coherency exception on data reference"},
	{0, 0, ""}};

// List of register descriptions
static regDesc srDesc[] = {{SR_CU3, SR_CU3, "CU3"},
						   {SR_CU2, SR_CU2, "CU2"},
						   {SR_CU1, SR_CU1, "CU1"},
						   {SR_CU0, SR_CU0, "CU0"},
						   {SR_RP, SR_RP, "RP"},
						   {SR_FR, SR_FR, "FR"},
						   {SR_RE, SR_RE, "RE"},
						   {SR_BEV, SR_BEV, "BEV"},
						   {SR_TS, SR_TS, "TS"},
						   {SR_SR, SR_SR, "SR"},
						   {SR_CH, SR_CH, "CH"},
						   {SR_CE, SR_CE, "CE"},
						   {SR_DE, SR_DE, "DE"},
						   {SR_IBIT8, SR_IBIT8, "IM8"},
						   {SR_IBIT7, SR_IBIT7, "IM7"},
						   {SR_IBIT6, SR_IBIT6, "IM6"},
						   {SR_IBIT5, SR_IBIT5, "IM5"},
						   {SR_IBIT4, SR_IBIT4, "IM4"},
						   {SR_IBIT3, SR_IBIT3, "IM3"},
						   {SR_IBIT2, SR_IBIT2, "IM2"},
						   {SR_IBIT1, SR_IBIT1, "IM1"},
						   {SR_KX, SR_KX, "KX"},
						   {SR_SX, SR_SX, "SX"},
						   {SR_UX, SR_UX, "UX"},
						   {SR_KSU_MASK, SR_KSU_USR, "USR"},
						   {SR_KSU_MASK, SR_KSU_SUP, "SUP"},
						   {SR_KSU_MASK, SR_KSU_KER, "KER"},
						   {SR_ERL, SR_ERL, "ERL"},
						   {SR_EXL, SR_EXL, "EXL"},
						   {SR_IE, SR_IE, "IE"},
						   {0, 0, ""}};

// List of floating point registers descriptions
static regDesc fpcsrDesc[] = {{FPCSR_FS, FPCSR_FS, "FS"},
							  {FPCSR_C, FPCSR_C, "C"},
							  {FPCSR_CE, FPCSR_CE, "Unimplemented operation"},
							  {FPCSR_CV, FPCSR_CV, "Invalid operation"},
							  {FPCSR_CZ, FPCSR_CZ, "Division by zero"},
							  {FPCSR_CO, FPCSR_CO, "Overflow"},
							  {FPCSR_CU, FPCSR_CU, "Underflow"},
							  {FPCSR_CI, FPCSR_CI, "Inexact operation"},
							  {FPCSR_EV, FPCSR_EV, "EV"},
							  {FPCSR_EZ, FPCSR_EZ, "EZ"},
							  {FPCSR_EO, FPCSR_EO, "EO"},
							  {FPCSR_EU, FPCSR_EU, "EU"},
							  {FPCSR_EI, FPCSR_EI, "EI"},
							  {FPCSR_FV, FPCSR_FV, "FV"},
							  {FPCSR_FZ, FPCSR_FZ, "FZ"},
							  {FPCSR_FO, FPCSR_FO, "FO"},
							  {FPCSR_FU, FPCSR_FU, "FU"},
							  {FPCSR_FI, FPCSR_FI, "FI"},
							  {FPCSR_RM_MASK, FPCSR_RM_RN, "RN"},
							  {FPCSR_RM_MASK, FPCSR_RM_RZ, "RZ"},
							  {FPCSR_RM_MASK, FPCSR_RM_RP, "RP"},
							  {FPCSR_RM_MASK, FPCSR_RM_RM, "RM"},
							  {0, 0, ""}};
#endif

/*********************************
		 Debug functions
*********************************/

/*==============================
	network_initialize
	Initializes the debug library
==============================*/

char network_initialize() {
	// Initialize the USB functions
	if (!usb_initialize())
		return 0;

// Overwrite osSyncPrintf
#ifndef LIBDRAGON
#if OVERWRITE_OSPRINT
	__printfunc = (void *)network_osSyncPrintf_implementation;
#endif

// Initialize the fault thread
#if USE_FAULTTHREAD
	osCreateThread(&faultThread, FAULT_THREAD_ID, network_thread_fault, 0,
				   (faultThreadStack + FAULT_THREAD_STACK / sizeof(u64)), FAULT_THREAD_PRI);
	osStartThread(&faultThread);
#endif

	// Initialize the USB thread
	osCreateThread(&usbThread, USB_THREAD_ID, network_thread_usb, 0,
				   (usbThreadStack + USB_THREAD_STACK / sizeof(u64)), USB_THREAD_PRI);
	osStartThread(&usbThread);
#endif

	// Mark the debug mode as initialized
	network_initialized = 1;
#if network_INIT_MSG
	network_printf("Debug mode initialized!\n\n");
#endif

	return 1;
}

#ifndef LIBDRAGON
/*==============================
	printf_handler
	Handles printf memory copying
	@param The buffer to copy the partial string to
	@param The string to copy
	@param The length of the string
	@returns The end of the buffer that was written to
==============================*/

static void *printf_handler(void *buf, const char *str, size_t len) {
	return ((char *)memcpy(buf, str, len) + len);
}
#endif

/*==============================
	network_printf
	Prints a formatted message to the developer's command prompt.
	Supports up to 256 characters.
	@param A string to print
	@param variadic arguments to print as well
==============================*/

void network_printf(const char *message, ...) {
	int len = 0;
	usbMesg msg;
	va_list args;

	// use the internal libultra printf function to format the string
	va_start(args, message);
#ifndef LIBDRAGON
	len = _Printf(&printf_handler, network_buffer, message, args);
#else
	len = vsprintf(network_buffer, message, args);
#endif
	va_end(args);

	// Attach the '\0' if necessary
	if (0 <= len)
		network_buffer[len] = '\0';

	// Send the printf to the usb thread
	msg.msgtype = MSG_WRITE;
	msg.datatype = NETTYPE_TEXT;
	msg.buff = network_buffer;
	msg.size = len + 1;
#ifndef LIBDRAGON
	osSendMesg(&usbMessageQ, (OSMesg)&msg, OS_MESG_BLOCK);
#else
	network_thread_usb(&msg);
#endif
}

/*==============================
	network_url_fetch
	Send a request to get data from URL.
	Supports up to 256 characters.
	@param A URL
==============================*/

void network_url_fetch(const char *url)
{
	int len = strlen(url);
	memcpy(network_buffer, url, len);

	// Attach the '\0' if necessary
	if (len >= 0)
		network_buffer[len] = '\0';

	// Send the printf to the usb thread
	usbMesg msg;
	msg.msgtype = MSG_WRITE;
	msg.datatype = NETTYPE_URL_FETCH;
	msg.buff = network_buffer;
	msg.size = len;
#ifndef LIBDRAGON
	osSendMesg(&usbMessageQ, (OSMesg)&msg, OS_MESG_BLOCK);
#else
	network_thread_usb(&msg);
#endif
}

/*==============================
	network_url_fetch
	Send a POST request to get data from URL.
	Supports up to 256 characters.
	@param A URL
	@param Data to POST (can be NULL)
==============================*/

void network_url_post(const char* url)
{
	int len = strlen(url);
	memcpy(network_buffer, url, len);

	// Attach the '\0' if necessary
	if (len >= 0)
		network_buffer[len] = '\0';

	// Send the printf to the usb thread
	usbMesg msg;
	msg.msgtype = MSG_WRITE;
	msg.datatype = NETTYPE_URL_POST;
	msg.buff = network_buffer;
	msg.size = len + 1;
#ifndef LIBDRAGON
	osSendMesg(&usbMessageQ, (OSMesg)&msg, OS_MESG_BLOCK);
#else
	network_thread_usb(&msg);
#endif
}

/*==============================
	network_udp_start_server
	Starts a server that will accept incoming data from clients.
	Supports up to 256 characters.
==============================*/

void network_udp_start_server(const char* port)
{
	int len = strlen(port);
	memcpy(network_buffer, port, len);

	// Attach the '\0' if necessary
	if (len >= 0)
		network_buffer[len] = '\0';

	// Send the printf to the usb thread
	usbMesg msg;
	msg.msgtype = MSG_WRITE;
	msg.datatype = NETTYPE_UDP_START_SERVER;
	msg.buff = network_buffer;
	msg.size = len + 1;
#ifndef LIBDRAGON
	osSendMesg(&usbMessageQ, (OSMesg)&msg, OS_MESG_BLOCK);
#else
	network_thread_usb(&msg);
#endif
}

/*==============================
	network_udp_connect
	Send a request to connect to an IP address.
	Supports up to 256 characters.
	@param IP address to connect to
==============================*/

void network_udp_connect(const char* ip_address)
{
	int len = strlen(ip_address);
	memcpy(network_buffer, ip_address, len);

	// Attach the '\0' if necessary
	if (len >= 0)
		network_buffer[len] = '\0';

	// Send the printf to the usb thread
	usbMesg msg;
	msg.msgtype = MSG_WRITE;
	msg.datatype = NETTYPE_UDP_CONNECT;
	msg.buff = network_buffer;
	msg.size = len + 1;
#ifndef LIBDRAGON
	osSendMesg(&usbMessageQ, (OSMesg)&msg, OS_MESG_BLOCK);
#else
	network_thread_usb(&msg);
#endif
}

/*==============================
	network_udp_disconnect
	Disconnects from the current server.
	Supports up to 256 characters.
==============================*/

void network_udp_disconnect()
{
	network_buffer[0] = '\0';
	// Send the printf to the usb thread
	usbMesg msg;
	msg.msgtype = MSG_WRITE;
	msg.datatype = NETTYPE_UDP_DISCONNECT;
	msg.size = 1;
	msg.buff = network_buffer;
#ifndef LIBDRAGON
	osSendMesg(&usbMessageQ, (OSMesg)&msg, OS_MESG_BLOCK);
#else
	network_thread_usb(&msg);
#endif
}

/*==============================
	network_udp_send_data
	Send data to an IP address.
	Supports up to 256 characters.
	@param IP address to connect to
	@param Data to send
==============================*/

void network_udp_send_data(void* data, int size)
{
	// Send the printf to the usb thread
	usbMesg msg;
	msg.msgtype = MSG_WRITE;
	msg.datatype = NETTYPE_UDP_SEND;
	msg.buff = data;
	msg.size = size;
#ifndef LIBDRAGON
	osSendMesg(&usbMessageQ, (OSMesg)&msg, OS_MESG_BLOCK);
#else
	network_thread_usb(&msg);
#endif
}

/*==============================
	network_addcommand
	Adds a command for the USB to listen for
	@param The command name
	@param The command description
	@param The function pointer to execute
==============================*/

void network_addcommand(char *command, char *description, char *(*execute)()) {
	int entry = command[0] % HASHTABLE_SIZE;
	networkCommand *slot = network_commands_hashtable[entry];

	// Ensure debug mode is initialized
	if (!network_initialized)
		return;

	// Ensure we haven't hit the command limit
	if (network_commands_count == MAX_COMMANDS) {
		network_printf("Max commands exceeded!\n");
		return;
	}

	// Look for an empty spot in the hash table
	if (slot != NULL) {
		while (slot->next != NULL)
			slot = slot->next;
		slot->next = &network_commands_elements[network_commands_count];
	} else
		network_commands_hashtable[entry] = &network_commands_elements[network_commands_count];

	// Fill this spot with info about this command
	network_commands_elements[network_commands_count].command = command;
	network_commands_elements[network_commands_count].description = description;
	network_commands_elements[network_commands_count].execute = execute;
	network_commands_count++;
}

/*==============================
	network_printcommands
	Prints a list of commands to the developer's command prompt.
==============================*/

void network_printcommands() {
	int i;

	// Ensure debug mode is initialized
	if (!network_initialized)
		return;

	// Ensure there are commands to print
	if (network_commands_count == 0)
		return;

	// Print the commands
	network_printf("Available USB commands\n----------------------\n");
	for (i = 0; i < network_commands_count; i++)
		network_printf("%d. %s\n\t%s\n", i + 1, network_commands_elements[i].command,
					   network_commands_elements[i].description);
	network_printf("\n");
}

/*==============================
	network_pollcommands
	Check the USB for incoming commands
==============================*/

void network_pollcommands() {
	usbMesg msg;

	// Ensure debug mode is initialized
	if (!network_initialized)
		return;

	// Send a read message to the USB thread
	msg.msgtype = MSG_READ;
#ifndef LIBDRAGON
	osSendMesg(&usbMessageQ, (OSMesg)&msg, OS_MESG_BLOCK);
#else
	network_thread_usb(&msg);
#endif
}

/*==============================
	network_sizecommand
	Returns the size of the data from this part of the command
	@return The size of the data in bytes, or 0
==============================*/

int network_sizecommand() {
	// If we're out of commands to read, return 0
	if (network_command_current == network_command_totaltokens)
		return 0;

	// Otherwise, return the amount of data to read
	return network_command_incoming_size[network_command_current];
}

/*==============================
	network_parsecommand
	Stores the next part of the incoming command into the provided buffer.
	Make sure the buffer can fit the amount of data from network_sizecommand!
	If you pass NULL, it skips this command.
	@param The buffer to store the data in
==============================*/

void network_parsecommand(void *buffer) {
	u8 curr = network_command_current;

	// Skip this command if no buffer exists
	if (buffer == NULL) {
		network_command_current++;
		return;
	}

	// If we're out of commands to read, do nothing
	if (curr == network_command_totaltokens)
		return;

	// Read from the correct offset
	usb_skip(network_command_incoming_start[curr]);
	usb_read(buffer, network_command_incoming_size[curr]);
	usb_rewind(network_command_incoming_size[curr] + network_command_incoming_start[curr]);
	network_command_current++;
}

/*==============================
	network_commands_setup
	Reads the entire incoming string and breaks it into parts for
	network_parsecommand and network_sizecommand
==============================*/

static void network_commands_setup() {
	int i;
	int datasize = USBHEADER_GETSIZE(usb_poll());
	int dataleft = datasize;
	int filesize = 0;
	char filestep = 0;

	// Initialize the starting offsets at -1
	memset(network_command_incoming_start, -1, COMMAND_TOKENS * sizeof(int));

	// Read data from USB in blocks
	while (dataleft > 0) {
		int readsize = BUFFER_SIZE;
		if (readsize > dataleft)
			readsize = dataleft;

		// Read a block from USB
		memset(network_buffer, 0, BUFFER_SIZE);
		usb_read(network_buffer, readsize);

		// Parse the block
		for (i = 0; i < readsize && dataleft > 0; i++) {
			// If we're not reading a file
			int offset = datasize - dataleft;
			u8 tok = network_command_totaltokens;

			// Decide what to do based on the current character
			switch (network_buffer[i]) {
				case ' ':
				case '\0':
					if (filestep < 2) {
						if (network_command_incoming_start[tok] != -1) {
							network_command_incoming_size[tok] =
								offset - network_command_incoming_start[tok];
							network_command_totaltokens++;
						}

						if (network_buffer[i] == '\0')
							dataleft = 0;
						break;
					}
				case '@':
					filestep++;
					if (filestep < 3)
						break;
				default:
					// Decide what to do based on the file handle
					if (filestep == 0 && network_command_incoming_start[tok] == -1) {
						// Store the data offsets and sizes in the global command buffers
						network_command_incoming_start[tok] = offset;
					} else if (filestep == 1) {
						// Get the filesize
						filesize = filesize * 10 + network_buffer[i] - '0';
					} else if (filestep > 1) {
						// Store the file offsets and sizes in the global command buffers
						network_command_incoming_start[tok] = offset;
						network_command_incoming_size[tok] = filesize;
						network_command_totaltokens++;

						// Skip a bunch of bytes
						if ((readsize - i) - filesize < 0)
							usb_skip(filesize - (readsize - i));
						dataleft -= filesize;
						i += filesize;
						filesize = 0;
						filestep = 0;
					}
					break;
			}
			dataleft--;
		}
	}

	// Rewind the USB fully
	usb_rewind(datasize);
}

/*==============================
	network_thread_usb
	Handles the USB thread
	@param Arbitrary data that the thread can receive
==============================*/

static void network_thread_usb(void *arg) {
	char errortype = USBERROR_NONE;
	usbMesg *threadMsg;

#ifndef LIBDRAGON
	// Create the message queue for the USB message
	osCreateMesgQueue(&usbMessageQ, &usbMessageBuf, 1);
#else
	// Set the received thread message to the argument
	threadMsg = (usbMesg *)arg;
#endif

	// Thread loop
	while (1) {
#ifndef LIBDRAGON
		// Wait for a USB message to arrive
		osRecvMesg(&usbMessageQ, (OSMesg *)&threadMsg, OS_MESG_BLOCK);
#endif

		// Ensure there's no data in the USB (which handles MSG_READ)
		while (usb_poll() != 0) {
			int header = usb_poll();
			networkCommand *entry;

			// Ensure we're receiving a text command
			if (USBHEADER_GETTYPE(header) != DATATYPE_TEXT) {
				errortype = USBERROR_NOTTEXT;
				usb_purge();
				break;
			}

			// Initialize the command trackers
			network_command_totaltokens = 0;
			network_command_current = 0;

			// Break the USB command into parts
			network_commands_setup();

			// Ensure we don't read past our buffer
			if (network_sizecommand() > BUFFER_SIZE) {
				errortype = USBERROR_TOOMUCH;
				usb_purge();
				break;
			}

			// Read from the USB to retrieve the command name
			network_parsecommand(network_buffer);

			// Iterate through the hashtable to see if we find the command
			entry = network_commands_hashtable[network_buffer[0] % HASHTABLE_SIZE];
			while (entry != NULL) {
				// If we found the command
				if (!strncmp(network_buffer, entry->command, network_command_incoming_size[0])) {
					// Execute the command function and exit the while loop
					network_command_error = entry->execute();
					if (network_command_error != NULL)
						errortype = USBERROR_CUSTOM;
					usb_purge();
					break;
				}
				entry = entry->next;
			}

			// If no command was found
			if (entry == NULL) {
				// Purge the USB contents and print unknown command
				usb_purge();
				errortype = USBERROR_UNKNOWN;
			}
		}

		// Spit out an error if there was one during the command parsing
		if (errortype != USBERROR_NONE) {
			switch (errortype) {
				case USBERROR_NOTTEXT:
					usb_write(DATATYPE_TEXT, "Error: USB data was not text\n", 29 + 1);
					break;
				case USBERROR_UNKNOWN:
					usb_write(DATATYPE_TEXT, "Error: Unknown command\n", 23 + 1);
					break;
				case USBERROR_TOOMUCH:
					usb_write(DATATYPE_TEXT, "Error: Command too large\n", 25 + 1);
					break;
				case USBERROR_CUSTOM:
					usb_write(DATATYPE_TEXT, network_command_error,
							  strlen(network_command_error) + 1);
					usb_write(DATATYPE_TEXT, "\n", 1 + 1);
					break;
			}
			errortype = USBERROR_NONE;
		}

		// Handle the other USB messages
		switch (threadMsg->msgtype) {
			case MSG_WRITE:
				usb_write(threadMsg->datatype, threadMsg->buff, threadMsg->size);
				break;
		}

// If we're in libdragon, break out of the loop as we don't need it
#ifdef LIBDRAGON
		break;
#endif
	}
}

#ifndef LIBDRAGON
#if OVERWRITE_OSPRINT

/*==============================
	network_osSyncPrintf_implementation
	Overwrites osSyncPrintf calls with this one
	@param Unused
	@param The buffer with the string
	@param The amount of characters to write
	@returns The end of the buffer that was written to
==============================*/

static void *network_osSyncPrintf_implementation(void *unused, const char *str, size_t len) {
	void *ret;
	usbMesg msg;

	// Clear the debug buffer and copy the formatted string to it
	memset(network_buffer, 0, len + 1);
	ret = ((char *)memcpy(network_buffer, str, len) + len);

	// Send the printf to the usb thread
	msg.msgtype = MSG_WRITE;
	msg.datatype = DATATYPE_TEXT;
	msg.buff = network_buffer;
	msg.size = len + 1;
	osSendMesg(&usbMessageQ, (OSMesg)&msg, OS_MESG_BLOCK);

	// Return the end of the buffer
	return ret;
}

#endif

#if USE_FAULTTHREAD

/*==============================
	network_printreg
	Prints info about a register
	@param The value of the register
	@param The name of the register
	@param The registry description to use
==============================*/

static void network_printreg(u32 value, char *name, regDesc *desc) {
	char first = 1;
	network_printf("%s\t\t0x%08x <", name, value);
	while (desc->mask != 0) {
		if ((value & desc->mask) == desc->value) {
			(first) ? (first = 0) : ((void)network_printf(","));
			network_printf("%s", desc->string);
		}
		desc++;
	}
	network_printf(">\n");
}

/*==============================
	network_thread_fault
	Handles the fault thread
	@param Arbitrary data that the thread can receive
==============================*/

static void network_thread_fault(void *arg) {
	OSMesg msg;
	OSThread *curr;

	// Create the message queue for the fault message
	osCreateMesgQueue(&faultMessageQ, &faultMessageBuf, 1);
	osSetEventMesg(OS_EVENT_FAULT, &faultMessageQ, (OSMesg)MSG_FAULT);

	// Thread loop
	while (1) {
		// Wait for a fault message to arrive
		osRecvMesg(&faultMessageQ, (OSMesg *)&msg, OS_MESG_BLOCK);

		// Get the faulted thread
		curr = (OSThread *)__osGetCurrFaultedThread();
		if (curr != NULL) {
			__OSThreadContext *context = &curr->context;

			// Print the basic info
			network_printf("Fault in thread: %d\n\n", curr->id);
			network_printf("pc\t\t0x%08x\n", context->pc);
			if (assert_file == NULL)
				network_printreg(context->cause, "cause", causeDesc);
			else
				network_printf("cause\t\tAssertion failed in file '%s', line %d.\n", assert_file,
							   assert_line);
			network_printreg(context->sr, "sr", srDesc);
			network_printf("badvaddr\t0x%08x\n\n", context->badvaddr);

			// Print the registers
			network_printf("at 0x%016llx v0 0x%016llx v1 0x%016llx\n", context->at, context->v0,
						   context->v1);
			network_printf("a0 0x%016llx a1 0x%016llx a2 0x%016llx\n", context->a0, context->a1,
						   context->a2);
			network_printf("a3 0x%016llx t0 0x%016llx t1 0x%016llx\n", context->a3, context->t0,
						   context->t1);
			network_printf("t2 0x%016llx t3 0x%016llx t4 0x%016llx\n", context->t2, context->t3,
						   context->t4);
			network_printf("t5 0x%016llx t6 0x%016llx t7 0x%016llx\n", context->t5, context->t6,
						   context->t7);
			network_printf("s0 0x%016llx s1 0x%016llx s2 0x%016llx\n", context->s0, context->s1,
						   context->s2);
			network_printf("s3 0x%016llx s4 0x%016llx s5 0x%016llx\n", context->s3, context->s4,
						   context->s5);
			network_printf("s6 0x%016llx s7 0x%016llx t8 0x%016llx\n", context->s6, context->s7,
						   context->t8);
			network_printf("t9 0x%016llx gp 0x%016llx sp 0x%016llx\n", context->t9, context->gp,
						   context->sp);
			network_printf("s8 0x%016llx ra 0x%016llx\n\n", context->s8, context->ra);

			// Print the floating point registers
			network_printreg(context->fpcsr, "fpcsr", fpcsrDesc);
			network_printf("\n");
			network_printf("d0  %.15e\td2  %.15e\n", context->fp0.d, context->fp2.d);
			network_printf("d4  %.15e\td6  %.15e\n", context->fp4.d, context->fp6.d);
			network_printf("d8  %.15e\td10 %.15e\n", context->fp8.d, context->fp10.d);
			network_printf("d12 %.15e\td14 %.15e\n", context->fp12.d, context->fp14.d);
			network_printf("d16 %.15e\td18 %.15e\n", context->fp16.d, context->fp18.d);
			network_printf("d20 %.15e\td22 %.15e\n", context->fp20.d, context->fp22.d);
			network_printf("d24 %.15e\td26 %.15e\n", context->fp24.d, context->fp26.d);
			network_printf("d28 %.15e\td30 %.15e\n", context->fp28.d, context->fp30.d);
		}
	}
}

#endif
#endif

#endif