/**
 @file  peer.c
 @brief ENet peer management functions
*/
#include <string.h>
#include "enetcpp/enetcpp.h"

/** @defgroup peer ENet peer functions
    @{
*/

void enet_peer_throttle_configure(ENetPeer *peer, uint32_t interval, uint32_t acceleration, uint32_t deceleration)
{
    ENetProtocol command;

    peer->packetThrottleInterval = interval;
    peer->packetThrottleAcceleration = acceleration;
    peer->packetThrottleDeceleration = deceleration;

    command.header.command = ENET_PROTOCOL_COMMAND_THROTTLE_CONFIGURE | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    command.header.channelID = 0xFF;

    command.throttleConfigure.packetThrottleInterval = ENET_HOST_TO_NET_32(interval);
    command.throttleConfigure.packetThrottleAcceleration = ENET_HOST_TO_NET_32(acceleration);
    command.throttleConfigure.packetThrottleDeceleration = ENET_HOST_TO_NET_32(deceleration);

    enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
}

int enet_peer_throttle(ENetPeer *peer, uint32_t rtt)
{
    if (peer->lastRoundTripTime <= peer->lastRoundTripTimeVariance)
    {
        peer->packetThrottle = peer->packetThrottleLimit;
    }
    else if (rtt <= peer->lastRoundTripTime)
    {
        peer->packetThrottle += peer->packetThrottleAcceleration;

        if (peer->packetThrottle > peer->packetThrottleLimit)
        {
            peer->packetThrottle = peer->packetThrottleLimit;
        }

        return 1;
    }
    else if (rtt > peer->lastRoundTripTime + 2 * peer->lastRoundTripTimeVariance)
    {
        if (peer->packetThrottle > peer->packetThrottleDeceleration)
        {
            peer->packetThrottle -= peer->packetThrottleDeceleration;
        }
        else
        {
            peer->packetThrottle = 0;
        }

        return -1;
    }

    return 0;
}

