// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CRYPTO_PROTOCOL_H_
#define QUICHE_QUIC_CORE_CRYPTO_CRYPTO_PROTOCOL_H_

#include <cstddef>
#include <string>

#include "quiche/quic/core/quic_tag.h"

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

namespace quic {

using ServerConfigID = std::string;

// The following tags have been deprecated and should not be reused:
// "1CON", "BBQ4", "NCON", "RCID", "SREJ", "TBKP", "TB10", "SCLS", "SMHL",
// "QNZR", "B2HI", "H2PR", "FIFO", "LIFO", "RRWS", "QNSP", "B2CL", "CHSP",
// "BPTE", "ACKD", "AKD2", "AKD4", "MAD1", "MAD4", "MAD5", "ACD0", "ACKQ",
// "TLPR", "CCS\0", "PDP4", "NCHP", "NBPE", "2RTO", "3RTO", "4RTO", "6RTO",
// "PDP1", "PDP2", "PDP3", "PDP5", "QLVE", "RVCM", "BBPD"

// clang-format off
const QuicTag kCHLO = TAG('C', 'H', 'L', 'O');   // Client hello
const QuicTag kSHLO = TAG('S', 'H', 'L', 'O');   // Server hello
const QuicTag kSCFG = TAG('S', 'C', 'F', 'G');   // Server config
const QuicTag kREJ  = TAG('R', 'E', 'J', '\0');  // Reject
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
const QuicTag kBBS1 = TAG('B', 'B', 'S', '1');   // DEPRECATED
const QuicTag kBBS2 = TAG('B', 'B', 'S', '2');   // More aggressive packet
                                                 // conservation in BBR STARTUP
const QuicTag kBBS3 = TAG('B', 'B', 'S', '3');   // Slowstart packet
                                                 // conservation in BBR STARTUP
const QuicTag kBBS4 = TAG('B', 'B', 'S', '4');   // DEPRECATED
const QuicTag kBBS5 = TAG('B', 'B', 'S', '5');   // DEPRECATED
const QuicTag kBBRR = TAG('B', 'B', 'R', 'R');   // Rate-based recovery in BBR
const QuicTag kBBR1 = TAG('B', 'B', 'R', '1');   // DEPRECATED
const QuicTag kBBR2 = TAG('B', 'B', 'R', '2');   // DEPRECATED
const QuicTag kBBR3 = TAG('B', 'B', 'R', '3');   // Fully drain the queue once
                                                 // per cycle
const QuicTag kBBR4 = TAG('B', 'B', 'R', '4');   // 20 RTT ack aggregation
const QuicTag kBBR5 = TAG('B', 'B', 'R', '5');   // 40 RTT ack aggregation
const QuicTag kBBR9 = TAG('B', 'B', 'R', '9');   // DEPRECATED
const QuicTag kBBRA = TAG('B', 'B', 'R', 'A');   // Starts a new ack aggregation
                                                 // epoch if a full round has
                                                 // passed
const QuicTag kBBRB = TAG('B', 'B', 'R', 'B');   // Use send rate in BBR's
                                                 // MaxAckHeightTracker
const QuicTag kBBRS = TAG('B', 'B', 'R', 'S');   // DEPRECATED
const QuicTag kBBQ1 = TAG('B', 'B', 'Q', '1');   // DEPRECATED
const QuicTag kBBQ2 = TAG('B', 'B', 'Q', '2');   // BBRv2 with 2.885 STARTUP and
                                                 // DRAIN CWND gain.
const QuicTag kBBQ3 = TAG('B', 'B', 'Q', '3');   // BBR with ack aggregation
                                                 // compensation in STARTUP.
const QuicTag kBBQ5 = TAG('B', 'B', 'Q', '5');   // Expire ack aggregation upon
                                                 // bandwidth increase in
                                                 // STARTUP.
const QuicTag kBBQ6 = TAG('B', 'B', 'Q', '6');   // Reduce STARTUP gain to 25%
                                                 // more than BW increase.
const QuicTag kBBQ7 = TAG('B', 'B', 'Q', '7');   // Reduce bw_lo by
                                                 // bytes_lost/min_rtt.
const QuicTag kBBQ8 = TAG('B', 'B', 'Q', '8');   // Reduce bw_lo by
                                                 // bw_lo * bytes_lost/inflight
const QuicTag kBBQ9 = TAG('B', 'B', 'Q', '9');   // Reduce bw_lo by
                                                 // bw_lo * bytes_lost/cwnd
const QuicTag kBBQ0 = TAG('B', 'B', 'Q', '0');   // Increase bytes_acked in
                                                 // PROBE_UP when app limited.
const QuicTag kBBHI = TAG('B', 'B', 'H', 'I');   // Increase inflight_hi in
                                                 // PROBE_UP if ever inflight_hi
                                                 // limited in round
const QuicTag kRENO = TAG('R', 'E', 'N', 'O');   // Reno Congestion Control
const QuicTag kTPCC = TAG('P', 'C', 'C', '\0');  // Performance-Oriented
                                                 // Congestion Control
const QuicTag kBYTE = TAG('B', 'Y', 'T', 'E');   // TCP cubic or reno in bytes
const QuicTag kIW03 = TAG('I', 'W', '0', '3');   // Force ICWND to 3
const QuicTag kIW10 = TAG('I', 'W', '1', '0');   // Force ICWND to 10
const QuicTag kIW20 = TAG('I', 'W', '2', '0');   // Force ICWND to 20
const QuicTag kIW50 = TAG('I', 'W', '5', '0');   // Force ICWND to 50
const QuicTag kB2ON = TAG('B', '2', 'O', 'N');   // Enable BBRv2
const QuicTag kB2NA = TAG('B', '2', 'N', 'A');   // For BBRv2, do not add ack
                                                 // height to queueing threshold
const QuicTag kB2NE = TAG('B', '2', 'N', 'E');   // For BBRv2, always exit
                                                 // STARTUP on loss, even if
                                                 // bandwidth growth exceeds
                                                 // threshold.
const QuicTag kB2RP = TAG('B', '2', 'R', 'P');   // For BBRv2, run PROBE_RTT on
                                                 // the regular schedule
const QuicTag kB2LO = TAG('B', '2', 'L', 'O');   // Ignore inflight_lo in BBR2
const QuicTag kB2HR = TAG('B', '2', 'H', 'R');   // 15% inflight_hi headroom.
const QuicTag kB2SL = TAG('B', '2', 'S', 'L');   // When exiting STARTUP due to
                                                 // loss, set inflight_hi to the
                                                 // max of bdp and max bytes
                                                 // delivered in round.
const QuicTag kB2H2 = TAG('B', '2', 'H', '2');   // When exiting PROBE_UP due to
                                                 // loss, set inflight_hi to the
                                                 // max of inflight@send and max
                                                 // bytes delivered in round.
const QuicTag kB2RC = TAG('B', '2', 'R', 'C');   // Disable Reno-coexistence for
                                                 // BBR2.
const QuicTag kBSAO = TAG('B', 'S', 'A', 'O');   // Avoid Overestimation in
                                                 // Bandwidth Sampler with ack
                                                 // aggregation
const QuicTag kB2DL = TAG('B', '2', 'D', 'L');   // Increase inflight_hi based
                                                 // on delievered, not inflight.
const QuicTag kB201 = TAG('B', '2', '0', '1');   // DEPRECATED
const QuicTag kB202 = TAG('B', '2', '0', '2');   // Do not exit PROBE_UP if
                                                 // inflight dips below 1.25*BW.
const QuicTag kB203 = TAG('B', '2', '0', '3');   // Ignore inflight_hi until
                                                 // PROBE_UP is exited.
const QuicTag kB204 = TAG('B', '2', '0', '4');   // Reduce extra acked when
                                                 // MaxBW incrases.
const QuicTag kB205 = TAG('B', '2', '0', '5');   // Add extra acked to CWND in
                                                 // STARTUP.
const QuicTag kB206 = TAG('B', '2', '0', '6');   // Exit STARTUP after 2 losses.
const QuicTag kB207 = TAG('B', '2', '0', '7');   // Exit STARTUP on persistent
                                                 // queue
const QuicTag kBB2U = TAG('B', 'B', '2', 'U');   // Exit PROBE_UP on
                                                 // min_bytes_in_flight for two
                                                 // rounds in a row.
const QuicTag kBB2S = TAG('B', 'B', '2', 'S');   // Exit STARTUP on
                                                 // min_bytes_in_flight for two
                                                 // rounds in a row.
const QuicTag kNTLP = TAG('N', 'T', 'L', 'P');   // No tail loss probe
const QuicTag k1TLP = TAG('1', 'T', 'L', 'P');   // 1 tail loss probe
const QuicTag k1RTO = TAG('1', 'R', 'T', 'O');   // Send 1 packet upon RTO
const QuicTag kNRTO = TAG('N', 'R', 'T', 'O');   // CWND reduction on loss
const QuicTag kTIME = TAG('T', 'I', 'M', 'E');   // Time based loss detection
const QuicTag kATIM = TAG('A', 'T', 'I', 'M');   // Adaptive time loss detection
const QuicTag kMIN1 = TAG('M', 'I', 'N', '1');   // Min CWND of 1 packet
const QuicTag kMIN4 = TAG('M', 'I', 'N', '4');   // Min CWND of 4 packets,
                                                 // with a min rate of 1 BDP.
const QuicTag kMAD0 = TAG('M', 'A', 'D', '0');   // Ignore ack delay
const QuicTag kMAD2 = TAG('M', 'A', 'D', '2');   // No min TLP
const QuicTag kMAD3 = TAG('M', 'A', 'D', '3');   // No min RTO
const QuicTag k1ACK = TAG('1', 'A', 'C', 'K');   // 1 fast ack for reordering
const QuicTag kAKD3 = TAG('A', 'K', 'D', '3');   // Ack decimation style acking
                                                 // with 1/8 RTT acks.
const QuicTag kAKDU = TAG('A', 'K', 'D', 'U');   // Unlimited number of packets
                                                 // received before acking
const QuicTag kAFFE = TAG('A', 'F', 'F', 'E');   // Enable client receiving
                                                 // AckFrequencyFrame.
const QuicTag kAFF1 = TAG('A', 'F', 'F', '1');   // Use SRTT in building
                                                 // AckFrequencyFrame.
const QuicTag kAFF2 = TAG('A', 'F', 'F', '2');   // Send AckFrequencyFrame upon
                                                 // handshake completion.
const QuicTag kSSLR = TAG('S', 'S', 'L', 'R');   // Slow Start Large Reduction.
const QuicTag kNPRR = TAG('N', 'P', 'R', 'R');   // Pace at unity instead of PRR
const QuicTag k5RTO = TAG('5', 'R', 'T', 'O');   // Close connection on 5 RTOs
const QuicTag kCBHD = TAG('C', 'B', 'H', 'D');   // Client only blackhole
                                                 // detection.
const QuicTag kNBHD = TAG('N', 'B', 'H', 'D');   // No blackhole detection.
const QuicTag kCONH = TAG('C', 'O', 'N', 'H');   // Conservative Handshake
                                                 // Retransmissions.
const QuicTag kLFAK = TAG('L', 'F', 'A', 'K');   // Don't invoke FACK on the
                                                 // first ack.
const QuicTag kSTMP = TAG('S', 'T', 'M', 'P');   // DEPRECATED
const QuicTag kEACK = TAG('E', 'A', 'C', 'K');   // Bundle ack-eliciting frame
                                                 // with an ACK after PTO/RTO

const QuicTag kILD0 = TAG('I', 'L', 'D', '0');   // IETF style loss detection
                                                 // (default with 1/8 RTT time
                                                 // threshold)
const QuicTag kILD1 = TAG('I', 'L', 'D', '1');   // IETF style loss detection
                                                 // with 1/4 RTT time threshold
const QuicTag kILD2 = TAG('I', 'L', 'D', '2');   // IETF style loss detection
                                                 // with adaptive packet
                                                 // threshold
const QuicTag kILD3 = TAG('I', 'L', 'D', '3');   // IETF style loss detection
                                                 // with 1/4 RTT time threshold
                                                 // and adaptive packet
                                                 // threshold
const QuicTag kILD4 = TAG('I', 'L', 'D', '4');   // IETF style loss detection
                                                 // with both adaptive time
                                                 // threshold (default 1/4 RTT)
                                                 // and adaptive packet
                                                 // threshold
const QuicTag kRUNT = TAG('R', 'U', 'N', 'T');   // No packet threshold loss
                                                 // detection for "runt" packet.
const QuicTag kNSTP = TAG('N', 'S', 'T', 'P');   // No stop waiting frames.
const QuicTag kNRTT = TAG('N', 'R', 'T', 'T');   // Ignore initial RTT

const QuicTag k1PTO = TAG('1', 'P', 'T', 'O');   // Send 1 packet upon PTO.
const QuicTag k2PTO = TAG('2', 'P', 'T', 'O');   // Send 2 packets upon PTO.

const QuicTag k6PTO = TAG('6', 'P', 'T', 'O');   // Closes connection on 6
                                                 // consecutive PTOs.
const QuicTag k7PTO = TAG('7', 'P', 'T', 'O');   // Closes connection on 7
                                                 // consecutive PTOs.
const QuicTag k8PTO = TAG('8', 'P', 'T', 'O');   // Closes connection on 8
                                                 // consecutive PTOs.
const QuicTag kPTOS = TAG('P', 'T', 'O', 'S');   // Skip packet number before
                                                 // sending the last PTO.
const QuicTag kPTOA = TAG('P', 'T', 'O', 'A');   // Do not add max ack delay
                                                 // when computing PTO timeout
                                                 // if an immediate ACK is
                                                 // expected.
const QuicTag kPEB1 = TAG('P', 'E', 'B', '1');   // Start exponential backoff
                                                 // since 1st PTO.
const QuicTag kPEB2 = TAG('P', 'E', 'B', '2');   // Start exponential backoff
                                                 // since 2nd PTO.
const QuicTag kPVS1 = TAG('P', 'V', 'S', '1');   // Use 2 * rttvar when
                                                 // calculating PTO timeout.
const QuicTag kPAG1 = TAG('P', 'A', 'G', '1');   // Make 1st PTO more aggressive
const QuicTag kPAG2 = TAG('P', 'A', 'G', '2');   // Make first 2 PTOs more
                                                 // aggressive
const QuicTag kPSDA = TAG('P', 'S', 'D', 'A');   // Use standard deviation when
                                                 // calculating PTO timeout.
const QuicTag kPLE1 = TAG('P', 'L', 'E', '1');   // Arm the 1st PTO with
                                                 // earliest in flight sent time
                                                 // and at least 0.5*srtt from
                                                 // last sent packet.
const QuicTag kPLE2 = TAG('P', 'L', 'E', '2');   // Arm the 1st PTO with
                                                 // earliest in flight sent time
                                                 // and at least 1.5*srtt from
                                                 // last sent packet.
const QuicTag kAPTO = TAG('A', 'P', 'T', 'O');   // Use 1.5 * initial RTT before
                                                 // any RTT sample is available.

const QuicTag kELDT = TAG('E', 'L', 'D', 'T');   // Enable Loss Detection Tuning

const QuicTag kSPAD = TAG('S', 'P', 'A', 'D');   // Use server preferred address
const QuicTag kSPA2 = TAG('S', 'P', 'A', '2');   // Start validating server
                                                 // preferred address once it is
                                                 // received. Send all coalesced
                                                 // packets to both addresses.
const QuicTag kEVMB = TAG('E', 'V', 'M', 'B');

const QuicTag kCRNT = TAG('C', 'R', 'N', 'T');

const QuicTag kPRGC = TAG('P', 'R', 'G', 'C');   // Prague Cubic congestion
                                                 // control (client-only)

// Optional support of truncated Connection IDs.  If sent by a peer, the value
// is the minimum number of bytes allowed for the connection ID sent to the
// peer.
const QuicTag kTCID = TAG('T', 'C', 'I', 'D');   // Connection ID truncation.

// Multipath option.
const QuicTag kMPTH = TAG('M', 'P', 'T', 'H');   // Enable multipath.

const QuicTag kNCMR = TAG('N', 'C', 'M', 'R');   // Do not attempt connection
                                                 // migration.

// Allows disabling defer_send_in_response_to_packets in QuicConnection.
const QuicTag kDFER = TAG('D', 'F', 'E', 'R');   // Do not defer sending.
const QuicTag kCDFR = TAG('C', 'D', 'F', 'R');   // Defer sending on client.

// Pacing options.
const QuicTag kNPCO = TAG('N', 'P', 'C', 'O');  // No pacing offload.
const QuicTag kRNIB = TAG('R', 'N', 'I', 'B');  // Remove non-initial burst.

// Enable bandwidth resumption experiment.
const QuicTag kBWRE = TAG('B', 'W', 'R', 'E');  // Bandwidth resumption.
const QuicTag kBWMX = TAG('B', 'W', 'M', 'X');  // Max bandwidth resumption.
const QuicTag kBWID = TAG('B', 'W', 'I', 'D');  // Send bandwidth when idle.
const QuicTag kBWI1 = TAG('B', 'W', 'I', '1');  // Resume bandwidth experiment 1
const QuicTag kBWRS = TAG('B', 'W', 'R', 'S');  // Server bandwidth resumption.
const QuicTag kBWS2 = TAG('B', 'W', 'S', '2');  // Server bw resumption v2.
const QuicTag kBWS3 = TAG('B', 'W', 'S', '3');  // QUIC Initial CWND - Control.
const QuicTag kBWS4 = TAG('B', 'W', 'S', '4');  // QUIC Initial CWND - Enabled.
const QuicTag kBWS5 = TAG('B', 'W', 'S', '5');  // QUIC Initial CWND up and down
const QuicTag kBWS6 = TAG('B', 'W', 'S', '6');  // QUIC Initial CWND - Enabled
                                                // with 0.5 * default
                                                // multiplier.
const QuicTag kBWP0 = TAG('B', 'W', 'P', '0');  // QUIC Initial CWND - SPDY
                                                // priority 0.
const QuicTag kBWP1 = TAG('B', 'W', 'P', '1');  // QUIC Initial CWND - SPDY
                                                // priorities 0 and 1.
const QuicTag kBWP2 = TAG('B', 'W', 'P', '2');  // QUIC Initial CWND - SPDY
                                                // priorities 0, 1 and 2.
const QuicTag kBWP3 = TAG('B', 'W', 'P', '3');  // QUIC Initial CWND - SPDY
                                                // priorities 0, 1, 2 and 3.
const QuicTag kBWP4 = TAG('B', 'W', 'P', '4');  // QUIC Initial CWND - SPDY
                                                // priorities >= 0, 1, 2, 3 and
                                                // 4.
const QuicTag kBWG4 = TAG('B', 'W', 'G', '4');  // QUIC Initial CWND -
                                                // Bandwidth model 1.
const QuicTag kBWG7 = TAG('B', 'W', 'G', '7');  // QUIC Initial CWND -
                                                // Bandwidth model 2.
const QuicTag kBWG8 = TAG('B', 'W', 'G', '8');  // QUIC Initial CWND -
                                                // Bandwidth model 3.
const QuicTag kBWS7 = TAG('B', 'W', 'S', '7');  // QUIC Initial CWND - Enabled
                                                // with 0.75 * default
                                                // multiplier.
const QuicTag kBWM3 = TAG('B', 'W', 'M', '3');  // Consider overshooting if
                                                // bytes lost after bandwidth
                                                // resumption * 3 > IW.
const QuicTag kBWM4 = TAG('B', 'W', 'M', '4');  // Consider overshooting if
                                                // bytes lost after bandwidth
                                                // resumption * 4 > IW.
const QuicTag kICW1 = TAG('I', 'C', 'W', '1');  // Max initial congestion window
                                                // 100.
const QuicTag kDTOS = TAG('D', 'T', 'O', 'S');  // Enable overshooting
                                                // detection.

const QuicTag kFIDT = TAG('F', 'I', 'D', 'T');  // Extend idle timer by PTO
                                                // instead of the whole idle
                                                // timeout.

const QuicTag k3AFF = TAG('3', 'A', 'F', 'F');  // 3 anti amplification factor.
const QuicTag k10AF = TAG('1', '0', 'A', 'F');  // 10 anti amplification factor.

// Enable path MTU discovery experiment.
const QuicTag kMTUH = TAG('M', 'T', 'U', 'H');  // High-target MTU discovery.
const QuicTag kMTUL = TAG('M', 'T', 'U', 'L');  // Low-target MTU discovery.

const QuicTag kNSLC = TAG('N', 'S', 'L', 'C');  // Always send connection close
                                                // for idle timeout.

// Enable application-driven pacing experiment.
const QuicTag kADP0 = TAG('A', 'D', 'P', '0');  // Enable App-Driven Pacing.

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
const QuicTag kMIBS = TAG('M', 'I', 'D', 'S');   // Max incoming bidi streams
const QuicTag kMIUS = TAG('M', 'I', 'U', 'S');   // Max incoming unidi streams
const QuicTag kADE  = TAG('A', 'D', 'E', 0);     // Ack Delay Exponent (IETF
                                                 // QUIC ACK Frame Only).
const QuicTag kIRTT = TAG('I', 'R', 'T', 'T');   // Estimated initial RTT in us.
const QuicTag kTRTT = TAG('T', 'R', 'T', 'T');   // If server receives an rtt
                                                 // from an address token, set
                                                 // it as the initial rtt.
const QuicTag kSNI  = TAG('S', 'N', 'I', '\0');  // Server name
                                                 // indication
const QuicTag kPUBS = TAG('P', 'U', 'B', 'S');   // Public key values
const QuicTag kSCID = TAG('S', 'C', 'I', 'D');   // Server config id
const QuicTag kORBT = TAG('O', 'B', 'I', 'T');   // Server orbit.
const QuicTag kPDMD = TAG('P', 'D', 'M', 'D');   // Proof demand.
const QuicTag kPROF = TAG('P', 'R', 'O', 'F');   // Proof (signature).
const QuicTag kCCRT = TAG('C', 'C', 'R', 'T');   // Cached certificate
const QuicTag kEXPY = TAG('E', 'X', 'P', 'Y');   // Expiry
const QuicTag kSTTL = TAG('S', 'T', 'T', 'L');   // Server Config TTL
const QuicTag kSFCW = TAG('S', 'F', 'C', 'W');   // Initial stream flow control
                                                 // receive window.
const QuicTag kCFCW = TAG('C', 'F', 'C', 'W');   // Initial session/connection
                                                 // flow control receive window.
const QuicTag kUAID = TAG('U', 'A', 'I', 'D');   // Client's User Agent ID.
const QuicTag kXLCT = TAG('X', 'L', 'C', 'T');   // Expected leaf certificate.

const QuicTag kQNZ2 = TAG('Q', 'N', 'Z', '2');   // Turn off QUIC crypto 0-RTT.

const QuicTag kMAD  = TAG('M', 'A', 'D', 0);     // Max Ack Delay (IETF QUIC)

const QuicTag kIGNP = TAG('I', 'G', 'N', 'P');   // Do not use PING only packet
                                                 // for RTT measure or
                                                 // congestion control.

const QuicTag kSRWP = TAG('S', 'R', 'W', 'P');   // Enable retransmittable on
                                                 // wire PING (ROWP) on the
                                                 // server side.
const QuicTag kROWF = TAG('R', 'O', 'W', 'F');   // Send first 1-RTT packet on
                                                 // ROWP timeout.
const QuicTag kROWR = TAG('R', 'O', 'W', 'R');   // Send random bytes on ROWP
                                                 // timeout.
// Selective Resumption variants.
const QuicTag kGSR0 = TAG('G', 'S', 'R', '0');
const QuicTag kGSR1 = TAG('G', 'S', 'R', '1');
const QuicTag kGSR2 = TAG('G', 'S', 'R', '2');
const QuicTag kGSR3 = TAG('G', 'S', 'R', '3');

const QuicTag kNRES = TAG('N', 'R', 'E', 'S');   // No resumption

const QuicTag kINVC = TAG('I', 'N', 'V', 'C');   // Send connection close for
                                                 // INVALID_VERSION

const QuicTag kMPQC = TAG('M', 'P', 'Q', 'C');   // Multi-port QUIC connection
const QuicTag kMPQM = TAG('M', 'P', 'Q', 'M');   // Enable multi-port QUIC
                                                 // migration

// Client Hints triggers.
const QuicTag kGWCH = TAG('G', 'W', 'C', 'H');
const QuicTag kYTCH = TAG('Y', 'T', 'C', 'H');
const QuicTag kACH0 = TAG('A', 'C', 'H', '0');

// Client sends these connection options to express the intention of skipping IP
// matching when trying to send a request on active sessions.
const QuicTag kNOIP = TAG('N', 'O', 'I', 'P');
const QuicTag kNIPA = TAG('N', 'I', 'P', 'A');  // Aggressively skip IP matching

// Rejection tags
const QuicTag kRREJ = TAG('R', 'R', 'E', 'J');   // Reasons for server sending

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

// Client Hello Padding tags, for experiments.
const QuicTag kCHP1 = TAG('C', 'H', 'P', '1');   // 1-packet padding to CHLO.
const QuicTag kCHP2 = TAG('C', 'H', 'P', '2');   // 2-packet padding to CHLO.

// Stats collection tags
const QuicTag kEPID = TAG('E', 'P', 'I', 'D');  // Endpoint identifier.

const QuicTag kMCS1 = TAG('M', 'C', 'S', '1');
const QuicTag kMCS2 = TAG('M', 'C', 'S', '2');
const QuicTag kMCS3 = TAG('M', 'C', 'S', '3');
const QuicTag kMCS4 = TAG('M', 'C', 'S', '4');
const QuicTag kMCS5 = TAG('M', 'C', 'S', '5');

// Per-loop stream limit experiments
const QuicTag kSLP1 = TAG('S', 'L', 'P', '1');  // 1 new request per event loop
const QuicTag kSLP2 = TAG('S', 'L', 'P', '2');  // 2 new requests per event loop
const QuicTag kSLPF = TAG('S', 'L', 'P', 'F');  // number of new requests per
                                                // event loop according to
                                                // internal flag.

constexpr QuicTag kBSUS = TAG('B', 'S', 'U', 'S');  // Blocks server connection
                                                    // until the SETTINGS frame
                                                    // is received.

// Enable Failed Path Probe experiment
const QuicTag kFPPE = TAG('F', 'P', 'P', 'E');

// Fix timeouts experiment.
const QuicTag kFTOE = TAG('F', 'T', 'O', 'E');
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

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CRYPTO_PROTOCOL_H_
