/**
 @file  protocol.c
 @brief ENet protocol functions
*/

#include <stdio.h>
#include <string.h>

#include "enetcpp/utility.h"
#include "enetcpp/time.h"
#include "enetcpp/enetcpp.h"

static const size_t commandSizes[ENet::PROTOCOL_COMMAND_COUNT] = {
    0,
    sizeof(ENet::ProtocolAcknowledge),
    sizeof(ENet::ProtocolConnect),
    sizeof(ENet::ProtocolVerifyConnect),
    sizeof(ENet::ProtocolDisconnect),
    sizeof(ENet::ProtocolPing),
    sizeof(ENet::ProtocolSendReliable),
    sizeof(ENet::ProtocolSendUnreliable),
    sizeof(ENet::ProtocolSendFragment),
    sizeof(ENet::ProtocolSendUnsequenced),
    sizeof(ENet::ProtocolBandwidthLimit),
    sizeof(ENet::ProtocolThrottleConfigure),
    sizeof(ENet::ProtocolSendFragment),
};

size_t ENet::protocol_command_size(uint8_t commandNumber)
{
    return commandSizes[commandNumber & ENet::PROTOCOL_COMMAND_MASK];
}

static void enet_protocol_change_state(ENet::Host *host, ENet::Peer *peer, ENetPeerState state)
{
    (void)host;

    if (state == ENET_PEER_STATE_CONNECTED || state == ENET_PEER_STATE_DISCONNECT_LATER)
    {
        ENet::peer_on_connect(peer);
    }
    else
    {
        ENet::peer_on_disconnect(peer);
    }

    peer->state = state;
}