int enet_peer_send(ENetPeer *peer, uint8_t channelID, ENetPacket *packet)
{
    ENetChannel *channel;
    ENetProtocol command;
    size_t fragmentLength;

    if (peer->state != ENET_PEER_STATE_CONNECTED || channelID >= peer->channelCount ||
        packet->dataLength > peer->host->maximumPacketSize)
    {
        return -1;
    }

    channel = &peer->channels[channelID];
    fragmentLength = peer->mtu - sizeof(ENetProtocolHeader) - sizeof(ENetProtocolSendFragment);
    if (peer->host->checksum != NULL)
    {
        fragmentLength -= sizeof(uint32_t);
    }

    if (packet->dataLength > fragmentLength)
    {
        uint32_t fragmentCount = (packet->dataLength + fragmentLength - 1) / fragmentLength, fragmentNumber,
                 fragmentOffset;
        uint8_t commandNumber;
        uint16_t startSequenceNumber;
        ENet::List fragments;
        ENetOutgoingCommand *fragment;

        if (fragmentCount > ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT)
        {
            return -1;
        }

        if ((packet->flags & (ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT)) ==
                ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT &&
            channel->outgoingUnreliableSequenceNumber < 0xFFFF)
        {
            commandNumber = ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT;
            startSequenceNumber = ENET_HOST_TO_NET_16(channel->outgoingUnreliableSequenceNumber + 1);
        }
        else
        {
            commandNumber = ENET_PROTOCOL_COMMAND_SEND_FRAGMENT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
            startSequenceNumber = ENET_HOST_TO_NET_16(channel->outgoingReliableSequenceNumber + 1);
        }

        ENet::list_clear(&fragments);

        for (fragmentNumber = 0, fragmentOffset = 0; fragmentOffset < packet->dataLength;
             ++fragmentNumber, fragmentOffset += fragmentLength)
        {
            if (packet->dataLength - fragmentOffset < fragmentLength)
            {
                fragmentLength = packet->dataLength - fragmentOffset;
            }

            fragment = (ENetOutgoingCommand *)ENet::enet_malloc(sizeof(ENetOutgoingCommand));
            if (fragment == NULL)
            {
                while (!ENet::list_empty(&fragments))
                {
                    fragment = (ENetOutgoingCommand *)ENet::list_remove(ENet::list_begin(&fragments));

                    ENet::enet_free(fragment);
                }

                return -1;
            }

            fragment->fragmentOffset = fragmentOffset;
            fragment->fragmentLength = fragmentLength;
            fragment->packet = packet;
            fragment->command.header.command = commandNumber;
            fragment->command.header.channelID = channelID;
            fragment->command.sendFragment.startSequenceNumber = startSequenceNumber;
            fragment->command.sendFragment.dataLength = ENET_HOST_TO_NET_16(fragmentLength);
            fragment->command.sendFragment.fragmentCount = ENET_HOST_TO_NET_32(fragmentCount);
            fragment->command.sendFragment.fragmentNumber = ENET_HOST_TO_NET_32(fragmentNumber);
            fragment->command.sendFragment.totalLength = ENET_HOST_TO_NET_32(packet->dataLength);
            fragment->command.sendFragment.fragmentOffset = ENET_NET_TO_HOST_32(fragmentOffset);

            ENet::list_insert(ENet::list_end(&fragments), fragment);
        }

        packet->referenceCount += fragmentNumber;

        while (!ENet::list_empty(&fragments))
        {
            fragment = (ENetOutgoingCommand *)ENet::list_remove(ENet::list_begin(&fragments));

            enet_peer_setup_outgoing_command(peer, fragment);
        }

        return 0;
    }

    command.header.channelID = channelID;

    if ((packet->flags & (ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNSEQUENCED)) == ENET_PACKET_FLAG_UNSEQUENCED)
    {
        command.header.command = ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED | ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
        command.sendUnsequenced.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
    }
    else if (packet->flags & ENET_PACKET_FLAG_RELIABLE || channel->outgoingUnreliableSequenceNumber >= 0xFFFF)
    {
        command.header.command = ENET_PROTOCOL_COMMAND_SEND_RELIABLE | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
        command.sendReliable.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
    }
    else
    {
        command.header.command = ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE;
        command.sendUnreliable.dataLength = ENET_HOST_TO_NET_16(packet->dataLength);
    }

    if (enet_peer_queue_outgoing_command(peer, &command, packet, 0, packet->dataLength) == NULL)
    {
        return -1;
    }

    return 0;
}

ENetPacket *enet_peer_receive(ENetPeer *peer, uint8_t *channelID)
{
    ENetIncomingCommand *incomingCommand;
    ENetPacket *packet;

    if (ENet::list_empty(&peer->dispatchedCommands))
    {
        return NULL;
    }

    incomingCommand = (ENetIncomingCommand *)ENet::list_remove(ENet::list_begin(&peer->dispatchedCommands));

    if (channelID != NULL)
    {
        *channelID = incomingCommand->command.header.channelID;
    }

    packet = incomingCommand->packet;

    --packet->referenceCount;

    if (incomingCommand->fragments != NULL)
    {
        ENet::enet_free(incomingCommand->fragments);
    }

    ENet::enet_free(incomingCommand);

    peer->totalWaitingData -= packet->dataLength;

    return packet;
}

static void enet_peer_reset_outgoing_commands(ENet::List *queue)
{
    ENetOutgoingCommand *outgoingCommand;

    while (!ENet::list_empty(queue))
    {
        outgoingCommand = (ENetOutgoingCommand *)ENet::list_remove(ENet::list_begin(queue));

        if (outgoingCommand->packet != NULL)
        {
            --outgoingCommand->packet->referenceCount;

            if (outgoingCommand->packet->referenceCount == 0)
            {
                enet_packet_destroy(outgoingCommand->packet);
            }
        }

        ENet::enet_free(outgoingCommand);
    }
}

