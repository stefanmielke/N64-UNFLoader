#include <stdio.h>

#include <libdragon.h>

#include "network.h"

int main(void) {
	console_init();

	network_initialize();

	network_url_fetch("https://smallwebsite.us/");

	printf("waiting for response...\n");
	while (1) {
		int header;

		if ((header = usb_poll())) {
			int size = USBHEADER_GETSIZE(header);
			char buffer[size];
			usb_read(buffer, size);
			printf("got response: %s\n", buffer);
		}
	}
}