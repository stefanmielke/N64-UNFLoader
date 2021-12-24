#include <stdio.h>

#include <libdragon.h>
#include "network.h"

int main(void) {
	console_init();

	network_initialize();

	network_udp_connect("localhost:12345");

	printf("Connecting to server...\n");

	while (1) {
		int header;
		if ((header = usb_poll())) {
			int type = USBHEADER_GETTYPE(header);
			int size = USBHEADER_GETSIZE(header);
			char buffer[size];
			usb_read(buffer, size);

			switch (type) {
				case NETTYPE_TEXT:
					printf("You got message: %s\n", buffer);
					break;
				case NETTYPE_UDP_CONNECT:
					printf("Connected to host: %s\n", buffer);
					network_udp_send_data("Yay", 3);
					break;
				case NETTYPE_UDP_DISCONNECT:
					printf("Disconnected from host: %s\n", buffer);
					break;
				case NETTYPE_UDP_SEND:
					printf("You got data: %s\n", buffer);
					break;
				default:
					break;
			}
		}
	}
}