static void enet_peer_remove_incoming_commands(ENet::List *queue, ENet::ListIterator startCommand,
                                               ENet::ListIterator endCommand, ENetIncomingCommand *excludeCommand)
{
    (void)queue;

    ENet::ListIterator currentCommand;

    for (currentCommand = startCommand; currentCommand != endCommand;)
    {
        ENetIncomingCommand *incomingCommand = (ENetIncomingCommand *)currentCommand;

        currentCommand = ENet::list_next(currentCommand);

        if (incomingCommand == excludeCommand)
        {
            continue;
        }

        ENet::list_remove(&incomingCommand->incomingCommandList);

        if (incomingCommand->packet != NULL)
        {
            --incomingCommand->packet->referenceCount;

            if (incomingCommand->packet->referenceCount == 0)
            {
                enet_packet_destroy(incomingCommand->packet);
            }
        }

        if (incomingCommand->fragments != NULL)
        {
            ENet::enet_free(incomingCommand->fragments);
        }

        ENet::enet_free(incomingCommand);
    }
}

static void enet_peer_reset_incoming_commands(ENet::List *queue)
{
    enet_peer_remove_incoming_commands(queue, ENet::list_begin(queue), ENet::list_end(queue), NULL);
}

void enet_peer_reset_queues(ENetPeer *peer)
{
    ENetChannel *channel;

    if (peer->flags & ENET_PEER_FLAG_NEEDS_DISPATCH)
    {
        ENet::list_remove(&peer->dispatchList);

        peer->flags &= ~ENET_PEER_FLAG_NEEDS_DISPATCH;
    }

    while (!ENet::list_empty(&peer->acknowledgements))
    {
        ENet::enet_free(ENet::list_remove(ENet::list_begin(&peer->acknowledgements)));
    }

    enet_peer_reset_outgoing_commands(&peer->sentReliableCommands);
    enet_peer_reset_outgoing_commands(&peer->outgoingCommands);
    enet_peer_reset_outgoing_commands(&peer->outgoingSendReliableCommands);
    enet_peer_reset_incoming_commands(&peer->dispatchedCommands);

    if (peer->channels != NULL && peer->channelCount > 0)
    {
        for (channel = peer->channels; channel < &peer->channels[peer->channelCount]; ++channel)
        {
            enet_peer_reset_incoming_commands(&channel->incomingReliableCommands);
            enet_peer_reset_incoming_commands(&channel->incomingUnreliableCommands);
        }

        ENet::enet_free(peer->channels);
    }

    peer->channels = NULL;
    peer->channelCount = 0;
}

void enet_peer_on_connect(ENetPeer *peer)
{
    if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
    {
        if (peer->incomingBandwidth != 0)
        {
            ++peer->host->bandwidthLimitedPeers;
        }

        ++peer->host->connectedPeers;
    }
}

void enet_peer_on_disconnect(ENetPeer *peer)
{
    if (peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER)
    {
        if (peer->incomingBandwidth != 0)
        {
            --peer->host->bandwidthLimitedPeers;
        }

        --peer->host->connectedPeers;
    }
}

