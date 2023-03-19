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

namespace ENet
{
struct Host;
struct Event;
struct Packet;
} // namespace ENet

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

using ENetPacketFreeCallback = void(ENET_CALLBACK *)(ENet::Packet *);

namespace ENet
{

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
struct Packet
{
    size_t referenceCount;               /**< internal use only */
    uint32_t flags;                      /**< bitwise-or of ENetPacketFlag constants */
    uint8_t *data;                       /**< allocated data for packet */
    size_t dataLength;                   /**< length of data */
    ENetPacketFreeCallback freeCallback; /**< function to be called when the packet is no longer in use */
    void *userData;                      /**< application private data, may be freely modified */
};

struct Acknowledgement
{
    ListNode acknowledgementList;
    uint32_t sentTime;
    Protocol command;
};

struct OutgoingCommand
{
    ListNode outgoingCommandList;
    uint16_t reliableSequenceNumber;
    uint16_t unreliableSequenceNumber;
    uint32_t sentTime;
    uint32_t roundTripTimeout;
    uint32_t queueTime;
    uint32_t fragmentOffset;
    uint16_t fragmentLength;
    uint16_t sendAttempts;
    Protocol command;
    Packet *packet;
};

struct IncomingCommand
{
    ListNode incomingCommandList;
    uint16_t reliableSequenceNumber;
    uint16_t unreliableSequenceNumber;
    Protocol command;
    uint32_t fragmentCount;
    uint32_t fragmentsRemaining;
    uint32_t *fragments;
    Packet *packet;
};

} // namespace ENet

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
    ENet::List incomingReliableCommands;
    ENet::List incomingUnreliableCommands;
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
    ENet::ListNode dispatchList;
    ENet::Host *host;
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
    ENet::List acknowledgements;
    ENet::List sentReliableCommands;
    ENet::List outgoingSendReliableCommands;
    ENet::List outgoingCommands;
    ENet::List dispatchedCommands;
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
using ENetInterceptCallback = int(ENET_CALLBACK *)(ENet::Host *host, ENet::Event *event);

namespace ENet
{

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
struct Host
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
    ENet::List dispatchQueue;
    uint32_t totalQueued;
    size_t packetSize;
    uint16_t headerFlags;
    Protocol commands[ENET_PROTOCOL_MAXIMUM_PACKET_COMMANDS];
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

} // namespace ENet

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

