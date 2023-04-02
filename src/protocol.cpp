/**
 @file  protocol.c
 @brief ENet protocol functions
*/

#include <cstdio>
#include <cstring>
#include <cstdint>

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

static void enet_protocol_change_state(ENet::Host *host, ENet::Peer *peer, ENet::PeerState state)
{
    (void)host;

    if (state == ENet::PEER_STATE_CONNECTED || state == ENet::PEER_STATE_DISCONNECT_LATER)
    {
        ENet::peer_on_connect(peer);
    }
    else
    {
        ENet::peer_on_disconnect(peer);
    }

    peer->state = state;
}

static void enet_protocol_dispatch_state(ENet::Host *host, ENet::Peer *peer, ENet::PeerState state)
{
    enet_protocol_change_state(host, peer, state);

    if (!(peer->flags & ENet::PEER_FLAG_NEEDS_DISPATCH))
    {
        ENet::list_insert(ENet::list_end(&host->dispatchQueue), peer);

        peer->flags |= ENet::PEER_FLAG_NEEDS_DISPATCH;
    }
}

static int enet_protocol_dispatch_incoming_commands(ENet::Host *host, ENet::Event *event)
{
    while (!ENet::list_empty(&host->dispatchQueue))
    {
        ENet::Peer *peer = (ENet::Peer *)ENet::list_remove(ENet::list_begin(&host->dispatchQueue));

        peer->flags &= ~ENet::PEER_FLAG_NEEDS_DISPATCH;

        switch (peer->state)
        {
        case ENet::PEER_STATE_CONNECTION_PENDING:
        case ENet::PEER_STATE_CONNECTION_SUCCEEDED:
            enet_protocol_change_state(host, peer, ENet::PEER_STATE_CONNECTED);

            event->type = ENet::EVENT_TYPE_CONNECT;
            event->peer = peer;
            event->data = peer->eventData;

            return 1;

        case ENet::PEER_STATE_ZOMBIE:
            host->recalculateBandwidthLimits = 1;

            event->type = ENet::EVENT_TYPE_DISCONNECT;
            event->peer = peer;
            event->data = peer->eventData;

            ENet::peer_reset(peer);

            return 1;

        case ENet::PEER_STATE_CONNECTED:
            if (ENet::list_empty(&peer->dispatchedCommands))
            {
                continue;
            }

            event->packet = ENet::peer_receive(peer, &event->channelID);
            if (event->packet == nullptr)
            {
                continue;
            }

            event->type = ENet::EVENT_TYPE_RECEIVE;
            event->peer = peer;

            if (!ENet::list_empty(&peer->dispatchedCommands))
            {
                peer->flags |= ENet::PEER_FLAG_NEEDS_DISPATCH;

                ENet::list_insert(ENet::list_end(&host->dispatchQueue), peer);
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

    if (event != nullptr)
    {
        enet_protocol_change_state(host, peer, ENet::PEER_STATE_CONNECTED);

        event->type = ENet::EVENT_TYPE_CONNECT;
        event->peer = peer;
        event->data = peer->eventData;
    }
    else
    {
        enet_protocol_dispatch_state(host, peer,
                                     peer->state == ENet::PEER_STATE_CONNECTING ? ENet::PEER_STATE_CONNECTION_SUCCEEDED
                                                                                : ENet::PEER_STATE_CONNECTION_PENDING);
    }
}

static void enet_protocol_notify_disconnect(ENet::Host *host, ENet::Peer *peer, ENet::Event *event)
{
    if (peer->state >= ENet::PEER_STATE_CONNECTION_PENDING)
    {
        host->recalculateBandwidthLimits = 1;
    }

    if (peer->state != ENet::PEER_STATE_CONNECTING && peer->state < ENet::PEER_STATE_CONNECTION_SUCCEEDED)
    {
        ENet::peer_reset(peer);
    }
    else if (event != nullptr)
    {
        event->type = ENet::EVENT_TYPE_DISCONNECT;
        event->peer = peer;
        event->data = 0;

        ENet::peer_reset(peer);
    }
    else
    {
        peer->eventData = 0;

        enet_protocol_dispatch_state(host, peer, ENet::PEER_STATE_ZOMBIE);
    }
}

static void enet_protocol_remove_sent_unreliable_commands(ENet::Peer *peer,
                                                          ENet::List<ENet::OutgoingCommand> *sentUnreliableCommands)
{
    ENet::OutgoingCommand *outgoingCommand;

    if (ENet::list_empty(sentUnreliableCommands))
    {
        return;
    }

    do
    {
        outgoingCommand = ENet::list_front(sentUnreliableCommands);

        ENet::list_remove(outgoingCommand);

        if (outgoingCommand->packet != nullptr)
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

    if (peer->state == ENet::PEER_STATE_DISCONNECT_LATER && !ENet::peer_has_outgoing_commands(peer))
    {
        ENet::peer_disconnect(peer, peer->eventData);
    }
}

static ENet::OutgoingCommand *enet_protocol_find_sent_reliable_command(ENet::List<ENet::OutgoingCommand> *list,
                                                                       uint16_t reliableSequenceNumber,
                                                                       uint8_t channelID)
{
    ENet::ListIterator<ENet::OutgoingCommand> currentCommand;

    for (currentCommand = ENet::list_begin(list); currentCommand != ENet::list_end(list);
         currentCommand = ENet::list_next(currentCommand))
    {
        ENet::OutgoingCommand *outgoingCommand = currentCommand;

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

    return nullptr;
}

static ENet::ProtocolCommand enet_protocol_remove_sent_reliable_command(ENet::Peer *peer,
                                                                        uint16_t reliableSequenceNumber,
                                                                        uint8_t channelID)
{
    ENet::OutgoingCommand *outgoingCommand = nullptr;
    ENet::ListIterator<ENet::OutgoingCommand> currentCommand;
    ENet::ProtocolCommand commandNumber;
    int wasSent = 1;

    for (currentCommand = ENet::list_begin(&peer->sentReliableCommands);
         currentCommand != ENet::list_end(&peer->sentReliableCommands);
         currentCommand = ENet::list_next(currentCommand))
    {
        outgoingCommand = currentCommand;

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
        if (outgoingCommand == nullptr)
        {
            outgoingCommand = enet_protocol_find_sent_reliable_command(&peer->outgoingSendReliableCommands,
                                                                       reliableSequenceNumber, channelID);
        }

        wasSent = 0;
    }

    if (outgoingCommand == nullptr)
    {
        return ENet::PROTOCOL_COMMAND_NONE;
    }

    if (channelID < peer->channelCount)
    {
        ENet::Channel *channel = &peer->channels[channelID];
        uint16_t reliableWindow = reliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE;
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

    ENet::list_remove(outgoingCommand);

    if (outgoingCommand->packet != nullptr)
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

    outgoingCommand = ENet::list_front(&peer->sentReliableCommands);

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
    ENet::Peer *currentPeer, *peer = nullptr;
    ENet::Protocol verifyCommand;

    channelCount = ENet::NET_TO_HOST_32(command->connect.channelCount);

    if (channelCount < ENet::PROTOCOL_MINIMUM_CHANNEL_COUNT || channelCount > ENet::PROTOCOL_MAXIMUM_CHANNEL_COUNT)
    {
        return nullptr;
    }

    for (currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer)
    {
        if (currentPeer->state == ENet::PEER_STATE_DISCONNECTED)
        {
            if (peer == nullptr)
            {
                peer = currentPeer;
            }
        }
        else if (currentPeer->state != ENet::PEER_STATE_CONNECTING &&
                 currentPeer->address.host == host->receivedAddress.host)
        {
            if (currentPeer->address.port == host->receivedAddress.port &&
                currentPeer->connectID == command->connect.connectID)
            {
                return nullptr;
            }

            ++duplicatePeers;
        }
    }

    if (peer == nullptr || duplicatePeers >= host->duplicatePeers)
    {
        return nullptr;
    }

    if (channelCount > host->channelLimit)
    {
        channelCount = host->channelLimit;
    }
    peer->channels = (ENet::Channel *)ENet::enet_malloc(channelCount * sizeof(ENet::Channel));
    if (peer->channels == nullptr)
    {
        return nullptr;
    }
    peer->channelCount = channelCount;
    peer->state = ENet::PEER_STATE_ACKNOWLEDGING_CONNECT;
    peer->connectID = command->connect.connectID;
    peer->address = host->receivedAddress;
    peer->outgoingPeerID = ENet::NET_TO_HOST_16(command->connect.outgoingPeerID);
    peer->incomingBandwidth = ENet::NET_TO_HOST_32(command->connect.incomingBandwidth);
    peer->outgoingBandwidth = ENet::NET_TO_HOST_32(command->connect.outgoingBandwidth);
    peer->packetThrottleInterval = ENet::NET_TO_HOST_32(command->connect.packetThrottleInterval);
    peer->packetThrottleAcceleration = ENet::NET_TO_HOST_32(command->connect.packetThrottleAcceleration);
    peer->packetThrottleDeceleration = ENet::NET_TO_HOST_32(command->connect.packetThrottleDeceleration);
    peer->eventData = ENet::NET_TO_HOST_32(command->connect.data);

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

    mtu = ENet::NET_TO_HOST_32(command->connect.mtu);

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
        peer->windowSize =
            (ENet::MAX(host->outgoingBandwidth, peer->incomingBandwidth) / ENet::PEER_WINDOW_SIZE_SCALE) *
            ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }
    else
    {
        peer->windowSize =
            (ENet::MIN(host->outgoingBandwidth, peer->incomingBandwidth) / ENet::PEER_WINDOW_SIZE_SCALE) *
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
        windowSize = (host->incomingBandwidth / ENet::PEER_WINDOW_SIZE_SCALE) * ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }

    if (windowSize > ENet::NET_TO_HOST_32(command->connect.windowSize))
    {
        windowSize = ENet::NET_TO_HOST_32(command->connect.windowSize);
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
    verifyCommand.verifyConnect.outgoingPeerID = ENet::HOST_TO_NET_16(peer->incomingPeerID);
    verifyCommand.verifyConnect.incomingSessionID = incomingSessionID;
    verifyCommand.verifyConnect.outgoingSessionID = outgoingSessionID;
    verifyCommand.verifyConnect.mtu = ENet::HOST_TO_NET_32(peer->mtu);
    verifyCommand.verifyConnect.windowSize = ENet::HOST_TO_NET_32(windowSize);
    verifyCommand.verifyConnect.channelCount = ENet::HOST_TO_NET_32(channelCount);
    verifyCommand.verifyConnect.incomingBandwidth = ENet::HOST_TO_NET_32(host->incomingBandwidth);
    verifyCommand.verifyConnect.outgoingBandwidth = ENet::HOST_TO_NET_32(host->outgoingBandwidth);
    verifyCommand.verifyConnect.packetThrottleInterval = ENet::HOST_TO_NET_32(peer->packetThrottleInterval);
    verifyCommand.verifyConnect.packetThrottleAcceleration = ENet::HOST_TO_NET_32(peer->packetThrottleAcceleration);
    verifyCommand.verifyConnect.packetThrottleDeceleration = ENet::HOST_TO_NET_32(peer->packetThrottleDeceleration);
    verifyCommand.verifyConnect.connectID = peer->connectID;

    ENet::peer_queue_outgoing_command(peer, &verifyCommand, nullptr, 0, 0);

    return peer;
}

static int enet_protocol_handle_send_reliable(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command,
                                              uint8_t **currentData)
{
    size_t dataLength;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENet::PEER_STATE_CONNECTED && peer->state != ENet::PEER_STATE_DISCONNECT_LATER))
    {
        return -1;
    }

    dataLength = ENet::NET_TO_HOST_16(command->sendReliable.dataLength);
    *currentData += dataLength;
    if (dataLength > host->maximumPacketSize || *currentData < host->receivedData ||
        *currentData > &host->receivedData[host->receivedDataLength])
    {
        return -1;
    }

    if (ENet::peer_queue_incoming_command(peer, command, (const uint8_t *)command + sizeof(ENet::ProtocolSendReliable),
                                          dataLength, ENet::PACKET_FLAG_RELIABLE, 0) == nullptr)
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
        (peer->state != ENet::PEER_STATE_CONNECTED && peer->state != ENet::PEER_STATE_DISCONNECT_LATER))
    {
        return -1;
    }

    dataLength = ENet::NET_TO_HOST_16(command->sendUnsequenced.dataLength);
    *currentData += dataLength;
    if (dataLength > host->maximumPacketSize || *currentData < host->receivedData ||
        *currentData > &host->receivedData[host->receivedDataLength])
    {
        return -1;
    }

    unsequencedGroup = ENet::NET_TO_HOST_16(command->sendUnsequenced.unsequencedGroup);
    index = unsequencedGroup % ENet::PEER_UNSEQUENCED_WINDOW_SIZE;

    if (unsequencedGroup < peer->incomingUnsequencedGroup)
    {
        unsequencedGroup += 0x10000;
    }

    if (unsequencedGroup >= (uint32_t)peer->incomingUnsequencedGroup +
                                ENet::PEER_FREE_UNSEQUENCED_WINDOWS * ENet::PEER_UNSEQUENCED_WINDOW_SIZE)
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
                                          ENet::PACKET_FLAG_UNSEQUENCED, 0) == nullptr)
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
        (peer->state != ENet::PEER_STATE_CONNECTED && peer->state != ENet::PEER_STATE_DISCONNECT_LATER))
    {
        return -1;
    }

    dataLength = ENet::NET_TO_HOST_16(command->sendUnreliable.dataLength);
    *currentData += dataLength;
    if (dataLength > host->maximumPacketSize || *currentData < host->receivedData ||
        *currentData > &host->receivedData[host->receivedDataLength])
    {
        return -1;
    }

    if (ENet::peer_queue_incoming_command(peer, command,
                                          (const uint8_t *)command + sizeof(ENet::ProtocolSendUnreliable), dataLength,
                                          0, 0) == nullptr)
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
    ENet::ListIterator<ENet::IncomingCommand> currentCommand;
    ENet::IncomingCommand *startCommand = nullptr;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENet::PEER_STATE_CONNECTED && peer->state != ENet::PEER_STATE_DISCONNECT_LATER))
    {
        return -1;
    }

    fragmentLength = ENet::NET_TO_HOST_16(command->sendFragment.dataLength);
    *currentData += fragmentLength;
    if (fragmentLength <= 0 || fragmentLength > host->maximumPacketSize || *currentData < host->receivedData ||
        *currentData > &host->receivedData[host->receivedDataLength])
    {
        return -1;
    }

    channel = &peer->channels[command->header.channelID];
    startSequenceNumber = ENet::NET_TO_HOST_16(command->sendFragment.startSequenceNumber);
    startWindow = startSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE;
    currentWindow = channel->incomingReliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE;

    if (startSequenceNumber < channel->incomingReliableSequenceNumber)
    {
        startWindow += ENet::PEER_RELIABLE_WINDOWS;
    }

    if (startWindow < currentWindow || startWindow >= currentWindow + ENet::PEER_FREE_RELIABLE_WINDOWS - 1)
    {
        return 0;
    }

    fragmentNumber = ENet::NET_TO_HOST_32(command->sendFragment.fragmentNumber);
    fragmentCount = ENet::NET_TO_HOST_32(command->sendFragment.fragmentCount);
    fragmentOffset = ENet::NET_TO_HOST_32(command->sendFragment.fragmentOffset);
    totalLength = ENet::NET_TO_HOST_32(command->sendFragment.totalLength);

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
        ENet::IncomingCommand *incomingCommand = currentCommand;

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

    if (startCommand == nullptr)
    {
        ENet::Protocol hostCommand = *command;

        hostCommand.header.reliableSequenceNumber = startSequenceNumber;

        startCommand = ENet::peer_queue_incoming_command(peer, &hostCommand, nullptr, totalLength,
                                                         ENet::PACKET_FLAG_RELIABLE, fragmentCount);
        if (startCommand == nullptr)
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
            ENet::peer_dispatch_incoming_reliable_commands(peer, channel, nullptr);
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
    ENet::ListIterator<ENet::IncomingCommand> currentCommand;
    ENet::IncomingCommand *startCommand = nullptr;

    if (command->header.channelID >= peer->channelCount ||
        (peer->state != ENet::PEER_STATE_CONNECTED && peer->state != ENet::PEER_STATE_DISCONNECT_LATER))
    {
        return -1;
    }

    fragmentLength = ENet::NET_TO_HOST_16(command->sendFragment.dataLength);
    *currentData += fragmentLength;
    if (fragmentLength > host->maximumPacketSize || *currentData < host->receivedData ||
        *currentData > &host->receivedData[host->receivedDataLength])
    {
        return -1;
    }

    channel = &peer->channels[command->header.channelID];
    reliableSequenceNumber = command->header.reliableSequenceNumber;
    startSequenceNumber = ENet::NET_TO_HOST_16(command->sendFragment.startSequenceNumber);

    reliableWindow = reliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE;
    currentWindow = channel->incomingReliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE;

    if (reliableSequenceNumber < channel->incomingReliableSequenceNumber)
    {
        reliableWindow += ENet::PEER_RELIABLE_WINDOWS;
    }

    if (reliableWindow < currentWindow || reliableWindow >= currentWindow + ENet::PEER_FREE_RELIABLE_WINDOWS - 1)
    {
        return 0;
    }

    if (reliableSequenceNumber == channel->incomingReliableSequenceNumber &&
        startSequenceNumber <= channel->incomingUnreliableSequenceNumber)
    {
        return 0;
    }

    fragmentNumber = ENet::NET_TO_HOST_32(command->sendFragment.fragmentNumber);
    fragmentCount = ENet::NET_TO_HOST_32(command->sendFragment.fragmentCount);
    fragmentOffset = ENet::NET_TO_HOST_32(command->sendFragment.fragmentOffset);
    totalLength = ENet::NET_TO_HOST_32(command->sendFragment.totalLength);

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
        ENet::IncomingCommand *incomingCommand = currentCommand;

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

    if (startCommand == nullptr)
    {
        startCommand = ENet::peer_queue_incoming_command(peer, command, nullptr, totalLength,
                                                         ENet::PACKET_FLAG_UNRELIABLE_FRAGMENT, fragmentCount);
        if (startCommand == nullptr)
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
            ENet::peer_dispatch_incoming_unreliable_commands(peer, channel, nullptr);
        }
    }

    return 0;
}