void enet_peer_reset(ENetPeer *peer)
{
    enet_peer_on_disconnect(peer);

    peer->outgoingPeerID = ENET_PROTOCOL_MAXIMUM_PEER_ID;
    peer->connectID = 0;

    peer->state = ENET_PEER_STATE_DISCONNECTED;

    peer->incomingBandwidth = 0;
    peer->outgoingBandwidth = 0;
    peer->incomingBandwidthThrottleEpoch = 0;
    peer->outgoingBandwidthThrottleEpoch = 0;
    peer->incomingDataTotal = 0;
    peer->outgoingDataTotal = 0;
    peer->lastSendTime = 0;
    peer->lastReceiveTime = 0;
    peer->nextTimeout = 0;
    peer->earliestTimeout = 0;
    peer->packetLossEpoch = 0;
    peer->packetsSent = 0;
    peer->packetsLost = 0;
    peer->packetLoss = 0;
    peer->packetLossVariance = 0;
    peer->packetThrottle = ENET_PEER_DEFAULT_PACKET_THROTTLE;
    peer->packetThrottleLimit = ENET_PEER_PACKET_THROTTLE_SCALE;
    peer->packetThrottleCounter = 0;
    peer->packetThrottleEpoch = 0;
    peer->packetThrottleAcceleration = ENET_PEER_PACKET_THROTTLE_ACCELERATION;
    peer->packetThrottleDeceleration = ENET_PEER_PACKET_THROTTLE_DECELERATION;
    peer->packetThrottleInterval = ENET_PEER_PACKET_THROTTLE_INTERVAL;
    peer->pingInterval = ENET_PEER_PING_INTERVAL;
    peer->timeoutLimit = ENET_PEER_TIMEOUT_LIMIT;
    peer->timeoutMinimum = ENET_PEER_TIMEOUT_MINIMUM;
    peer->timeoutMaximum = ENET_PEER_TIMEOUT_MAXIMUM;
    peer->lastRoundTripTime = ENET_PEER_DEFAULT_ROUND_TRIP_TIME;
    peer->lowestRoundTripTime = ENET_PEER_DEFAULT_ROUND_TRIP_TIME;
    peer->lastRoundTripTimeVariance = 0;
    peer->highestRoundTripTimeVariance = 0;
    peer->roundTripTime = ENET_PEER_DEFAULT_ROUND_TRIP_TIME;
    peer->roundTripTimeVariance = 0;
    peer->mtu = peer->host->mtu;
    peer->reliableDataInTransit = 0;
    peer->outgoingReliableSequenceNumber = 0;
    peer->windowSize = ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE;
    peer->incomingUnsequencedGroup = 0;
    peer->outgoingUnsequencedGroup = 0;
    peer->eventData = 0;
    peer->totalWaitingData = 0;
    peer->flags = 0;

    memset(peer->unsequencedWindow, 0, sizeof(peer->unsequencedWindow));

    enet_peer_reset_queues(peer);
}

void enet_peer_ping(ENetPeer *peer)
{
    ENetProtocol command;

    if (peer->state != ENET_PEER_STATE_CONNECTED)
    {
        return;
    }

    command.header.command = ENET_PROTOCOL_COMMAND_PING | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    command.header.channelID = 0xFF;

    enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);
}

void enet_peer_ping_interval(ENetPeer *peer, uint32_t pingInterval)
{
    peer->pingInterval = pingInterval ? pingInterval : ENET_PEER_PING_INTERVAL;
}

void enet_peer_timeout(ENetPeer *peer, uint32_t timeoutLimit, uint32_t timeoutMinimum, uint32_t timeoutMaximum)
{
    peer->timeoutLimit = timeoutLimit ? timeoutLimit : ENET_PEER_TIMEOUT_LIMIT;
    peer->timeoutMinimum = timeoutMinimum ? timeoutMinimum : ENET_PEER_TIMEOUT_MINIMUM;
    peer->timeoutMaximum = timeoutMaximum ? timeoutMaximum : ENET_PEER_TIMEOUT_MAXIMUM;
}

void enet_peer_disconnect_now(ENetPeer *peer, uint32_t data)
{
    ENetProtocol command;

    if (peer->state == ENET_PEER_STATE_DISCONNECTED)
    {
        return;
    }

    if (peer->state != ENET_PEER_STATE_ZOMBIE && peer->state != ENET_PEER_STATE_DISCONNECTING)
    {
        enet_peer_reset_queues(peer);

        command.header.command = ENET_PROTOCOL_COMMAND_DISCONNECT | ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
        command.header.channelID = 0xFF;
        command.disconnect.data = ENET_HOST_TO_NET_32(data);

        enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);

        enet_host_flush(peer->host);
    }

    enet_peer_reset(peer);
}

void enet_peer_disconnect(ENetPeer *peer, uint32_t data)
{
    ENetProtocol command;

    if (peer->state == ENET_PEER_STATE_DISCONNECTING || peer->state == ENET_PEER_STATE_DISCONNECTED ||
        peer->state == ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT || peer->state == ENET_PEER_STATE_ZOMBIE)
    {
        return;
    }

    enet_peer_reset_queues(peer);

    command.header.command = ENET_PROTOCOL_COMMAND_DISCONNECT;
    command.header.channelID = 0xFF;
    command.disconnect.data = ENET_HOST_TO_NET_32(data);

    if (peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER)
    {
        command.header.command |= ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    }
    else
    {
        command.header.command |= ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
    }

    enet_peer_queue_outgoing_command(peer, &command, NULL, 0, 0);

    if (peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER)
    {
        enet_peer_on_disconnect(peer);

        peer->state = ENET_PEER_STATE_DISCONNECTING;
    }
    else
    {
        enet_host_flush(peer->host);
        enet_peer_reset(peer);
    }
}

