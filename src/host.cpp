/**
 @file host.c
 @brief ENet host management functions
*/

#include <string.h>
#include "enetcpp/enetcpp.h"

/** @defgroup host ENet host functions
    @{
*/

ENet::Host *ENet::host_create(const ENet::Address *address, size_t peerCount, size_t channelLimit,
                              uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    ENet::Host *host;
    ENet::Peer *currentPeer;

    if (peerCount > ENET_PROTOCOL_MAXIMUM_PEER_ID)
    {
        return NULL;
    }

    host = (ENet::Host *)ENet::enet_malloc(sizeof(ENet::Host));
    if (host == NULL)
    {
        return NULL;
    }
    memset(host, 0, sizeof(ENet::Host));

    host->peers = (ENet::Peer *)ENet::enet_malloc(peerCount * sizeof(ENet::Peer));
    if (host->peers == NULL)
    {
        ENet::enet_free(host);

        return NULL;
    }
    memset(host->peers, 0, peerCount * sizeof(ENet::Peer));

    host->socket = ENet::socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (host->socket == ENET_SOCKET_NULL || (address != NULL && ENet::socket_bind(host->socket, address) < 0))
    {
        if (host->socket != ENET_SOCKET_NULL)
        {
            ENet::socket_destroy(host->socket);
        }

        ENet::enet_free(host->peers);
        ENet::enet_free(host);

        return NULL;
    }

    ENet::socket_set_option(host->socket, ENET_SOCKOPT_NONBLOCK, 1);
    ENet::socket_set_option(host->socket, ENET_SOCKOPT_BROADCAST, 1);
    ENet::socket_set_option(host->socket, ENET_SOCKOPT_RCVBUF, ENET_HOST_RECEIVE_BUFFER_SIZE);
    ENet::socket_set_option(host->socket, ENET_SOCKOPT_SNDBUF, ENET_HOST_SEND_BUFFER_SIZE);

    if (address != NULL && ENet::socket_get_address(host->socket, &host->address) < 0)
    {
        host->address = *address;
    }

    if (!channelLimit || channelLimit > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
    {
        channelLimit = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;
    }
    else if (channelLimit < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
    {
        channelLimit = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;
    }

    host->randomSeed = (uint32_t)(size_t)host;
    host->randomSeed += ENet::host_random_seed();
    host->randomSeed = (host->randomSeed << 16) | (host->randomSeed >> 16);
    host->channelLimit = channelLimit;
    host->incomingBandwidth = incomingBandwidth;
    host->outgoingBandwidth = outgoingBandwidth;
    host->bandwidthThrottleEpoch = 0;
    host->recalculateBandwidthLimits = 0;
    host->mtu = ENET_HOST_DEFAULT_MTU;
    host->peerCount = peerCount;
    host->commandCount = 0;
    host->bufferCount = 0;
    host->checksum = NULL;
    host->receivedAddress.host = ENet::HOST_ANY;
    host->receivedAddress.port = 0;
    host->receivedData = NULL;
    host->receivedDataLength = 0;

    host->totalSentData = 0;
    host->totalSentPackets = 0;
    host->totalReceivedData = 0;
    host->totalReceivedPackets = 0;
    host->totalQueued = 0;

    host->connectedPeers = 0;
    host->bandwidthLimitedPeers = 0;
    host->duplicatePeers = ENET_PROTOCOL_MAXIMUM_PEER_ID;
    host->maximumPacketSize = ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE;
    host->maximumWaitingData = ENET_HOST_DEFAULT_MAXIMUM_WAITING_DATA;

    host->compressor.context = NULL;
    host->compressor.compress = NULL;
    host->compressor.decompress = NULL;
    host->compressor.destroy = NULL;

    host->intercept = NULL;

    ENet::list_clear(&host->dispatchQueue);

    for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer)
    {
        currentPeer->host = host;
        currentPeer->incomingPeerID = currentPeer - host->peers;
        currentPeer->outgoingSessionID = currentPeer->incomingSessionID = 0xFF;
        currentPeer->data = NULL;

        ENet::list_clear(&currentPeer->acknowledgements);
        ENet::list_clear(&currentPeer->sentReliableCommands);
        ENet::list_clear(&currentPeer->outgoingCommands);
        ENet::list_clear(&currentPeer->outgoingSendReliableCommands);
        ENet::list_clear(&currentPeer->dispatchedCommands);

        ENet::peer_reset(currentPeer);
    }

    return host;
}

void ENet::host_destroy(ENet::Host *host)
{
    ENet::Peer *currentPeer;

    if (host == NULL)
    {
        return;
    }

    ENet::socket_destroy(host->socket);

    for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer)
    {
        ENet::peer_reset(currentPeer);
    }

    if (host->compressor.context != NULL && host->compressor.destroy)
    {
        (*host->compressor.destroy)(host->compressor.context);
    }

    ENet::enet_free(host->peers);
    ENet::enet_free(host);
}

