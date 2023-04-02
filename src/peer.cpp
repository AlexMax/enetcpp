/**
 @file  peer.c
 @brief ENet peer management functions
*/
#include <cstring>
#include "enetcpp/enetcpp.h"

/** @defgroup peer ENet peer functions
    @{
*/

void ENet::peer_throttle_configure(ENet::Peer *peer, uint32_t interval, uint32_t acceleration, uint32_t deceleration)
{
    ENet::Protocol command;

    peer->packetThrottleInterval = interval;
    peer->packetThrottleAcceleration = acceleration;
    peer->packetThrottleDeceleration = deceleration;

    command.header.command = ENet::PROTOCOL_COMMAND_THROTTLE_CONFIGURE | ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    command.header.channelID = 0xFF;

    command.throttleConfigure.packetThrottleInterval = ENet::HOST_TO_NET_32(interval);
    command.throttleConfigure.packetThrottleAcceleration = ENet::HOST_TO_NET_32(acceleration);
    command.throttleConfigure.packetThrottleDeceleration = ENet::HOST_TO_NET_32(deceleration);

    ENet::peer_queue_outgoing_command(peer, &command, nullptr, 0, 0);
}

int ENet::peer_throttle(ENet::Peer *peer, uint32_t rtt)
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

int ENet::peer_send(ENet::Peer *peer, uint8_t channelID, ENet::Packet *packet)
{
    ENet::Channel *channel;
    ENet::Protocol command;
    size_t fragmentLength;

    if (peer->state != ENet::PEER_STATE_CONNECTED || channelID >= peer->channelCount ||
        packet->dataLength > peer->host->maximumPacketSize)
    {
        return -1;
    }

    channel = &peer->channels[channelID];
    fragmentLength = peer->mtu - sizeof(ENet::ProtocolHeader) - sizeof(ENet::ProtocolSendFragment);
    if (peer->host->checksum != nullptr)
    {
        fragmentLength -= sizeof(uint32_t);
    }

    if (packet->dataLength > fragmentLength)
    {
        uint32_t fragmentCount = (packet->dataLength + fragmentLength - 1) / fragmentLength, fragmentNumber,
                 fragmentOffset;
        uint8_t commandNumber;
        uint16_t startSequenceNumber;
        ENet::List<ENet::OutgoingCommand> fragments;
        ENet::OutgoingCommand *fragment;

        if (fragmentCount > ENet::PROTOCOL_MAXIMUM_FRAGMENT_COUNT)
        {
            return -1;
        }

        if ((packet->flags & (ENet::PACKET_FLAG_RELIABLE | ENet::PACKET_FLAG_UNRELIABLE_FRAGMENT)) ==
                ENet::PACKET_FLAG_UNRELIABLE_FRAGMENT &&
            channel->outgoingUnreliableSequenceNumber < 0xFFFF)
        {
            commandNumber = ENet::PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT;
            startSequenceNumber = ENet::HOST_TO_NET_16(channel->outgoingUnreliableSequenceNumber + 1);
        }
        else
        {
            commandNumber = ENet::PROTOCOL_COMMAND_SEND_FRAGMENT | ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
            startSequenceNumber = ENet::HOST_TO_NET_16(channel->outgoingReliableSequenceNumber + 1);
        }

        ENet::list_clear(&fragments);

        for (fragmentNumber = 0, fragmentOffset = 0; fragmentOffset < packet->dataLength;
             ++fragmentNumber, fragmentOffset += fragmentLength)
        {
            if (packet->dataLength - fragmentOffset < fragmentLength)
            {
                fragmentLength = packet->dataLength - fragmentOffset;
            }

            fragment = (ENet::OutgoingCommand *)ENet::enet_malloc(sizeof(ENet::OutgoingCommand));
            if (fragment == nullptr)
            {
                while (!ENet::list_empty(&fragments))
                {
                    fragment = ENet::list_remove(ENet::list_begin(&fragments));

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
            fragment->command.sendFragment.dataLength = ENet::HOST_TO_NET_16(fragmentLength);
            fragment->command.sendFragment.fragmentCount = ENet::HOST_TO_NET_32(fragmentCount);
            fragment->command.sendFragment.fragmentNumber = ENet::HOST_TO_NET_32(fragmentNumber);
            fragment->command.sendFragment.totalLength = ENet::HOST_TO_NET_32(packet->dataLength);
            fragment->command.sendFragment.fragmentOffset = ENet::NET_TO_HOST_32(fragmentOffset);

            ENet::list_insert(ENet::list_end(&fragments), fragment);
        }

        packet->referenceCount += fragmentNumber;

        while (!ENet::list_empty(&fragments))
        {
            fragment = ENet::list_remove(ENet::list_begin(&fragments));

            ENet::peer_setup_outgoing_command(peer, fragment);
        }

        return 0;
    }

    command.header.channelID = channelID;

    if ((packet->flags & (ENet::PACKET_FLAG_RELIABLE | ENet::PACKET_FLAG_UNSEQUENCED)) == ENet::PACKET_FLAG_UNSEQUENCED)
    {
        command.header.command = ENet::PROTOCOL_COMMAND_SEND_UNSEQUENCED | ENet::PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
        command.sendUnsequenced.dataLength = ENet::HOST_TO_NET_16(packet->dataLength);
    }
    else if (packet->flags & ENet::PACKET_FLAG_RELIABLE || channel->outgoingUnreliableSequenceNumber >= 0xFFFF)
    {
        command.header.command = ENet::PROTOCOL_COMMAND_SEND_RELIABLE | ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
        command.sendReliable.dataLength = ENet::HOST_TO_NET_16(packet->dataLength);
    }
    else
    {
        command.header.command = ENet::PROTOCOL_COMMAND_SEND_UNRELIABLE;
        command.sendUnreliable.dataLength = ENet::HOST_TO_NET_16(packet->dataLength);
    }

    if (ENet::peer_queue_outgoing_command(peer, &command, packet, 0, packet->dataLength) == nullptr)
    {
        return -1;
    }

    return 0;
}

ENet::Packet *ENet::peer_receive(ENet::Peer *peer, uint8_t *channelID)
{
    ENet::IncomingCommand *incomingCommand;
    ENet::Packet *packet;

    if (ENet::list_empty(&peer->dispatchedCommands))
    {
        return nullptr;
    }

    incomingCommand = ENet::list_remove(ENet::list_begin(&peer->dispatchedCommands));

    if (channelID != nullptr)
    {
        *channelID = incomingCommand->command.header.channelID;
    }

    packet = incomingCommand->packet;

    --packet->referenceCount;

    if (incomingCommand->fragments != nullptr)
    {
        ENet::enet_free(incomingCommand->fragments);
    }

    ENet::enet_free(incomingCommand);

    peer->totalWaitingData -= packet->dataLength;

    return packet;
}

static void enet_peer_reset_outgoing_commands(ENet::List<ENet::OutgoingCommand> *queue)
{
    ENet::OutgoingCommand *outgoingCommand;

    while (!ENet::list_empty(queue))
    {
        outgoingCommand = ENet::list_remove(ENet::list_begin(queue));

        if (outgoingCommand->packet != nullptr)
        {
            --outgoingCommand->packet->referenceCount;

            if (outgoingCommand->packet->referenceCount == 0)
            {
                ENet::packet_destroy(outgoingCommand->packet);
            }
        }

        ENet::enet_free(outgoingCommand);
    }
}

static void enet_peer_remove_incoming_commands(ENet::List<ENet::IncomingCommand> *queue,
                                               ENet::ListIterator<ENet::IncomingCommand> startCommand,
                                               ENet::ListIterator<ENet::IncomingCommand> endCommand,
                                               ENet::IncomingCommand *excludeCommand)
{
    (void)queue;

    ENet::ListIterator<ENet::IncomingCommand> currentCommand;

    for (currentCommand = startCommand; currentCommand != endCommand;)
    {
        ENet::IncomingCommand *incomingCommand = currentCommand;

        currentCommand = ENet::list_next(currentCommand);

        if (incomingCommand == excludeCommand)
        {
            continue;
        }

        ENet::list_remove(incomingCommand);

        if (incomingCommand->packet != nullptr)
        {
            --incomingCommand->packet->referenceCount;

            if (incomingCommand->packet->referenceCount == 0)
            {
                ENet::packet_destroy(incomingCommand->packet);
            }
        }

        if (incomingCommand->fragments != nullptr)
        {
            ENet::enet_free(incomingCommand->fragments);
        }

        ENet::enet_free(incomingCommand);
    }
}

static void enet_peer_reset_incoming_commands(ENet::List<ENet::IncomingCommand> *queue)
{
    enet_peer_remove_incoming_commands(queue, ENet::list_begin(queue), ENet::list_end(queue), nullptr);
}

void ENet::peer_reset_queues(ENet::Peer *peer)
{
    ENet::Channel *channel;

    if (peer->flags & ENet::PEER_FLAG_NEEDS_DISPATCH)
    {
        ENet::list_remove(peer);

        peer->flags &= ~ENet::PEER_FLAG_NEEDS_DISPATCH;
    }

    while (!ENet::list_empty(&peer->acknowledgements))
    {
        ENet::enet_free(ENet::list_remove(ENet::list_begin(&peer->acknowledgements)));
    }

    enet_peer_reset_outgoing_commands(&peer->sentReliableCommands);
    enet_peer_reset_outgoing_commands(&peer->outgoingCommands);
    enet_peer_reset_outgoing_commands(&peer->outgoingSendReliableCommands);
    enet_peer_reset_incoming_commands(&peer->dispatchedCommands);

    if (peer->channels != nullptr && peer->channelCount > 0)
    {
        for (channel = peer->channels; channel < &peer->channels[peer->channelCount]; ++channel)
        {
            enet_peer_reset_incoming_commands(&channel->incomingReliableCommands);
            enet_peer_reset_incoming_commands(&channel->incomingUnreliableCommands);
        }

        ENet::enet_free(peer->channels);
    }

    peer->channels = nullptr;
    peer->channelCount = 0;
}

void ENet::peer_on_connect(ENet::Peer *peer)
{
    if (peer->state != ENet::PEER_STATE_CONNECTED && peer->state != ENet::PEER_STATE_DISCONNECT_LATER)
    {
        if (peer->incomingBandwidth != 0)
        {
            ++peer->host->bandwidthLimitedPeers;
        }

        ++peer->host->connectedPeers;
    }
}

void ENet::peer_on_disconnect(ENet::Peer *peer)
{
    if (peer->state == ENet::PEER_STATE_CONNECTED || peer->state == ENet::PEER_STATE_DISCONNECT_LATER)
    {
        if (peer->incomingBandwidth != 0)
        {
            --peer->host->bandwidthLimitedPeers;
        }

        --peer->host->connectedPeers;
    }
}

void ENet::peer_reset(ENet::Peer *peer)
{
    ENet::peer_on_disconnect(peer);

    peer->outgoingPeerID = ENet::PROTOCOL_MAXIMUM_PEER_ID;
    peer->connectID = 0;

    peer->state = ENet::PEER_STATE_DISCONNECTED;

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
    peer->packetThrottle = ENet::PEER_DEFAULT_PACKET_THROTTLE;
    peer->packetThrottleLimit = ENet::PEER_PACKET_THROTTLE_SCALE;
    peer->packetThrottleCounter = 0;
    peer->packetThrottleEpoch = 0;
    peer->packetThrottleAcceleration = ENet::PEER_PACKET_THROTTLE_ACCELERATION;
    peer->packetThrottleDeceleration = ENet::PEER_PACKET_THROTTLE_DECELERATION;
    peer->packetThrottleInterval = ENet::PEER_PACKET_THROTTLE_INTERVAL;
    peer->pingInterval = ENet::PEER_PING_INTERVAL;
    peer->timeoutLimit = ENet::PEER_TIMEOUT_LIMIT;
    peer->timeoutMinimum = ENet::PEER_TIMEOUT_MINIMUM;
    peer->timeoutMaximum = ENet::PEER_TIMEOUT_MAXIMUM;
    peer->lastRoundTripTime = ENet::PEER_DEFAULT_ROUND_TRIP_TIME;
    peer->lowestRoundTripTime = ENet::PEER_DEFAULT_ROUND_TRIP_TIME;
    peer->lastRoundTripTimeVariance = 0;
    peer->highestRoundTripTimeVariance = 0;
    peer->roundTripTime = ENet::PEER_DEFAULT_ROUND_TRIP_TIME;
    peer->roundTripTimeVariance = 0;
    peer->mtu = peer->host->mtu;
    peer->reliableDataInTransit = 0;
    peer->outgoingReliableSequenceNumber = 0;
    peer->windowSize = ENet::PROTOCOL_MAXIMUM_WINDOW_SIZE;
    peer->incomingUnsequencedGroup = 0;
    peer->outgoingUnsequencedGroup = 0;
    peer->eventData = 0;
    peer->totalWaitingData = 0;
    peer->flags = 0;

    memset(peer->unsequencedWindow, 0, sizeof(peer->unsequencedWindow));

    ENet::peer_reset_queues(peer);
}

void ENet::peer_ping(ENet::Peer *peer)
{
    ENet::Protocol command;

    if (peer->state != ENet::PEER_STATE_CONNECTED)
    {
        return;
    }

    command.header.command = ENet::PROTOCOL_COMMAND_PING | ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    command.header.channelID = 0xFF;

    ENet::peer_queue_outgoing_command(peer, &command, nullptr, 0, 0);
}

void ENet::peer_ping_interval(ENet::Peer *peer, uint32_t pingInterval)
{
    peer->pingInterval = pingInterval ? pingInterval : ENet::PEER_PING_INTERVAL;
}

void ENet::peer_timeout(ENet::Peer *peer, uint32_t timeoutLimit, uint32_t timeoutMinimum, uint32_t timeoutMaximum)
{
    peer->timeoutLimit = timeoutLimit ? timeoutLimit : ENet::PEER_TIMEOUT_LIMIT;
    peer->timeoutMinimum = timeoutMinimum ? timeoutMinimum : ENet::PEER_TIMEOUT_MINIMUM;
    peer->timeoutMaximum = timeoutMaximum ? timeoutMaximum : ENet::PEER_TIMEOUT_MAXIMUM;
}

void ENet::peer_disconnect_now(ENet::Peer *peer, uint32_t data)
{
    ENet::Protocol command;

    if (peer->state == ENet::PEER_STATE_DISCONNECTED)
    {
        return;
    }

    if (peer->state != ENet::PEER_STATE_ZOMBIE && peer->state != ENet::PEER_STATE_DISCONNECTING)
    {
        ENet::peer_reset_queues(peer);

        command.header.command = ENet::PROTOCOL_COMMAND_DISCONNECT | ENet::PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
        command.header.channelID = 0xFF;
        command.disconnect.data = ENet::HOST_TO_NET_32(data);

        ENet::peer_queue_outgoing_command(peer, &command, nullptr, 0, 0);

        ENet::host_flush(peer->host);
    }

    ENet::peer_reset(peer);
}

void ENet::peer_disconnect(ENet::Peer *peer, uint32_t data)
{
    ENet::Protocol command;

    if (peer->state == ENet::PEER_STATE_DISCONNECTING || peer->state == ENet::PEER_STATE_DISCONNECTED ||
        peer->state == ENet::PEER_STATE_ACKNOWLEDGING_DISCONNECT || peer->state == ENet::PEER_STATE_ZOMBIE)
    {
        return;
    }

    ENet::peer_reset_queues(peer);

    command.header.command = ENet::PROTOCOL_COMMAND_DISCONNECT;
    command.header.channelID = 0xFF;
    command.disconnect.data = ENet::HOST_TO_NET_32(data);

    if (peer->state == ENet::PEER_STATE_CONNECTED || peer->state == ENet::PEER_STATE_DISCONNECT_LATER)
    {
        command.header.command |= ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE;
    }
    else
    {
        command.header.command |= ENet::PROTOCOL_COMMAND_FLAG_UNSEQUENCED;
    }

    ENet::peer_queue_outgoing_command(peer, &command, nullptr, 0, 0);

    if (peer->state == ENet::PEER_STATE_CONNECTED || peer->state == ENet::PEER_STATE_DISCONNECT_LATER)
    {
        ENet::peer_on_disconnect(peer);

        peer->state = ENet::PEER_STATE_DISCONNECTING;
    }
    else
    {
        ENet::host_flush(peer->host);
        ENet::peer_reset(peer);
    }
}

int ENet::peer_has_outgoing_commands(ENet::Peer *peer)
{
    if (ENet::list_empty(&peer->outgoingCommands) && ENet::list_empty(&peer->outgoingSendReliableCommands) &&
        ENet::list_empty(&peer->sentReliableCommands))
    {
        return 0;
    }

    return 1;
}

void ENet::peer_disconnect_later(ENet::Peer *peer, uint32_t data)
{
    if ((peer->state == ENet::PEER_STATE_CONNECTED || peer->state == ENet::PEER_STATE_DISCONNECT_LATER) &&
        ENet::peer_has_outgoing_commands(peer))
    {
        peer->state = ENet::PEER_STATE_DISCONNECT_LATER;
        peer->eventData = data;
    }
    else
    {
        ENet::peer_disconnect(peer, data);
    }
}

ENet::Acknowledgement *ENet::peer_queue_acknowledgement(ENet::Peer *peer, const ENet::Protocol *command,
                                                        uint16_t sentTime)
{
    ENet::Acknowledgement *acknowledgement;

    if (command->header.channelID < peer->channelCount)
    {
        ENet::Channel *channel = &peer->channels[command->header.channelID];
        uint16_t reliableWindow = command->header.reliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE,
                 currentWindow = channel->incomingReliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE;

        if (command->header.reliableSequenceNumber < channel->incomingReliableSequenceNumber)
        {
            reliableWindow += ENet::PEER_RELIABLE_WINDOWS;
        }

        if (reliableWindow >= currentWindow + ENet::PEER_FREE_RELIABLE_WINDOWS - 1 &&
            reliableWindow <= currentWindow + ENet::PEER_FREE_RELIABLE_WINDOWS)
        {
            return nullptr;
        }
    }

    acknowledgement = (ENet::Acknowledgement *)ENet::enet_malloc(sizeof(ENet::Acknowledgement));
    if (acknowledgement == nullptr)
    {
        return nullptr;
    }

    peer->outgoingDataTotal += sizeof(ENet::ProtocolAcknowledge);

    acknowledgement->sentTime = sentTime;
    acknowledgement->command = *command;

    ENet::list_insert(ENet::list_end(&peer->acknowledgements), acknowledgement);

    return acknowledgement;
}

void ENet::peer_setup_outgoing_command(ENet::Peer *peer, ENet::OutgoingCommand *outgoingCommand)
{
    peer->outgoingDataTotal +=
        ENet::protocol_command_size(outgoingCommand->command.header.command) + outgoingCommand->fragmentLength;

    if (outgoingCommand->command.header.channelID == 0xFF)
    {
        ++peer->outgoingReliableSequenceNumber;

        outgoingCommand->reliableSequenceNumber = peer->outgoingReliableSequenceNumber;
        outgoingCommand->unreliableSequenceNumber = 0;
    }
    else
    {
        ENet::Channel *channel = &peer->channels[outgoingCommand->command.header.channelID];

        if (outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
        {
            ++channel->outgoingReliableSequenceNumber;
            channel->outgoingUnreliableSequenceNumber = 0;

            outgoingCommand->reliableSequenceNumber = channel->outgoingReliableSequenceNumber;
            outgoingCommand->unreliableSequenceNumber = 0;
        }
        else if (outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_FLAG_UNSEQUENCED)
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
        ENet::HOST_TO_NET_16(outgoingCommand->reliableSequenceNumber);
    outgoingCommand->queueTime = ++peer->host->totalQueued;

    switch (outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_MASK)
    {
    case ENet::PROTOCOL_COMMAND_SEND_UNRELIABLE:
        outgoingCommand->command.sendUnreliable.unreliableSequenceNumber =
            ENet::HOST_TO_NET_16(outgoingCommand->unreliableSequenceNumber);
        break;

    case ENet::PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        outgoingCommand->command.sendUnsequenced.unsequencedGroup =
            ENet::HOST_TO_NET_16(peer->outgoingUnsequencedGroup);
        break;

    default:
        break;
    }

    if ((outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) != 0 &&
        outgoingCommand->packet != nullptr)
    {
        ENet::list_insert(ENet::list_end(&peer->outgoingSendReliableCommands), outgoingCommand);
    }
    else
    {
        ENet::list_insert(ENet::list_end(&peer->outgoingCommands), outgoingCommand);
    }
}

ENet::OutgoingCommand *ENet::peer_queue_outgoing_command(ENet::Peer *peer, const ENet::Protocol *command,
                                                         ENet::Packet *packet, uint32_t offset, uint16_t length)
{
    ENet::OutgoingCommand *outgoingCommand = (ENet::OutgoingCommand *)ENet::enet_malloc(sizeof(ENet::OutgoingCommand));
    if (outgoingCommand == nullptr)
    {
        return nullptr;
    }

    outgoingCommand->command = *command;
    outgoingCommand->fragmentOffset = offset;
    outgoingCommand->fragmentLength = length;
    outgoingCommand->packet = packet;
    if (packet != nullptr)
    {
        ++packet->referenceCount;
    }

    ENet::peer_setup_outgoing_command(peer, outgoingCommand);

    return outgoingCommand;
}

void ENet::peer_dispatch_incoming_unreliable_commands(ENet::Peer *peer, ENet::Channel *channel,
                                                      ENet::IncomingCommand *queuedCommand)
{
    ENet::ListIterator<ENet::IncomingCommand> droppedCommand, startCommand, currentCommand;

    for (droppedCommand = startCommand = currentCommand = ENet::list_begin(&channel->incomingUnreliableCommands);
         currentCommand != ENet::list_end(&channel->incomingUnreliableCommands);
         currentCommand = ENet::list_next(currentCommand))
    {
        ENet::IncomingCommand *incomingCommand = currentCommand;

        if ((incomingCommand->command.header.command & ENet::PROTOCOL_COMMAND_MASK) ==
            ENet::PROTOCOL_COMMAND_SEND_UNSEQUENCED)
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

                if (!(peer->flags & ENet::PEER_FLAG_NEEDS_DISPATCH))
                {
                    ENet::list_insert(ENet::list_end(&peer->host->dispatchQueue), peer);

                    peer->flags |= ENet::PEER_FLAG_NEEDS_DISPATCH;
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
            uint16_t reliableWindow = incomingCommand->reliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE,
                     currentWindow = channel->incomingReliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE;
            if (incomingCommand->reliableSequenceNumber < channel->incomingReliableSequenceNumber)
            {
                reliableWindow += ENet::PEER_RELIABLE_WINDOWS;
            }
            if (reliableWindow >= currentWindow &&
                reliableWindow < currentWindow + ENet::PEER_FREE_RELIABLE_WINDOWS - 1)
            {
                break;
            }

            droppedCommand = ENet::list_next(currentCommand);

            if (startCommand != currentCommand)
            {
                ENet::list_move(ENet::list_end(&peer->dispatchedCommands), startCommand,
                                ENet::list_previous(currentCommand));

                if (!(peer->flags & ENet::PEER_FLAG_NEEDS_DISPATCH))
                {
                    ENet::list_insert(ENet::list_end(&peer->host->dispatchQueue), peer);

                    peer->flags |= ENet::PEER_FLAG_NEEDS_DISPATCH;
                }
            }
        }

        startCommand = ENet::list_next(currentCommand);
    }

    if (startCommand != currentCommand)
    {
        ENet::list_move(ENet::list_end(&peer->dispatchedCommands), startCommand, ENet::list_previous(currentCommand));

        if (!(peer->flags & ENet::PEER_FLAG_NEEDS_DISPATCH))
        {
            ENet::list_insert(ENet::list_end(&peer->host->dispatchQueue), peer);

            peer->flags |= ENet::PEER_FLAG_NEEDS_DISPATCH;
        }

        droppedCommand = currentCommand;
    }

    enet_peer_remove_incoming_commands(&channel->incomingUnreliableCommands,
                                       ENet::list_begin(&channel->incomingUnreliableCommands), droppedCommand,
                                       queuedCommand);
}

void ENet::peer_dispatch_incoming_reliable_commands(ENet::Peer *peer, ENet::Channel *channel,
                                                    ENet::IncomingCommand *queuedCommand)
{
    ENet::ListIterator<ENet::IncomingCommand> currentCommand;

    for (currentCommand = ENet::list_begin(&channel->incomingReliableCommands);
         currentCommand != ENet::list_end(&channel->incomingReliableCommands);
         currentCommand = ENet::list_next(currentCommand))
    {
        ENet::IncomingCommand *incomingCommand = currentCommand;

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

    if (!(peer->flags & ENet::PEER_FLAG_NEEDS_DISPATCH))
    {
        ENet::list_insert(ENet::list_end(&peer->host->dispatchQueue), peer);

        peer->flags |= ENet::PEER_FLAG_NEEDS_DISPATCH;
    }

    if (!ENet::list_empty(&channel->incomingUnreliableCommands))
    {
        ENet::peer_dispatch_incoming_unreliable_commands(peer, channel, queuedCommand);
    }
}

ENet::IncomingCommand *ENet::peer_queue_incoming_command(ENet::Peer *peer, const ENet::Protocol *command,
                                                         const void *data, size_t dataLength, uint32_t flags,
                                                         uint32_t fragmentCount)
{
    static ENet::IncomingCommand dummyCommand;

    ENet::Channel *channel = &peer->channels[command->header.channelID];
    uint32_t unreliableSequenceNumber = 0, reliableSequenceNumber = 0;
    uint16_t reliableWindow, currentWindow;
    ENet::IncomingCommand *incomingCommand;
    ENet::ListIterator<IncomingCommand> currentCommand;
    ENet::Packet *packet = nullptr;

    if (peer->state == ENet::PEER_STATE_DISCONNECT_LATER)
    {
        goto discardCommand;
    }

    if ((command->header.command & ENet::PROTOCOL_COMMAND_MASK) != ENet::PROTOCOL_COMMAND_SEND_UNSEQUENCED)
    {
        reliableSequenceNumber = command->header.reliableSequenceNumber;
        reliableWindow = reliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE;
        currentWindow = channel->incomingReliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE;

        if (reliableSequenceNumber < channel->incomingReliableSequenceNumber)
        {
            reliableWindow += ENet::PEER_RELIABLE_WINDOWS;
        }

        if (reliableWindow < currentWindow || reliableWindow >= currentWindow + ENet::PEER_FREE_RELIABLE_WINDOWS - 1)
        {
            goto discardCommand;
        }
    }

    switch (command->header.command & ENet::PROTOCOL_COMMAND_MASK)
    {
    case ENet::PROTOCOL_COMMAND_SEND_FRAGMENT:
    case ENet::PROTOCOL_COMMAND_SEND_RELIABLE:
        if (reliableSequenceNumber == channel->incomingReliableSequenceNumber)
        {
            goto discardCommand;
        }

        for (currentCommand = ENet::list_previous(ENet::list_end(&channel->incomingReliableCommands));
             currentCommand != ENet::list_end(&channel->incomingReliableCommands);
             currentCommand = ENet::list_previous(currentCommand))
        {
            incomingCommand = currentCommand;

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

    case ENet::PROTOCOL_COMMAND_SEND_UNRELIABLE:
    case ENet::PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT:
        unreliableSequenceNumber = ENet::NET_TO_HOST_16(command->sendUnreliable.unreliableSequenceNumber);

        if (reliableSequenceNumber == channel->incomingReliableSequenceNumber &&
            unreliableSequenceNumber <= channel->incomingUnreliableSequenceNumber)
        {
            goto discardCommand;
        }

        for (currentCommand = ENet::list_previous(ENet::list_end(&channel->incomingUnreliableCommands));
             currentCommand != ENet::list_end(&channel->incomingUnreliableCommands);
             currentCommand = ENet::list_previous(currentCommand))
        {
            incomingCommand = currentCommand;

            if ((command->header.command & ENet::PROTOCOL_COMMAND_MASK) == ENet::PROTOCOL_COMMAND_SEND_UNSEQUENCED)
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

    case ENet::PROTOCOL_COMMAND_SEND_UNSEQUENCED:
        currentCommand = ENet::list_end(&channel->incomingUnreliableCommands);
        break;

    default:
        goto discardCommand;
    }

    if (peer->totalWaitingData >= peer->host->maximumWaitingData)
    {
        goto notifyError;
    }

    packet = ENet::packet_create(data, dataLength, flags);
    if (packet == nullptr)
    {
        goto notifyError;
    }

    incomingCommand = (ENet::IncomingCommand *)ENet::enet_malloc(sizeof(ENet::IncomingCommand));
    if (incomingCommand == nullptr)
    {
        goto notifyError;
    }

    incomingCommand->reliableSequenceNumber = command->header.reliableSequenceNumber;
    incomingCommand->unreliableSequenceNumber = unreliableSequenceNumber & 0xFFFF;
    incomingCommand->command = *command;
    incomingCommand->fragmentCount = fragmentCount;
    incomingCommand->fragmentsRemaining = fragmentCount;
    incomingCommand->packet = packet;
    incomingCommand->fragments = nullptr;

    if (fragmentCount > 0)
    {
        if (fragmentCount <= ENet::PROTOCOL_MAXIMUM_FRAGMENT_COUNT)
        {
            incomingCommand->fragments = (uint32_t *)ENet::enet_malloc((fragmentCount + 31) / 32 * sizeof(uint32_t));
        }
        if (incomingCommand->fragments == nullptr)
        {
            ENet::enet_free(incomingCommand);

            goto notifyError;
        }
        memset(incomingCommand->fragments, 0, (fragmentCount + 31) / 32 * sizeof(uint32_t));
    }

    if (packet != nullptr)
    {
        ++packet->referenceCount;

        peer->totalWaitingData += packet->dataLength;
    }

    ENet::list_insert(ENet::list_next(currentCommand), incomingCommand);

    switch (command->header.command & ENet::PROTOCOL_COMMAND_MASK)
    {
    case ENet::PROTOCOL_COMMAND_SEND_FRAGMENT:
    case ENet::PROTOCOL_COMMAND_SEND_RELIABLE:
        ENet::peer_dispatch_incoming_reliable_commands(peer, channel, incomingCommand);
        break;

    default:
        ENet::peer_dispatch_incoming_unreliable_commands(peer, channel, incomingCommand);
        break;
    }

    return incomingCommand;

discardCommand:
    if (fragmentCount > 0)
    {
        goto notifyError;
    }

    if (packet != nullptr && packet->referenceCount == 0)
    {
        ENet::packet_destroy(packet);
    }

    return &dummyCommand;

notifyError:
    if (packet != nullptr && packet->referenceCount == 0)
    {
        ENet::packet_destroy(packet);
    }

    return nullptr;
}

/** @} */