int enet_peer_has_outgoing_commands(ENetPeer *peer)
{
    if (ENet::list_empty(&peer->outgoingCommands) && ENet::list_empty(&peer->outgoingSendReliableCommands) &&
        ENet::list_empty(&peer->sentReliableCommands))
    {
        return 0;
    }

    return 1;
}

void enet_peer_disconnect_later(ENetPeer *peer, uint32_t data)
{
    if ((peer->state == ENET_PEER_STATE_CONNECTED || peer->state == ENET_PEER_STATE_DISCONNECT_LATER) &&
        enet_peer_has_outgoing_commands(peer))
    {
        peer->state = ENET_PEER_STATE_DISCONNECT_LATER;
        peer->eventData = data;
    }
    else
    {
        enet_peer_disconnect(peer, data);
    }
}

ENetAcknowledgement *enet_peer_queue_acknowledgement(ENetPeer *peer, const ENetProtocol *command, uint16_t sentTime)
{
    ENetAcknowledgement *acknowledgement;

    if (command->header.channelID < peer->channelCount)
    {
        ENetChannel *channel = &peer->channels[command->header.channelID];
        uint16_t reliableWindow = command->header.reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE,
                 currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

        if (command->header.reliableSequenceNumber < channel->incomingReliableSequenceNumber)
        {
            reliableWindow += ENET_PEER_RELIABLE_WINDOWS;
        }

        if (reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1 &&
            reliableWindow <= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS)
        {
            return NULL;
        }
    }

    acknowledgement = (ENetAcknowledgement *)ENet::enet_malloc(sizeof(ENetAcknowledgement));
    if (acknowledgement == NULL)
    {
        return NULL;
    }

    peer->outgoingDataTotal += sizeof(ENetProtocolAcknowledge);

    acknowledgement->sentTime = sentTime;
    acknowledgement->command = *command;

    ENet::list_insert(ENet::list_end(&peer->acknowledgements), acknowledgement);

    return acknowledgement;
}

void enet_peer_setup_outgoing_command(ENetPeer *peer, ENetOutgoingCommand *outgoingCommand)
{
    peer->outgoingDataTotal +=
        enet_protocol_command_size(outgoingCommand->command.header.command) + outgoingCommand->fragmentLength;

    if (outgoingCommand->command.header.channelID == 0xFF)
    {
        ++peer->outgoingReliableSequenceNumber;

        outgoingCommand->reliableSequenceNumber = peer->outgoingReliableSequenceNumber;
        outgoingCommand->unreliableSequenceNumber = 0;
    }
    else
    {
        ENetChannel *channel = &peer->channels[outgoingCommand->command.header.channelID];

        if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
        {
            ++channel->outgoingReliableSequenceNumber;
            channel->outgoingUnreliableSequenceNumber = 0;

            outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
            outgoingCommand->unreliableSequenceNumber = 0;
        }
        else if (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED)
        {
            ++peer->outgoingUnsequencedGroup;

            outgoingCommand->reliableSequenceNumber = 0;
            outgoingCommand->unreliableSequenceNumber = 0;
        }
        else
        {
            if (outgoingCommand->fragmentOffset == 0)
            {
                ++channel->outgoingUnreliableSequenceNumber;
            }

            outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
            outgoingCommand->unreliableSequenceNumber = channel->outgoingUnreliableSequenceNumber;
        }
    }

    outgoingCommand->sendAttempts = 0;
    outgoingCommand->sentTime = 0;
    outgoingCommand->roundTripTimeout = 0;
    outgoingCommand->command.header.reliableSequenceNumber =
        ENET_HOST_TO_NET_16(outgoingCommand->reliableSequenceNumber);
    outgoingCommand->queueTime = ++peer->host->totalQueued;

    switch (outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK)
    {
    case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
        outgoingCommand->command.sendUnreliable.unreliableSequenceNumber =
            ENET_HOST_TO_NET_16(outgoingCommand->unreliableSequenceNumber);
        break;

    case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        outgoingCommand->command.sendUnsequenced.unsequencedGroup = ENET_HOST_TO_NET_16(peer->outgoingUnsequencedGroup);
        break;

    default:
        break;
    }

    if ((outgoingCommand->command.header.command & ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) != 0 &&
        outgoingCommand->packet != NULL)
    {
        ENet::list_insert(ENet::list_end(&peer->outgoingSendReliableCommands), outgoingCommand);
    }
    else
    {
        ENet::list_insert(ENet::list_end(&peer->outgoingCommands), outgoingCommand);
    }
}

