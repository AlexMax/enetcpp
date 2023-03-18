/**
 @file  enet.h
 @brief ENet public header file
*/
#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#include "enetcpp/win32.h"
#else
#include "enetcpp/unix.h"
#endif

#include "enetcpp/protocol.h"
#include "enetcpp/list.h"
#include "enetcpp/callbacks.h"

#define ENET_VERSION_MAJOR 1
#define ENET_VERSION_MINOR 3
#define ENET_VERSION_PATCH 17
#define ENET_VERSION_CREATE(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ENET_VERSION_GET_MAJOR(version) (((version) >> 16) & 0xFF)
#define ENET_VERSION_GET_MINOR(version) (((version) >> 8) & 0xFF)
#define ENET_VERSION_GET_PATCH(version) ((version)&0xFF)
#define ENET_VERSION ENET_VERSION_CREATE(ENET_VERSION_MAJOR, ENET_VERSION_MINOR, ENET_VERSION_PATCH)

using ENetVersion = uint32_t;

struct ENetHost;
struct ENetEvent;
struct ENetPacket;

enum ENetSocketType
{
    ENET_SOCKET_TYPE_STREAM = 1,
    ENET_SOCKET_TYPE_DATAGRAM = 2
};

enum ENetSocketWait
{
    ENET_SOCKET_WAIT_NONE = 0,
    ENET_SOCKET_WAIT_SEND = (1 << 0),
    ENET_SOCKET_WAIT_RECEIVE = (1 << 1),
    ENET_SOCKET_WAIT_INTERRUPT = (1 << 2)
};

enum ENetSocketOption
{
    ENET_SOCKOPT_NONBLOCK = 1,
    ENET_SOCKOPT_BROADCAST = 2,
    ENET_SOCKOPT_RCVBUF = 3,
    ENET_SOCKOPT_SNDBUF = 4,
    ENET_SOCKOPT_REUSEADDR = 5,
    ENET_SOCKOPT_RCVTIMEO = 6,
    ENET_SOCKOPT_SNDTIMEO = 7,
    ENET_SOCKOPT_ERROR = 8,
    ENET_SOCKOPT_NODELAY = 9,
    ENET_SOCKOPT_TTL = 10
};

enum ENetSocketShutdown
{
    ENET_SOCKET_SHUTDOWN_READ = 0,
    ENET_SOCKET_SHUTDOWN_WRITE = 1,
    ENET_SOCKET_SHUTDOWN_READ_WRITE = 2
};

#define ENET_HOST_ANY 0
#define ENET_HOST_BROADCAST 0xFFFFFFFFU
#define ENET_PORT_ANY 0

/**
 * Portable internet address structure.
 *
 * The host must be specified in network byte-order, and the port must be in host
 * byte-order. The constant ENET_HOST_ANY may be used to specify the default
 * server host. The constant ENET_HOST_BROADCAST may be used to specify the
 * broadcast address (255.255.255.255).  This makes sense for enet_host_connect,
 * but not for enet_host_create.  Once a server responds to a broadcast, the
 * address is updated from ENET_HOST_BROADCAST to the server's actual IP address.
 */
struct ENetAddress
{
    uint32_t host;
    uint16_t port;
};

/**
 * Packet flag bit constants.
 *
 * The host must be specified in network byte-order, and the port must be in
 * host byte-order. The constant ENET_HOST_ANY may be used to specify the
 * default server host.

   @sa ENetPacket
*/
enum ENetPacketFlag
{
    /** packet must be received by the target peer and resend attempts should be
     * made until the packet is delivered */
    ENET_PACKET_FLAG_RELIABLE = (1 << 0),
    /** packet will not be sequenced with other packets
     * not supported for reliable packets
     */
    ENET_PACKET_FLAG_UNSEQUENCED = (1 << 1),
    /** packet will not allocate data, and user must supply it instead */
    ENET_PACKET_FLAG_NO_ALLOCATE = (1 << 2),
    /** packet will be fragmented using unreliable (instead of reliable) sends
     * if it exceeds the MTU */
    ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT = (1 << 3),