static int enet_protocol_handle_ping(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command)
{
    (void)host;
    (void)command;

    if (peer->state != ENet::PEER_STATE_CONNECTED && peer->state != ENet::PEER_STATE_DISCONNECT_LATER)
    {
        return -1;
    }

    return 0;
}

static int enet_protocol_handle_bandwidth_limit(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command)
{
    if (peer->state != ENet::PEER_STATE_CONNECTED && peer->state != ENet::PEER_STATE_DISCONNECT_LATER)
    {
        return -1;
    }

    if (peer->incomingBandwidth != 0)
    {
        --host->bandwidthLimitedPeers;
    }

    peer->incomingBandwidth = ENet::NET_TO_HOST_32(command->bandwidthLimit.incomingBandwidth);
    peer->outgoingBandwidth = ENet::NET_TO_HOST_32(command->bandwidthLimit.outgoingBandwidth);

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
        peer->windowSize =
            (ENet::MAX(peer->incomingBandwidth, host->outgoingBandwidth) / ENet::PEER_WINDOW_SIZE_SCALE) *
            ENet::PROTOCOL_MINIMUM_WINDOW_SIZE;
    }
    else
    {
        peer->windowSize =
            (ENet::MIN(peer->incomingBandwidth, host->outgoingBandwidth) / ENet::PEER_WINDOW_SIZE_SCALE) *
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

    if (peer->state != ENet::PEER_STATE_CONNECTED && peer->state != ENet::PEER_STATE_DISCONNECT_LATER)
    {
        return -1;
    }

    peer->packetThrottleInterval = ENet::NET_TO_HOST_32(command->throttleConfigure.packetThrottleInterval);
    peer->packetThrottleAcceleration = ENet::NET_TO_HOST_32(command->throttleConfigure.packetThrottleAcceleration);
    peer->packetThrottleDeceleration = ENet::NET_TO_HOST_32(command->throttleConfigure.packetThrottleDeceleration);

    return 0;
}