ENetOutgoingCommand *enet_peer_queue_outgoing_command(ENetPeer *peer, const ENetProtocol *command, ENetPacket *packet,
                                                      uint32_t offset, uint16_t length)
{
    ENetOutgoingCommand *outgoingCommand = (ENetOutgoingCommand *)ENet::enet_malloc(sizeof(ENetOutgoingCommand));
    if (outgoingCommand == NULL)
    {
        return NULL;
    }

    outgoingCommand->command = *command;
    outgoingCommand->fragmentOffset = offset;
    outgoingCommand->fragmentLength = length;
    outgoingCommand->packet = packet;
    if (packet != NULL)
    {
        ++packet->referenceCount;
    }

    enet_peer_setup_outgoing_command(peer, outgoingCommand);

    return outgoingCommand;
}

void enet_peer_dispatch_incoming_unreliable_commands(ENetPeer *peer, ENetChannel *channel,
                                                     ENetIncomingCommand *queuedCommand)
{
    ENet::ListIterator droppedCommand, startCommand, currentCommand;

    for (droppedCommand = startCommand = currentCommand = ENet::list_begin(&channel->incomingUnreliableCommands);
         currentCommand != ENet::list_end(&channel->incomingUnreliableCommands);
         currentCommand = ENet::list_next(currentCommand))
    {
        ENetIncomingCommand *incomingCommand = (ENetIncomingCommand *)currentCommand;

        if ((incomingCommand->command.header.command & ENET_PROTOCOL_COMMAND_MASK) ==
            ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
        {
            continue;
        }

        if (incomingCommand->reliableSequenceNumber == channel->incomingReliableSequenceNumber)
        {
            if (incomingCommand->fragmentsRemaining <= 0)
            {
                channel->incomingUnreliableSequenceNumber = incomingCommand->unreliableSequenceNumber;
                continue;
            }

            if (startCommand != currentCommand)
            {
                ENet::list_move(ENet::list_end(&peer->dispatchedCommands), startCommand,
                                ENet::list_previous(currentCommand));

                if (!(peer->flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
                {
                    ENet::list_insert(ENet::list_end(&peer->host->dispatchQueue), &peer->dispatchList);

                    peer->flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
                }

                droppedCommand = currentCommand;
            }
            else if (droppedCommand != currentCommand)
            {
                droppedCommand = ENet::list_previous(currentCommand);
            }
        }
        else
        {
            uint16_t reliableWindow = incomingCommand->reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE,
                     currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
            if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
            {
                reliableWindow += ENET_PEER_RELIABLE_WINDOWS;
            }
            if (reliableWindow >= currentWindow && reliableWindow < currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
            {
                break;
            }

            droppedCommand = ENet::list_next(currentCommand);

            if (startCommand != currentCommand)
            {
                ENet::list_move(ENet::list_end(&peer->dispatchedCommands), startCommand,
                                ENet::list_previous(currentCommand));

                if (!(peer->flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
                {
                    ENet::list_insert(ENet::list_end(&peer->host->dispatchQueue), &peer->dispatchList);

                    peer->flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
                }
            }
        }

        startCommand = ENet::list_next(currentCommand);
    }

    if (startCommand != currentCommand)
    {
        ENet::list_move(ENet::list_end(&peer->dispatchedCommands), startCommand, ENet::list_previous(currentCommand));

        if (!(peer->flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
        {
            ENet::list_insert(ENet::list_end(&peer->host->dispatchQueue), &peer->dispatchList);

            peer->flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
        }

        droppedCommand = currentCommand;
    }

    enet_peer_remove_incoming_commands(&channel->incomingUnreliableCommands,
                                       ENet::list_begin(&channel->incomingUnreliableCommands), droppedCommand,
                                       queuedCommand);
}

void enet_peer_dispatch_incoming_reliable_commands(ENetPeer *peer, ENetChannel *channel,
                                                   ENetIncomingCommand *queuedCommand)
{
    ENet::ListIterator currentCommand;

    for (currentCommand = ENet::list_begin(&channel->incomingReliableCommands);
         currentCommand != ENet::list_end(&channel->incomingReliableCommands);
         currentCommand = ENet::list_next(currentCommand))
    {
        ENetIncomingCommand *incomingCommand = (ENetIncomingCommand *)currentCommand;

        if (incomingCommand->fragmentsRemaining > 0 ||
            incomingCommand->reliableSequenceNumber != (uint16_t)(channel->incomingReliableSequenceNumber + 1))
        {
            break;
        }

        channel->incomingReliableSequenceNumber = incomingCommand->reliableSequenceNumber;

        if (incomingCommand->fragmentCount > 0)
        {
            channel->incomingReliableSequenceNumber += incomingCommand->fragmentCount - 1;
        }
    }

    if (currentCommand == ENet::list_begin(&channel->incomingReliableCommands))
    {
        return;
    }

    channel->incomingUnreliableSequenceNumber = 0;

    ENet::list_move(ENet::list_end(&peer->dispatchedCommands), ENet::list_begin(&channel->incomingReliableCommands),
                    ENet::list_previous(currentCommand));

    if (!(peer->flags & ENET_PEER_FLAG_NEEDS_DISPATCH))
    {
        ENet::list_insert(ENet::list_end(&peer->host->dispatchQueue), &peer->dispatchList);

        peer->flags |= ENET_PEER_FLAG_NEEDS_DISPATCH;
    }

    if (!ENet::list_empty(&channel->incomingUnreliableCommands))
    {
        enet_peer_dispatch_incoming_unreliable_commands(peer, channel, queuedCommand);
    }
}

ENetIncomingCommand *enet_peer_queue_incoming_command(ENetPeer *peer, const ENetProtocol *command, const void *data,
                                                      size_t dataLength, uint32_t flags, uint32_t fragmentCount)
{
    static ENetIncomingCommand dummyCommand;

    ENetChannel *channel = &peer->channels[command->header.channelID];
    uint32_t unreliableSequenceNumber = 0, reliableSequenceNumber = 0;
    uint16_t reliableWindow, currentWindow;
    ENetIncomingCommand *incomingCommand;
    ENet::ListIterator currentCommand;
    ENetPacket *packet = NULL;

    if (peer->state == ENET_PEER_STATE_DISCONNECT_LATER)
    {
        goto discardCommand;
    }

    if ((command->header.command & ENET_PROTOCOL_COMMAND_MASK) != ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
    {
        reliableSequenceNumber = command->header.reliableSequenceNumber;
        reliableWindow = reliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;
        currentWindow = channel->incomingReliableSequenceNumber / ENET_PEER_RELIABLE_WINDOW_SIZE;

        if (reliableSequenceNumber < channel->incomingReliableSequenceNumber)
        {
            reliableWindow += ENET_PEER_RELIABLE_WINDOWS;
        }

        if (reliableWindow < currentWindow || reliableWindow >= currentWindow + ENET_PEER_FREE_RELIABLE_WINDOWS - 1)
        {
            goto discardCommand;
        }
    }

    switch (command->header.command & ENET_PROTOCOL_COMMAND_MASK)
    {
    case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
    case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:
        if (reliableSequenceNumber == channel->incomingReliableSequenceNumber)
        {
            goto discardCommand;
        }

        for (currentCommand = ENet::list_previous(ENet::list_end(&channel->incomingReliableCommands));
             currentCommand != ENet::list_end(&channel->incomingReliableCommands);
             currentCommand = ENet::list_previous(currentCommand))
        {
            incomingCommand = (ENetIncomingCommand *)currentCommand;

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

            if (incomingCommand->reliableSequenceNumber <= reliableSequenceNumber)
            {
                if (incomingCommand->reliableSequenceNumber < reliableSequenceNumber)
                {
                    break;
                }

                goto discardCommand;
            }
        }
        break;

    case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE:
    case ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
        unreliableSequenceNumber = ENET_NET_TO_HOST_16(command->sendUnreliable.unreliableSequenceNumber);

        if (reliableSequenceNumber == channel->incomingReliableSequenceNumber &&
            unreliableSequenceNumber <= channel->incomingUnreliableSequenceNumber)
        {
            goto discardCommand;
        }

        for (currentCommand = ENet::list_previous(ENet::list_end(&channel->incomingUnreliableCommands));
             currentCommand != ENet::list_end(&channel->incomingUnreliableCommands);
             currentCommand = ENet::list_previous(currentCommand))
        {
            incomingCommand = (ENetIncomingCommand *)currentCommand;

            if ((command->header.command & ENET_PROTOCOL_COMMAND_MASK) == ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED)
            {
                continue;
            }

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

            if (incomingCommand->unreliableSequenceNumber <= unreliableSequenceNumber)
            {
                if (incomingCommand->unreliableSequenceNumber < unreliableSequenceNumber)
                {
                    break;
                }

                goto discardCommand;
            }
        }
        break;

    case ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        currentCommand = ENet::list_end(&channel->incomingUnreliableCommands);
        break;

    default:
        goto discardCommand;
    }

    if (peer->totalWaitingData >= peer->host->maximumWaitingData)
    {
        goto notifyError;
    }

    packet = enet_packet_create(data, dataLength, flags);
    if (packet == NULL)
    {
        goto notifyError;
    }

    incomingCommand = (ENetIncomingCommand *)ENet::enet_malloc(sizeof(ENetIncomingCommand));
    if (incomingCommand == NULL)
    {
        goto notifyError;
    }

    incomingCommand->reliableSequenceNumber = command->header.reliableSequenceNumber;
    incomingCommand->unreliableSequenceNumber = unreliableSequenceNumber & 0xFFFF;
    incomingCommand->command = *command;
    incomingCommand->fragmentCount = fragmentCount;
    incomingCommand->fragmentsRemaining = fragmentCount;
    incomingCommand->packet = packet;
    incomingCommand->fragments = NULL;

    if (fragmentCount > 0)
    {
        if (fragmentCount <= ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT)
        {
            incomingCommand->fragments = (uint32_t *)ENet::enet_malloc((fragmentCount + 31) / 32 * sizeof(uint32_t));
        }
        if (incomingCommand->fragments == NULL)
        {
            ENet::enet_free(incomingCommand);

            goto notifyError;
        }
        memset(incomingCommand->fragments, 0, (fragmentCount + 31) / 32 * sizeof(uint32_t));
    }

    if (packet != NULL)
    {
        ++packet->referenceCount;

        peer->totalWaitingData += packet->dataLength;
    }

    ENet::list_insert(ENet::list_next(currentCommand), incomingCommand);

    switch (command->header.command & ENET_PROTOCOL_COMMAND_MASK)
    {
    case ENET_PROTOCOL_COMMAND_SEND_FRAGMENT:
    case ENET_PROTOCOL_COMMAND_SEND_RELIABLE:
        enet_peer_dispatch_incoming_reliable_commands(peer, channel, incomingCommand);
        break;

    default:
        enet_peer_dispatch_incoming_unreliable_commands(peer, channel, incomingCommand);
        break;
    }

    return incomingCommand;

discardCommand:
    if (fragmentCount > 0)
    {
        goto notifyError;
    }

    if (packet != NULL && packet->referenceCount == 0)
    {
        enet_packet_destroy(packet);
    }

    return &dummyCommand;

notifyError:
    if (packet != NULL && packet->referenceCount == 0)
    {
        enet_packet_destroy(packet);
    }

    return NULL;
}

/** @} */
