// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CONSTANTS_H_
#define QUICHE_QUIC_CORE_QUIC_CONSTANTS_H_

#include <stddef.h>

#include <cstdint>
#include <limits>

#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

// Definitions of constant values used throughout the QUIC code.

namespace quic {

// Simple time constants.
inline constexpr uint64_t kNumSecondsPerMinute = 60;
inline constexpr uint64_t kNumSecondsPerHour = kNumSecondsPerMinute * 60;
inline constexpr uint64_t kNumSecondsPerWeek = kNumSecondsPerHour * 24 * 7;
inline constexpr uint64_t kNumMillisPerSecond = 1000;
inline constexpr uint64_t kNumMicrosPerMilli = 1000;
inline constexpr uint64_t kNumMicrosPerSecond =
    kNumMicrosPerMilli * kNumMillisPerSecond;

// Default number of connections for N-connection emulation.
inline constexpr uint32_t kDefaultNumConnections = 2;
// Default initial maximum size in bytes of a QUIC packet.
inline constexpr QuicByteCount kDefaultMaxPacketSize = 1250;
// Tunnels (such as MASQUE and QBONE) reduce the inner MTU so they work best
// with a higher outer MTU. This means that outer connections could fail on some
// networks where the UDP MTU is between 1250 and 1350, but allows inner QUIC
// connections to still have 1200 bytes of UDP MTU, even if we apply two nested
// levels of connect-udp proxying, as we do for IP Protection.
inline constexpr QuicByteCount kDefaultMaxPacketSizeForTunnels = 1350;
// Default initial maximum size in bytes of a QUIC packet for servers.
inline constexpr QuicByteCount kDefaultServerMaxPacketSize = 1000;
// Maximum transmission unit on Ethernet.
inline constexpr QuicByteCount kEthernetMTU = 1500;
// The maximum packet size of any QUIC packet over IPv6, based on ethernet's max
// size, minus the IP and UDP headers. IPv6 has a 40 byte header, UDP adds an
// additional 8 bytes.  This is a total overhead of 48 bytes.  Ethernet's
// max packet size is 1500 bytes,  1500 - 48 = 1452.
inline constexpr QuicByteCount kMaxV6PacketSize = 1452;
// The maximum packet size of any QUIC packet over IPv4.
// 1500(Ethernet) - 20(IPv4 header) - 8(UDP header) = 1472.
inline constexpr QuicByteCount kMaxV4PacketSize = 1472;
// The maximum incoming packet size allowed.
inline constexpr QuicByteCount kMaxIncomingPacketSize = kMaxV4PacketSize;
// The maximum outgoing packet size allowed.
inline constexpr QuicByteCount kMaxOutgoingPacketSize = kMaxV6PacketSize;
// ETH_MAX_MTU - MAX(sizeof(iphdr), sizeof(ip6_hdr)) - sizeof(udphdr).
inline constexpr QuicByteCount kMaxGsoPacketSize = 65535 - 40 - 8;
// The maximal IETF DATAGRAM frame size we'll accept. Choosing 2^16 ensures
// that it is greater than the biggest frame we could ever fit in a QUIC packet.
inline constexpr QuicByteCount kMaxAcceptedDatagramFrameSize = 65536;
// Default value of the max_packet_size transport parameter if it is not
// transmitted.
inline constexpr QuicByteCount kDefaultMaxPacketSizeTransportParam = 65527;
// Default maximum packet size used in the Linux TCP implementation.
// Used in QUIC for congestion window computations in bytes.
inline constexpr QuicByteCount kDefaultTCPMSS = 1460;
inline constexpr QuicByteCount kMaxSegmentSize = kDefaultTCPMSS;
// The minimum size of a packet which can elicit a version negotiation packet,
// as per section 8.1 of the QUIC spec.
inline constexpr QuicByteCount kMinPacketSizeForVersionNegotiation = 1200;

// We match SPDY's use of 32 (since we'd compete with SPDY).
inline constexpr QuicPacketCount kInitialCongestionWindow = 32;

// Do not allow initial congestion window to be greater than 200 packets.
inline constexpr QuicPacketCount kMaxInitialCongestionWindow = 200;

// Do not allow initial congestion window to be smaller than 10 packets.
inline constexpr QuicPacketCount kMinInitialCongestionWindow = 10;

// Minimum size of initial flow control window, for both stream and session.
// This is only enforced when version.AllowsLowFlowControlLimits() is false.
inline constexpr QuicByteCount kMinimumFlowControlSendWindow =
    16 * 1024;  // 16 KB
// Default size of initial flow control window, for both stream and session.
inline constexpr QuicByteCount kDefaultFlowControlSendWindow =
    16 * 1024;  // 16 KB

// Maximum flow control receive window limits for connection and stream.
inline constexpr QuicByteCount kStreamReceiveWindowLimit =
    16 * 1024 * 1024;  // 16 MB
inline constexpr QuicByteCount kSessionReceiveWindowLimit =
    24 * 1024 * 1024;  // 24 MB

// Minimum size of the CWND, in packets, when doing bandwidth resumption.
inline constexpr QuicPacketCount kMinCongestionWindowForBandwidthResumption =
    10;

// Default size of the socket receive buffer in bytes.
inline constexpr QuicByteCount kDefaultSocketReceiveBuffer = 1024 * 1024;

// The lower bound of an untrusted initial rtt value.
inline constexpr uint32_t kMinUntrustedInitialRoundTripTimeUs =
    10 * kNumMicrosPerMilli;

// The lower bound of a trusted initial rtt value.
inline constexpr uint32_t kMinTrustedInitialRoundTripTimeUs =
    5 * kNumMicrosPerMilli;

// Don't allow a client to suggest an RTT longer than 1 second.
inline constexpr uint32_t kMaxInitialRoundTripTimeUs = kNumMicrosPerSecond;

// Maximum number of open streams per connection.
inline constexpr size_t kDefaultMaxStreamsPerConnection = 100;

// Number of bytes reserved for public flags in the packet header.
inline constexpr size_t kPublicFlagsSize = 1;
// Number of bytes reserved for version number in the packet header.
inline constexpr size_t kQuicVersionSize = 4;

// Minimum number of active connection IDs that an end point can maintain.
inline constexpr uint32_t kMinNumOfActiveConnectionIds = 2;

// Length of the retry integrity tag in bytes.
// https://tools.ietf.org/html/draft-ietf-quic-transport-25#section-17.2.5
inline constexpr size_t kRetryIntegrityTagLength = 16;

// By default, UnackedPacketsMap allocates buffer of 64 after the first packet
// is added.
inline constexpr int kDefaultUnackedPacketsInitialCapacity = 64;

// Signifies that the QuicPacket will contain version of the protocol.
inline constexpr bool kIncludeVersion = true;
// Signifies that the QuicPacket will include a diversification nonce.
inline constexpr bool kIncludeDiversificationNonce = true;

// Header key used to identify final offset on data stream when sending HTTP/2
// trailing headers over QUIC.
QUICHE_EXPORT extern const char* const kFinalOffsetHeaderKey;

// Returns the local default delayed ack time, in ms.
QUICHE_EXPORT int64_t GetDefaultDelayedAckTimeMs();

// Delayed ack time that we assume the peer will use by default, in ms. This
// should be equal to the default value of --quic_default_delayed_ack_time_ms.
inline constexpr int64_t kDefaultPeerDelayedAckTimeMs = 25;

// Default minimum delayed ack time, in ms (used only for sender control of ack
// frequency).
inline constexpr uint32_t kDefaultMinAckDelayTimeMs = 5;

// Default shift of the ACK delay in the IETF QUIC ACK frame.
inline constexpr uint32_t kDefaultAckDelayExponent = 3;

// Minimum tail loss probe time in ms.
inline constexpr int64_t kMinTailLossProbeTimeoutMs = 10;

// The timeout before the handshake succeeds.
inline constexpr int64_t kInitialIdleTimeoutSecs = 5;
// The maximum idle timeout that can be negotiated.
inline constexpr int64_t kMaximumIdleTimeoutSecs = 60 * 10;  // 10 minutes.
// The default timeout for a connection until the crypto handshake succeeds.
inline constexpr int64_t kMaxTimeForCryptoHandshakeSecs = 10;  // 10 secs.

// Default limit on the number of undecryptable packets the connection buffers
// before the CHLO/SHLO arrive.
inline constexpr size_t kDefaultMaxUndecryptablePackets = 10;

// Default ping timeout.
inline constexpr int64_t kPingTimeoutSecs = 15;  // 15 secs.

// Minimum number of RTTs between Server Config Updates (SCUP) sent to client.
inline constexpr int kMinIntervalBetweenServerConfigUpdatesRTTs = 10;

// Minimum time between Server Config Updates (SCUP) sent to client.
inline constexpr int kMinIntervalBetweenServerConfigUpdatesMs = 1000;

// Minimum number of packets between Server Config Updates (SCUP).
inline constexpr int kMinPacketsBetweenServerConfigUpdates = 100;

// The number of open streams that a server will accept is set to be slightly
// larger than the negotiated limit. Immediately closing the connection if the
// client opens slightly too many streams is not ideal: the client may have sent
// a FIN that was lost, and simultaneously opened a new stream. The number of
// streams a server accepts is a fixed increment over the negotiated limit, or a
// percentage increase, whichever is larger.
inline constexpr float kMaxStreamsMultiplier = 1.1f;
inline constexpr int kMaxStreamsMinimumIncrement = 10;

// Available streams are ones with IDs less than the highest stream that has
// been opened which have neither been opened or reset. The limit on the number
// of available streams is 10 times the limit on the number of open streams.
inline constexpr int kMaxAvailableStreamsMultiplier = 10;

// The 1st PTO is armed with max of earliest in flight sent time + PTO
// delay and kFirstPtoSrttMultiplier * srtt from last in flight packet.
inline constexpr float kFirstPtoSrttMultiplier = 1.5;

// The multiplier of RTT variation when calculating PTO timeout.
inline constexpr int kPtoRttvarMultiplier = 2;

// TCP RFC calls for 1 second RTO however Linux differs from this default and
// define the minimum RTO to 200ms, we will use the same until we have data to
// support a higher or lower value.
inline constexpr const int64_t kMinRetransmissionTimeMs = 200;

// We define an unsigned 16-bit floating point value, inspired by IEEE floats
// (http://en.wikipedia.org/wiki/Half_precision_floating-point_format),
// with 5-bit exponent (bias 1), 11-bit mantissa (effective 12 with hidden
// bit) and denormals, but without signs, transfinites or fractions. Wire format
// 16 bits (little-endian byte order) are split into exponent (high 5) and
// mantissa (low 11) and decoded as:
//   uint64_t value;
//   if (exponent == 0) value = mantissa;
//   else value = (mantissa | 1 << 11) << (exponent - 1)
inline constexpr int kUFloat16ExponentBits = 5;
inline constexpr int kUFloat16MaxExponent =
    (1 << kUFloat16ExponentBits) - 2;                                     // 30
inline constexpr int kUFloat16MantissaBits = 16 - kUFloat16ExponentBits;  // 11
inline constexpr int kUFloat16MantissaEffectiveBits =
    kUFloat16MantissaBits + 1;                 // 12
inline constexpr uint64_t kUFloat16MaxValue =  // 0x3FFC0000000
    ((UINT64_C(1) << kUFloat16MantissaEffectiveBits) - 1)
    << kUFloat16MaxExponent;

// kDiversificationNonceSize is the size, in bytes, of the nonce that a server
// may set in the packet header to ensure that its INITIAL keys are not
// duplicated.
inline constexpr size_t kDiversificationNonceSize = 32;

// The largest gap in packets we'll accept without closing the connection.
// This will likely have to be tuned.
inline constexpr QuicPacketCount kMaxPacketGap = 5000;

// The max number of sequence number intervals that
// QuicPeerIssuedConnetionIdManager can maintain.
inline constexpr size_t kMaxNumConnectionIdSequenceNumberIntervals = 20;

// The maximum number of random padding bytes to add.
inline constexpr QuicByteCount kMaxNumRandomPaddingBytes = 256;

// The size of stream send buffer data slice size in bytes. A data slice is
// piece of stream data stored in contiguous memory, and a stream frame can
// contain data from multiple data slices.
inline constexpr QuicByteCount kQuicStreamSendBufferSliceSize = 4 * 1024;

// For When using Random Initial Packet Numbers, they can start
// anyplace in the range 1...((2^31)-1) or 0x7fffffff
QUICHE_EXPORT QuicPacketNumber MaxRandomInitialPacketNumber();

// Used to represent an invalid or no control frame id.
inline constexpr QuicControlFrameId kInvalidControlFrameId = 0;

// The max length a stream can have.
inline constexpr QuicByteCount kMaxStreamLength = (UINT64_C(1) << 62) - 1;

// The max value that can be encoded using IETF Var Ints.
inline constexpr uint64_t kMaxIetfVarInt = UINT64_C(0x3fffffffffffffff);

// The maximum stream id value that is supported - (2^32)-1
inline constexpr QuicStreamId kMaxQuicStreamId = 0xffffffff;

// The maximum value that can be stored in a 32-bit QuicStreamCount.
inline constexpr QuicStreamCount kMaxQuicStreamCount = 0xffffffff;

// Number of bytes reserved for packet header type.
inline constexpr size_t kPacketHeaderTypeSize = 1;

// Number of bytes reserved for connection ID length.
inline constexpr size_t kConnectionIdLengthSize = 1;

// Minimum length of random bytes in IETF stateless reset packet.
inline constexpr size_t kMinRandomBytesLengthInStatelessReset = 24;

// Maximum length allowed for the token in a NEW_TOKEN frame.
inline constexpr size_t kMaxNewTokenTokenLength = 0xffff;

// The prefix used by a source address token in a NEW_TOKEN frame.
inline constexpr uint8_t kAddressTokenPrefix = 0;

// Default initial rtt used before any samples are received.
inline constexpr int kInitialRttMs = 100;

// Default threshold of packet reordering before a packet is declared lost.
inline constexpr QuicPacketCount kDefaultPacketReorderingThreshold = 3;

// Default fraction (1/4) of an RTT the algorithm waits before determining a
// packet is lost due to early retransmission by time based loss detection.
inline constexpr int kDefaultLossDelayShift = 2;

// Default fraction (1/8) of an RTT when doing IETF loss detection.
inline constexpr int kDefaultIetfLossDelayShift = 3;

// Maximum number of retransmittable packets received before sending an ack.
inline constexpr QuicPacketCount kDefaultRetransmittablePacketsBeforeAck = 2;
// Wait for up to 10 retransmittable packets before sending an ack.
inline constexpr QuicPacketCount kMaxRetransmittablePacketsBeforeAck = 10;
// Minimum number of packets received before ack decimation is enabled.
// This intends to avoid the beginning of slow start, when CWNDs may be
// rapidly increasing.
inline constexpr QuicPacketCount kMinReceivedBeforeAckDecimation = 100;
// Ask peer to use one quarter RTT delay when doing ack decimation.
inline constexpr float kPeerAckDecimationDelay = 0.25;

// The default alarm granularity assumed by QUIC code.
inline constexpr QuicTime::Delta kAlarmGranularity =
    QuicTime::Delta::FromMilliseconds(1);

// Maximum number of unretired connection IDs a connection can have.
inline constexpr size_t kMaxNumConnectonIdsInUse = 10u;

// Packet number of first sending packet of a connection. Please note, this
// cannot be used as first received packet because peer can choose its starting
// packet number.
QUICHE_EXPORT QuicPacketNumber FirstSendingPacketNumber();

// Used by clients to tell if a public reset is sent from a Google frontend.
QUICHE_EXPORT extern const char* const kEPIDGoogleFrontEnd;
QUICHE_EXPORT extern const char* const kEPIDGoogleFrontEnd0;

inline constexpr uint64_t kHttpDatagramStreamIdDivisor = 4;

inline constexpr QuicTime::Delta kDefaultMultiPortProbingInterval =
    QuicTime::Delta::FromSeconds(3);

inline constexpr size_t kMaxNumMultiPortPaths = 5;

inline constexpr size_t kMaxDuplicatedPacketsSentToServerPreferredAddress = 5;

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONSTANTS_H_
