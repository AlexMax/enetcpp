// The MIT License (MIT)
//
// Copyright (c) 2002-2016 Lee Salzman
// Copyright (c) 2017-2022 Vladyslav Hrytsenko, Dominik Madar�sz
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//
// This test was taken from the zpl fork of ENet
//

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "enetcpp/enetcpp.h"
#include <stdio.h>

struct Client
{
    ENet::Host *host;
    ENet::Peer *peer;
};

void host_server(ENet::Host *server)
{
    ENet::Event event;
    while (ENet::host_service(server, &event, 2) > 0)
    {
        switch (event.type)
        {
        case ENET_EVENT_TYPE_CONNECT:
            printf("A new client connected from ::1:%u.\n", event.peer->address.port);
            /* Store any relevant client information here. */
            event.peer->data = (void *)"Client information";
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            printf("A packet of length %zu containing %s was received from %s on channel %u.\n",
                   event.packet->dataLength, event.packet->data, (char *)event.peer->data, event.channelID);

            /* Clean up the packet now that we're done using it. */
            ENet::packet_destroy(event.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            printf("%s disconnected.\n", (char *)event.peer->data);
            /* Reset the peer's client information. */
            event.peer->data = NULL;
            break;

        case ENET_EVENT_TYPE_NONE:
            break;
        }
    }
}

int main()
{
    if (ENet::initialize() != 0)
    {
        printf("An error occurred while initializing ENet.\n");
        return 1;
    }

#define MAX_CLIENTS 32

    int i = 0;
    ENet::Host *server;
    Client clients[MAX_CLIENTS];
    ENetAddress address = {0, 0};

    address.host = ENET_HOST_ANY; /* Bind the server to the default localhost. */
    address.port = 7777;          /* Bind the server to port 7777. */

    /* create a server */
    printf("starting server...\n");
    server = ENet::host_create(&address, MAX_CLIENTS, 2, 0, 0);
    if (server == NULL)
    {
        printf("An error occurred while trying to create an ENet server host.\n");
        return 1;
    }

    printf("starting clients...\n");
    for (i = 0; i < MAX_CLIENTS; ++i)
    {
        ENet::address_set_host(&address, "127.0.0.1");
        clients[i].host = ENet::host_create(NULL, 1, 2, 0, 0);
        clients[i].peer = ENet::host_connect(clients[i].host, &address, 2, 0);
        if (clients[i].peer == NULL)
        {
            printf("coundlnt connect\n");
            return 1;
        }
    }

    // program will make N iterations, and then exit
    static int counter = 1000;

    do
    {
        host_server(server);

        ENet::Event event;
        for (i = 0; i < MAX_CLIENTS; ++i)
        {
            ENet::host_service(clients[i].host, &event, 0);
        }

        counter--;
    } while (counter > 0);

    for (i = 0; i < MAX_CLIENTS; ++i)
    {
        ENet::peer_disconnect_now(clients[i].peer, 0);
        ENet::host_destroy(clients[i].host);
    }

    host_server(server);

    ENet::host_destroy(server);
    ENet::deinitialize();
    return 0;
}