static void enet_protocol_dispatch_state(ENet::Host *host, ENet::Peer *peer, ENetPeerState state)
{
    enet_protocol_change_state(host, peer, state);

    if (!(peer->flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
    {
        ENet::list_insert(ENet::list_end(&host->dispatchQueue), &peer->dispatchList);

        peer->flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
    }
}

static int enet_protocol_dispatch_incoming_commands(ENet::Host *host, ENet::Event *event)
{
    while (!ENet::list_empty(&host->dispatchQueue))
    {
        ENet::Peer *peer = (ENet::Peer *)ENet::list_remove(ENet::list_begin(&host->dispatchQueue));

        peer->flags &= ~ENET_PEER_FLAG_NEEDS_DISPATCH;

        switch (peer->state)
        {
        case ENET_PEER_STATE_CONNECTION_PENDING:
        case ENET_PEER_STATE_CONNECTION_SUCCEEDED:
            enet_protocol_change_state(host, peer, ENET_PEER_STATE_CONNECTED);

            event->type = ENET_EVENT_TYPE_CONNECT;
            event->peer = peer;
            event->data = peer->eventData;

            return 1;

        case ENET_PEER_STATE_ZOMBIE:
            host->recalculateBandwidthLimits = 1;

            event->type = ENET_EVENT_TYPE_DISCONNECT;
            event->peer = peer;
            event->data = peer->eventData;

            ENet::peer_reset(peer);

            return 1;

        case ENET_PEER_STATE_CONNECTED:
            if (ENet::list_empty(&peer->dispatchedCommands))
            {
                continue;
            }

            event->packet = ENet::peer_receive(peer, &event->channelID);
            if (event->packet == NULL)
            {
                continue;
            }

            event->type = ENET_EVENT_TYPE_RECEIVE;
            event->peer = peer;

            if (!ENet::list_empty(&peer->dispatchedCommands))
            {
                peer->flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;

                ENet::list_insert(ENet::list_end(&host->dispatchQueue), &peer->dispatchList);
            }

            return 1;

        default:
            break;
        }
    }

    return 0;
}

static void enet_protocol_notify_connect(ENet::Host *host, ENet::Peer *peer, ENet::Event *event)
{
    host->recalculateBandwidthLimits = 1;

    if (event != NULL)
    {
        enet_protocol_change_state(host, peer, ENET_PEER_STATE_CONNECTED);

        event->type = ENET_EVENT_TYPE_CONNECT;
        event->peer = peer;
        event->data = peer->eventData;
    }
    else
    {
        enet_protocol_dispatch_state(host, peer,
                                     peer->state == ENET_PEER_STATE_CONNECTING ? ENET_PEER_STATE_CONNECTION_SUCCEEDED
                                                                               : ENET_PEER_STATE_CONNECTION_PENDING);
    }
}

static void enet_protocol_notify_disconnect(ENet::Host *host, ENet::Peer *peer, ENet::Event *event)
{
    if (peer->state >= ENET_PEER_STATE_CONNECTION_PENDING)
    {
        host->recalculateBandwidthLimits = 1;
    }

    if (peer->state != ENET_PEER_STATE_CONNECTING && peer->state < ENET_PEER_STATE_CONNECTION_SUCCEEDED)
    {
        ENet::peer_reset(peer);
    }
    else if (event != NULL)
    {
        event->type = ENET_EVENT_TYPE_DISCONNECT;
        event->peer = peer;
        event->data = 0;

        ENet::peer_reset(peer);
    }
    else
    {
        peer->eventData = 0;

        enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);
    }
}

static void enet_protocol_remove_sent_unreliable_commands(ENet::Peer *peer, ENet::List *sentUnreliableCommands)
{
    ENet::OutgoingCommand *outgoingCommand;

    if (ENet::list_empty(sentUnreliableCommands))
    {
        return;
    }

    do
    {
        outgoingCommand = (ENet::OutgoingCommand *)ENet::list_front(sentUnreliableCommands);

        ENet::list_remove(&outgoingCommand->outgoingCommandList);

        if (outgoingCommand->packet != NULL)
        {
            --outgoingCommand->packet->referenceCount;

            if (outgoingCommand->packet->referenceCount == 0)
            {
                outgoingCommand->packet->flags |= ENet::PACKET_FLAG_SENT;

                ENet::packet_destroy(outgoingCommand->packet);
            }
        }

        ENet::enet_free(outgoingCommand);
    } while (!ENet::list_empty(sentUnreliableCommands));

    if (peer->state == ENET_PEER_STATE_DISCONNECT_LATER && !ENet::peer_has_outgoing_commands(peer))
    {
        ENet::peer_disconnect(peer, peer->eventData);
    }
}

static ENet::OutgoingCommand *enet_protocol_find_sent_reliable_command(ENet::List *list,
                                                                       uint16_t reliableSequenceNumber,
                                                                       uint8_t channelID)
{
    ENet::ListIterator currentCommand;

    for (currentCommand = ENet::list_begin(list); currentCommand != ENet::list_end(list);
         currentCommand = ENet::list_next(currentCommand))
    {
        ENet::OutgoingCommand *outgoingCommand = (ENet::OutgoingCommand *)currentCommand;

        if (!(outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE))
        {
            continue;
        }

        if (outgoingCommand->sendAttempts < 1)
        {
            break;
        }

        if (outgoingCommand->reliableSequenceNumber == reliableSequenceNumber &&
            outgoingCommand->command.header.channelID == channelID)
        {
            return outgoingCommand;
        }
    }

    return NULL;
}

static ENet::ProtocolCommand enet_protocol_remove_sent_reliable_command(ENet::Peer *peer,
                                                                        uint16_t reliableSequenceNumber,
                                                                        uint8_t channelID)
{
    ENet::OutgoingCommand *outgoingCommand = NULL;
    ENet::ListIterator currentCommand;
    ENet::ProtocolCommand commandNumber;
    int wasSent = 1;

    for (currentCommand = ENet::list_begin(&peer->sentReliableCommands);
         currentCommand != ENet::list_end(&peer->sentReliableCommands);
         currentCommand = ENet::list_next(currentCommand))
    {
        outgoingCommand = (ENet::OutgoingCommand *)currentCommand;

        if (outgoingCommand->reliableSequenceNumber == reliableSequenceNumber &&
            outgoingCommand->command.header.channelID == channelID)
        {
            break;
        }
    }

    if (currentCommand == ENet::list_end(&peer->sentReliableCommands))
    {
        outgoingCommand =
            enet_protocol_find_sent_reliable_command(&peer->outgoingCommands, reliableSequenceNumber, channelID);
        if (outgoingCommand == NULL)
        {
            outgoingCommand = enet_protocol_find_sent_reliable_command(&peer->outgoingSendReliableCommands,
                                                                       reliableSequenceNumber, channelID);
        }

        wasSent = 0;
    }

    if (outgoingCommand == NULL)
    {
        return ENet::PROTOCOL_COMMAND_NONE;
    }

    if (channelID < peer->channelCount)
    {
        ENet::Channel *channel = &peer->channels[channelID];
        uint16_t reliableWindow = reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
        if (channel->reliableWindows[reliableWindow] > 0)
        {
            --channel->reliableWindows[reliableWindow];
            if (!channel->reliableWindows[reliableWindow])
            {
                channel->usedReliableWindows &= ~(1 << reliableWindow);
            }
        }
    }

    commandNumber = (ENet::ProtocolCommand)(outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_MASK);

    ENet::list_remove(&outgoingCommand->outgoingCommandList);

    if (outgoingCommand->packet != NULL)
    {
        if (wasSent)
        {
            peer->reliableDataInTransit -= outgoingCommand->fragmentLength;
        }

        --outgoingCommand->packet->referenceCount;

        if (outgoingCommand->packet->referenceCount == 0)
        {
            outgoingCommand->packet->flags |= ENet::PACKET_FLAG_SENT;

            ENet::packet_destroy(outgoingCommand->packet);
        }
    }

    ENet::enet_free(outgoingCommand);

    if (ENet::list_empty(&peer->sentReliableCommands))
    {
        return commandNumber;
    }

    outgoingCommand = (ENet::OutgoingCommand *)ENet::list_front(&peer->sentReliableCommands);

    peer->nextTimeout = outgoingCommand->sentTime + outgoingCommand->roundTripTimeout;

    return commandNumber;
}

static ENet::Peer *enet_protocol_handle_connect(ENet::Host *host, ENet::ProtocolHeader *header, ENet::Protocol *command)
{
    (void)header;

    uint8_t incomingSessionID, outgoingSessionID;
    uint32_t mtu, windowSize;
    ENet::Channel *channel;
    size_t channelCount, duplicatePeers = 0;
    ENet::Peer *currentPeer, *peer = NULL;
    ENet::Protocol verifyCommand;

    channelCount = ENET_NET_TO_HOST_32(command->connect.channelCount);

    if (channelCount < ENet::PROTOCOL_MINIMUM_CHANNEL_COUNT || channelCount > ENet::PROTOCOL_MAXIMUM_CHANNEL_COUNT)
    {
        return NULL;
    }

    for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer)
    {
        if (currentPeer->state == ENET_PEER_STATE_DISCONNECTED)
        {
            if (peer == NULL)
            {
                peer = currentPeer;
            }
        }
        else if (currentPeer->state != ENET_PEER_STATE_CONNECTING &&
                 currentPeer->address.host == host->receivedAddress.host)
        {
            if (currentPeer->address.port == host->receivedAddress.port &&
                currentPeer->connectID == command->connect.connectID)
            {
                return NULL;
            }

            ++duplicatePeers;
        }
    }

    if (peer == NULL || duplicatePeers >= host->duplicatePeers)
    {
        return NULL;
    }

    if (channelCount > host->channelLimit)
    {
        channelCount = host->channelLimit;
    }
    peer->channels = (ENet::Channel *)ENet::enet_malloc(channelCount * sizeof(ENet::Channel));
    if (peer->channels == NULL)
    {
        return NULL;
    }
    peer->channelCount = channelCount;
    peer->state = ENET_PEER_STATE_ACKNOWLEDGING_CONNECT;
    peer->connectID = command->connect.connectID;
    peer->address = host->receivedAddress;
    peer->outgoingPeerID = ENET_NET_TO_HOST_16(command->connect.outgoingPeerID);
    peer->incomingBandwidth = ENET_NET_TO_HOST_32(command->connect.incomingBandwidth);
    peer->outgoingBandwidth = ENET_NET_TO_HOST_32(command->connect.outgoingBandwidth);
    peer->packetThrottleInterval = ENET_NET_TO_HOST_32(command->connect.packetThrottleInterval);
    peer->packetThrottleAcceleration = ENET_NET_TO_HOST_32(command->connect.packetThrottleAcceleration);
    peer->packetThrottleDeceleration = ENET_NET_TO_HOST_32(command->connect.packetThrottleDeceleration);
    peer->eventData = ENET_NET_TO_HOST_32(command->connect.data);

    incomingSessionID =
        command->connect.incomingSessionID == 0xFF ? peer->outgoingSessionID : command->connect.incomingSessionID;
    incomingSessionID =
        (incomingSessionID + 1) & (ENet::PROTOCOL_HEADER_SESSION_MASK >> ENet::PROTOCOL_HEADER_SESSION_SHIFT);
    if (incomingSessionID == peer->outgoingSessionID)
    {
        incomingSessionID =
            (incomingSessionID + 1) & (ENet::PROTOCOL_HEADER_SESSION_MASK >> ENet::PROTOCOL_HEADER_SESSION_SHIFT);
    }
    peer->outgoingSessionID = incomingSessionID;

    outgoingSessionID =
        command->connect.outgoingSessionID == 0xFF ? peer->incomingSessionID : command->connect.outgoingSessionID;
    outgoingSessionID =
        (outgoingSessionID + 1) & (ENet::PROTOCOL_HEADER_SESSION_MASK >> ENet::PROTOCOL_HEADER_SESSION_SHIFT);
    if (outgoingSessionID == peer->incomingSessionID)
    {
        outgoingSessionID =
            (outgoingSessionID + 1) & (ENet::PROTOCOL_HEADER_SESSION_MASK >> ENet::PROTOCOL_HEADER_SESSION_SHIFT);
    }
    peer->incomingSessionID = outgoingSessionID;

    for (channel = peer->channels; channel < &peer->channels[channelCount]; ++channel)
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

    mtu = ENET_NET_TO_HOST_32(command->connect.mtu);

    if (mtu < ENet::PROTOCOL_MINIMUM_MTU)
    {
        mtu = ENet::PROTOCOL_MINIMUM_MTU;
    }
    else if (mtu > ENet::PROTOCOL_MAXIMUM_MTU)
    {
        mtu = ENet::PROTOCOL_MAXIMUM_MTU;
    }

    if (mtu < peer->mtu)
    {
        peer->mtu = mtu;
    }

    if (host->outgoingBandwidth == 0 && peer->incomingBandwidth == 0)
    {
        peer->windowSize = ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE;
    }
    else if (host->outgoingBandwidth == 0 || peer->incomingBandwidth == 0)
    {
        peer->windowSize = (ENET_MAX(host->outgoingBandwidth, peer->incomingBandwidth) / ENET_PEER_WINDOW_SIZE_SCALE) *
                           ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }
    else
    {
        peer->windowSize = (ENET_MIN(host->outgoingBandwidth, peer->incomingBandwidth) / ENET_PEER_WINDOW_SIZE_SCALE) *
                           ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }

    if (peer->windowSize < ENet::PROTOCOL_MINIMUM_WINDOW_SIZE)
    {
        peer->windowSize = ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }
    else if (peer->windowSize > ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE)
    {
        peer->windowSize = ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE;
    }

    if (host->incomingBandwidth == 0)
    {
        windowSize = ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE;
    }
    else
    {
        windowSize = (host->incomingBandwidth / ENET_PEER_WINDOW_SIZE_SCALE) * ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }

    if (windowSize > ENET_NET_TO_HOST_32(command->connect.windowSize))
    {
        windowSize = ENET_NET_TO_HOST_32(command->connect.windowSize);
    }

    if (windowSize < ENet::PROTOCOL_MINIMUM_WINDOW_SIZE)
    {
        windowSize = ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }
    else if (windowSize > ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE)
    {
        windowSize = ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE;
    }

    verifyCommand.header.command = ENet::PROTOCOL_COMMAND_VERIFY_CONNECT | ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    verifyCommand.header.channelID = 0xFF;
    verifyCommand.verifyConnect.outgoingPeerID = ENET_HOST_TO_NET_16(peer->incomingPeerID);
    verifyCommand.verifyConnect.incomingSessionID = incomingSessionID;
    verifyCommand.verifyConnect.outgoingSessionID = outgoingSessionID;
    verifyCommand.verifyConnect.mtu = ENET_HOST_TO_NET_32(peer->mtu);
    verifyCommand.verifyConnect.windowSize = ENET_HOST_TO_NET_32(windowSize);
    verifyCommand.verifyConnect.channelCount = ENET_HOST_TO_NET_32(channelCount);
    verifyCommand.verifyConnect.incomingBandwidth = ENET_HOST_TO_NET_32(host->incomingBandwidth);
    verifyCommand.verifyConnect.outgoingBandwidth = ENET_HOST_TO_NET_32(host->outgoingBandwidth);
    verifyCommand.verifyConnect.packetThrottleInterval = ENET_HOST_TO_NET_32(peer->packetThrottleInterval);
    verifyCommand.verifyConnect.packetThrottleAcceleration = ENET_HOST_TO_NET_32(peer->packetThrottleAcceleration);
    verifyCommand.verifyConnect.packetThrottleDeceleration = ENET_HOST_TO_NET_32(peer->packetThrottleDeceleration);
    verifyCommand.verifyConnect.connectID = peer->connectID;

    ENet::peer_queue_outgoing_command(peer, &verifyCommand, NULL, 0, 0);

    return peer;
}