    /** whether the packet has been sent from all queues it has been entered into */
    ENET_PACKET_FLAG_SENT = (1 << 8)
};

using ENetPacketFreeCallback = void(ENET_CALLBACK *)(ENetPacket *);

/**
 * ENet packet structure.
 *
 * An ENet data packet that may be sent to or received from a peer. The shown
 * fields should only be read and never modified. The data field contains the
 * allocated data for the packet. The dataLength fields specifies the length
 * of the allocated data.  The flags field is either 0 (specifying no flags),
 * or a bitwise-or of any combination of the following flags:
 *
 *    ENET_PACKET_FLAG_RELIABLE - packet must be received by the target peer
 *    and resend attempts should be made until the packet is delivered
 *
 *    ENET_PACKET_FLAG_UNSEQUENCED - packet will not be sequenced with other packets
 *    (not supported for reliable packets)
 *
 *    ENET_PACKET_FLAG_NO_ALLOCATE - packet will not allocate data, and user must supply it instead
 *
 *    ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT - packet will be fragmented using unreliable
 *    (instead of reliable) sends if it exceeds the MTU
 *
 *    ENET_PACKET_FLAG_SENT - whether the packet has been sent from all queues it has been entered into
   @sa ENetPacketFlag
 */
struct ENetPacket
{
    size_t referenceCount;               /**< internal use only */
    uint32_t flags;                      /**< bitwise-or of ENetPacketFlag constants */
    uint8_t *data;                       /**< allocated data for packet */
    size_t dataLength;                   /**< length of data */
    ENetPacketFreeCallback freeCallback; /**< function to be called when the packet is no longer in use */
    void *userData;                      /**< application private data, may be freely modified */
};

struct ENetAcknowledgement
{
    ENetListNode acknowledgementList;
    uint32_t sentTime;
    ENetProtocol command;
};

struct ENetOutgoingCommand
{
    ENetListNode outgoingCommandList;
    uint16_t reliableSequenceNumber;
    uint16_t unreliableSequenceNumber;
    uint32_t sentTime;
    uint32_t roundTripTimeout;
    uint32_t queueTime;
    uint32_t fragmentOffset;
    uint16_t fragmentLength;
    uint16_t sendAttempts;
    ENetProtocol command;
    ENetPacket *packet;
};

struct ENetIncomingCommand
{
    ENetListNode incomingCommandList;
    uint16_t reliableSequenceNumber;
    uint16_t unreliableSequenceNumber;
    ENetProtocol command;
    uint32_t fragmentCount;
    uint32_t fragmentsRemaining;
    uint32_t *fragments;
    ENetPacket *packet;
};

enum ENetPeerState
{
    ENET_PEER_STATE_DISCONNECTED = 0,
    ENET_PEER_STATE_CONNECTING = 1,
    ENET_PEER_STATE_ACKNOWLEDGING_CONNECT = 2,
    ENET_PEER_STATE_CONNECTION_PENDING = 3,
    ENET_PEER_STATE_CONNECTION_SUCCEEDED = 4,
    ENET_PEER_STATE_CONNECTED = 5,
    ENET_PEER_STATE_DISCONNECT_LATER = 6,
    ENET_PEER_STATE_DISCONNECTING = 7,
    ENET_PEER_STATE_ACKNOWLEDGING_DISCONNECT = 8,
    ENET_PEER_STATE_ZOMBIE = 9
};

#ifndef ENET_BUFFER_MAXIMUM
#define ENET_BUFFER_MAXIMUM (1 + 2 * ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS)
#endif

enum
{
    ENET_HOST_RECEIVE_BUFFER_SIZE = 256 * 1024,
    ENET_HOST_SEND_BUFFER_SIZE = 256 * 1024,
    ENET_HOST_BANDWIDTH_THROTTLE_INTERVAL = 1000,
    ENET_HOST_DEFAULT_MTU = 1400,
    ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE = 32 * 1024 * 1024,
    ENET_HOST_DEFAULT_MAXIMUM_WAITING_DATA = 32 * 1024 * 1024,

