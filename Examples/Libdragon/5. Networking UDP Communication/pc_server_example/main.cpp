#include <cstdio>
#include <string>
#include <enet/enet.h>

int main() {
    ENetAddress address;
    ENetHost *server;

    address.host = ENET_HOST_ANY;
    address.port = 12345;

    server = enet_host_create(&address, 3, 2, 0, 0);
    if (!server) {
        printf("Error creating server.");
        return -1;
    }

    printf("Listening on port 1234");
    fflush(stdout);

    ENetEvent event;
    bool exit = false;
    while (!exit) {
        /* Wait up to 1000 milliseconds for an event. */
        while (enet_host_service(server, &event, 1000)) {
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    printf("\nA new client connected from %x:%u.\n",
                           event.peer->address.host,
                           event.peer->address.port);

                    break;
                case ENET_EVENT_TYPE_RECEIVE: {
                    printf("\nA packet of length %zu containing %s was received from %u on channel %u.\n",
                           event.packet->dataLength,
                           event.packet->data,
                           event.peer->connectID,
                           event.channelID);
                    /* Clean up the packet now that we're done using it. */
                    enet_packet_destroy(event.packet);

                    std::string result("Received packet!");
                    ENetPacket *packet = enet_packet_create(result.c_str(), result.length(), 0);
                    enet_host_broadcast(server, 0, packet);
                }
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    printf("\n%u disconnected.\n", event.peer->connectID);
                    break;
                case 4:
                    exit = true;
                    break;
            }
        }
    }

    enet_host_destroy(server);
    return 0;
}