static int enet_protocol_handle_send_reliable(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command,
                                              uint8_t **currentData)
{
    size_t dataLength;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
    {
        return -1;
    }

    dataLength = ENET_NET_TO_HOST_16(command->sendReliable.dataLength);
    *currentData += dataLength;
    if (dataLength > host->maximumPacketSize || *currentData < host->receivedData ||
        *currentData > &host->receivedData[host->receivedDataLength])
    {
        return -1;
    }

    if (ENet::peer_queue_incoming_command(peer, command, (const uint8_t *)command + sizeof(ENet::ProtocolSendReliable),
                                          dataLength, ENet::PACKET_FLAG_RELIABLE, 0) == NULL)
    {
        return -1;
    }

    return 0;
}

static int enet_protocol_handle_send_unsequenced(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command,
                                                 uint8_t **currentData)
{
    uint32_t unsequencedGroup, index;
    size_t dataLength;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
    {
        return -1;
    }

    dataLength = ENET_NET_TO_HOST_16(command->sendUnsequenced.dataLength);
    *currentData += dataLength;
    if (dataLength > host->maximumPacketSize || *currentData < host->receivedData ||
        *currentData > &host->receivedData[host->receivedDataLength])
    {
        return -1;
    }

    unsequencedGroup = ENET_NET_TO_HOST_16(command->sendUnsequenced.unsequencedGroup);
    index = unsequencedGroup % ENET_PEER_UNSEQUENCED_WINDOW_SIZE;

    if (unsequencedGroup < peer->incomingUnsequencedGroup)
    {
        unsequencedGroup += 0x10000;
    }

    if (unsequencedGroup >= (uint32_t)peer->incomingUnsequencedGroup +
                                ENET_PEER_FREE_UNSEQUENCED_WINDOWS * ENET_PEER_UNSEQUENCED_WINDOW_SIZE)
    {
        return 0;
    }

    unsequencedGroup &= 0xFFFF;

    if (unsequencedGroup - index != peer->incomingUnsequencedGroup)
    {
        peer->incomingUnsequencedGroup = unsequencedGroup - index;

        memset(peer->unsequencedWindow, 0, sizeof(peer->unsequencedWindow));
    }
    else if (peer->unsequencedWindow[index / 32] & (1 << (index % 32)))
    {
        return 0;
    }

    if (ENet::peer_queue_incoming_command(peer, command,
                                          (const uint8_t *)command + sizeof(ENet::ProtocolSendUnsequenced), dataLength,
                                          ENet::PACKET_FLAG_UNSEQUENCED, 0) == NULL)
    {
        return -1;
    }

    peer->unsequencedWindow[index / 32] |= 1 << (index % 32);

    return 0;
}

static int enet_protocol_handle_send_unreliable(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command,
                                                uint8_t **currentData)
{
    size_t dataLength;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
    {
        return -1;
    }

    dataLength = ENET_NET_TO_HOST_16(command->sendUnreliable.dataLength);
    *currentData += dataLength;
    if (dataLength > host->maximumPacketSize || *currentData < host->receivedData ||
        *currentData > &host->receivedData[host->receivedDataLength])
    {
        return -1;
    }

    if (ENet::peer_queue_incoming_command(
            peer, command, (const uint8_t *)command + sizeof(ENet::ProtocolSendUnreliable), dataLength, 0, 0) == NULL)
    {
        return -1;
    }

    return 0;
}

static int enet_protocol_handle_send_fragment(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command,
                                              uint8_t **currentData)
{
    uint32_t fragmentNumber, fragmentCount, fragmentOffset, fragmentLength, startSequenceNumber, totalLength;
    ENet::Channel *channel;
    uint16_t startWindow, currentWindow;
    ENet::ListIterator currentCommand;
    ENet::IncomingCommand *startCommand = NULL;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
    {
        return -1;
    }

    fragmentLength = ENET_NET_TO_HOST_16(command->sendFragment.dataLength);
    *currentData += fragmentLength;
    if (fragmentLength <= 0 || fragmentLength > host->maximumPacketSize || *currentData < host->receivedData ||
        *currentData > &host->receivedData[host->receivedDataLength])
    {
        return -1;
    }

    channel = &peer->channels[command->header.channelID];
    startSequenceNumber = ENET_NET_TO_HOST_16(command->sendFragment.startSequenceNumber);
    startWindow = startSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
    currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

    if (startSequenceNumber < channel->incomingReliableSequenceNumber)
    {
        startWindow += ENET_PEER_RELIABLE_WINDOWS;
    }

    if (startWindow < currentWindow || startWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
    {
        return 0;
    }

    fragmentNumber = ENET_NET_TO_HOST_32(command->sendFragment.fragmentNumber);
    fragmentCount = ENET_NET_TO_HOST_32(command->sendFragment.fragmentCount);
    fragmentOffset = ENET_NET_TO_HOST_32(command->sendFragment.fragmentOffset);
    totalLength = ENET_NET_TO_HOST_32(command->sendFragment.totalLength);

    if (fragmentCount > ENet::PROTOCOL_MAXIMUM_FRAGMENT_COUNT || fragmentNumber >= fragmentCount ||
        totalLength > host->maximumPacketSize || totalLength < fragmentCount || fragmentOffset >= totalLength ||
        fragmentLength > totalLength - fragmentOffset)
    {
        return -1;
    }

    for (currentCommand = ENet::list_previous(ENet::list_end(&channel->incomingReliableCommands));
         currentCommand != ENet::list_end(&channel->incomingReliableCommands);
         currentCommand = ENet::list_previous(currentCommand))
    {
        ENet::IncomingCommand *incomingCommand = (ENet::IncomingCommand *)currentCommand;

        if (startSequenceNumber >= channel->incomingReliableSequenceNumber)
        {
            if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
            {
                continue;
            }
        }
        else if (incomingCommand->reliableSequenceNumber >= channel->incomingReliableSequenceNumber)
        {
            break;
        }

        if (incomingCommand->reliableSequenceNumber <= startSequenceNumber)
        {
            if (incomingCommand->reliableSequenceNumber < startSequenceNumber)
            {
                break;
            }

            if ((incomingCommand->command.header.command & ENet::PROTOCOL_COMMAND_MASK) !=
                    ENet::PROTOCOL_COMMAND_SEND_FRAGMENT ||
                totalLength != incomingCommand->packet->dataLength || fragmentCount != incomingCommand->fragmentCount)
            {
                return -1;
            }

            startCommand = incomingCommand;
            break;
        }
    }

    if (startCommand == NULL)
    {
        ENet::Protocol hostCommand = *command;

        hostCommand.header.reliableSequenceNumber = startSequenceNumber;

        startCommand = ENet::peer_queue_incoming_command(peer, &hostCommand, NULL, totalLength,
                                                         ENet::PACKET_FLAG_RELIABLE, fragmentCount);
        if (startCommand == NULL)
        {
            return -1;
        }
    }

    if ((startCommand->fragments[fragmentNumber / 32] & (1 << (fragmentNumber % 32))) == 0)
    {
        --startCommand->fragmentsRemaining;

        startCommand->fragments[fragmentNumber / 32] |= (1 << (fragmentNumber % 32));

        if (fragmentOffset + fragmentLength > startCommand->packet->dataLength)
        {
            fragmentLength = startCommand->packet->dataLength - fragmentOffset;
        }

        memcpy(startCommand->packet->data + fragmentOffset, (uint8_t *)command + sizeof(ENet::ProtocolSendFragment),
               fragmentLength);

        if (startCommand->fragmentsRemaining <= 0)
        {
            ENet::peer_dispatch_incoming_reliable_commands(peer, channel, NULL);
        }
    }

    return 0;
}

static int enet_protocol_handle_send_unreliable_fragment(ENet::Host *host, ENet::Peer *peer,
                                                         const ENet::Protocol *command, uint8_t **currentData)
{
    uint32_t fragmentNumber, fragmentCount, fragmentOffset, fragmentLength, reliableSequenceNumber, startSequenceNumber,
        totalLength;
    uint16_t reliableWindow, currentWindow;
    ENet::Channel *channel;
    ENet::ListIterator currentCommand;
    ENet::IncomingCommand *startCommand = NULL;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER))
    {
        return -1;
    }

    fragmentLength = ENET_NET_TO_HOST_16(command->sendFragment.dataLength);
    *currentData += fragmentLength;
    if (fragmentLength > host->maximumPacketSize || *currentData < host->receivedData ||
        *currentData > &host->receivedData[host->receivedDataLength])
    {
        return -1;
    }

    channel = &peer->channels[command->header.channelID];
    reliableSequenceNumber = command->header.reliableSequenceNumber;
    startSequenceNumber = ENET_NET_TO_HOST_16(command->sendFragment.startSequenceNumber);

    reliableWindow = reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
    currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

    if (reliableSequenceNumber < channel->incomingReliableSequenceNumber)
    {
        reliableWindow += ENET_PEER_RELIABLE_WINDOWS;
    }

    if (reliableWindow < currentWindow || reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
    {
        return 0;
    }

    if (reliableSequenceNumber == channel->incomingReliableSequenceNumber &&
        startSequenceNumber <= channel->incomingUnreliableSequenceNumber)
    {
        return 0;
    }

    fragmentNumber = ENET_NET_TO_HOST_32(command->sendFragment.fragmentNumber);
    fragmentCount = ENET_NET_TO_HOST_32(command->sendFragment.fragmentCount);
    fragmentOffset = ENET_NET_TO_HOST_32(command->sendFragment.fragmentOffset);
    totalLength = ENET_NET_TO_HOST_32(command->sendFragment.totalLength);

    if (fragmentCount > ENet::PROTOCOL_MAXIMUM_FRAGMENT_COUNT || fragmentNumber >= fragmentCount ||
        totalLength > host->maximumPacketSize || fragmentOffset >= totalLength ||
        fragmentLength > totalLength - fragmentOffset)
    {
        return -1;
    }

    for (currentCommand = ENet::list_previous(ENet::list_end(&channel->incomingUnreliableCommands));
         currentCommand != ENet::list_end(&channel->incomingUnreliableCommands);
         currentCommand = ENet::list_previous(currentCommand))
    {
        ENet::IncomingCommand *incomingCommand = (ENet::IncomingCommand *)currentCommand;

        if (reliableSequenceNumber >= channel->incomingReliableSequenceNumber)
        {
            if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
            {
                continue;
            }
        }
        else if (incomingCommand->reliableSequenceNumber >= channel->incomingReliableSequenceNumber)
        {
            break;
        }

        if (incomingCommand->reliableSequenceNumber < reliableSequenceNumber)
        {
            break;
        }

        if (incomingCommand->reliableSequenceNumber > reliableSequenceNumber)
        {
            continue;
        }

        if (incomingCommand->unreliableSequenceNumber <= startSequenceNumber)
        {
            if (incomingCommand->unreliableSequenceNumber < startSequenceNumber)
            {
                break;
            }

            if ((incomingCommand->command.header.command & ENet::PROTOCOL_COMMAND_MASK) !=
                    ENet::PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT ||
                totalLength != incomingCommand->packet->dataLength || fragmentCount != incomingCommand->fragmentCount)
            {
                return -1;
            }

            startCommand = incomingCommand;
            break;
        }
    }

    if (startCommand == NULL)
    {
        startCommand = ENet::peer_queue_incoming_command(peer, command, NULL, totalLength,
                                                         ENet::PACKET_FLAG_UNRELIABLE_FRAGMENT, fragmentCount);
        if (startCommand == NULL)
        {
            return -1;
        }
    }

    if ((startCommand->fragments[fragmentNumber / 32] & (1 << (fragmentNumber % 32))) == 0)
    {
        --startCommand->fragmentsRemaining;

        startCommand->fragments[fragmentNumber / 32] |= (1 << (fragmentNumber % 32));

        if (fragmentOffset + fragmentLength > startCommand->packet->dataLength)
        {
            fragmentLength = startCommand->packet->dataLength - fragmentOffset;
        }

        memcpy(startCommand->packet->data + fragmentOffset, (uint8_t *)command + sizeof(ENet::ProtocolSendFragment),
               fragmentLength);

        if (startCommand->fragmentsRemaining <= 0)
        {
            ENet::peer_dispatch_incoming_unreliable_commands(peer, channel, NULL);
        }
    }

    return 0;
}