    ENET_PEER_DEFAULT_ROUND_TRIP_TIME = 500,
    ENET_PEER_DEFAULT_PACKET_THROTTLE = 32,
    ENET_PEER_PACKET_THROTTLE_SCALE = 32,
    ENET_PEER_PACKET_THROTTLE_COUNTER = 7,
    ENET_PEER_PACKET_THROTTLE_ACCELERATION = 2,
    ENET_PEER_PACKET_THROTTLE_DECELERATION = 2,
    ENET_PEER_PACKET_THROTTLE_INTERVAL = 5000,
    ENET_PEER_PACKET_LOSS_SCALE = (1 << 16),
    ENET_PEER_PACKET_LOSS_INTERVAL = 10000,
    ENET_PEER_WINDOW_SIZE_SCALE = 64 * 1024,
    ENET_PEER_TIMEOUT_LIMIT = 32,
    ENET_PEER_TIMEOUT_MINIMUM = 5000,
    ENET_PEER_TIMEOUT_MAXIMUM = 30000,
    ENET_PEER_PING_INTERVAL = 500,
    ENET_PEER_UNSEQUENCED_WINDOWS = 64,
    ENET_PEER_UNSEQUENCED_WINDOW_SIZE = 1024,
    ENET_PEER_FREE_UNSEQUENCED_WINDOWS = 32,
    ENET_PEER_RELIABLE_WINDOWS = 16,
    ENET_PEER_RELIABLE_WINDOW_SIZE = 0x1000,
    ENET_PEER_FREE_RELIABLE_WINDOWS = 8
};

struct ENetChannel
{
    uint16_t outgoingReliableSequenceNumber;
    uint16_t outgoingUnreliableSequenceNumber;
    uint16_t usedReliableWindows;
    uint16_t reliableWindows[ENET_PEER_RELIABLE_WINDOWS];
    uint16_t incomingReliableSequenceNumber;
    uint16_t incomingUnreliableSequenceNumber;
    ENetList incomingReliableCommands;
    ENetList incomingUnreliableCommands;
};

enum ENetPeerFlag
{
    ENET_PEER_FLAG_NEEDS_DISPATCH = (1 << 0),
    ENET_PEER_FLAG_CONTINUE_SENDING = (1 << 1)
};

/**
 * An ENet peer which data packets may be sent or received from.
 *
 * No fields should be modified unless otherwise specified.
 */
