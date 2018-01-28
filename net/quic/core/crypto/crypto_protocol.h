// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_CRYPTO_PROTOCOL_H_
#define NET_QUIC_CORE_CRYPTO_CRYPTO_PROTOCOL_H_

#include <cstddef>
#include <string>

#include "net/quic/core/quic_tag.h"

// Version and Crypto tags are written to the wire with a big-endian
// representation of the name of the tag.  For example
// the client hello tag (CHLO) will be written as the
// following 4 bytes: 'C' 'H' 'L' 'O'.  Since it is
// stored in memory as a little endian uint32_t, we need
// to reverse the order of the bytes.
//
// We use a macro to ensure that no static initialisers are created. Use the
// MakeQuicTag function in normal code.
#define TAG(a, b, c, d) \
  static_cast<QuicTag>((d << 24) + (c << 16) + (b << 8) + a)

namespace net {

typedef std::string ServerConfigID;

// clang-format off
const QuicTag kCHLO = TAG('C', 'H', 'L', 'O');   // Client hello
const QuicTag kSHLO = TAG('S', 'H', 'L', 'O');   // Server hello
const QuicTag kSCFG = TAG('S', 'C', 'F', 'G');   // Server config
const QuicTag kREJ  = TAG('R', 'E', 'J', '\0');  // Reject
const QuicTag kSREJ = TAG('S', 'R', 'E', 'J');   // Stateless reject
const QuicTag kCETV = TAG('C', 'E', 'T', 'V');   // Client encrypted tag-value
                                                 // pairs
const QuicTag kPRST = TAG('P', 'R', 'S', 'T');   // Public reset
const QuicTag kSCUP = TAG('S', 'C', 'U', 'P');   // Server config update
const QuicTag kALPN = TAG('A', 'L', 'P', 'N');   // Application-layer protocol

// Key exchange methods
const QuicTag kP256 = TAG('P', '2', '5', '6');   // ECDH, Curve P-256
const QuicTag kC255 = TAG('C', '2', '5', '5');   // ECDH, Curve25519

// AEAD algorithms
const QuicTag kAESG = TAG('A', 'E', 'S', 'G');   // AES128 + GCM-12
const QuicTag kCC20 = TAG('C', 'C', '2', '0');   // ChaCha20 + Poly1305 RFC7539

// Socket receive buffer
const QuicTag kSRBF = TAG('S', 'R', 'B', 'F');   // Socket receive buffer

// Congestion control feedback types
const QuicTag kQBIC = TAG('Q', 'B', 'I', 'C');   // TCP cubic

// Connection options (COPT) values
const QuicTag kAFCW = TAG('A', 'F', 'C', 'W');   // Auto-tune flow control
                                                 // receive windows.
const QuicTag kIFW5 = TAG('I', 'F', 'W', '5');   // Set initial size
                                                 // of stream flow control
                                                 // receive window to
                                                 // 32KB. (2^5 KB).
const QuicTag kIFW6 = TAG('I', 'F', 'W', '6');   // Set initial size
                                                 // of stream flow control
                                                 // receive window to
                                                 // 64KB. (2^6 KB).
const QuicTag kIFW7 = TAG('I', 'F', 'W', '7');   // Set initial size
                                                 // of stream flow control
                                                 // receive window to
                                                 // 128KB. (2^7 KB).
const QuicTag kIFW8 = TAG('I', 'F', 'W', '8');   // Set initial size
                                                 // of stream flow control
                                                 // receive window to
                                                 // 256KB. (2^8 KB).
const QuicTag kIFW9 = TAG('I', 'F', 'W', '9');   // Set initial size
                                                 // of stream flow control
                                                 // receive window to
                                                 // 512KB. (2^9 KB).
const QuicTag kIFWA = TAG('I', 'F', 'W', 'a');   // Set initial size
                                                 // of stream flow control
                                                 // receive window to
                                                 // 1MB. (2^0xa KB).
const QuicTag kTBBR = TAG('T', 'B', 'B', 'R');   // Reduced Buffer Bloat TCP
const QuicTag k1RTT = TAG('1', 'R', 'T', 'T');   // STARTUP in BBR for 1 RTT
const QuicTag k2RTT = TAG('2', 'R', 'T', 'T');   // STARTUP in BBR for 2 RTTs
const QuicTag kLRTT = TAG('L', 'R', 'T', 'T');   // Exit STARTUP in BBR on loss
const QuicTag kBBRR = TAG('B', 'B', 'R', 'R');   // Rate-based recovery in BBR
const QuicTag kBBR1 = TAG('B', 'B', 'R', '1');   // Ack aggregatation v1
const QuicTag kBBR2 = TAG('B', 'B', 'R', '2');   // Ack aggregatation v2
const QuicTag kRENO = TAG('R', 'E', 'N', 'O');   // Reno Congestion Control
const QuicTag kTPCC = TAG('P', 'C', 'C', '\0');  // Performance-Oriented
                                                 // Congestion Control
const QuicTag kBYTE = TAG('B', 'Y', 'T', 'E');   // TCP cubic or reno in bytes
const QuicTag kIW03 = TAG('I', 'W', '0', '3');   // Force ICWND to 3
const QuicTag kIW10 = TAG('I', 'W', '1', '0');   // Force ICWND to 10
const QuicTag kIW20 = TAG('I', 'W', '2', '0');   // Force ICWND to 20
const QuicTag kIW50 = TAG('I', 'W', '5', '0');   // Force ICWND to 50
const QuicTag k1CON = TAG('1', 'C', 'O', 'N');   // Emulate a single connection
const QuicTag kNTLP = TAG('N', 'T', 'L', 'P');   // No tail loss probe
const QuicTag kNCON = TAG('N', 'C', 'O', 'N');   // N Connection Congestion Ctrl
const QuicTag kNRTO = TAG('N', 'R', 'T', 'O');   // CWND reduction on loss
const QuicTag kTIME = TAG('T', 'I', 'M', 'E');   // Time based loss detection
const QuicTag kATIM = TAG('A', 'T', 'I', 'M');   // Adaptive time loss detection
const QuicTag kMIN1 = TAG('M', 'I', 'N', '1');   // Min CWND of 1 packet
const QuicTag kMIN4 = TAG('M', 'I', 'N', '4');   // Min CWND of 4 packets,
                                                 // with a min rate of 1 BDP.
const QuicTag kTLPR = TAG('T', 'L', 'P', 'R');   // Tail loss probe delay of
                                                 // 0.5RTT.
const QuicTag kACKD = TAG('A', 'C', 'K', 'D');   // Ack decimation style acking.
const QuicTag kAKD2 = TAG('A', 'K', 'D', '2');   // Ack decimation tolerating
                                                 // out of order packets.
const QuicTag kAKD3 = TAG('A', 'K', 'D', '3');   // Ack decimation style acking
                                                 // with 1/8 RTT acks.
const QuicTag kAKD4 = TAG('A', 'K', 'D', '4');   // Ack decimation with 1/8 RTT
                                                 // tolerating out of order.
const QuicTag kAKDU = TAG('A', 'K', 'D', 'U');   // Unlimited number of packets
                                                 // receieved before acking
const QuicTag kSSLR = TAG('S', 'S', 'L', 'R');   // Slow Start Large Reduction.
const QuicTag kNPRR = TAG('N', 'P', 'R', 'R');   // Pace at unity instead of PRR
const QuicTag k5RTO = TAG('5', 'R', 'T', 'O');   // Close connection on 5 RTOs
const QuicTag k3RTO = TAG('3', 'R', 'T', 'O');   // Close connection on 3 RTOs
const QuicTag kCTIM = TAG('C', 'T', 'I', 'M');   // Client timestamp in seconds
                                                 // since UNIX epoch.
const QuicTag kDHDT = TAG('D', 'H', 'D', 'T');   // Disable HPACK dynamic table.
const QuicTag kCONH = TAG('C', 'O', 'N', 'H');   // Conservative Handshake
                                                 // Retransmissions.
const QuicTag kLFAK = TAG('L', 'F', 'A', 'K');   // Don't invoke FACK on the
                                                 // first ack.
// TODO(fayang): Remove this connection option in QUIC_VERSION_37, in which
// MAX_HEADER_LIST_SIZE settings frame should be supported.
const QuicTag kSMHL = TAG('S', 'M', 'H', 'L');   // Support MAX_HEADER_LIST_SIZE
                                                 // settings frame.
const QuicTag kCCVX = TAG('C', 'C', 'V', 'X');   // Fix Cubic convex bug.
const QuicTag kCBQT = TAG('C', 'B', 'Q', 'T');   // Fix CubicBytes quantization.
const QuicTag kBLMX = TAG('B', 'L', 'M', 'X');   // Fix Cubic BetaLastMax bug.
const QuicTag kCPAU = TAG('C', 'P', 'A', 'U');   // Allow Cubic per-ack-updates.
const QuicTag kNSTP = TAG('N', 'S', 'T', 'P');   // No stop waiting frames.

// Optional support of truncated Connection IDs.  If sent by a peer, the value
// is the minimum number of bytes allowed for the connection ID sent to the
// peer.
const QuicTag kTCID = TAG('T', 'C', 'I', 'D');   // Connection ID truncation.

// Multipath option.
const QuicTag kMPTH = TAG('M', 'P', 'T', 'H');   // Enable multipath.

const QuicTag kNCMR = TAG('N', 'C', 'M', 'R');   // Do not attempt connection
                                                 // migration.

// Enable bandwidth resumption experiment.
const QuicTag kBWRE = TAG('B', 'W', 'R', 'E');  // Bandwidth resumption.
const QuicTag kBWMX = TAG('B', 'W', 'M', 'X');  // Max bandwidth resumption.
const QuicTag kBWRS = TAG('B', 'W', 'R', 'S');  // Server bandwidth resumption.
const QuicTag kBWS2 = TAG('B', 'W', 'S', '2');  // Server bw resumption v2.

// Enable path MTU discovery experiment.
const QuicTag kMTUH = TAG('M', 'T', 'U', 'H');  // High-target MTU discovery.
const QuicTag kMTUL = TAG('M', 'T', 'U', 'L');  // Low-target MTU discovery.

// Tags for async signing experiments
const QuicTag kASYN = TAG('A', 'S', 'Y', 'N');  // Perform asynchronous signing
const QuicTag kSYNC = TAG('S', 'Y', 'N', 'C');  // Perform synchronous signing

// Proof types (i.e. certificate types)
// NOTE: although it would be silly to do so, specifying both kX509 and kX59R
// is allowed and is equivalent to specifying only kX509.
const QuicTag kX509 = TAG('X', '5', '0', '9');   // X.509 certificate, all key
                                                 // types
const QuicTag kX59R = TAG('X', '5', '9', 'R');   // X.509 certificate, RSA keys
                                                 // only
const QuicTag kCHID = TAG('C', 'H', 'I', 'D');   // Channel ID.

// Client hello tags
const QuicTag kVER  = TAG('V', 'E', 'R', '\0');  // Version
const QuicTag kNONC = TAG('N', 'O', 'N', 'C');   // The client's nonce
const QuicTag kNONP = TAG('N', 'O', 'N', 'P');   // The client's proof nonce
const QuicTag kKEXS = TAG('K', 'E', 'X', 'S');   // Key exchange methods
const QuicTag kAEAD = TAG('A', 'E', 'A', 'D');   // Authenticated
                                                 // encryption algorithms
const QuicTag kCOPT = TAG('C', 'O', 'P', 'T');   // Connection options
const QuicTag kCLOP = TAG('C', 'L', 'O', 'P');   // Client connection options
const QuicTag kICSL = TAG('I', 'C', 'S', 'L');   // Idle network timeout
const QuicTag kSCLS = TAG('S', 'C', 'L', 'S');   // Silently close on timeout
const QuicTag kMSPC = TAG('M', 'S', 'P', 'C');   // Max streams per connection.
const QuicTag kMIDS = TAG('M', 'I', 'D', 'S');   // Max incoming dynamic streams
const QuicTag kIRTT = TAG('I', 'R', 'T', 'T');   // Estimated initial RTT in us.
const QuicTag kSWND = TAG('S', 'W', 'N', 'D');   // Server's Initial congestion
                                                 // window.
const QuicTag kSNI  = TAG('S', 'N', 'I', '\0');  // Server name
                                                 // indication
const QuicTag kPUBS = TAG('P', 'U', 'B', 'S');   // Public key values
const QuicTag kSCID = TAG('S', 'C', 'I', 'D');   // Server config id
const QuicTag kORBT = TAG('O', 'B', 'I', 'T');   // Server orbit.
const QuicTag kPDMD = TAG('P', 'D', 'M', 'D');   // Proof demand.
const QuicTag kPROF = TAG('P', 'R', 'O', 'F');   // Proof (signature).
const QuicTag kCCS  = TAG('C', 'C', 'S', 0);     // Common certificate set
const QuicTag kCCRT = TAG('C', 'C', 'R', 'T');   // Cached certificate
const QuicTag kEXPY = TAG('E', 'X', 'P', 'Y');   // Expiry
const QuicTag kSTTL = TAG('S', 'T', 'T', 'L');   // Server Config TTL
const QuicTag kSFCW = TAG('S', 'F', 'C', 'W');   // Initial stream flow control
                                                 // receive window.
const QuicTag kCFCW = TAG('C', 'F', 'C', 'W');   // Initial session/connection
                                                 // flow control receive window.
const QuicTag kUAID = TAG('U', 'A', 'I', 'D');   // Client's User Agent ID.
const QuicTag kXLCT = TAG('X', 'L', 'C', 'T');   // Expected leaf certificate.
const QuicTag kTBKP = TAG('T', 'B', 'K', 'P');   // Token Binding key params.

// Token Binding tags
const QuicTag kTB10 = TAG('T', 'B', '1', '0');   // TB draft 10 with P256.

// Rejection tags
const QuicTag kRREJ = TAG('R', 'R', 'E', 'J');   // Reasons for server sending
// Stateless Reject tags
const QuicTag kRCID = TAG('R', 'C', 'I', 'D');   // Server-designated
                                                 // connection ID
// Server hello tags
const QuicTag kCADR = TAG('C', 'A', 'D', 'R');   // Client IP address and port
const QuicTag kASAD = TAG('A', 'S', 'A', 'D');   // Alternate Server IP address
                                                 // and port.
const QuicTag kSRST = TAG('S', 'R', 'S', 'T');   // Stateless reset token used
                                                 // in IETF public reset packet

// CETV tags
const QuicTag kCIDK = TAG('C', 'I', 'D', 'K');   // ChannelID key
const QuicTag kCIDS = TAG('C', 'I', 'D', 'S');   // ChannelID signature

// Public reset tags
const QuicTag kRNON = TAG('R', 'N', 'O', 'N');   // Public reset nonce proof
const QuicTag kRSEQ = TAG('R', 'S', 'E', 'Q');   // Rejected packet number

// Universal tags
const QuicTag kPAD  = TAG('P', 'A', 'D', '\0');  // Padding

// Server push tags
const QuicTag kSPSH = TAG('S', 'P', 'S', 'H');  // Support server push.

// clang-format on

// These tags have a special form so that they appear either at the beginning
// or the end of a handshake message. Since handshake messages are sorted by
// tag value, the tags with 0 at the end will sort first and those with 255 at
// the end will sort last.
//
// The certificate chain should have a tag that will cause it to be sorted at
// the end of any handshake messages because it's likely to be large and the
// client might be able to get everything that it needs from the small values at
// the beginning.
//
// Likewise tags with random values should be towards the beginning of the
// message because the server mightn't hold state for a rejected client hello
// and therefore the client may have issues reassembling the rejection message
// in the event that it sent two client hellos.
const QuicTag kServerNonceTag = TAG('S', 'N', 'O', 0);  // The server's nonce
const QuicTag kSourceAddressTokenTag =
    TAG('S', 'T', 'K', 0);  // Source-address token
const QuicTag kCertificateTag = TAG('C', 'R', 'T', 255);  // Certificate chain
const QuicTag kCertificateSCTTag =
    TAG('C', 'S', 'C', 'T');  // Signed cert timestamp (RFC6962) of leaf cert.

#undef TAG

const size_t kMaxEntries = 128;  // Max number of entries in a message.

const size_t kNonceSize = 32;  // Size in bytes of the connection nonce.

const size_t kOrbitSize = 8;  // Number of bytes in an orbit value.

// kProofSignatureLabel is prepended to the CHLO hash and server configs before
// signing to avoid any cross-protocol attacks on the signature.
const char kProofSignatureLabel[] = "QUIC CHLO and server config signature";

// kClientHelloMinimumSize is the minimum size of a client hello. Client hellos
// will have PAD tags added in order to ensure this minimum is met and client
// hellos smaller than this will be an error. This minimum size reduces the
// amplification factor of any mirror DoS attack.
//
// A client may pad an inchoate client hello to a size larger than
// kClientHelloMinimumSize to make it more likely to receive a complete
// rejection message.
const size_t kClientHelloMinimumSize = 1024;

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_CRYPTO_PROTOCOL_H_