static int enet_protocol_handle_ping(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command)
{
    (void)host;
    (void)command;

    if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
    {
        return -1;
    }

    return 0;
}

static int enet_protocol_handle_bandwidth_limit(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command)
{
    if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
    {
        return -1;
    }

    if (peer->incomingBandwidth != 0)
    {
        --host->bandwidthLimitedPeers;
    }

    peer->incomingBandwidth = ENET_NET_TO_HOST_32(command->bandwidthLimit.incomingBandwidth);
    peer->outgoingBandwidth = ENET_NET_TO_HOST_32(command->bandwidthLimit.outgoingBandwidth);

    if (peer->incomingBandwidth != 0)
    {
        ++host->bandwidthLimitedPeers;
    }

    if (peer->incomingBandwidth == 0 && host->outgoingBandwidth == 0)
    {
        peer->windowSize = ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE;
    }
    else if (peer->incomingBandwidth == 0 || host->outgoingBandwidth == 0)
    {
        peer->windowSize = (ENET_MAX(peer->incomingBandwidth, host->outgoingBandwidth) / ENET_PEER_WINDOW_SIZE_SCALE) *
                           ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }
    else
    {
        peer->windowSize = (ENET_MIN(peer->incomingBandwidth, host->outgoingBandwidth) / ENET_PEER_WINDOW_SIZE_SCALE) *
                           ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }

    if (peer->windowSize < ENet::PROTOCOL_MINIMUM_WINDOW_SIZE)
    {
        peer->windowSize = ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }
    else if (peer->windowSize > ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE)
    {
        peer->windowSize = ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE;
    }

    return 0;
}

static int enet_protocol_handle_throttle_configure(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command)
{
    (void)host;

    if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
    {
        return -1;
    }

    peer->packetThrottleInterval = ENET_NET_TO_HOST_32(command->throttleConfigure.packetThrottleInterval);
    peer->packetThrottleAcceleration = ENET_NET_TO_HOST_32(command->throttleConfigure.packetThrottleAcceleration);
    peer->packetThrottleDeceleration = ENET_NET_TO_HOST_32(command->throttleConfigure.packetThrottleDeceleration);

    return 0;
}

static int enet_protocol_handle_disconnect(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command)
{
    if (peer->state == ENET_PEER_STATE_DISCONNECTED || peer->state == ENET_PEER_STATE_ZOMBIE ||
        peer->state == ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT)
    {
        return 0;
    }

    ENet::peer_reset_queues(peer);

    if (peer->state == ENET_PEER_STATE_CONNECTION_SUCCEEDED || peer->state == ENET_PEER_STATE_DISCONNECTING ||
        peer->state == ENET_PEER_STATE_CONNECTING)
    {
        enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);
    }
    else if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
    {
        if (peer->state == ENET_PEER_STATE_CONNECTION_PENDING)
        {
            host->recalculateBandwidthLimits = 1;
        }

        ENet::peer_reset(peer);
    }
    else if (command->header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
    {
        enet_protocol_change_state(host, peer, ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT);
    }
    else
    {
        enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);
    }

    if (peer->state != ENET_PEER_STATE_DISCONNECTED)
    {
        peer->eventData = ENET_NET_TO_HOST_32(command->disconnect.data);
    }

    return 0;
}

static int enet_protocol_handle_acknowledge(ENet::Host *host, ENet::Event *event, ENet::Peer *peer,
                                            const ENet::Protocol *command)
{
    uint32_t roundTripTime, receivedSentTime, receivedReliableSequenceNumber;
    ENet::ProtocolCommand commandNumber;

    if (peer->state == ENET_PEER_STATE_DISCONNECTED || peer->state == ENET_PEER_STATE_ZOMBIE)
    {
        return 0;
    }

    receivedSentTime = ENET_NET_TO_HOST_16(command->acknowledge.receivedSentTime);
    receivedSentTime |= host->serviceTime & 0xFFFF0000;
    if ((receivedSentTime & 0x8000) > (host->serviceTime & 0x8000))
    {
        receivedSentTime -= 0x10000;
    }

    if (ENET_TIME_LESS(host->serviceTime, receivedSentTime))
    {
        return 0;
    }

    roundTripTime = ENET_TIME_DIFFERENCE(host->serviceTime, receivedSentTime);
    roundTripTime = ENET_MAX(roundTripTime, 1);

    if (peer->lastReceiveTime > 0)
    {
        ENet::peer_throttle(peer, roundTripTime);

        peer->roundTripTimeVariance -= peer->roundTripTimeVariance / 4;

        if (roundTripTime >= peer->roundTripTime)
        {
            uint32_t diff = roundTripTime - peer->roundTripTime;
            peer->roundTripTimeVariance += diff / 4;
            peer->roundTripTime += diff / 8;
        }
        else
        {
            uint32_t diff = peer->roundTripTime - roundTripTime;
            peer->roundTripTimeVariance += diff / 4;
            peer->roundTripTime -= diff / 8;
        }
    }
    else
    {
        peer->roundTripTime = roundTripTime;
        peer->roundTripTimeVariance = (roundTripTime + 1) / 2;
    }

    if (peer->roundTripTime < peer->lowestRoundTripTime)
    {
        peer->lowestRoundTripTime = peer->roundTripTime;
    }

    if (peer->roundTripTimeVariance > peer->highestRoundTripTimeVariance)
    {
        peer->highestRoundTripTimeVariance = peer->roundTripTimeVariance;
    }

    if (peer->packetThrottleEpoch == 0 ||
        ENET_TIME_DIFFERENCE(host->serviceTime, peer->packetThrottleEpoch) >= peer->packetThrottleInterval)
    {
        peer->lastRoundTripTime = peer->lowestRoundTripTime;
        peer->lastRoundTripTimeVariance = ENET_MAX(peer->highestRoundTripTimeVariance, 1);
        peer->lowestRoundTripTime = peer->roundTripTime;
        peer->highestRoundTripTimeVariance = peer->roundTripTimeVariance;
        peer->packetThrottleEpoch = host->serviceTime;
    }

    peer->lastReceiveTime = ENET_MAX(host->serviceTime, 1);
    peer->earliestTimeout = 0;

    receivedReliableSequenceNumber = ENET_NET_TO_HOST_16(command->acknowledge.receivedReliableSequenceNumber);

    commandNumber =
        enet_protocol_remove_sent_reliable_command(peer, receivedReliableSequenceNumber, command->header.channelID);

    switch (peer->state)
    {
    case ENET_PEER_STATE_ACKNOWLEDGING_CONNECT:
        if (commandNumber != ENet::PROTOCOL_COMMAND_VERIFY_CONNECT)
        {
            return -1;
        }

        enet_protocol_notify_connect(host, peer, event);
        break;

    case ENET_PEER_STATE_DISCONNECTING:
        if (commandNumber != ENet::PROTOCOL_COMMAND_DISCONNECT)
        {
            return -1;
        }

        enet_protocol_notify_disconnect(host, peer, event);
        break;

    case ENET_PEER_STATE_DISCONNECT_LATER:
        if (!ENet::peer_has_outgoing_commands(peer))
        {
            ENet::peer_disconnect(peer, peer->eventData);
        }
        break;

    default:
        break;
    }

    return 0;
}