struct ENetPeer
{
    ENetListNode dispatchList;
    ENetHost *host;
    uint16_t outgoingPeerID;
    uint16_t incomingPeerID;
    uint32_t connectID;
    uint8_t outgoingSessionID;
    uint8_t incomingSessionID;
    ENetAddress address; /**< Internet address of the peer */
    void *data;          /**< Application private data, may be freely modified */
    ENetPeerState state;
    ENetChannel *channels;
    size_t channelCount;        /**< Number of channels allocated for communication with peer */
    uint32_t incomingBandwidth; /**< Downstream bandwidth of the client in bytes/second */
    uint32_t outgoingBandwidth; /**< Upstream bandwidth of the client in bytes/second */
    uint32_t incomingBandwidthThrottleEpoch;
    uint32_t outgoingBandwidthThrottleEpoch;
    uint32_t incomingDataTotal;
    uint32_t outgoingDataTotal;
    uint32_t lastSendTime;
    uint32_t lastReceiveTime;
    uint32_t nextTimeout;
    uint32_t earliestTimeout;
    uint32_t packetLossEpoch;
    uint32_t packetsSent;
    uint32_t packetsLost;
    uint32_t packetLoss; /**< mean packet loss of reliable packets as a ratio with respect to the constant
                               ENET_PEER_PACKET_LOSS_SCALE */
    uint32_t packetLossVariance;
    uint32_t packetThrottle;
    uint32_t packetThrottleLimit;
    uint32_t packetThrottleCounter;
    uint32_t packetThrottleEpoch;
    uint32_t packetThrottleAcceleration;
    uint32_t packetThrottleDeceleration;
    uint32_t packetThrottleInterval;
    uint32_t pingInterval;
    uint32_t timeoutLimit;
    uint32_t timeoutMinimum;
    uint32_t timeoutMaximum;
    uint32_t lastRoundTripTime;
    uint32_t lowestRoundTripTime;
    uint32_t lastRoundTripTimeVariance;
    uint32_t highestRoundTripTimeVariance;
    uint32_t roundTripTime; /**< mean round trip time (RTT), in milliseconds, between sending a reliable packet
                                  and receiving its acknowledgement */
    uint32_t roundTripTimeVariance;
    uint32_t mtu;
    uint32_t windowSize;
    uint32_t reliableDataInTransit;
    uint16_t outgoingReliableSequenceNumber;
    ENetList acknowledgements;
    ENetList sentReliableCommands;
    ENetList outgoingSendReliableCommands;
    ENetList outgoingCommands;
    ENetList dispatchedCommands;
    uint16_t flags;
    uint16_t reserved;
    uint16_t incomingUnsequencedGroup;
    uint16_t outgoingUnsequencedGroup;
    uint32_t unsequencedWindow[ENET_PEER_UNSEQUENCED_WINDOW_SIZE / 32];
    uint32_t eventData;
    size_t totalWaitingData;
};

/** An ENet packet compressor for compressing UDP packets before socket sends or receives.
 */
struct ENetCompressor
{
    /** Context data for the compressor. Must be non-NULL. */
    void *context;
    /** Compresses from inBuffers[0:inBufferCount-1], containing inLimit bytes, to outData, outputting at most
     * outLimit bytes. Should return 0 on failure. */
    size_t(ENET_CALLBACK *compress)(void *context, const ENetBuffer *inBuffers, size_t inBufferCount, size_t inLimit,
                                    uint8_t *outData, size_t outLimit);
    /** Decompresses from inData, containing inLimit bytes, to outData, outputting at most outLimit bytes. Should
     * return 0 on failure. */
    size_t(ENET_CALLBACK *decompress)(void *context, const uint8_t *inData, size_t inLimit, uint8_t *outData,
                                      size_t outLimit);
    /** Destroys the context when compression is disabled or the host is destroyed. May be NULL. */
    void(ENET_CALLBACK *destroy)(void *context);
};

/** Callback that computes the checksum of the data held in buffers[0:bufferCount-1] */
using ENetChecksumCallback = uint32_t(ENET_CALLBACK *)(const ENetBuffer *buffers, size_t bufferCount);

/** Callback for intercepting received raw UDP packets. Should return 1 to intercept, 0 to ignore, or -1 to
 * propagate an error. */
using ENetInterceptCallback = int(ENET_CALLBACK *)(ENetHost *host, ENetEvent *event);

/** An ENet host for communicating with peers.
  *
  * No fields should be modified unless otherwise stated.

    @sa enet_host_create()
    @sa enet_host_destroy()
    @sa enet_host_connect()
    @sa enet_host_service()
    @sa enet_host_flush()
    @sa enet_host_broadcast()
    @sa enet_host_compress()
    @sa enet_host_compress_with_range_coder()
    @sa enet_host_channel_limit()
    @sa enet_host_bandwidth_limit()
    @sa enet_host_bandwidth_throttle()
  */
