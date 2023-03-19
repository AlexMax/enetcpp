/**
 @file  protocol.h
 @brief ENet protocol
*/
#pragma once

namespace ENet
{

enum
{
    PROTOCOL_MINIMUM_MTU = 576,
    PROTOCOL_MAXIMUM_MTU = 4096,
    PROTOCOL_MAXIMUM_PACKET_COMMANDS = 32,
    PROTOCOL_MINIMUM_WINDOW_SIZE = 4096,
    PROTOCOL_MAXIMUM_WINDOW_SIZE = 65536,
    PROTOCOL_MINIMUM_CHANNEL_COUNT = 1,
    PROTOCOL_MAXIMUM_CHANNEL_COUNT = 255,
    PROTOCOL_MAXIMUM_PEER_ID = 0xFFF,
    PROTOCOL_MAXIMUM_FRAGMENT_COUNT = 1024 * 1024
};

enum ProtocolCommand
{
    PROTOCOL_COMMAND_NONE = 0,
    PROTOCOL_COMMAND_ACKNOWLEDGE = 1,
    PROTOCOL_COMMAND_CONNECT = 2,
    PROTOCOL_COMMAND_VERIFY_CONNECT = 3,
    PROTOCOL_COMMAND_DISCONNECT = 4,
    PROTOCOL_COMMAND_PING = 5,
    PROTOCOL_COMMAND_SEND_RELIABLE = 6,
    PROTOCOL_COMMAND_SEND_UNRELIABLE = 7,
    PROTOCOL_COMMAND_SEND_FRAGMENT = 8,
    PROTOCOL_COMMAND_SEND_UNSEQUENCED = 9,
    PROTOCOL_COMMAND_BANDWIDTH_LIMIT = 10,
    PROTOCOL_COMMAND_THROTTLE_CONFIGURE = 11,
    PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT = 12,
    PROTOCOL_COMMAND_COUNT = 13,

    PROTOCOL_COMMAND_MASK = 0x0F
};

enum ProtocolFlag
{
    PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE = (1 << 7),
    PROTOCOL_COMMAND_FLAG_UNSEQUENCED = (1 << 6),

    PROTOCOL_HEADER_FLAG_COMPRESSED = (1 << 14),
    PROTOCOL_HEADER_FLAG_SENT_TIME = (1 << 15),
    PROTOCOL_HEADER_FLAG_MASK = PROTOCOL_HEADER_FLAG_COMPRESSED | PROTOCOL_HEADER_FLAG_SENT_TIME,

    PROTOCOL_HEADER_SESSION_MASK = (3 << 12),
    PROTOCOL_HEADER_SESSION_SHIFT = 12
};

#ifdef _MSC_VER
#pragma pack(push, 1)
#define ENET_PACKED
#elif defined(__GNUC__) || defined(__clang__)
#define ENET_PACKED __attribute__((packed))
#else
#define ENET_PACKED
#endif

struct ProtocolHeader
{
    uint16_t peerID;
    uint16_t sentTime;
} ENET_PACKED;

struct ProtocolCommandHeader
{
    uint8_t command;
    uint8_t channelID;
    uint16_t reliableSequenceNumber;
} ENET_PACKED;

struct ProtocolAcknowledge
{
    ProtocolCommandHeader header;
    uint16_t receivedReliableSequenceNumber;
    uint16_t receivedSentTime;
} ENET_PACKED;

struct ProtocolConnect
{
    ProtocolCommandHeader header;
    uint16_t outgoingPeerID;
    uint8_t incomingSessionID;
    uint8_t outgoingSessionID;
    uint32_t mtu;
    uint32_t windowSize;
    uint32_t channelCount;
    uint32_t incomingBandwidth;
    uint32_t outgoingBandwidth;
    uint32_t packetThrottleInterval;
    uint32_t packetThrottleAcceleration;
    uint32_t packetThrottleDeceleration;
    uint32_t connectID;
    uint32_t data;
} ENET_PACKED;

struct ProtocolVerifyConnect
{
    ProtocolCommandHeader header;
    uint16_t outgoingPeerID;
    uint8_t incomingSessionID;
    uint8_t outgoingSessionID;
    uint32_t mtu;
    uint32_t windowSize;
    uint32_t channelCount;
    uint32_t incomingBandwidth;
    uint32_t outgoingBandwidth;
    uint32_t packetThrottleInterval;
    uint32_t packetThrottleAcceleration;
    uint32_t packetThrottleDeceleration;
    uint32_t connectID;
} ENET_PACKED;

struct ProtocolBandwidthLimit
{
    ProtocolCommandHeader header;
    uint32_t incomingBandwidth;
    uint32_t outgoingBandwidth;
} ENET_PACKED;

struct ProtocolThrottleConfigure
{
    ProtocolCommandHeader header;
    uint32_t packetThrottleInterval;
    uint32_t packetThrottleAcceleration;
    uint32_t packetThrottleDeceleration;
} ENET_PACKED;

struct ProtocolDisconnect
{
    ProtocolCommandHeader header;
    uint32_t data;
} ENET_PACKED;

struct ProtocolPing
{
    ProtocolCommandHeader header;
} ENET_PACKED;

struct ProtocolSendReliable
{
    ProtocolCommandHeader header;
    uint16_t dataLength;
} ENET_PACKED;

struct ProtocolSendUnreliable
{
    ProtocolCommandHeader header;
    uint16_t unreliableSequenceNumber;
    uint16_t dataLength;
} ENET_PACKED;

struct ProtocolSendUnsequenced
{
    ProtocolCommandHeader header;
    uint16_t unsequencedGroup;
    uint16_t dataLength;
} ENET_PACKED;

struct ProtocolSendFragment
{
    ProtocolCommandHeader header;
    uint16_t startSequenceNumber;
    uint16_t dataLength;
    uint32_t fragmentCount;
    uint32_t fragmentNumber;
    uint32_t totalLength;
    uint32_t fragmentOffset;
} ENET_PACKED;

union Protocol {
    ProtocolCommandHeader header;
    ProtocolAcknowledge acknowledge;
    ProtocolConnect connect;
    ProtocolVerifyConnect verifyConnect;
    ProtocolDisconnect disconnect;
    ProtocolPing ping;
    ProtocolSendReliable sendReliable;
    ProtocolSendUnreliable sendUnreliable;
    ProtocolSendUnsequenced sendUnsequenced;
    ProtocolSendFragment sendFragment;
    ProtocolBandwidthLimit bandwidthLimit;
    ProtocolThrottleConfigure throttleConfigure;
} ENET_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif

} // namespace ENet