static int enet_protocol_handle_verify_connect(ENet::Host *host, ENet::Event *event, ENet::Peer *peer,
                                               const ENet::Protocol *command)
{
    uint32_t mtu, windowSize;
    size_t channelCount;

    if (peer->state != ENET_PEER_STATE_CONNECTING)
    {
        return 0;
    }

    channelCount = ENET_NET_TO_HOST_32(command->verifyConnect.channelCount);

    if (channelCount < ENet::PROTOCOL_MINIMUM_CHANNEL_COUNT || channelCount > ENet::PROTOCOL_MAXIMUM_CHANNEL_COUNT ||
        ENET_NET_TO_HOST_32(command->verifyConnect.packetThrottleInterval) != peer->packetThrottleInterval ||
        ENET_NET_TO_HOST_32(command->verifyConnect.packetThrottleAcceleration) != peer->packetThrottleAcceleration ||
        ENET_NET_TO_HOST_32(command->verifyConnect.packetThrottleDeceleration) != peer->packetThrottleDeceleration ||
        command->verifyConnect.connectID != peer->connectID)
    {
        peer->eventData = 0;

        enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);

        return -1;
    }

    enet_protocol_remove_sent_reliable_command(peer, 1, 0xFF);

    if (channelCount < peer->channelCount)
    {
        peer->channelCount = channelCount;
    }

    peer->outgoingPeerID = ENET_NET_TO_HOST_16(command->verifyConnect.outgoingPeerID);
    peer->incomingSessionID = command->verifyConnect.incomingSessionID;
    peer->outgoingSessionID = command->verifyConnect.outgoingSessionID;

    mtu = ENET_NET_TO_HOST_32(command->verifyConnect.mtu);

    if (mtu < ENet::PROTOCOL_MINIMUM_MTU)
    {
        mtu = ENet::PROTOCOL_MINIMUM_MTU;
    }
    else if (mtu > ENet::PROTOCOL_MAXIMUM_MTU)
    {
        mtu = ENet::PROTOCOL_MAXIMUM_MTU;
    }

    if (mtu < peer->mtu)
    {
        peer->mtu = mtu;
    }

    windowSize = ENET_NET_TO_HOST_32(command->verifyConnect.windowSize);

    if (windowSize < ENet::PROTOCOL_MINIMUM_WINDOW_SIZE)
    {
        windowSize = ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }

    if (windowSize > ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE)
    {
        windowSize = ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE;
    }

    if (windowSize < peer->windowSize)
    {
        peer->windowSize = windowSize;
    }

    peer->incomingBandwidth = ENET_NET_TO_HOST_32(command->verifyConnect.incomingBandwidth);
    peer->outgoingBandwidth = ENET_NET_TO_HOST_32(command->verifyConnect.outgoingBandwidth);

    enet_protocol_notify_connect(host, peer, event);
    return 0;
}

static int enet_protocol_handle_incoming_commands(ENet::Host *host, ENet::Event *event)
{
    ENet::ProtocolHeader *header;
    ENet::Protocol *command;
    ENet::Peer *peer;
    uint8_t *currentData;
    size_t headerSize;
    uint16_t peerID, flags;
    uint8_t sessionID;

    if (host->receivedDataLength < (size_t) & ((ENet::ProtocolHeader *)0)->sentTime)
    {
        return 0;
    }

    header = (ENet::ProtocolHeader *)host->receivedData;

    peerID = ENET_NET_TO_HOST_16(header->peerID);
    sessionID = (peerID & ENet::PROTOCOL_HEADER_SESSION_MASK) >> ENet::PROTOCOL_HEADER_SESSION_SHIFT;
    flags = peerID & ENet::PROTOCOL_HEADER_FLAG_MASK;
    peerID &= ~(ENet::PROTOCOL_HEADER_FLAG_MASK | ENet::PROTOCOL_HEADER_SESSION_MASK);

    headerSize = (flags & ENet::PROTOCOL_HEADER_FLAG_SENT_TIME ? sizeof(ENet::ProtocolHeader)
                                                               : (size_t) & ((ENet::ProtocolHeader *)0)->sentTime);
    if (host->checksum != NULL)
    {
        headerSize += sizeof(uint32_t);
    }

    if (peerID == ENet::PROTOCOL_MAXIMUM_PEER_ID)
    {
        peer = NULL;
    }
    else if (peerID >= host->peerCount)
    {
        return 0;
    }
    else
    {
        peer = &host->peers[peerID];

        if (peer->state == ENET_PEER_STATE_DISCONNECTED || peer->state == ENET_PEER_STATE_ZOMBIE ||
            ((host->receivedAddress.host != peer->address.host || host->receivedAddress.port != peer->address.port) &&
             peer->address.host != ENet::HOST_BROADCAST) ||
            (peer->outgoingPeerID < ENet::PROTOCOL_MAXIMUM_PEER_ID && sessionID != peer->incomingSessionID))
        {
            return 0;
        }
    }

    if (flags & ENet::PROTOCOL_HEADER_FLAG_COMPRESSED)
    {
        size_t originalSize;
        if (host->compressor.context == NULL || host->compressor.decompress == NULL)
        {
            return 0;
        }

        originalSize = host->compressor.decompress(
            host->compressor.context, host->receivedData + headerSize, host->receivedDataLength - headerSize,
            host->packetData[1] + headerSize, sizeof(host->packetData[1]) - headerSize);
        if (originalSize <= 0 || originalSize > sizeof(host->packetData[1]) - headerSize)
        {
            return 0;
        }

        memcpy(host->packetData[1], header, headerSize);
        host->receivedData = host->packetData[1];
        host->receivedDataLength = headerSize + originalSize;
    }

    if (host->checksum != NULL)
    {
        uint32_t *checksum = (uint32_t *)&host->receivedData[headerSize - sizeof(uint32_t)],
                 desiredChecksum = *checksum;
        ENetBuffer buffer;

        *checksum = peer != NULL ? peer->connectID : 0;

        buffer.data = host->receivedData;
        buffer.dataLength = host->receivedDataLength;

        if (host->checksum(&buffer, 1) != desiredChecksum)
        {
            return 0;
        }
    }

    if (peer != NULL)
    {
        peer->address.host = host->receivedAddress.host;
        peer->address.port = host->receivedAddress.port;
        peer->incomingDataTotal += host->receivedDataLength;
    }

    currentData = host->receivedData + headerSize;

    while (currentData < &host->receivedData[host->receivedDataLength])
    {
        uint8_t commandNumber;
        size_t commandSize;

        command = (ENet::Protocol *)currentData;

        if (currentData + sizeof(ENet::ProtocolCommandHeader) > &host->receivedData[host->receivedDataLength])
        {
            break;
        }

        commandNumber = command->header.command & ENet::PROTOCOL_COMMAND_MASK;
        if (commandNumber >= ENet::PROTOCOL_COMMAND_COUNT)
        {
            break;
        }

        commandSize = commandSizes[commandNumber];
        if (commandSize == 0 || currentData + commandSize > &host->receivedData[host->receivedDataLength])
        {
            break;
        }

        currentData += commandSize;

        if (peer == NULL && commandNumber != ENet::PROTOCOL_COMMAND_CONNECT)
        {
            break;
        }

        command->header.reliableSequenceNumber = ENET_NET_TO_HOST_16(command->header.reliableSequenceNumber);

        switch (commandNumber)
        {
        case ENet::PROTOCOL_COMMAND_ACKNOWLEDGE:
            if (enet_protocol_handle_acknowledge(host, event, peer, command))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_CONNECT:
            if (peer != NULL)
            {
                goto commandError;
            }
            peer = enet_protocol_handle_connect(host, header, command);
            if (peer == NULL)
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_VERIFY_CONNECT:
            if (enet_protocol_handle_verify_connect(host, event, peer, command))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_DISCONNECT:
            if (enet_protocol_handle_disconnect(host, peer, command))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_PING:
            if (enet_protocol_handle_ping(host, peer, command))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_SEND_RELIABLE:
            if (enet_protocol_handle_send_reliable(host, peer, command, &currentData))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_SEND_UNRELIABLE:
            if (enet_protocol_handle_send_unreliable(host, peer, command, &currentData))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_SEND_UNSEQUENCED:
            if (enet_protocol_handle_send_unsequenced(host, peer, command, &currentData))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_SEND_FRAGMENT:
            if (enet_protocol_handle_send_fragment(host, peer, command, &currentData))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_BANDWIDTH_LIMIT:
            if (enet_protocol_handle_bandwidth_limit(host, peer, command))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_THROTTLE_CONFIGURE:
            if (enet_protocol_handle_throttle_configure(host, peer, command))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
            if (enet_protocol_handle_send_unreliable_fragment(host, peer, command, &currentData))
            {
                goto commandError;
            }
            break;

        default:
            goto commandError;
        }

        if (peer != NULL && (command->header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) != 0)
        {
            uint16_t sentTime;

            if (!(flags & ENet::PROTOCOL_HEADER_FLAG_SENT_TIME))
            {
                break;
            }

            sentTime = ENET_NET_TO_HOST_16(header->sentTime);

            switch (peer->state)
            {
            case ENET_PEER_STATE_DISCONNECTING:
            case ENET_PEER_STATE_ACKNOWLEDGING_CONNECT:
            case ENET_PEER_STATE_DISCONNECTED:
            case ENET_PEER_STATE_ZOMBIE:
                break;

            case ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT:
                if ((command->header.command & ENet::PROTOCOL_COMMAND_MASK) == ENet::PROTOCOL_COMMAND_DISCONNECT)
                {
                    ENet::peer_queue_acknowledgement(peer, command, sentTime);
                }
                break;

            default:
                ENet::peer_queue_acknowledgement(peer, command, sentTime);
                break;
            }
        }
    }

commandError:
    if (event != NULL && event->type != ENET_EVENT_TYPE_NONE)
    {
        return 1;
    }

    return 0;
}