struct ENetHost
{
    ENetSocket socket;
    ENetAddress address;        /**< Internet address of the host */
    uint32_t incomingBandwidth; /**< downstream bandwidth of the host */
    uint32_t outgoingBandwidth; /**< upstream bandwidth of the host */
    uint32_t bandwidthThrottleEpoch;
    uint32_t mtu;
    uint32_t randomSeed;
    int recalculateBandwidthLimits;
    ENetPeer *peers;     /**< array of peers allocated for this host */
    size_t peerCount;    /**< number of peers allocated for this host */
    size_t channelLimit; /**< maximum number of channels allowed for connected peers */
    uint32_t serviceTime;
    ENetList dispatchQueue;
    uint32_t totalQueued;
    size_t packetSize;
    uint16_t headerFlags;
    ENetProtocol commands[ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS];
    size_t commandCount;
    ENetBuffer buffers[ENET_BUFFER_MAXIMUM];
    size_t bufferCount;
    ENetChecksumCallback checksum; /**< callback the user can set to enable packet checksums for this host */
    ENetCompressor compressor;
    uint8_t packetData[2][ENET_PROTOCOL_MAXIMUM_MTU];
    ENetAddress receivedAddress;
    uint8_t *receivedData;
    size_t receivedDataLength;
    uint32_t totalSentData;        /**< total data sent, user should reset to 0 as needed to prevent overflow */
    uint32_t totalSentPackets;     /**< total UDP packets sent, user should reset to 0 as needed to prevent overflow */
    uint32_t totalReceivedData;    /**< total data received, user should reset to 0 as needed to prevent overflow */
    uint32_t totalReceivedPackets; /**< total UDP packets received, user should reset to 0 as needed to prevent
                                         overflow */
    ENetInterceptCallback intercept; /**< callback the user can set to intercept received raw UDP packets */
    size_t connectedPeers;
    size_t bandwidthLimitedPeers;
    size_t duplicatePeers;     /**< optional number of allowed peers from duplicate IPs, defaults to
                                  ENET_PROTOCOL_MAXIMUM_PEER_ID */
    size_t maximumPacketSize;  /**< the maximum allowable packet size that may be sent or received on a peer */
    size_t maximumWaitingData; /**< the maximum aggregate amount of buffer space a peer may use waiting for packets
                                  to be delivered */
};

/**
 * An ENet event type, as specified in @ref ENetEvent.
 */
enum ENetEventType
{
    /** no event occurred within the specified time limit */
    ENET_EVENT_TYPE_NONE = 0,

    /** a connection request initiated by enet_host_connect has completed.
     * The peer field contains the peer which successfully connected.
     */
    ENET_EVENT_TYPE_CONNECT = 1,

    /** a peer has disconnected.  This event is generated on a successful
     * completion of a disconnect initiated by enet_peer_disconnect, if
     * a peer has timed out, or if a connection request intialized by
     * enet_host_connect has timed out.  The peer field contains the peer
     * which disconnected. The data field contains user supplied data
     * describing the disconnection, or 0, if none is available.
     */
    ENET_EVENT_TYPE_DISCONNECT = 2,

    /** a packet has been received from a peer.  The peer field specifies the
     * peer which sent the packet.  The channelID field specifies the channel
     * number upon which the packet was received.  The packet field contains
     * the packet that was received; this packet must be destroyed with
     * enet_packet_destroy after use.
     */
    ENET_EVENT_TYPE_RECEIVE = 3
};

/**
 * An ENet event as returned by enet_host_service().

   @sa enet_host_service
 */
struct ENetEvent
{
    ENetEventType type; /**< type of the event */
    ENetPeer *peer;     /**< peer that generated a connect, disconnect or receive event */
    uint8_t channelID;  /**< channel on the peer that generated the event, if appropriate */
    uint32_t data;      /**< data associated with the event, if appropriate */
    ENetPacket *packet; /**< packet associated with the event, if appropriate */
};

/** @defgroup global ENet global functions
    @{
*/

/**
  Initializes ENet globally.  Must be called prior to using any functions in
  ENet.
  @returns 0 on success, < 0 on failure
*/
ENET_API int enet_initialize(void);