uint32_t ENet::host_random(ENet::Host *host)
{
    /* Mulberry32 by Tommy Ettinger */
    uint32_t n = (host->randomSeed += 0x6D2B79F5U);
    n = (n ^ (n >> 15)) * (n | 1U);
    n ^= n + (n ^ (n >> 7)) * (n | 61U);
    return n ^ (n >> 14);
}

ENet::Peer *ENet::host_connect(ENet::Host *host, const ENet::Address *address, size_t channelCount, uint32_t data)
{
    ENet::Peer *currentPeer;
    ENet::Channel *channel;
    ENet::Protocol command;

    if (channelCount < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
    {
        channelCount = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;
    }
    else if (channelCount > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
    {
        channelCount = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;
    }

    for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer)
    {
        if (currentPeer->state == ENET_PEER_STATE_DISCONNECTED)
        {
            break;
        }
    }

    if (currentPeer >= &host->peers[host->peerCount])
    {
        return NULL;
    }

    currentPeer->channels = (ENet::Channel *)ENet::enet_malloc(channelCount * sizeof(ENet::Channel));
    if (currentPeer->channels == NULL)
    {
        return NULL;
    }
    currentPeer->channelCount = channelCount;
    currentPeer->state = ENET_PEER_STATE_CONNECTING;
    currentPeer->address = *address;
    currentPeer->connectID = ENet::host_random(host);

    if (host->outgoingBandwidth == 0)
    {
        currentPeer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
    }
    else
    {
        currentPeer->windowSize =
            (host->outgoingBandwidth / ENET_PEER_WINDOW_SIZE_SCALE) * ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
    }

    if (currentPeer->windowSize < ENET_PROTOCOL_MINIMUM_WINDOW_SIZE)
    {
        currentPeer->windowSize = ENET_PROTOCOL_MINIMUM_WINDOW_SIZE;
    }
    else if (currentPeer->windowSize > ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE)
    {
        currentPeer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
    }

    for (channel = currentPeer->channels; channel < &currentPeer->channels[channelCount]; ++channel)
    {
        channel->outgoingReliableSequenceNumber = 0;
        channel->outgoingUnreliableSequenceNumber = 0;
        channel->incomingReliableSequenceNumber = 0;
        channel->incomingUnreliableSequenceNumber = 0;

        ENet::list_clear(&channel->incomingReliableCommands);
        ENet::list_clear(&channel->incomingUnreliableCommands);

        channel->usedReliableWindows = 0;
        memset(channel->reliableWindows, 0, sizeof(channel->reliableWindows));
    }

    command.header.command = ENET_PROTOCOL_COMMAND_CONNECT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    command.header.channelID = 0xFF;
    command.connect.outgoingPeerID = ENET_HOST_TO_NET_16(currentPeer->incomingPeerID);
    command.connect.incomingSessionID = currentPeer->incomingSessionID;
    command.connect.outgoingSessionID = currentPeer->outgoingSessionID;
    command.connect.mtu = ENET_HOST_TO_NET_32(currentPeer->mtu);
    command.connect.windowSize = ENET_HOST_TO_NET_32(currentPeer->windowSize);
    command.connect.channelCount = ENET_HOST_TO_NET_32(channelCount);
    command.connect.incomingBandwidth = ENET_HOST_TO_NET_32(host->incomingBandwidth);
    command.connect.outgoingBandwidth = ENET_HOST_TO_NET_32(host->outgoingBandwidth);
    command.connect.packetThrottleInterval = ENET_HOST_TO_NET_32(currentPeer->packetThrottleInterval);
    command.connect.packetThrottleAcceleration = ENET_HOST_TO_NET_32(currentPeer->packetThrottleAcceleration);
    command.connect.packetThrottleDeceleration = ENET_HOST_TO_NET_32(currentPeer->packetThrottleDeceleration);
    command.connect.connectID = currentPeer->connectID;
    command.connect.data = ENET_HOST_TO_NET_32(data);

    ENet::peer_queue_outgoing_command(currentPeer, &command, NULL, 0, 0);

    return currentPeer;
}

void ENet::host_broadcast(ENet::Host *host, uint8_t channelID, ENet::Packet *packet)
{
    ENet::Peer *currentPeer;

    for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer)
    {
        if (currentPeer->state != ENET_PEER_STATE_CONNECTED)
        {
            continue;
        }

        ENet::peer_send(currentPeer, channelID, packet);
    }

    if (packet->referenceCount == 0)
    {
        ENet::packet_destroy(packet);
    }
}