static int enet_protocol_receive_incoming_commands(ENet::Host *host, ENet::Event *event)
{
    int packets;

    for (packets = 0; packets < 256; ++packets)
    {
        int receivedLength;
        ENetBuffer buffer;

        buffer.data = host->packetData[0];
        buffer.dataLength = sizeof(host->packetData[0]);

        receivedLength = ENet::socket_receive(host->socket, &host->receivedAddress, &buffer, 1);

        if (receivedLength < 0)
        {
            return -1;
        }

        if (receivedLength == 0)
        {
            return 0;
        }

        host->receivedData = host->packetData[0];
        host->receivedDataLength = receivedLength;

        host->totalReceivedData += receivedLength;
        host->totalReceivedPackets++;

        if (host->intercept != NULL)
        {
            switch (host->intercept(host, event))
            {
            case 1:
                if (event != NULL && event->type != ENET_EVENT_TYPE_NONE)
                {
                    return 1;
                }

                continue;

            case -1:
                return -1;

            default:
                break;
            }
        }

        switch (enet_protocol_handle_incoming_commands(host, event))
        {
        case 1:
            return 1;

        case -1:
            return -1;

        default:
            break;
        }
    }

    return 0;
}

static void enet_protocol_send_acknowledgements(ENet::Host *host, ENet::Peer *peer)
{
    ENet::Protocol *command = &host->commands[host->commandCount];
    ENetBuffer *buffer = &host->buffers[host->bufferCount];
    ENet::Acknowledgement *acknowledgement;
    ENet::ListIterator currentAcknowledgement;
    uint16_t reliableSequenceNumber;

    currentAcknowledgement = ENet::list_begin(&peer->acknowledgements);

    while (currentAcknowledgement != ENet::list_end(&peer->acknowledgements))
    {
        if (command >= &host->commands[sizeof(host->commands) / sizeof(ENet::Protocol)] ||
            buffer >= &host->buffers[sizeof(host->buffers) / sizeof(ENetBuffer)] ||
            peer->mtu - host->packetSize < sizeof(ENet::ProtocolAcknowledge))
        {
            peer->flags |= ENET_PEER_FLAG_CONTINUE_SENDING;

            break;
        }

        acknowledgement = (ENet::Acknowledgement *)currentAcknowledgement;

        currentAcknowledgement = ENet::list_next(currentAcknowledgement);

        buffer->data = command;
        buffer->dataLength = sizeof(ENet::ProtocolAcknowledge);

        host->packetSize += buffer->dataLength;

        reliableSequenceNumber = ENET_HOST_TO_NET_16(acknowledgement->command.header.reliableSequenceNumber);

        command->header.command = ENet::PROTOCOL_COMMAND_ACKNOWLEDGE;
        command->header.channelID = acknowledgement->command.header.channelID;
        command->header.reliableSequenceNumber = reliableSequenceNumber;
        command->acknowledge.receivedReliableSequenceNumber = reliableSequenceNumber;
        command->acknowledge.receivedSentTime = ENET_HOST_TO_NET_16(acknowledgement->sentTime);

        if ((acknowledgement->command.header.command & ENet::PROTOCOL_COMMAND_MASK) ==
            ENet::PROTOCOL_COMMAND_DISCONNECT)
        {
            enet_protocol_dispatch_state(host, peer, ENET_PEER_STATE_ZOMBIE);
        }

        ENet::list_remove(&acknowledgement->acknowledgementList);
        ENet::enet_free(acknowledgement);

        ++command;
        ++buffer;
    }

    host->commandCount = command - host->commands;
    host->bufferCount = buffer - host->buffers;
}

static int enet_protocol_check_timeouts(ENet::Host *host, ENet::Peer *peer, ENet::Event *event)
{
    ENet::OutgoingCommand *outgoingCommand;
    ENet::ListIterator currentCommand, insertPosition, insertSendReliablePosition;

    currentCommand = ENet::list_begin(&peer->sentReliableCommands);
    insertPosition = ENet::list_begin(&peer->outgoingCommands);
    insertSendReliablePosition = ENet::list_begin(&peer->outgoingSendReliableCommands);

    while (currentCommand != ENet::list_end(&peer->sentReliableCommands))
    {
        outgoingCommand = (ENet::OutgoingCommand *)currentCommand;

        currentCommand = ENet::list_next(currentCommand);

        if (ENET_TIME_DIFFERENCE(host->serviceTime, outgoingCommand->sentTime) < outgoingCommand->roundTripTimeout)
        {
            continue;
        }

        if (peer->earliestTimeout == 0 || ENET_TIME_LESS(outgoingCommand->sentTime, peer->earliestTimeout))
        {
            peer->earliestTimeout = outgoingCommand->sentTime;
        }

        if (peer->earliestTimeout != 0 &&
            (ENET_TIME_DIFFERENCE(host->serviceTime, peer->earliestTimeout) >= peer->timeoutMaximum ||
             ((1 << (outgoingCommand->sendAttempts - 1)) >= peer->timeoutLimit &&
              ENET_TIME_DIFFERENCE(host->serviceTime, peer->earliestTimeout) >= peer->timeoutMinimum)))
        {
            enet_protocol_notify_disconnect(host, peer, event);

            return 1;
        }

        ++peer->packetsLost;

        outgoingCommand->roundTripTimeout *= 2;

        if (outgoingCommand->packet != NULL)
        {
            peer->reliableDataInTransit -= outgoingCommand->fragmentLength;

            ENet::list_insert(insertSendReliablePosition, ENet::list_remove(&outgoingCommand->outgoingCommandList));
        }
        else
        {
            ENet::list_insert(insertPosition, ENet::list_remove(&outgoingCommand->outgoingCommandList));
        }

        if (currentCommand == ENet::list_begin(&peer->sentReliableCommands) &&
            !ENet::list_empty(&peer->sentReliableCommands))
        {
            outgoingCommand = (ENet::OutgoingCommand *)currentCommand;

            peer->nextTimeout = outgoingCommand->sentTime + outgoingCommand->roundTripTimeout;
        }
    }

    return 0;
}