/**
  Initializes ENet globally and supplies user-overridden callbacks. Must be called prior to using any functions in
  ENet. Do not use enet_initialize() if you use this variant. Make sure the ENetCallbacks structure is zeroed out so
  that any additional callbacks added in future versions will be properly ignored.

  @param version the constant ENET_VERSION should be supplied so ENet knows which version of ENetCallbacks struct to
  use
  @param inits user-overridden callbacks where any NULL callbacks will use ENet's defaults
  @returns 0 on success, < 0 on failure
*/
ENET_API int enet_initialize_with_callbacks(ENetVersion version, const ENetCallbacks *inits);

/**
  Shuts down ENet globally.  Should be called when a program that has
  initialized ENet exits.
*/
ENET_API void enet_deinitialize(void);

/**
  Gives the linked version of the ENet library.
  @returns the version number
*/
ENET_API ENetVersion enet_linked_version(void);

/** @} */

/** @defgroup private ENet private implementation functions */

/**
  Returns the wall-time in milliseconds.  Its initial value is unspecified
  unless otherwise set.
  */
ENET_API uint32_t enet_time_get(void);
/**
  Sets the current wall-time in milliseconds.
  */
ENET_API void enet_time_set(uint32_t);

/** @defgroup socket ENet socket functions
    @{
*/
ENET_API ENetSocket enet_socket_create(ENetSocketType);
ENET_API int enet_socket_bind(ENetSocket, const ENetAddress *);
ENET_API int enet_socket_get_address(ENetSocket, ENetAddress *);
ENET_API int enet_socket_listen(ENetSocket, int);
ENET_API ENetSocket enet_socket_accept(ENetSocket, ENetAddress *);
ENET_API int enet_socket_connect(ENetSocket, const ENetAddress *);
ENET_API int enet_socket_send(ENetSocket, const ENetAddress *, const ENetBuffer *, size_t);
ENET_API int enet_socket_receive(ENetSocket, ENetAddress *, ENetBuffer *, size_t);
ENET_API int enet_socket_wait(ENetSocket, uint32_t *, uint32_t);
ENET_API int enet_socket_set_option(ENetSocket, ENetSocketOption, int);
ENET_API int enet_socket_get_option(ENetSocket, ENetSocketOption, int *);
ENET_API int enet_socket_shutdown(ENetSocket, ENetSocketShutdown);
ENET_API void enet_socket_destroy(ENetSocket);
ENET_API int enet_socketset_select(ENetSocket, ENetSocketSet *, ENetSocketSet *, uint32_t);

/** @} */

/** @defgroup Address ENet address functions
    @{
*/

/** Attempts to parse the printable form of the IP address in the parameter hostName
    and sets the host field in the address parameter if successful.
    @param address destination to store the parsed IP address
    @param hostName IP address to parse
    @retval 0 on success
    @retval < 0 on failure
    @returns the address of the given hostName in address on success
*/
ENET_API int enet_address_set_host_ip(ENetAddress *address, const char *hostName);

/** Attempts to resolve the host named by the parameter hostName and sets
    the host field in the address parameter if successful.
    @param address destination to store resolved address
    @param hostName host name to lookup
    @retval 0 on success
    @retval < 0 on failure
    @returns the address of the given hostName in address on success
*/
ENET_API int enet_address_set_host(ENetAddress *address, const char *hostName);

/** Gives the printable form of the IP address specified in the address parameter.
    @param address    address printed
    @param hostName   destination for name, must not be NULL
    @param nameLength maximum length of hostName.
    @returns the null-terminated name of the host in hostName on success
    @retval 0 on success
    @retval < 0 on failure
*/
ENET_API int enet_address_get_host_ip(const ENetAddress *address, char *hostName, size_t nameLength);

/** Attempts to do a reverse lookup of the host field in the address parameter.
    @param address    address used for reverse lookup
    @param hostName   destination for name, must not be NULL
    @param nameLength maximum length of hostName.
    @returns the null-terminated name of the host in hostName on success
    @retval 0 on success
    @retval < 0 on failure
*/
ENET_API int enet_address_get_host(const ENetAddress *address, char *hostName, size_t nameLength);