namespace ENet
{

/**
 * An ENet event as returned by enet_host_service().

   @sa enet_host_service
 */
struct Event
{
    ENetEventType type; /**< type of the event */
    ENetPeer *peer;     /**< peer that generated a connect, disconnect or receive event */
    uint8_t channelID;  /**< channel on the peer that generated the event, if appropriate */
    uint32_t data;      /**< data associated with the event, if appropriate */
    Packet *packet;     /**< packet associated with the event, if appropriate */
};

/**
 * @brief Struct that holds platform-specific functions.
 *
 * @detail When writing your own platform abstraction, create a "final" struct
 *         that subclasses this one, provide concrete implementations for all
 *         methods, and implement ENet::Platform::Get() to return a reference
 *         to a static instance of your platform struct.
 */
struct Platform
{
    virtual int initialize() = 0;
    virtual void deinitialize() = 0;
    virtual uint32_t time_get() = 0;
    virtual void time_set(uint32_t newTimeBase) = 0;
    virtual ENetSocket socket_create(ENetSocketType type) = 0;
    virtual int socket_bind(ENetSocket socket, const ENetAddress *address) = 0;
    virtual int socket_get_address(ENetSocket socket, ENetAddress *address) = 0;
    virtual uint32_t host_random_seed() = 0;
    virtual int socket_listen(ENetSocket socket, int backlog) = 0;
    virtual ENetSocket socket_accept(ENetSocket socket, ENetAddress *address) = 0;
    virtual int socket_connect(ENetSocket socket, const ENetAddress *address) = 0;
    virtual int socket_send(ENetSocket socket, const ENetAddress *address, const ENetBuffer *buffers,
                            size_t bufferCount) = 0;
    virtual int socket_receive(ENetSocket socket, ENetAddress *address, ENetBuffer *buffers, size_t bufferCount) = 0;
    virtual int socket_wait(ENetSocket socket, uint32_t *condition, uint32_t timeout) = 0;
    virtual int socket_set_option(ENetSocket socket, ENetSocketOption option, int value) = 0;
    virtual int socket_get_option(ENetSocket socket, ENetSocketOption option, int *value) = 0;
    virtual int socket_shutdown(ENetSocket socket, ENetSocketShutdown how) = 0;
    virtual void socket_destroy(ENetSocket socket) = 0;
    virtual int socketset_select(ENetSocket maxSocket, ENetSocketSet *readSet, ENetSocketSet *writeSet,
                                 uint32_t timeout) = 0;
    virtual int address_set_host_ip(ENetAddress *address, const char *hostName) = 0;
    virtual int address_set_host(ENetAddress *address, const char *hostName) = 0;
    virtual int address_get_host_ip(const ENetAddress *address, char *hostName, size_t nameLength) = 0;
    virtual int address_get_host(const ENetAddress *address, char *hostName, size_t nameLength) = 0;
    // TODO
    static Platform &Get();
};

/** @defgroup global ENet global functions
    @{
*/

/**
  Initializes ENet globally.  Must be called prior to using any functions in
  ENet.
  @returns 0 on success, < 0 on failure
*/
ENET_API int initialize();

/**
  Initializes ENet globally and supplies user-overridden callbacks. Must be called prior to using any functions in
  ENet. Do not use enet_initialize() if you use this variant. Make sure the ENetCallbacks structure is zeroed out so
  that any additional callbacks added in future versions will be properly ignored.

  @param version the constant ENET_VERSION should be supplied so ENet knows which version of ENetCallbacks struct to
  use
  @param inits user-overridden callbacks where any NULL callbacks will use ENet's defaults
  @returns 0 on success, < 0 on failure
*/
ENET_API int initialize_with_callbacks(ENetVersion version, const Callbacks *inits);

/**
  Shuts down ENet globally.  Should be called when a program that has
  initialized ENet exits.
*/
ENET_API void deinitialize();

/**
  Gives the linked version of the ENet library.
  @returns the version number
*/
ENET_API ENetVersion linked_version();

/** @} */

/** @defgroup private ENet private implementation functions */

/**
  Returns the wall-time in milliseconds.  Its initial value is unspecified
  unless otherwise set.
  */
ENET_API uint32_t time_get();

/**
  Sets the current wall-time in milliseconds.
  */
ENET_API void time_set(uint32_t newTimeBase);

/** @defgroup socket ENet socket functions
    @{
*/
ENET_API ENetSocket socket_create(ENetSocketType type);
ENET_API int socket_bind(ENetSocket socket, const ENetAddress *address);
ENET_API int socket_get_address(ENetSocket socket, ENetAddress *address);
ENET_API int socket_listen(ENetSocket socket, int backlog);
ENET_API ENetSocket socket_accept(ENetSocket socket, ENetAddress *address);
ENET_API int socket_connect(ENetSocket socket, const ENetAddress *address);
ENET_API int socket_send(ENetSocket socket, const ENetAddress *address, const ENetBuffer *buffers, size_t bufferCount);
ENET_API int socket_receive(ENetSocket socket, ENetAddress *address, ENetBuffer *buffers, size_t bufferCount);
ENET_API int socket_wait(ENetSocket socket, uint32_t *condition, uint32_t timeout);
ENET_API int socket_set_option(ENetSocket socket, ENetSocketOption option, int value);
ENET_API int socket_get_option(ENetSocket socket, ENetSocketOption option, int *value);
ENET_API int socket_shutdown(ENetSocket socket, ENetSocketShutdown how);
ENET_API void socket_destroy(ENetSocket socket);
ENET_API int socketset_select(ENetSocket maxSocket, ENetSocketSet *readSet, ENetSocketSet *writeSet, uint32_t timeout);

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
ENET_API int address_set_host_ip(ENetAddress *address, const char *hostName);

/** Attempts to resolve the host named by the parameter hostName and sets
    the host field in the address parameter if successful.
    @param address destination to store resolved address
    @param hostName host name to lookup
    @retval 0 on success
    @retval < 0 on failure
    @returns the address of the given hostName in address on success
*/
ENET_API int address_set_host(ENetAddress *address, const char *hostName);

/** Gives the printable form of the IP address specified in the address parameter.
    @param address    address printed
    @param hostName   destination for name, must not be NULL
    @param nameLength maximum length of hostName.
    @returns the null-terminated name of the host in hostName on success
    @retval 0 on success
    @retval < 0 on failure
*/
ENET_API int address_get_host_ip(const ENetAddress *address, char *hostName, size_t nameLength);

/** Attempts to do a reverse lookup of the host field in the address parameter.
    @param address    address used for reverse lookup
    @param hostName   destination for name, must not be NULL
    @param nameLength maximum length of hostName.
    @returns the null-terminated name of the host in hostName on success
    @retval 0 on success
    @retval < 0 on failure
*/
ENET_API int address_get_host(const ENetAddress *address, char *hostName, size_t nameLength);

/** @} */

/** Creates a packet that may be sent to a peer.
    @param data         initial contents of the packet's data; the packet's data will remain uninitialized if data is
   NULL.
    @param dataLength   size of the data allocated for this packet
    @param flags        flags for this packet as described for the ENetPacket structure.
    @returns the packet on success, NULL on failure
*/
ENET_API Packet *packet_create(const void *data, size_t dataLength, uint32_t flags);

/** Destroys the packet and deallocates its data.
    @param packet packet to be destroyed
*/
ENET_API void packet_destroy(Packet *packet);

/** Attempts to resize the data in the packet to length specified in the
    dataLength parameter
    @param packet packet to resize
    @param dataLength new size for the packet data
    @returns 0 on success, < 0 on failure
*/
ENET_API int packet_resize(Packet *packet, size_t dataLength);
ENET_API uint32_t crc32(const ENetBuffer *buffers, size_t bufferCount);

/** Creates a host for communicating to peers.

    @param address   the address at which other peers may connect to this host.  If NULL, then no peers may connect to
   the host.
    @param peerCount the maximum number of peers that should be allocated for the host.
    @param channelLimit the maximum number of channels allowed; if 0, then this is equivalent to
   ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT
    @param incomingBandwidth downstream bandwidth of the host in bytes/second; if 0, ENet will assume unlimited
   bandwidth.
    @param outgoingBandwidth upstream bandwidth of the host in bytes/second; if 0, ENet will assume unlimited bandwidth.

    @returns the host on success and NULL on failure

    @remarks ENet will strategically drop packets on specific sides of a connection between hosts
    to ensure the host's bandwidth is not overwhelmed.  The bandwidth parameters also determine
    the window size of a connection which limits the amount of reliable packets that may be in transit
    at any given time.
*/
ENET_API ENet::Host *host_create(const ENetAddress *address, size_t peerCount, size_t channelLimit,
                                 uint32_t incomingBandwidth, uint32_t outgoingBandwidth);

/** Destroys the host and all resources associated with it.
    @param host pointer to the host to destroy
*/
ENET_API void host_destroy(ENet::Host *host);

/** Initiates a connection to a foreign host.
    @param host host seeking the connection
    @param address destination for the connection
    @param channelCount number of channels to allocate
    @param data user data supplied to the receiving host
    @returns a peer representing the foreign host on success, NULL on failure
    @remarks The peer returned will have not completed the connection until enet_host_service()
    notifies of an ENET_EVENT_TYPE_CONNECT event for the peer.
*/
ENET_API ENetPeer *host_connect(Host *host, const ENetAddress *address, size_t channelCount, uint32_t data);

/** Checks for any queued events on the host and dispatches one if available.

    @param host    host to check for events
    @param event   an event structure where event details will be placed if available
    @retval > 0 if an event was dispatched
    @retval 0 if no events are available
    @retval < 0 on failure
    @ingroup host
*/
ENET_API int host_check_events(Host *host, Event *event);

/** Waits for events on the host specified and shuttles packets between
    the host and its peers.

    @param host    host to service
    @param event   an event structure where event details will be placed if one occurs
                   if event == NULL then no events will be delivered
    @param timeout number of milliseconds that ENet should wait for events
    @retval > 0 if an event occurred within the specified time limit
    @retval 0 if no event occurred
    @retval < 0 on failure
    @remarks enet_host_service should be called fairly regularly for adequate performance
    @ingroup host
*/
ENET_API int host_service(ENet::Host *host, ENet::Event *event, uint32_t timeout);

/** Sends any queued packets on the host specified to its designated peers.

    @param host   host to flush
    @remarks this function need only be used in circumstances where one wishes to send queued packets earlier than in a
   call to enet_host_service().
    @ingroup host
*/
ENET_API void host_flush(ENet::Host *host);

/** Queues a packet to be sent to all peers associated with the host.
    @param host host on which to broadcast the packet
    @param channelID channel on which to broadcast
    @param packet packet to broadcast
*/
ENET_API void host_broadcast(ENet::Host *host, uint8_t channelID, Packet *packet);

/** Sets the packet compressor the host should use to compress and decompress packets.
    @param host host to enable or disable compression for
    @param compressor callbacks for for the packet compressor; if NULL, then compression is disabled
*/
ENET_API void host_compress(ENet::Host *host, const ENetCompressor *compressor);

/** Sets the packet compressor the host should use to the default range coder.
    @param host host to enable the range coder for
    @returns 0 on success, < 0 on failure
*/
ENET_API int host_compress_with_range_coder(ENet::Host *host);

/** Limits the maximum allowed channels of future incoming connections.
    @param host host to limit
    @param channelLimit the maximum number of channels allowed; if 0, then this is equivalent to
   ENET_PROTOCOL_MAXIMUM_CHANNEL_COUNT
*/
ENET_API void host_channel_limit(ENet::Host *host, size_t channelLimit);

/** Adjusts the bandwidth limits of a host.
    @param host host to adjust
    @param incomingBandwidth new incoming bandwidth
    @param outgoingBandwidth new outgoing bandwidth
    @remarks the incoming and outgoing bandwidth parameters are identical in function to those
    specified in enet_host_create().
*/
ENET_API void host_bandwidth_limit(ENet::Host *host, uint32_t incomingBandwidth, uint32_t outgoingBandwidth);

extern void host_bandwidth_throttle(ENet::Host *host);
extern uint32_t host_random_seed();
extern uint32_t host_random(ENet::Host *host);

/** Queues a packet to be sent.

    On success, ENet will assume ownership of the packet, and so enet_packet_destroy
    should not be called on it thereafter. On failure, the caller still must destroy
    the packet on its own as ENet has not queued the packet. The caller can also
    check the packet's referenceCount field after sending to check if ENet queued
    the packet and thus incremented the referenceCount.

    @param peer destination for the packet
    @param channelID channel on which to send
    @param packet packet to send
    @retval 0 on success
    @retval < 0 on failure
*/
ENET_API int peer_send(ENetPeer *peer, uint8_t channelID, Packet *packet);

/** Attempts to dequeue any incoming queued packet.
    @param peer peer to dequeue packets from
    @param channelID holds the channel ID of the channel the packet was received on success
    @returns a pointer to the packet, or NULL if there are no available incoming queued packets
*/
ENET_API Packet *peer_receive(ENetPeer *peer, uint8_t *channelID);

/** Sends a ping request to a peer.
    @param peer destination for the ping request
    @remarks ping requests factor into the mean round trip time as designated by the
    roundTripTime field in the ENetPeer structure.  ENet automatically pings all connected
    peers at regular intervals, however, this function may be called to ensure more
    frequent ping requests.
*/
ENET_API void peer_ping(ENetPeer *peer);

/** Sets the interval at which pings will be sent to a peer.

    Pings are used both to monitor the liveness of the connection and also to dynamically
    adjust the throttle during periods of low traffic so that the throttle has reasonable
    responsiveness during traffic spikes.

    @param peer the peer to adjust
    @param pingInterval the interval at which to send pings; defaults to ENET_PEER_PING_INTERVAL if 0
*/
ENET_API void peer_ping_interval(ENetPeer *peer, uint32_t pingInterval);

/** Sets the timeout parameters for a peer.

    The timeout parameter control how and when a peer will timeout from a failure to acknowledge
    reliable traffic. Timeout values use an exponential backoff mechanism, where if a reliable
    packet is not acknowledge within some multiple of the average RTT plus a variance tolerance,
    the timeout will be doubled until it reaches a set limit. If the timeout is thus at this
    limit and reliable packets have been sent but not acknowledged within a certain minimum time
    period, the peer will be disconnected. Alternatively, if reliable packets have been sent
    but not acknowledged for a certain maximum time period, the peer will be disconnected regardless
    of the current timeout limit value.

    @param peer the peer to adjust
    @param timeoutLimit the timeout limit; defaults to ENET_PEER_TIMEOUT_LIMIT if 0
    @param timeoutMinimum the timeout minimum; defaults to ENET_PEER_TIMEOUT_MINIMUM if 0
    @param timeoutMaximum the timeout maximum; defaults to ENET_PEER_TIMEOUT_MAXIMUM if 0
*/
ENET_API void peer_timeout(ENetPeer *peer, uint32_t timeoutLimit, uint32_t timeoutMinimum, uint32_t timeoutMaximum);

/** Forcefully disconnects a peer.
    @param peer peer to forcefully disconnect
    @remarks The foreign host represented by the peer is not notified of the disconnection and will timeout
    on its connection to the local host.
*/
ENET_API void peer_reset(ENetPeer *peer);

/** Request a disconnection from a peer.
    @param peer peer to request a disconnection
    @param data data describing the disconnection
    @remarks An ENET_EVENT_DISCONNECT event will be generated by enet_host_service()
    once the disconnection is complete.
*/
ENET_API void peer_disconnect(ENetPeer *peer, uint32_t data);

/** Force an immediate disconnection from a peer.
    @param peer peer to disconnect
    @param data data describing the disconnection
    @remarks No ENET_EVENT_DISCONNECT event will be generated. The foreign peer is not
    guaranteed to receive the disconnect notification, and is reset immediately upon
    return from this function.
*/
ENET_API void peer_disconnect_now(ENetPeer *peer, uint32_t data);

/** Request a disconnection from a peer, but only after all queued outgoing packets are sent.
    @param peer peer to request a disconnection
    @param data data describing the disconnection
    @remarks An ENET_EVENT_DISCONNECT event will be generated by enet_host_service()
    once the disconnection is complete.
*/
ENET_API void peer_disconnect_later(ENetPeer *peer, uint32_t data);

/** Configures throttle parameter for a peer.

    Unreliable packets are dropped by ENet in response to the varying conditions
    of the Internet connection to the peer.  The throttle represents a probability
    that an unreliable packet should not be dropped and thus sent by ENet to the peer.
    The lowest mean round trip time from the sending of a reliable packet to the
    receipt of its acknowledgement is measured over an amount of time specified by
    the interval parameter in milliseconds.  If a measured round trip time happens to
    be significantly less than the mean round trip time measured over the interval,
    then the throttle probability is increased to allow more traffic by an amount
    specified in the acceleration parameter, which is a ratio to the ENET_PEER_PACKET_THROTTLE_SCALE
    constant.  If a measured round trip time happens to be significantly greater than
    the mean round trip time measured over the interval, then the throttle probability
    is decreased to limit traffic by an amount specified in the deceleration parameter, which
    is a ratio to the ENET_PEER_PACKET_THROTTLE_SCALE constant.  When the throttle has
    a value of ENET_PEER_PACKET_THROTTLE_SCALE, no unreliable packets are dropped by
    ENet, and so 100% of all unreliable packets will be sent.  When the throttle has a
    value of 0, all unreliable packets are dropped by ENet, and so 0% of all unreliable
    packets will be sent.  Intermediate values for the throttle represent intermediate
    probabilities between 0% and 100% of unreliable packets being sent.  The bandwidth
    limits of the local and foreign hosts are taken into account to determine a
    sensible limit for the throttle probability above which it should not raise even in
    the best of conditions.

    @param peer peer to configure
    @param interval interval, in milliseconds, over which to measure lowest mean RTT; the default value is
   ENET_PEER_PACKET_THROTTLE_INTERVAL.
    @param acceleration rate at which to increase the throttle probability as mean RTT declines
    @param deceleration rate at which to decrease the throttle probability as mean RTT increases
*/
ENET_API void peer_throttle_configure(ENetPeer *peer, uint32_t interval, uint32_t acceleration, uint32_t deceleration);

extern int peer_throttle(ENetPeer *peer, uint32_t rtt);
extern void peer_reset_queues(ENetPeer *peer);
extern int peer_has_outgoing_commands(ENetPeer *peer);
extern void peer_setup_outgoing_command(ENetPeer *peer, OutgoingCommand *outgoingCommand);
extern OutgoingCommand *peer_queue_outgoing_command(ENetPeer *peer, const Protocol *command, Packet *packet,
                                                    uint32_t offset, uint16_t length);
extern IncomingCommand *peer_queue_incoming_command(ENetPeer *peer, const Protocol *command, const void *data,
                                                    size_t dataLength, uint32_t flags, uint32_t fragmentCount);
extern Acknowledgement *peer_queue_acknowledgement(ENetPeer *peer, const Protocol *command, uint16_t sentTime);
extern void peer_dispatch_incoming_unreliable_commands(ENetPeer *peer, ENetChannel *channel,
                                                       IncomingCommand *queuedCommand);
extern void peer_dispatch_incoming_reliable_commands(ENetPeer *peer, ENetChannel *channel,
                                                     IncomingCommand *queuedCommand);
extern void peer_on_connect(ENetPeer *peer);
extern void peer_on_disconnect(ENetPeer *peer);

ENET_API void *range_coder_create();
ENET_API void range_coder_destroy(void *context);
ENET_API size_t range_coder_compress(void *context, const ENetBuffer *inBuffers, size_t inBufferCount, size_t inLimit,
                                     uint8_t *outData, size_t outLimit);
ENET_API size_t range_coder_decompress(void *context, const uint8_t *inData, size_t inLimit, uint8_t *outData,
                                       size_t outLimit);

extern size_t protocol_command_size(uint8_t commandNumber);

} // namespace ENet