static int enet_protocol_check_outgoing_commands(ENet::Host *host, ENet::Peer *peer, ENet::List *sentUnreliableCommands)
{
    ENet::Protocol *command = &host->commands[host->commandCount];
    ENetBuffer *buffer = &host->buffers[host->bufferCount];
    ENet::OutgoingCommand *outgoingCommand;
    ENet::ListIterator currentCommand, currentSendReliableCommand;
    ENet::Channel *channel = NULL;
    uint16_t reliableWindow = 0;
    size_t commandSize;
    int windowWrap = 0, canPing = 1;

    currentCommand = ENet::list_begin(&peer->outgoingCommands);
    currentSendReliableCommand = ENet::list_begin(&peer->outgoingSendReliableCommands);

    for (;;)
    {
        if (currentCommand != ENet::list_end(&peer->outgoingCommands))
        {
            outgoingCommand = (ENet::OutgoingCommand *)currentCommand;

            if (currentSendReliableCommand != ENet::list_end(&peer->outgoingSendReliableCommands) &&
                ENET_TIME_LESS(((ENet::OutgoingCommand *)currentSendReliableCommand)->queueTime,
                               outgoingCommand->queueTime))
            {
                goto useSendReliableCommand;
            }

            currentCommand = ENet::list_next(currentCommand);
        }
        else if (currentSendReliableCommand != ENet::list_end(&peer->outgoingSendReliableCommands))
        {
        useSendReliableCommand:
            outgoingCommand = (ENet::OutgoingCommand *)currentSendReliableCommand;
            currentSendReliableCommand = ENet::list_next(currentSendReliableCommand);
        }
        else
        {
            break;
        }

        if (outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
        {
            channel = outgoingCommand->command.header.channelID < peer->channelCount
                          ? &peer->channels[outgoingCommand->command.header.channelID]
                          : NULL;
            reliableWindow = outgoingCommand->reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
            if (channel != NULL)
            {
                if (windowWrap)
                {
                    continue;
                }
                else if (outgoingCommand->sendAttempts < 1 &&
                         !(outgoingCommand->reliableSequenceNumber % ENET_PEER_RELIABLE_WINDOW_SIZE) &&
                         (channel->reliableWindows[(reliableWindow + ENET_PEER_RELIABLE_WINDOWS - 1) %
                                                   ENET_PEER_RELIABLE_WINDOWS] >= ENET_PEER_RELIABLE_WINDOW_SIZE ||
                          channel->usedReliableWindows &
                              ((((1 << (ENET_PEER_FREE_RELIABLE_WINDOWS + 2)) - 1) << reliableWindow) |
                               (((1 << (ENET_PEER_FREE_RELIABLE_WINDOWS + 2)) - 1) >>
                                (ENET_PEER_RELIABLE_WINDOWS - reliableWindow)))))
                {
                    windowWrap = 1;
                    currentSendReliableCommand = ENet::list_end(&peer->outgoingSendReliableCommands);

                    continue;
                }
            }

            if (outgoingCommand->packet != NULL)
            {
                uint32_t windowSize = (peer->packetThrottle * peer->windowSize) / ENET_PEER_PACKET_THROTTLE_SCALE;

                if (peer->reliableDataInTransit + outgoingCommand->fragmentLength > ENET_MAX(windowSize, peer->mtu))
                {
                    currentSendReliableCommand = ENet::list_end(&peer->outgoingSendReliableCommands);

                    continue;
                }
            }

            canPing = 0;
        }

        commandSize = commandSizes[outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_MASK];
        if (command >= &host->commands[sizeof(host->commands) / sizeof(ENet::Protocol)] ||
            buffer + 1 >= &host->buffers[sizeof(host->buffers) / sizeof(ENetBuffer)] ||
            peer->mtu - host->packetSize < commandSize ||
            (outgoingCommand->packet != NULL &&
             (uint16_t)(peer->mtu - host->packetSize) < (uint16_t)(commandSize + outgoingCommand->fragmentLength)))
        {
            peer->flags |= ENET_PEER_FLAG_CONTINUE_SENDING;

            break;
        }

        if (outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
        {
            if (channel != NULL && outgoingCommand->sendAttempts < 1)
            {
                channel->usedReliableWindows |= 1 << reliableWindow;
                ++channel->reliableWindows[reliableWindow];
            }

            ++outgoingCommand->sendAttempts;

            if (outgoingCommand->roundTripTimeout == 0)
            {
                outgoingCommand->roundTripTimeout = peer->roundTripTime + 4 * peer->roundTripTimeVariance;
            }

            if (ENet::list_empty(&peer->sentReliableCommands))
            {
                peer->nextTimeout = host->serviceTime + outgoingCommand->roundTripTimeout;
            }

            ENet::list_insert(ENet::list_end(&peer->sentReliableCommands),
                              ENet::list_remove(&outgoingCommand->outgoingCommandList));

            outgoingCommand->sentTime = host->serviceTime;

            host->headerFlags |= ENet::PROTOCOL_HEADER_FLAG_SENT_TIME;

            peer->reliableDataInTransit += outgoingCommand->fragmentLength;
        }
        else
        {
            if (outgoingCommand->packet != NULL && outgoingCommand->fragmentOffset == 0)
            {
                peer->packetThrottleCounter += ENET_PEER_PACKET_THROTTLE_COUNTER;
                peer->packetThrottleCounter %= ENET_PEER_PACKET_THROTTLE_SCALE;

                if (peer->packetThrottleCounter > peer->packetThrottle)
                {
                    uint16_t reliableSequenceNumber = outgoingCommand->reliableSequenceNumber,
                             unreliableSequenceNumber = outgoingCommand->unreliableSequenceNumber;
                    for (;;)
                    {
                        --outgoingCommand->packet->referenceCount;

                        if (outgoingCommand->packet->referenceCount == 0)
                        {
                            ENet::packet_destroy(outgoingCommand->packet);
                        }

                        ENet::list_remove(&outgoingCommand->outgoingCommandList);
                        ENet::enet_free(outgoingCommand);

                        if (currentCommand == ENet::list_end(&peer->outgoingCommands))
                        {
                            break;
                        }

                        outgoingCommand = (ENet::OutgoingCommand *)currentCommand;
                        if (outgoingCommand->reliableSequenceNumber != reliableSequenceNumber ||
                            outgoingCommand->unreliableSequenceNumber != unreliableSequenceNumber)
                        {
                            break;
                        }

                        currentCommand = ENet::list_next(currentCommand);
                    }

                    continue;
                }
            }

            ENet::list_remove(&outgoingCommand->outgoingCommandList);

            if (outgoingCommand->packet != NULL)
            {
                ENet::list_insert(ENet::list_end(sentUnreliableCommands), outgoingCommand);
            }
        }

        buffer->data = command;
        buffer->dataLength = commandSize;

        host->packetSize += buffer->dataLength;

        *command = outgoingCommand->command;

        if (outgoingCommand->packet != NULL)
        {
            ++buffer;

            buffer->data = outgoingCommand->packet->data + outgoingCommand->fragmentOffset;
            buffer->dataLength = outgoingCommand->fragmentLength;

            host->packetSize += outgoingCommand->fragmentLength;
        }
        else if (!(outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE))
        {
            ENet::enet_free(outgoingCommand);
        }

        ++peer->packetsSent;

        ++command;
        ++buffer;
    }

    host->commandCount = command - host->commands;
    host->bufferCount = buffer - host->buffers;

    if (peer->state == ENET_PEER_STATE_DISCONNECT_LATER && !ENet::peer_has_outgoing_commands(peer) &&
        ENet::list_empty(sentUnreliableCommands))
    {
        ENet::peer_disconnect(peer, peer->eventData);
    }

    return canPing;
}

static int enet_protocol_send_outgoing_commands(ENet::Host *host, ENet::Event *event, int checkForTimeouts)
{
    uint8_t headerData[sizeof(ENet::ProtocolHeader) + sizeof(uint32_t)];
    ENet::ProtocolHeader *header = (ENet::ProtocolHeader *)headerData;
    int sentLength = 0;
    size_t shouldCompress = 0;
    ENet::List sentUnreliableCommands;

    ENet::list_clear(&sentUnreliableCommands);

    for (int sendPass = 0, continueSending = 0; sendPass <= continueSending; ++sendPass)
    {
        for (ENet::Peer *currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer)
        {
            if (currentPeer->state == ENET_PEER_STATE_DISCONNECTED || currentPeer->state == ENET_PEER_STATE_ZOMBIE ||
                (sendPass > 0 && !(currentPeer->flags & ENET_PEER_FLAG_CONTINUE_SENDING)))
            {
                continue;
            }

            currentPeer->flags &= ~ENET_PEER_FLAG_CONTINUE_SENDING;

            host->headerFlags = 0;
            host->commandCount = 0;
            host->bufferCount = 1;
            host->packetSize = sizeof(ENet::ProtocolHeader);

            if (!ENet::list_empty(&currentPeer->acknowledgements))
            {
                enet_protocol_send_acknowledgements(host, currentPeer);
            }

            if (checkForTimeouts != 0 && !ENet::list_empty(&currentPeer->sentReliableCommands) &&
                ENET_TIME_GREATER_EQUAL(host->serviceTime, currentPeer->nextTimeout) &&
                enet_protocol_check_timeouts(host, currentPeer, event) == 1)
            {
                if (event != NULL && event->type != ENET_EVENT_TYPE_NONE)
                {
                    return 1;
                }
                else
                {
                    goto nextPeer;
                }
            }

            if (((ENet::list_empty(&currentPeer->outgoingCommands) &&
                  ENet::list_empty(&currentPeer->outgoingSendReliableCommands)) ||
                 enet_protocol_check_outgoing_commands(host, currentPeer, &sentUnreliableCommands)) &&
                ENet::list_empty(&currentPeer->sentReliableCommands) &&
                ENET_TIME_DIFFERENCE(host->serviceTime, currentPeer->lastReceiveTime) >= currentPeer->pingInterval &&
                currentPeer->mtu - host->packetSize >= sizeof(ENet::ProtocolPing))
            {
                ENet::peer_ping(currentPeer);
                enet_protocol_check_outgoing_commands(host, currentPeer, &sentUnreliableCommands);
            }

            if (host->commandCount == 0)
            {
                goto nextPeer;
            }

            if (currentPeer->packetLossEpoch == 0)
            {
                currentPeer->packetLossEpoch = host->serviceTime;
            }
            else if (ENET_TIME_DIFFERENCE(host->serviceTime, currentPeer->packetLossEpoch) >=
                         ENET_PEER_PACKET_LOSS_INTERVAL &&
                     currentPeer->packetsSent > 0)
            {
                uint32_t packetLoss = currentPeer->packetsLost * ENET_PEER_PACKET_LOSS_SCALE / currentPeer->packetsSent;

#ifdef ENET_DEBUG
                printf("peer %u: %f%%+-%f%% packet loss, %u+-%u ms round trip time, %f%% throttle, %u outgoing, %u/%u "
                       "incoming\n",
                       currentPeer->incomingPeerID, currentPeer->packetLoss / (float)ENET_PEER_PACKET_LOSS_SCALE,
                       currentPeer->packetLossVariance / (float)ENET_PEER_PACKET_LOSS_SCALE, currentPeer->roundTripTime,
                       currentPeer->roundTripTimeVariance,
                       currentPeer->packetThrottle / (float)ENET_PEER_PACKET_THROTTLE_SCALE,
                       enet_list_size(&currentPeer->outgoingCommands) +
                           enet_list_size(&currentPeer->outgoingSendReliableCommands),
                       currentPeer->channels != NULL ? enet_list_size(&currentPeer->channels->incomingReliableCommands)
                                                     : 0,
                       currentPeer->channels != NULL
                           ? enet_list_size(&currentPeer->channels->incomingUnreliableCommands)
                           : 0);
#endif

                currentPeer->packetLossVariance =
                    (currentPeer->packetLossVariance * 3 + ENET_DIFFERENCE(packetLoss, currentPeer->packetLoss)) / 4;
                currentPeer->packetLoss = (currentPeer->packetLoss * 7 + packetLoss) / 8;

                currentPeer->packetLossEpoch = host->serviceTime;
                currentPeer->packetsSent = 0;
                currentPeer->packetsLost = 0;
            }

            host->buffers->data = headerData;
            if (host->headerFlags & ENet::PROTOCOL_HEADER_FLAG_SENT_TIME)
            {
                header->sentTime = ENET_HOST_TO_NET_16(host->serviceTime & 0xFFFF);

                host->buffers->dataLength = sizeof(ENet::ProtocolHeader);
            }
            else
            {
                host->buffers->dataLength = (size_t) & ((ENet::ProtocolHeader *)0)->sentTime;
            }

            shouldCompress = 0;
            if (host->compressor.context != NULL && host->compressor.compress != NULL)
            {
                size_t originalSize = host->packetSize - sizeof(ENet::ProtocolHeader),
                       compressedSize =
                           host->compressor.compress(host->compressor.context, &host->buffers[1], host->bufferCount - 1,
                                                     originalSize, host->packetData[1], originalSize);
                if (compressedSize > 0 && compressedSize < originalSize)
                {
                    host->headerFlags |= ENet::PROTOCOL_HEADER_FLAG_COMPRESSED;
                    shouldCompress = compressedSize;
#ifdef ENET_DEBUG_COMPRESS
                    printf("peer %u: compressed %u -> %u (%u%%)\n", currentPeer->incomingPeerID, originalSize,
                           compressedSize, (compressedSize * 100) / originalSize);
#endif
                }
            }

            if (currentPeer->outgoingPeerID < ENet::PROTOCOL_MAXIMUM_PEER_ID)
            {
                host->headerFlags |= currentPeer->outgoingSessionID << ENet::PROTOCOL_HEADER_SESSION_SHIFT;
            }
            header->peerID = ENET_HOST_TO_NET_16(currentPeer->outgoingPeerID | host->headerFlags);
            if (host->checksum != NULL)
            {
                uint32_t *checksum = (uint32_t *)&headerData[host->buffers->dataLength];
                *checksum = currentPeer->outgoingPeerID < ENet::PROTOCOL_MAXIMUM_PEER_ID ? currentPeer->connectID : 0;
                host->buffers->dataLength += sizeof(uint32_t);
                *checksum = host->checksum(host->buffers, host->bufferCount);
            }

            if (shouldCompress > 0)
            {
                host->buffers[1].data = host->packetData[1];
                host->buffers[1].dataLength = shouldCompress;
                host->bufferCount = 2;
            }

            currentPeer->lastSendTime = host->serviceTime;

            sentLength = ENet::socket_send(host->socket, &currentPeer->address, host->buffers, host->bufferCount);

            enet_protocol_remove_sent_unreliable_commands(currentPeer, &sentUnreliableCommands);

            if (sentLength < 0)
            {
                return -1;
            }

            host->totalSentData += sentLength;
            host->totalSentPackets++;

        nextPeer:
            if (currentPeer->flags & ENET_PEER_FLAG_CONTINUE_SENDING)
            {
                continueSending = sendPass + 1;
            }
        }
    }

    return 0;
}

void ENet::host_flush(ENet::Host *host)
{
    host->serviceTime = ENet::time_get();

    enet_protocol_send_outgoing_commands(host, NULL, 0);
}

int ENet::host_check_events(ENet::Host *host, ENet::Event *event)
{
    if (event == NULL)
    {
        return -1;
    }

    event->type = ENET_EVENT_TYPE_NONE;
    event->peer = NULL;
    event->packet = NULL;

    return enet_protocol_dispatch_incoming_commands(host, event);
}

int ENet::host_service(ENet::Host *host, ENet::Event *event, uint32_t timeout)
{
    uint32_t waitCondition;

    if (event != NULL)
    {
        event->type = ENET_EVENT_TYPE_NONE;
        event->peer = NULL;
        event->packet = NULL;

        switch (enet_protocol_dispatch_incoming_commands(host, event))
        {
        case 1:
            return 1;

        case -1:
#ifdef ENET_DEBUG
            perror("Error dispatching incoming packets");
#endif

            return -1;

        default:
            break;
        }
    }

    host->serviceTime = ENet::time_get();

    timeout += host->serviceTime;

    do
    {
        if (ENET_TIME_DIFFERENCE(host->serviceTime, host->bandwidthThrottleEpoch) >=
            ENET_HOST_BANDWIDTH_THROTTLE_INTERVAL)
        {
            ENet::host_bandwidth_throttle(host);
        }

        switch (enet_protocol_send_outgoing_commands(host, event, 1))
        {
        case 1:
            return 1;

        case -1:
#ifdef ENET_DEBUG
            perror("Error sending outgoing packets");
#endif

            return -1;

        default:
            break;
        }

        switch (enet_protocol_receive_incoming_commands(host, event))
        {
        case 1:
            return 1;

        case -1:
#ifdef ENET_DEBUG
            perror("Error receiving incoming packets");
#endif

            return -1;

        default:
            break;
        }

        switch (enet_protocol_send_outgoing_commands(host, event, 1))
        {
        case 1:
            return 1;

        case -1:
#ifdef ENET_DEBUG
            perror("Error sending outgoing packets");
#endif

            return -1;

        default:
            break;
        }

        if (event != NULL)
        {
            switch (enet_protocol_dispatch_incoming_commands(host, event))
            {
            case 1:
                return 1;

            case -1:
#ifdef ENET_DEBUG
                perror("Error dispatching incoming packets");
#endif

                return -1;

            default:
                break;
            }
        }

        if (ENET_TIME_GREATER_EQUAL(host->serviceTime, timeout))
        {
            return 0;
        }

        do
        {
            host->serviceTime = ENet::time_get();

            if (ENET_TIME_GREATER_EQUAL(host->serviceTime, timeout))
            {
                return 0;
            }

            waitCondition = ENET_SOCKET_WAIT_RECEIVE | ENET_SOCKET_WAIT_INTERRUPT;

            if (ENet::socket_wait(host->socket, &waitCondition, ENET_TIME_DIFFERENCE(timeout, host->serviceTime)) != 0)
            {
                return -1;
            }
        } while (waitCondition & ENET_SOCKET_WAIT_INTERRUPT);

        host->serviceTime = ENet::time_get();
    } while (waitCondition & ENET_SOCKET_WAIT_RECEIVE);

    return 0;
}