void ENet::host_compress(ENet::Host *host, const ENet::Compressor *compressor)
{
    if (host->compressor.context != NULL && host->compressor.destroy)
    {
        (*host->compressor.destroy)(host->compressor.context);
    }

    if (compressor)
    {
        host->compressor = *compressor;
    }
    else
    {
        host->compressor.context = NULL;
    }
}

void ENet::host_channel_limit(ENet::Host *host, size_t channelLimit)
{
    if (!channelLimit || channelLimit > ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT)
    {
        channelLimit = ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT;
    }
    else if (channelLimit < ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT)
    {
        channelLimit = ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT;
    }

    host->channelLimit = channelLimit;
}

void ENet::host_bandwidth_limit(ENet::Host *host, uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    host->incomingBandwidth = incomingBandwidth;
    host->outgoingBandwidth = outgoingBandwidth;
    host->recalculateBandwidthLimits = 1;
}

void ENet::host_bandwidth_throttle(ENet::Host *host)
{
    uint32_t timeCurrent = ENet::time_get(), elapsedTime = timeCurrent - host->bandwidthThrottleEpoch,
             peersRemaining = (uint32_t)host->connectedPeers, dataTotal = ~0, bandwidth = ~0, throttle = 0,
             bandwidthLimit = 0;
    int needsAdjustment = host->bandwidthLimitedPeers > 0 ? 1 : 0;
    ENet::Peer *peer;
    ENet::Protocol command;

    if (elapsedTime < ENET_HOST_BANDWIDTH_THROTTLE_INTERVAL)
    {
        return;
    }

    host->bandwidthThrottleEpoch = timeCurrent;

    if (peersRemaining == 0)
    {
        return;
    }

    if (host->outgoingBandwidth != 0)
    {
        dataTotal = 0;
        bandwidth = (host->outgoingBandwidth * elapsedTime) / 1000;

        for (peer = host->peers; peer < &host->peers[host->peerCount]; ++peer)
        {
            if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
            {
                continue;
            }

            dataTotal += peer->outgoingDataTotal;
        }
    }

    while (peersRemaining > 0 && needsAdjustment != 0)
    {
        needsAdjustment = 0;

        if (dataTotal <= bandwidth)
        {
            throttle = ENET_PEER_PACKET_THROTTLE_SCALE;
        }
        else
        {
            throttle = (bandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / dataTotal;
        }

        for (peer = host->peers; peer < &host->peers[host->peerCount]; ++peer)
        {
            uint32_t peerBandwidth;

            if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) ||
                peer->incomingBandwidth == 0 || peer->outgoingBandwidthThrottleEpoch == timeCurrent)
            {
                continue;
            }

            peerBandwidth = (peer->incomingBandwidth * elapsedTime) / 1000;
            if ((throttle * peer->outgoingDataTotal) / ENET_PEER_PACKET_THROTTLE_SCALE <= peerBandwidth)
            {
                continue;
            }

            peer->packetThrottleLimit = (peerBandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / peer->outgoingDataTotal;

            if (peer->packetThrottleLimit == 0)
            {
                peer->packetThrottleLimit = 1;
            }

            if (peer->packetThrottle > peer->packetThrottleLimit)
            {
                peer->packetThrottle = peer->packetThrottleLimit;
            }

            peer->outgoingBandwidthThrottleEpoch = timeCurrent;

            peer->incomingDataTotal = 0;
            peer->outgoingDataTotal = 0;

            needsAdjustment = 1;
            --peersRemaining;
            bandwidth -= peerBandwidth;
            dataTotal -= peerBandwidth;
        }
    }

    if (peersRemaining > 0)
    {
        if (dataTotal <= bandwidth)
        {
            throttle = ENET_PEER_PACKET_THROTTLE_SCALE;
        }
        else
        {
            throttle = (bandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / dataTotal;
        }

        for (peer = host->peers; peer < &host->peers[host->peerCount]; ++peer)
        {
            if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) ||
                peer->outgoingBandwidthThrottleEpoch == timeCurrent)
            {
                continue;
            }

            peer->packetThrottleLimit = throttle;

            if (peer->packetThrottle > peer->packetThrottleLimit)
            {
                peer->packetThrottle = peer->packetThrottleLimit;
            }

            peer->incomingDataTotal = 0;
            peer->outgoingDataTotal = 0;
        }
    }

    if (host->recalculateBandwidthLimits)
    {
        host->recalculateBandwidthLimits = 0;

        peersRemaining = (uint32_t)host->connectedPeers;
        bandwidth = host->incomingBandwidth;
        needsAdjustment = 1;

        if (bandwidth == 0)
        {
            bandwidthLimit = 0;
        }
        else
        {
            while (peersRemaining > 0 && needsAdjustment != 0)
            {
                needsAdjustment = 0;
                bandwidthLimit = bandwidth / peersRemaining;

                for (peer = host->peers; peer < &host->peers[host->peerCount]; ++peer)
                {
                    if ((peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER) ||
                        peer->incomingBandwidthThrottleEpoch == timeCurrent)
                    {
                        continue;
                    }

                    if (peer->outgoingBandwidth > 0 && peer->outgoingBandwidth >= bandwidthLimit)
                    {
                        continue;
                    }

                    peer->incomingBandwidthThrottleEpoch = timeCurrent;

                    needsAdjustment = 1;
                    --peersRemaining;
                    bandwidth -= peer->outgoingBandwidth;
                }
            }
        }

        for (peer = host->peers; peer < &host->peers[host->peerCount]; ++peer)
        {
            if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
            {
                continue;
            }

            command.header.command = ENET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
            command.header.channelID = 0xFF;
            command.bandwidthLimit.outgoingBandwidth = ENET_HOST_TO_NET_32(host->outgoingBandwidth);

            if (peer->incomingBandwidthThrottleEpoch == timeCurrent)
            {
                command.bandwidthLimit.incomingBandwidth = ENET_HOST_TO_NET_32(peer->outgoingBandwidth);
            }
            else
            {
                command.bandwidthLimit.incomingBandwidth = ENET_HOST_TO_NET_32(bandwidthLimit);
            }

            ENet::peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
        }
    }
}

/** @} */