static int enet_protocol_handle_disconnect(ENet::Host *host, ENet::Peer *peer, const ENet::Protocol *command)
{
    if (peer->state == ENet::PEER_STATE_DISCONNECTED || peer->state == ENet::PEER_STATE_ZOMBIE ||
        peer->state == ENet::PEER_STATE_ACKNOWLEDGING_DISCONNECT)
    {
        return 0;
    }

    ENet::peer_reset_queues(peer);

    if (peer->state == ENet::PEER_STATE_CONNECTION_SUCCEEDED || peer->state == ENet::PEER_STATE_DISCONNECTING ||
        peer->state == ENet::PEER_STATE_CONNECTING)
    {
        enet_protocol_dispatch_state(host, peer, ENet::PEER_STATE_ZOMBIE);
    }
    else if (peer->state != ENet::PEER_STATE_CONNECTED && peer->state != ENet::PEER_STATE_DISCONNECT_LATER)
    {
        if (peer->state == ENet::PEER_STATE_CONNECTION_PENDING)
        {
            host->recalculateBandwidthLimits = 1;
        }

        ENet::peer_reset(peer);
    }
    else if (command->header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
    {
        enet_protocol_change_state(host, peer, ENet::PEER_STATE_ACKNOWLEDGING_DISCONNECT);
    }
    else
    {
        enet_protocol_dispatch_state(host, peer, ENet::PEER_STATE_ZOMBIE);
    }

    if (peer->state != ENet::PEER_STATE_DISCONNECTED)
    {
        peer->eventData = ENet::NET_TO_HOST_32(command->disconnect.data);
    }

    return 0;
}

static int enet_protocol_handle_acknowledge(ENet::Host *host, ENet::Event *event, ENet::Peer *peer,
                                            const ENet::Protocol *command)
{
    uint32_t roundTripTime, receivedSentTime, receivedReliableSequenceNumber;
    ENet::ProtocolCommand commandNumber;

    if (peer->state == ENet::PEER_STATE_DISCONNECTED || peer->state == ENet::PEER_STATE_ZOMBIE)
    {
        return 0;
    }

    receivedSentTime = ENet::NET_TO_HOST_16(command->acknowledge.receivedSentTime);
    receivedSentTime |= host->serviceTime & 0xFFFF0000;
    if ((receivedSentTime & 0x8000) > (host->serviceTime & 0x8000))
    {
        receivedSentTime -= 0x10000;
    }

    if (ENet::TIME_LESS(host->serviceTime, receivedSentTime))
    {
        return 0;
    }

    roundTripTime = ENet::TIME_DIFFERENCE(host->serviceTime, receivedSentTime);
    roundTripTime = ENet::MAX(roundTripTime, 1u);

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
        ENet::TIME_DIFFERENCE(host->serviceTime, peer->packetThrottleEpoch) >= peer->packetThrottleInterval)
    {
        peer->lastRoundTripTime = peer->lowestRoundTripTime;
        peer->lastRoundTripTimeVariance = ENet::MAX(peer->highestRoundTripTimeVariance, 1u);
        peer->lowestRoundTripTime = peer->roundTripTime;
        peer->highestRoundTripTimeVariance = peer->roundTripTimeVariance;
        peer->packetThrottleEpoch = host->serviceTime;
    }

    peer->lastReceiveTime = ENet::MAX(host->serviceTime, 1u);
    peer->earliestTimeout = 0;

    receivedReliableSequenceNumber = ENet::NET_TO_HOST_16(command->acknowledge.receivedReliableSequenceNumber);

    commandNumber =
        enet_protocol_remove_sent_reliable_command(peer, receivedReliableSequenceNumber, command->header.channelID);

    switch (peer->state)
    {
    case ENet::PEER_STATE_ACKNOWLEDGING_CONNECT:
        if (commandNumber != ENet::PROTOCOL_COMMAND_VERIFY_CONNECT)
        {
            return -1;
        }

        enet_protocol_notify_connect(host, peer, event);
        break;

    case ENet::PEER_STATE_DISCONNECTING:
        if (commandNumber != ENet::PROTOCOL_COMMAND_DISCONNECT)
        {
            return -1;
        }

        enet_protocol_notify_disconnect(host, peer, event);
        break;

    case ENet::PEER_STATE_DISCONNECT_LATER:
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

    if (peer->state != ENet::PEER_STATE_CONNECTING)
    {
        return 0;
    }

    channelCount = ENet::NET_TO_HOST_32(command->verifyConnect.channelCount);

    if (channelCount < ENet::PROTOCOL_MINIMUM_CHANNEL_COUNT || channelCount > ENet::PROTOCOL_MAXIMUM_CHANNEL_COUNT ||
        ENet::NET_TO_HOST_32(command->verifyConnect.packetThrottleInterval) != peer->packetThrottleInterval ||
        ENet::NET_TO_HOST_32(command->verifyConnect.packetThrottleAcceleration) != peer->packetThrottleAcceleration ||
        ENet::NET_TO_HOST_32(command->verifyConnect.packetThrottleDeceleration) != peer->packetThrottleDeceleration ||
        command->verifyConnect.connectID != peer->connectID)
    {
        peer->eventData = 0;

        enet_protocol_dispatch_state(host, peer, ENet::PEER_STATE_ZOMBIE);

        return -1;
    }

    enet_protocol_remove_sent_reliable_command(peer, 1, 0xFF);

    if (channelCount < peer->channelCount)
    {
        peer->channelCount = channelCount;
    }

    peer->outgoingPeerID = ENet::NET_TO_HOST_16(command->verifyConnect.outgoingPeerID);
    peer->incomingSessionID = command->verifyConnect.incomingSessionID;
    peer->outgoingSessionID = command->verifyConnect.outgoingSessionID;

    mtu = ENet::NET_TO_HOST_32(command->verifyConnect.mtu);

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

    windowSize = ENet::NET_TO_HOST_32(command->verifyConnect.windowSize);

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

    peer->incomingBandwidth = ENet::NET_TO_HOST_32(command->verifyConnect.incomingBandwidth);
    peer->outgoingBandwidth = ENet::NET_TO_HOST_32(command->verifyConnect.outgoingBandwidth);

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

    if (host->receivedDataLength < (size_t) & ((ENet::ProtocolHeader *)nullptr)->sentTime)
    {
        return 0;
    }

    header = (ENet::ProtocolHeader *)host->receivedData;

    peerID = ENet::NET_TO_HOST_16(header->peerID);
    sessionID = (peerID & ENet::PROTOCOL_HEADER_SESSION_MASK) >> ENet::PROTOCOL_HEADER_SESSION_SHIFT;
    flags = peerID & ENet::PROTOCOL_HEADER_FLAG_MASK;
    peerID &= ~(ENet::PROTOCOL_HEADER_FLAG_MASK | ENet::PROTOCOL_HEADER_SESSION_MASK);

    headerSize =
        (flags & ENet::PROTOCOL_HEADER_FLAG_SENT_TIME ? sizeof(ENet::ProtocolHeader)
                                                      : (size_t) & ((ENet::ProtocolHeader *)nullptr)->sentTime);
    if (host->checksum != nullptr)
    {
        headerSize += sizeof(uint32_t);
    }

    if (peerID == ENet::PROTOCOL_MAXIMUM_PEER_ID)
    {
        peer = nullptr;
    }
    else if (peerID >= host->peerCount)
    {
        return 0;
    }
    else
    {
        peer = &host->peers[peerID];

        if (peer->state == ENet::PEER_STATE_DISCONNECTED || peer->state == ENet::PEER_STATE_ZOMBIE ||
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
        if (host->compressor.context == nullptr || host->compressor.decompress == nullptr)
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

    if (host->checksum != nullptr)
    {
        uint32_t *checksum = (uint32_t *)&host->receivedData[headerSize - sizeof(uint32_t)],
                 desiredChecksum = *checksum;
        ENet::Buffer buffer;

        *checksum = peer != nullptr ? peer->connectID : 0;

        buffer.data = host->receivedData;
        buffer.dataLength = host->receivedDataLength;

        if (host->checksum(&buffer, 1) != desiredChecksum)
        {
            return 0;
        }
    }

    if (peer != nullptr)
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

        if (peer == nullptr && commandNumber != ENet::PROTOCOL_COMMAND_CONNECT)
        {
            break;
        }

        command->header.reliableSequenceNumber = ENet::NET_TO_HOST_16(command->header.reliableSequenceNumber);

        switch (commandNumber)
        {
        case ENet::PROTOCOL_COMMAND_ACKNOWLEDGE:
            if (enet_protocol_handle_acknowledge(host, event, peer, command))
            {
                goto commandError;
            }
            break;

        case ENet::PROTOCOL_COMMAND_CONNECT:
            if (peer != nullptr)
            {
                goto commandError;
            }
            peer = enet_protocol_handle_connect(host, header, command);
            if (peer == nullptr)
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

        if (peer != nullptr && (command->header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE) != 0)
        {
            uint16_t sentTime;

            if (!(flags & ENet::PROTOCOL_HEADER_FLAG_SENT_TIME))
            {
                break;
            }

            sentTime = ENet::NET_TO_HOST_16(header->sentTime);

            switch (peer->state)
            {
            case ENet::PEER_STATE_DISCONNECTING:
            case ENet::PEER_STATE_ACKNOWLEDGING_CONNECT:
            case ENet::PEER_STATE_DISCONNECTED:
            case ENet::PEER_STATE_ZOMBIE:
                break;

            case ENet::PEER_STATE_ACKNOWLEDGING_DISCONNECT:
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
    if (event != nullptr && event->type != ENet::EVENT_TYPE_NONE)
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
        ENet::Buffer buffer;

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

        if (host->intercept != nullptr)
        {
            switch (host->intercept(host, event))
            {
            case 1:
                if (event != nullptr && event->type != ENet::EVENT_TYPE_NONE)
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
    ENet::Buffer *buffer = &host->buffers[host->bufferCount];
    ENet::Acknowledgement *acknowledgement;
    ENet::ListIterator<ENet::Acknowledgement> currentAcknowledgement;
    uint16_t reliableSequenceNumber;

    currentAcknowledgement = ENet::list_begin(&peer->acknowledgements);

    while (currentAcknowledgement != ENet::list_end(&peer->acknowledgements))
    {
        if (command >= &host->commands[sizeof(host->commands) / sizeof(ENet::Protocol)] ||
            buffer >= &host->buffers[sizeof(host->buffers) / sizeof(ENet::Buffer)] ||
            peer->mtu - host->packetSize < sizeof(ENet::ProtocolAcknowledge))
        {
            peer->flags |= ENet::PEER_FLAG_CONTINUE_SENDING;

            break;
        }

        acknowledgement = currentAcknowledgement;

        currentAcknowledgement = ENet::list_next(currentAcknowledgement);

        buffer->data = command;
        buffer->dataLength = sizeof(ENet::ProtocolAcknowledge);

        host->packetSize += buffer->dataLength;

        reliableSequenceNumber = ENet::HOST_TO_NET_16(acknowledgement->command.header.reliableSequenceNumber);

        command->header.command = ENet::PROTOCOL_COMMAND_ACKNOWLEDGE;
        command->header.channelID = acknowledgement->command.header.channelID;
        command->header.reliableSequenceNumber = reliableSequenceNumber;
        command->acknowledge.receivedReliableSequenceNumber = reliableSequenceNumber;
        command->acknowledge.receivedSentTime = ENet::HOST_TO_NET_16(acknowledgement->sentTime);

        if ((acknowledgement->command.header.command & ENet::PROTOCOL_COMMAND_MASK) ==
            ENet::PROTOCOL_COMMAND_DISCONNECT)
        {
            enet_protocol_dispatch_state(host, peer, ENet::PEER_STATE_ZOMBIE);
        }

        ENet::list_remove(acknowledgement);
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
    ENet::ListIterator<ENet::OutgoingCommand> currentCommand, insertPosition, insertSendReliablePosition;

    currentCommand = ENet::list_begin(&peer->sentReliableCommands);
    insertPosition = ENet::list_begin(&peer->outgoingCommands);
    insertSendReliablePosition = ENet::list_begin(&peer->outgoingSendReliableCommands);

    while (currentCommand != ENet::list_end(&peer->sentReliableCommands))
    {
        outgoingCommand = currentCommand;

        currentCommand = ENet::list_next(currentCommand);

        if (ENet::TIME_DIFFERENCE(host->serviceTime, outgoingCommand->sentTime) < outgoingCommand->roundTripTimeout)
        {
            continue;
        }

        if (peer->earliestTimeout == 0 || ENet::TIME_LESS(outgoingCommand->sentTime, peer->earliestTimeout))
        {
            peer->earliestTimeout = outgoingCommand->sentTime;
        }

        if (peer->earliestTimeout != 0 &&
            (ENet::TIME_DIFFERENCE(host->serviceTime, peer->earliestTimeout) >= peer->timeoutMaximum ||
             ((1 << (outgoingCommand->sendAttempts - 1)) >= peer->timeoutLimit &&
              ENet::TIME_DIFFERENCE(host->serviceTime, peer->earliestTimeout) >= peer->timeoutMinimum)))
        {
            enet_protocol_notify_disconnect(host, peer, event);

            return 1;
        }

        ++peer->packetsLost;

        outgoingCommand->roundTripTimeout *= 2;

        if (outgoingCommand->packet != nullptr)
        {
            peer->reliableDataInTransit -= outgoingCommand->fragmentLength;

            ENet::list_insert(insertSendReliablePosition, ENet::list_remove(outgoingCommand));
        }
        else
        {
            ENet::list_insert(insertPosition, ENet::list_remove(outgoingCommand));
        }

        if (currentCommand == ENet::list_begin(&peer->sentReliableCommands) &&
            !ENet::list_empty(&peer->sentReliableCommands))
        {
            outgoingCommand = currentCommand;

            peer->nextTimeout = outgoingCommand->sentTime + outgoingCommand->roundTripTimeout;
        }
    }

    return 0;
}

static int enet_protocol_check_outgoing_commands(ENet::Host *host, ENet::Peer *peer,
                                                 ENet::List<ENet::OutgoingCommand> *sentUnreliableCommands)
{
    ENet::Protocol *command = &host->commands[host->commandCount];
    ENet::Buffer *buffer = &host->buffers[host->bufferCount];
    ENet::OutgoingCommand *outgoingCommand;
    ENet::ListIterator<ENet::OutgoingCommand> currentCommand, currentSendReliableCommand;
    ENet::Channel *channel = nullptr;
    uint16_t reliableWindow = 0;
    size_t commandSize;
    int windowWrap = 0, canPing = 1;

    currentCommand = ENet::list_begin(&peer->outgoingCommands);
    currentSendReliableCommand = ENet::list_begin(&peer->outgoingSendReliableCommands);

    for (;;)
    {
        if (currentCommand != ENet::list_end(&peer->outgoingCommands))
        {
            outgoingCommand = currentCommand;

            if (currentSendReliableCommand != ENet::list_end(&peer->outgoingSendReliableCommands) &&
                ENet::TIME_LESS(((ENet::OutgoingCommand *)currentSendReliableCommand)->queueTime,
                                outgoingCommand->queueTime))
            {
                goto useSendReliableCommand;
            }

            currentCommand = ENet::list_next(currentCommand);
        }
        else if (currentSendReliableCommand != ENet::list_end(&peer->outgoingSendReliableCommands))
        {
        useSendReliableCommand:
            outgoingCommand = currentSendReliableCommand;
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
                          : nullptr;
            reliableWindow = outgoingCommand->reliableSequenceNumber / ENet::PEER_RELIABLE_WINDOW_SIZE;
            if (channel != nullptr)
            {
                if (windowWrap)
                {
                    continue;
                }
                else if (outgoingCommand->sendAttempts < 1 &&
                         !(outgoingCommand->reliableSequenceNumber % ENet::PEER_RELIABLE_WINDOW_SIZE) &&
                         (channel->reliableWindows[(reliableWindow + ENet::PEER_RELIABLE_WINDOWS - 1) %
                                                   ENet::PEER_RELIABLE_WINDOWS] >= ENet::PEER_RELIABLE_WINDOW_SIZE ||
                          channel->usedReliableWindows &
                              ((((1 << (ENet::PEER_FREE_RELIABLE_WINDOWS + 2)) - 1) << reliableWindow) |
                               (((1 << (ENet::PEER_FREE_RELIABLE_WINDOWS + 2)) - 1) >>
                                (ENet::PEER_RELIABLE_WINDOWS - reliableWindow)))))
                {
                    windowWrap = 1;
                    currentSendReliableCommand = ENet::list_end(&peer->outgoingSendReliableCommands);

                    continue;
                }
            }

            if (outgoingCommand->packet != nullptr)
            {
                uint32_t windowSize = (peer->packetThrottle * peer->windowSize) / ENet::PEER_PACKET_THROTTLE_SCALE;

                if (peer->reliableDataInTransit + outgoingCommand->fragmentLength > ENet::MAX(windowSize, peer->mtu))
                {
                    currentSendReliableCommand = ENet::list_end(&peer->outgoingSendReliableCommands);

                    continue;
                }
            }

            canPing = 0;
        }

        commandSize = commandSizes[outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_MASK];
        if (command >= &host->commands[sizeof(host->commands) / sizeof(ENet::Protocol)] ||
            buffer + 1 >= &host->buffers[sizeof(host->buffers) / sizeof(ENet::Buffer)] ||
            peer->mtu - host->packetSize < commandSize ||
            (outgoingCommand->packet != nullptr &&
             (uint16_t)(peer->mtu - host->packetSize) < (uint16_t)(commandSize + outgoingCommand->fragmentLength)))
        {
            peer->flags |= ENet::PEER_FLAG_CONTINUE_SENDING;

            break;
        }

        if (outgoingCommand->command.header.command & ENet::PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
        {
            if (channel != nullptr && outgoingCommand->sendAttempts < 1)
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

            ENet::list_insert(ENet::list_end(&peer->sentReliableCommands), ENet::list_remove(outgoingCommand));

            outgoingCommand->sentTime = host->serviceTime;

            host->headerFlags |= ENet::PROTOCOL_HEADER_FLAG_SENT_TIME;

            peer->reliableDataInTransit += outgoingCommand->fragmentLength;
        }
        else
        {
            if (outgoingCommand->packet != nullptr && outgoingCommand->fragmentOffset == 0)
            {
                peer->packetThrottleCounter += ENet::PEER_PACKET_THROTTLE_COUNTER;
                peer->packetThrottleCounter %= ENet::PEER_PACKET_THROTTLE_SCALE;

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

                        ENet::list_remove(outgoingCommand);
                        ENet::enet_free(outgoingCommand);

                        if (currentCommand == ENet::list_end(&peer->outgoingCommands))
                        {
                            break;
                        }

                        outgoingCommand = currentCommand;
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

            ENet::list_remove(outgoingCommand);

            if (outgoingCommand->packet != nullptr)
            {
                ENet::list_insert(ENet::list_end(sentUnreliableCommands), outgoingCommand);
            }
        }

        buffer->data = command;
        buffer->dataLength = commandSize;

        host->packetSize += buffer->dataLength;

        *command = outgoingCommand->command;

        if (outgoingCommand->packet != nullptr)
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

    if (peer->state == ENet::PEER_STATE_DISCONNECT_LATER && !ENet::peer_has_outgoing_commands(peer) &&
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
    ENet::List<ENet::OutgoingCommand> sentUnreliableCommands;

    ENet::list_clear(&sentUnreliableCommands);

    for (int sendPass = 0, continueSending = 0; sendPass <= continueSending; ++sendPass)
    {
        for (ENet::Peer *currentPeer = host->peers; currentPeer < &host->peers[host->peerCount]; ++currentPeer)
        {
            if (currentPeer->state == ENet::PEER_STATE_DISCONNECTED || currentPeer->state == ENet::PEER_STATE_ZOMBIE ||
                (sendPass > 0 && !(currentPeer->flags & ENet::PEER_FLAG_CONTINUE_SENDING)))
            {
                continue;
            }

            currentPeer->flags &= ~ENet::PEER_FLAG_CONTINUE_SENDING;

            host->headerFlags = 0;
            host->commandCount = 0;
            host->bufferCount = 1;
            host->packetSize = sizeof(ENet::ProtocolHeader);

            if (!ENet::list_empty(&currentPeer->acknowledgements))
            {
                enet_protocol_send_acknowledgements(host, currentPeer);
            }

            if (checkForTimeouts != 0 && !ENet::list_empty(&currentPeer->sentReliableCommands) &&
                ENet::TIME_GREATER_EQUAL(host->serviceTime, currentPeer->nextTimeout) &&
                enet_protocol_check_timeouts(host, currentPeer, event) == 1)
            {
                if (event != nullptr && event->type != ENet::EVENT_TYPE_NONE)
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
                ENet::TIME_DIFFERENCE(host->serviceTime, currentPeer->lastReceiveTime) >= currentPeer->pingInterval &&
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
            else if (ENet::TIME_DIFFERENCE(host->serviceTime, currentPeer->packetLossEpoch) >=
                         ENet::PEER_PACKET_LOSS_INTERVAL &&
                     currentPeer->packetsSent > 0)
            {
                uint32_t packetLoss =
                    currentPeer->packetsLost * ENet::PEER_PACKET_LOSS_SCALE / currentPeer->packetsSent;

#ifdef ENET_DEBUG
                printf("peer %u: %f%%+-%f%% packet loss, %u+-%u ms round trip time, %f%% throttle, %u outgoing, %u/%u "
                       "incoming\n",
                       currentPeer->incomingPeerID, currentPeer->packetLoss / (float)ENet::PEER_PACKET_LOSS_SCALE,
                       currentPeer->packetLossVariance / (float)ENet::PEER_PACKET_LOSS_SCALE,
                       currentPeer->roundTripTime, currentPeer->roundTripTimeVariance,
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
                    (currentPeer->packetLossVariance * 3 + ENet::DISTANCE(packetLoss, currentPeer->packetLoss)) / 4;
                currentPeer->packetLoss = (currentPeer->packetLoss * 7 + packetLoss) / 8;

                currentPeer->packetLossEpoch = host->serviceTime;
                currentPeer->packetsSent = 0;
                currentPeer->packetsLost = 0;
            }

            host->buffers->data = headerData;
            if (host->headerFlags & ENet::PROTOCOL_HEADER_FLAG_SENT_TIME)
            {
                header->sentTime = ENet::HOST_TO_NET_16(host->serviceTime & 0xFFFF);

                host->buffers->dataLength = sizeof(ENet::ProtocolHeader);
            }
            else
            {
                host->buffers->dataLength = (size_t) & ((ENet::ProtocolHeader *)nullptr)->sentTime;
            }

            shouldCompress = 0;
            if (host->compressor.context != nullptr && host->compressor.compress != nullptr)
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
            header->peerID = ENet::HOST_TO_NET_16(currentPeer->outgoingPeerID | host->headerFlags);
            if (host->checksum != nullptr)
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
            if (currentPeer->flags & ENet::PEER_FLAG_CONTINUE_SENDING)
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

    enet_protocol_send_outgoing_commands(host, nullptr, 0);
}

int ENet::host_check_events(ENet::Host *host, ENet::Event *event)
{
    if (event == nullptr)
    {
        return -1;
    }

    event->type = ENet::EVENT_TYPE_NONE;
    event->peer = nullptr;
    event->packet = nullptr;

    return enet_protocol_dispatch_incoming_commands(host, event);
}

int ENet::host_service(ENet::Host *host, ENet::Event *event, uint32_t timeout)
{
    uint32_t waitCondition;

    if (event != nullptr)
    {
        event->type = ENet::EVENT_TYPE_NONE;
        event->peer = nullptr;
        event->packet = nullptr;

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
        if (ENet::TIME_DIFFERENCE(host->serviceTime, host->bandwidthThrottleEpoch) >=
            ENet::HOST_BANDWIDTH_THROTTLE_INTERVAL)
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

        if (event != nullptr)
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

        if (ENet::TIME_GREATER_EQUAL(host->serviceTime, timeout))
        {
            return 0;
        }

        do
        {
            host->serviceTime = ENet::time_get();

            if (ENet::TIME_GREATER_EQUAL(host->serviceTime, timeout))
            {
                return 0;
            }

            waitCondition = ENet::SOCKET_WAIT_RECEIVE | ENet::SOCKET_WAIT_INTERRUPT;

            if (ENet::socket_wait(host->socket, &waitCondition, ENet::TIME_DIFFERENCE(timeout, host->serviceTime)) != 0)
            {
                return -1;
            }
        } while (waitCondition & ENet::SOCKET_WAIT_INTERRUPT);

        host->serviceTime = ENet::time_get();
    } while (waitCondition & ENet::SOCKET_WAIT_RECEIVE);

    return 0;
}