/** @} */

ENET_API ENetPacket *enet_packet_create(const void *, size_t, uint32_t);
ENET_API void enet_packet_destroy(ENetPacket *);
ENET_API int enet_packet_resize(ENetPacket *, size_t);
ENET_API uint32_t enet_crc32(const ENetBuffer *, size_t);

ENET_API ENetHost *enet_host_create(const ENetAddress *, size_t, size_t, uint32_t, uint32_t);
ENET_API void enet_host_destroy(ENetHost *);
ENET_API ENetPeer *enet_host_connect(ENetHost *, const ENetAddress *, size_t, uint32_t);
ENET_API int enet_host_check_events(ENetHost *, ENetEvent *);
ENET_API int enet_host_service(ENetHost *, ENetEvent *, uint32_t);
ENET_API void enet_host_flush(ENetHost *);
ENET_API void enet_host_broadcast(ENetHost *, uint8_t, ENetPacket *);
ENET_API void enet_host_compress(ENetHost *, const ENetCompressor *);
ENET_API int enet_host_compress_with_range_coder(ENetHost *host);
ENET_API void enet_host_channel_limit(ENetHost *, size_t);
ENET_API void enet_host_bandwidth_limit(ENetHost *, uint32_t, uint32_t);
extern void enet_host_bandwidth_throttle(ENetHost *);
extern uint32_t enet_host_random_seed(void);
extern uint32_t enet_host_random(ENetHost *);

ENET_API int enet_peer_send(ENetPeer *, uint8_t, ENetPacket *);
ENET_API ENetPacket *enet_peer_receive(ENetPeer *, uint8_t *channelID);
ENET_API void enet_peer_ping(ENetPeer *);
ENET_API void enet_peer_ping_interval(ENetPeer *, uint32_t);
ENET_API void enet_peer_timeout(ENetPeer *, uint32_t, uint32_t, uint32_t);
ENET_API void enet_peer_reset(ENetPeer *);
ENET_API void enet_peer_disconnect(ENetPeer *, uint32_t);
ENET_API void enet_peer_disconnect_now(ENetPeer *, uint32_t);
ENET_API void enet_peer_disconnect_later(ENetPeer *, uint32_t);
ENET_API void enet_peer_throttle_configure(ENetPeer *, uint32_t, uint32_t, uint32_t);
extern int enet_peer_throttle(ENetPeer *, uint32_t);
extern void enet_peer_reset_queues(ENetPeer *);
extern int enet_peer_has_outgoing_commands(ENetPeer *);
extern void enet_peer_setup_outgoing_command(ENetPeer *, ENetOutgoingCommand *);
extern ENetOutgoingCommand *enet_peer_queue_outgoing_command(ENetPeer *, const ENetProtocol *, ENetPacket *, uint32_t,
                                                             uint16_t);
extern ENetIncomingCommand *enet_peer_queue_incoming_command(ENetPeer *, const ENetProtocol *, const void *, size_t,
                                                             uint32_t, uint32_t);
extern ENetAcknowledgement *enet_peer_queue_acknowledgement(ENetPeer *, const ENetProtocol *, uint16_t);
extern void enet_peer_dispatch_incoming_unreliable_commands(ENetPeer *, ENetChannel *, ENetIncomingCommand *);
extern void enet_peer_dispatch_incoming_reliable_commands(ENetPeer *, ENetChannel *, ENetIncomingCommand *);
extern void enet_peer_on_connect(ENetPeer *);
extern void enet_peer_on_disconnect(ENetPeer *);

ENET_API void *enet_range_coder_create(void);
ENET_API void enet_range_coder_destroy(void *);
ENET_API size_t enet_range_coder_compress(void *, const ENetBuffer *, size_t, size_t, uint8_t *, size_t);
ENET_API size_t enet_range_coder_decompress(void *, const uint8_t *, size_t, uint8_t *, size_t);

extern size_t enet_protocol_command_size(uint8_t);
