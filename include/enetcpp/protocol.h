/**
 @file  protocol.h
 @brief ENet protocol
*/
#pragma once

enum
{
    ENET_PROTOCOL_MINIMUM_MTU = 576,
    ENET_PROTOCOL_MAXIMUM_MTU = 4096,
    ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS = 32,
    ENET_PROTOCOL_MINIMUM_WINDOW_SIZE = 4096,
    ENET_PROTOCOL_MAXIMUM_WINDOW_SIZE = 65536,
    ENET_PROTOCOL_MINIMUM_CHANNEL_COUNT = 1,
    ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT = 255,
    ENET_PROTOCOL_MAXIMUM_PEER_ID = 0xFFF,
    ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT = 1024 * 1024
};

enum ENetProtocolCommand
{
    ENET_PROTOCOL_COMMAND_NONE = 0,
    ENET_PROTOCOL_COMMAND_ACKNOWLEDGE = 1,
    ENET_PROTOCOL_COMMAND_CONNECT = 2,
    ENET_PROTOCOL_COMMAND_VERIFY_CONNECT = 3,
    ENET_PROTOCOL_COMMAND_DISCONNECT = 4,
    ENET_PROTOCOL_COMMAND_PING = 5,
    ENET_PROTOCOL_COMMAND_SEND_RELIABLE = 6,
    ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE = 7,
    ENET_PROTOCOL_COMMAND_SEND_FRAGMENT = 8,
    ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED = 9,
    ENET_PROTOCOL_COMMAND_BANDWIDTH_LIMIT = 10,
    ENET_PROTOCOL_COMMAND_THROTTLE_CONFIGURE = 11,
    ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT = 12,
    ENET_PROTOCOL_COMMAND_COUNT = 13,

    ENET_PROTOCOL_COMMAND_MASK = 0x0F
};

enum ENetProtocolFlag
{
    ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE = (1 << 7),
    ENET_PROTOCOL_COMMAND_FLAG_UNSEQUENCED = (1 << 6),

    ENET_PROTOCOL_HEADER_FLAG_COMPRESSED = (1 << 14),
    ENET_PROTOCOL_HEADER_FLAG_SENT_TIME = (1 << 15),
    ENET_PROTOCOL_HEADER_FLAG_MASK = ENET_PROTOCOL_HEADER_FLAG_COMPRESSED | ENET_PROTOCOL_HEADER_FLAG_SENT_TIME,

    ENET_PROTOCOL_HEADER_SESSION_MASK = (3 << 12),
    ENET_PROTOCOL_HEADER_SESSION_SHIFT = 12
};

#ifdef _MSC_VER
#pragma pack(push, 1)
#define ENET_PACKED
#elif defined(__GNUC__) || defined(__clang__)
#define ENET_PACKED __attribute__((packed))
#else
#define ENET_PACKED
#endif

struct ENetProtocolHeader
{
    uint16_t peerID;
    uint16_t sentTime;
} ENET_PACKED;

struct ENetProtocolCommandHeader
{
    uint8_t command;
    uint8_t channelID;
    uint16_t reliableSequenceNumber;
} ENET_PACKED;

struct ENetProtocolAcknowledge
{
    ENetProtocolCommandHeader header;
    uint16_t receivedReliableSequenceNumber;
    uint16_t receivedSentTime;
} ENET_PACKED;

struct ENetProtocolConnect
{
    ENetProtocolCommandHeader header;
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

struct ENetProtocolVerifyConnect
{
    ENetProtocolCommandHeader header;
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

struct ENetProtocolBandwidthLimit
{
    ENetProtocolCommandHeader header;
    uint32_t incomingBandwidth;
    uint32_t outgoingBandwidth;
} ENET_PACKED;

struct ENetProtocolThrottleConfigure
{
    ENetProtocolCommandHeader header;
    uint32_t packetThrottleInterval;
    uint32_t packetThrottleAcceleration;
    uint32_t packetThrottleDeceleration;
} ENET_PACKED;

struct ENetProtocolDisconnect
{
    ENetProtocolCommandHeader header;
    uint32_t data;
} ENET_PACKED;

struct ENetProtocolPing
{
    ENetProtocolCommandHeader header;
} ENET_PACKED;

struct ENetProtocolSendReliable
{
    ENetProtocolCommandHeader header;
    uint16_t dataLength;
} ENET_PACKED;

struct ENetProtocolSendUnreliable
{
    ENetProtocolCommandHeader header;
    uint16_t unreliableSequenceNumber;
    uint16_t dataLength;
} ENET_PACKED;

struct ENetProtocolSendUnsequenced
{
    ENetProtocolCommandHeader header;
    uint16_t unsequencedGroup;
    uint16_t dataLength;
} ENET_PACKED;

struct ENetProtocolSendFragment
{
    ENetProtocolCommandHeader header;
    uint16_t startSequenceNumber;
    uint16_t dataLength;
    uint32_t fragmentCount;
    uint32_t fragmentNumber;
    uint32_t totalLength;
    uint32_t fragmentOffset;
} ENET_PACKED;

union ENetProtocol {
    ENetProtocolCommandHeader header;
    ENetProtocolAcknowledge acknowledge;
    ENetProtocolConnect connect;
    ENetProtocolVerifyConnect verifyConnect;
    ENetProtocolDisconnect disconnect;
    ENetProtocolPing ping;
    ENetProtocolSendReliable sendReliable;
    ENetProtocolSendUnreliable sendUnreliable;
    ENetProtocolSendUnsequenced sendUnsequenced;
    ENetProtocolSendFragment sendFragment;
    ENetProtocolBandwidthLimit bandwidthLimit;
    ENetProtocolThrottleConfigure throttleConfigure;
} ENET_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif
