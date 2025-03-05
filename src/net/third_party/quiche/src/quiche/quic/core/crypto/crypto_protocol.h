// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CRYPTO_PROTOCOL_H_
#define QUICHE_QUIC_CORE_CRYPTO_CRYPTO_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "quiche/quic/core/quic_tag.h"

// Version and Crypto tags are written to the wire with a big-endian
// representation of the name of the tag.  For example
// the client hello tag (CHLO) will be written as the
// following 4 bytes: 'C' 'H' 'L' 'O'.  Since it is
// stored in memory as a little endian uint32_t, we need
// to reverse the order of the bytes.
//
// This macro ensures that the name matches the definition.
#define DEFINE_STATIC_QUIC_TAG(name) \
  inline constexpr QuicTag k##name = internal::MakeStaticQuicTag(#name)

namespace quic {

namespace internal {

// Construct a QuicTag from a 4 digit string. The input array is five bytes
// because of the trailing null byte.
constexpr QuicTag MakeStaticQuicTag(const char (&input)[5]) {
  constexpr auto u8 = [](char c) { return static_cast<uint8_t>(c); };
  return static_cast<QuicTag>((u8(input[3]) << 24) | (u8(input[2]) << 16) |
                              (u8(input[1]) << 8) | u8(input[0]));
}

// A variant for three-character QUIC tags. Pads the end with null bytes.
constexpr QuicTag MakeStaticQuicTag(const char (&input)[4]) {
  const char extended_input[5] = {input[0], input[1], input[2], 0, 0};
  return MakeStaticQuicTag(extended_input);
}

}  // namespace internal

using ServerConfigID = std::string;

// The following tags have been deprecated and should not be reused:
// "1CON", "BBQ4", "NCON", "RCID", "SREJ", "TBKP", "TB10", "SCLS", "SMHL",
// "QNZR", "B2HI", "H2PR", "FIFO", "LIFO", "RRWS", "QNSP", "B2CL", "CHSP",
// "BPTE", "ACKD", "AKD2", "AKD4", "MAD1", "MAD4", "MAD5", "ACD0", "ACKQ",
// "TLPR", "CCS\0", "PDP4", "NCHP", "NBPE", "2RTO", "3RTO", "4RTO", "6RTO",
// "PDP1", "PDP2", "PDP3", "PDP5", "QLVE", "RVCM", "BBPD", "TPC\0", "AFFE"

DEFINE_STATIC_QUIC_TAG(CHLO);  // Client hello
DEFINE_STATIC_QUIC_TAG(SHLO);  // Server hello
DEFINE_STATIC_QUIC_TAG(SCFG);  // Server config
DEFINE_STATIC_QUIC_TAG(REJ);   // Reject
DEFINE_STATIC_QUIC_TAG(CETV);  // Client encrypted tag-value
                               // pairs
DEFINE_STATIC_QUIC_TAG(PRST);  // Public reset
DEFINE_STATIC_QUIC_TAG(SCUP);  // Server config update
DEFINE_STATIC_QUIC_TAG(ALPN);  // Application-layer protocol

// Key exchange methods
DEFINE_STATIC_QUIC_TAG(P256);  // ECDH, Curve P-256
DEFINE_STATIC_QUIC_TAG(C255);  // ECDH, Curve25519

// AEAD algorithms
DEFINE_STATIC_QUIC_TAG(AESG);  // AES128 + GCM-12
DEFINE_STATIC_QUIC_TAG(CC20);  // ChaCha20 + Poly1305 RFC7539

// Congestion control feedback types
DEFINE_STATIC_QUIC_TAG(QBIC);  // TCP cubic

// Connection options (COPT) values
DEFINE_STATIC_QUIC_TAG(AFCW);  // Auto-tune flow control
                               // receive windows.
DEFINE_STATIC_QUIC_TAG(IFW5);  // Set initial size
                               // of stream flow control
                               // receive window to
                               // 32KB. (2^5 KB).
DEFINE_STATIC_QUIC_TAG(IFW6);  // Set initial size
                               // of stream flow control
                               // receive window to
                               // 64KB. (2^6 KB).
DEFINE_STATIC_QUIC_TAG(IFW7);  // Set initial size
                               // of stream flow control
                               // receive window to
                               // 128KB. (2^7 KB).
DEFINE_STATIC_QUIC_TAG(IFW8);  // Set initial size
                               // of stream flow control
                               // receive window to
                               // 256KB. (2^8 KB).
DEFINE_STATIC_QUIC_TAG(IFW9);  // Set initial size
                               // of stream flow control
                               // receive window to
                               // 512KB. (2^9 KB).
DEFINE_STATIC_QUIC_TAG(IFWa);  // Set initial size
                               // of stream flow control
                               // receive window to
                               // 1MB. (2^0xa KB).
DEFINE_STATIC_QUIC_TAG(TBBR);  // Reduced Buffer Bloat TCP
DEFINE_STATIC_QUIC_TAG(1RTT);  // STARTUP in BBR for 1 RTT
DEFINE_STATIC_QUIC_TAG(2RTT);  // STARTUP in BBR for 2 RTTs
DEFINE_STATIC_QUIC_TAG(LRTT);  // Exit STARTUP in BBR on loss
DEFINE_STATIC_QUIC_TAG(BBS1);  // DEPRECATED
DEFINE_STATIC_QUIC_TAG(BBS2);  // More aggressive packet
                               // conservation in BBR STARTUP
DEFINE_STATIC_QUIC_TAG(BBS3);  // Slowstart packet
                               // conservation in BBR STARTUP
DEFINE_STATIC_QUIC_TAG(BBS4);  // DEPRECATED
DEFINE_STATIC_QUIC_TAG(BBS5);  // DEPRECATED
DEFINE_STATIC_QUIC_TAG(BBRR);  // Rate-based recovery in BBR
DEFINE_STATIC_QUIC_TAG(BBR1);  // DEPRECATED
DEFINE_STATIC_QUIC_TAG(BBR2);  // DEPRECATED
DEFINE_STATIC_QUIC_TAG(BBR3);  // Fully drain the queue once
                               // per cycle
DEFINE_STATIC_QUIC_TAG(BBR4);  // 20 RTT ack aggregation
DEFINE_STATIC_QUIC_TAG(BBR5);  // 40 RTT ack aggregation
DEFINE_STATIC_QUIC_TAG(BBR9);  // DEPRECATED
DEFINE_STATIC_QUIC_TAG(BBRA);  // Starts a new ack aggregation
                               // epoch if a full round has
                               // passed
DEFINE_STATIC_QUIC_TAG(BBRB);  // Use send rate in BBR's
                               // MaxAckHeightTracker
DEFINE_STATIC_QUIC_TAG(BBRS);  // DEPRECATED
DEFINE_STATIC_QUIC_TAG(BBQ1);  // DEPRECATED
DEFINE_STATIC_QUIC_TAG(BBQ2);  // BBRv2 with 2.885 STARTUP and
                               // DRAIN CWND gain.
DEFINE_STATIC_QUIC_TAG(BBQ3);  // BBR with ack aggregation
                               // compensation in STARTUP.
DEFINE_STATIC_QUIC_TAG(BBQ5);  // Expire ack aggregation upon
                               // bandwidth increase in
                               // STARTUP.
DEFINE_STATIC_QUIC_TAG(BBQ6);  // Reduce STARTUP gain to 25%
                               // more than BW increase.
DEFINE_STATIC_QUIC_TAG(BBQ7);  // Reduce bw_lo by
                               // bytes_lost/min_rtt.
DEFINE_STATIC_QUIC_TAG(BBQ8);  // Reduce bw_lo by
                               // bw_lo * bytes_lost/inflight
DEFINE_STATIC_QUIC_TAG(BBQ9);  // Reduce bw_lo by
                               // bw_lo * bytes_lost/cwnd
DEFINE_STATIC_QUIC_TAG(BBQ0);  // Increase bytes_acked in
                               // PROBE_UP when app limited.
DEFINE_STATIC_QUIC_TAG(BBHI);  // Increase inflight_hi in
                               // PROBE_UP if ever inflight_hi
                               // limited in round
DEFINE_STATIC_QUIC_TAG(RENO);  // Reno Congestion Control
DEFINE_STATIC_QUIC_TAG(BYTE);  // TCP cubic or reno in bytes
DEFINE_STATIC_QUIC_TAG(IW03);  // Force ICWND to 3
DEFINE_STATIC_QUIC_TAG(IW10);  // Force ICWND to 10
DEFINE_STATIC_QUIC_TAG(IW20);  // Force ICWND to 20
DEFINE_STATIC_QUIC_TAG(IW50);  // Force ICWND to 50
DEFINE_STATIC_QUIC_TAG(B2ON);  // Enable BBRv2
DEFINE_STATIC_QUIC_TAG(B2NA);  // For BBRv2, do not add ack
                               // height to queueing threshold
DEFINE_STATIC_QUIC_TAG(B2NE);  // For BBRv2, always exit
                               // STARTUP on loss, even if
                               // bandwidth growth exceeds
                               // threshold.
DEFINE_STATIC_QUIC_TAG(B2RP);  // For BBRv2, run PROBE_RTT on
                               // the regular schedule
DEFINE_STATIC_QUIC_TAG(B2LO);  // Ignore inflight_lo in BBR2
DEFINE_STATIC_QUIC_TAG(B2HR);  // 15% inflight_hi headroom.
DEFINE_STATIC_QUIC_TAG(B2SL);  // When exiting STARTUP due to
                               // loss, set inflight_hi to the
                               // max of bdp and max bytes
                               // delivered in round.
DEFINE_STATIC_QUIC_TAG(B2H2);  // When exiting PROBE_UP due to
                               // loss, set inflight_hi to the
                               // max of inflight@send and max
                               // bytes delivered in round.
DEFINE_STATIC_QUIC_TAG(B2RC);  // Disable Reno-coexistence for
                               // BBR2.
DEFINE_STATIC_QUIC_TAG(BSAO);  // Avoid Overestimation in
                               // Bandwidth Sampler with ack
                               // aggregation
DEFINE_STATIC_QUIC_TAG(B2DL);  // Increase inflight_hi based
                               // on delievered, not inflight.
DEFINE_STATIC_QUIC_TAG(B201);  // DEPRECATED
DEFINE_STATIC_QUIC_TAG(B202);  // Do not exit PROBE_UP if
                               // inflight dips below 1.25*BW.
DEFINE_STATIC_QUIC_TAG(B203);  // Ignore inflight_hi until
                               // PROBE_UP is exited.
DEFINE_STATIC_QUIC_TAG(B204);  // Reduce extra acked when
                               // MaxBW incrases.
DEFINE_STATIC_QUIC_TAG(B205);  // Add extra acked to CWND in
                               // STARTUP.
DEFINE_STATIC_QUIC_TAG(B206);  // Exit STARTUP after 2 losses.
DEFINE_STATIC_QUIC_TAG(B207);  // Exit STARTUP on persistent
                               // queue
DEFINE_STATIC_QUIC_TAG(BB2U);  // Exit PROBE_UP on
                               // min_bytes_in_flight for two
                               // rounds in a row.
DEFINE_STATIC_QUIC_TAG(BB2S);  // Exit STARTUP on
                               // min_bytes_in_flight for two
                               // rounds in a row.
DEFINE_STATIC_QUIC_TAG(NTLP);  // No tail loss probe
DEFINE_STATIC_QUIC_TAG(1TLP);  // 1 tail loss probe
DEFINE_STATIC_QUIC_TAG(1RTO);  // Send 1 packet upon RTO
DEFINE_STATIC_QUIC_TAG(NRTO);  // CWND reduction on loss
DEFINE_STATIC_QUIC_TAG(TIME);  // Time based loss detection
DEFINE_STATIC_QUIC_TAG(ATIM);  // Adaptive time loss detection
DEFINE_STATIC_QUIC_TAG(MIN1);  // Min CWND of 1 packet
DEFINE_STATIC_QUIC_TAG(MIN4);  // Min CWND of 4 packets,
                               // with a min rate of 1 BDP.
DEFINE_STATIC_QUIC_TAG(MAD0);  // Ignore ack delay
DEFINE_STATIC_QUIC_TAG(MAD2);  // No min TLP
DEFINE_STATIC_QUIC_TAG(MAD3);  // No min RTO
DEFINE_STATIC_QUIC_TAG(1ACK);  // 1 fast ack for reordering
DEFINE_STATIC_QUIC_TAG(AKD3);  // Ack decimation style acking
                               // with 1/8 RTT acks.
DEFINE_STATIC_QUIC_TAG(AKDU);  // Unlimited number of packets
                               // received before acking
DEFINE_STATIC_QUIC_TAG(AFF1);  // Use SRTT in building
                               // AckFrequencyFrame.
DEFINE_STATIC_QUIC_TAG(AFF2);  // Send AckFrequencyFrame upon
                               // handshake completion.
DEFINE_STATIC_QUIC_TAG(SSLR);  // Slow Start Large Reduction.
DEFINE_STATIC_QUIC_TAG(NPRR);  // Pace at unity instead of PRR
DEFINE_STATIC_QUIC_TAG(5RTO);  // Close connection on 5 RTOs
DEFINE_STATIC_QUIC_TAG(CBHD);  // Client only blackhole
                               // detection.
DEFINE_STATIC_QUIC_TAG(NBHD);  // No blackhole detection.
DEFINE_STATIC_QUIC_TAG(CONH);  // Conservative Handshake
                               // Retransmissions.
DEFINE_STATIC_QUIC_TAG(LFAK);  // Don't invoke FACK on the
                               // first ack.
DEFINE_STATIC_QUIC_TAG(STMP);  // DEPRECATED
DEFINE_STATIC_QUIC_TAG(EACK);  // Bundle ack-eliciting frame
                               // with an ACK after PTO/RTO

DEFINE_STATIC_QUIC_TAG(ILD0);  // IETF style loss detection
                               // (default with 1/8 RTT time
                               // threshold)
DEFINE_STATIC_QUIC_TAG(ILD1);  // IETF style loss detection
                               // with 1/4 RTT time threshold
DEFINE_STATIC_QUIC_TAG(ILD2);  // IETF style loss detection
                               // with adaptive packet
                               // threshold
DEFINE_STATIC_QUIC_TAG(ILD3);  // IETF style loss detection
                               // with 1/4 RTT time threshold
                               // and adaptive packet
                               // threshold
DEFINE_STATIC_QUIC_TAG(ILD4);  // IETF style loss detection
                               // with both adaptive time
                               // threshold (default 1/4 RTT)
                               // and adaptive packet
                               // threshold
DEFINE_STATIC_QUIC_TAG(RUNT);  // No packet threshold loss
                               // detection for "runt" packet.
DEFINE_STATIC_QUIC_TAG(NSTP);  // No stop waiting frames.
DEFINE_STATIC_QUIC_TAG(NRTT);  // Ignore initial RTT

DEFINE_STATIC_QUIC_TAG(1PTO);  // Send 1 packet upon PTO.
DEFINE_STATIC_QUIC_TAG(2PTO);  // Send 2 packets upon PTO.

DEFINE_STATIC_QUIC_TAG(6PTO);  // Closes connection on 6
                               // consecutive PTOs.
DEFINE_STATIC_QUIC_TAG(7PTO);  // Closes connection on 7
                               // consecutive PTOs.
DEFINE_STATIC_QUIC_TAG(8PTO);  // Closes connection on 8
                               // consecutive PTOs.
DEFINE_STATIC_QUIC_TAG(PTOS);  // Skip packet number before
                               // sending the last PTO.
DEFINE_STATIC_QUIC_TAG(PTOA);  // Do not add max ack delay
                               // when computing PTO timeout
                               // if an immediate ACK is
                               // expected.
DEFINE_STATIC_QUIC_TAG(PEB1);  // Start exponential backoff
                               // since 1st PTO.
DEFINE_STATIC_QUIC_TAG(PEB2);  // Start exponential backoff
                               // since 2nd PTO.
DEFINE_STATIC_QUIC_TAG(PVS1);  // Use 2 * rttvar when
                               // calculating PTO timeout.
DEFINE_STATIC_QUIC_TAG(PAG1);  // Make 1st PTO more aggressive
DEFINE_STATIC_QUIC_TAG(PAG2);  // Make first 2 PTOs more
                               // aggressive
DEFINE_STATIC_QUIC_TAG(PSDA);  // Use standard deviation when
                               // calculating PTO timeout.
DEFINE_STATIC_QUIC_TAG(PLE1);  // Arm the 1st PTO with
                               // earliest in flight sent time
                               // and at least 0.5*srtt from
                               // last sent packet.
DEFINE_STATIC_QUIC_TAG(PLE2);  // Arm the 1st PTO with
                               // earliest in flight sent time
                               // and at least 1.5*srtt from
                               // last sent packet.
DEFINE_STATIC_QUIC_TAG(APTO);  // Use 1.5 * initial RTT before
                               // any RTT sample is available.

DEFINE_STATIC_QUIC_TAG(ELDT);  // Enable Loss Detection Tuning

DEFINE_STATIC_QUIC_TAG(SPAD);  // Use server preferred address
DEFINE_STATIC_QUIC_TAG(SPA2);  // Start validating server
                               // preferred address once it is
                               // received. Send all coalesced
                               // packets to both addresses.
DEFINE_STATIC_QUIC_TAG(EVMB);

DEFINE_STATIC_QUIC_TAG(CRNT);

DEFINE_STATIC_QUIC_TAG(PRGC);  // Prague Cubic congestion
                               // control (client-only)
DEFINE_STATIC_QUIC_TAG(CQBC);  // Client-only Cubic congestion control. Used
                               // for a control in the PRGC experiment.

// Optional support of truncated Connection IDs.  If sent by a peer, the value
// is the minimum number of bytes allowed for the connection ID sent to the
// peer.
DEFINE_STATIC_QUIC_TAG(TCID);  // Connection ID truncation.

// Multipath option.
DEFINE_STATIC_QUIC_TAG(MPTH);  // Enable multipath.

DEFINE_STATIC_QUIC_TAG(NCMR);  // Do not attempt connection
                               // migration.

// Allows disabling defer_send_in_response_to_packets in QuicConnection.
DEFINE_STATIC_QUIC_TAG(DFER);  // Do not defer sending.
DEFINE_STATIC_QUIC_TAG(CDFR);  // Defer sending on client.

// Pacing options.
DEFINE_STATIC_QUIC_TAG(NPCO);  // No pacing offload.
DEFINE_STATIC_QUIC_TAG(RNIB);  // Remove non-initial burst.

// Enable bandwidth resumption experiment.
DEFINE_STATIC_QUIC_TAG(BWRE);  // Bandwidth resumption.
DEFINE_STATIC_QUIC_TAG(BWMX);  // Max bandwidth resumption.
DEFINE_STATIC_QUIC_TAG(BWID);  // Send bandwidth when idle.
DEFINE_STATIC_QUIC_TAG(BWI1);  // Resume bandwidth experiment 1
DEFINE_STATIC_QUIC_TAG(BWRS);  // Server bandwidth resumption.
DEFINE_STATIC_QUIC_TAG(BWS2);  // Server bw resumption v2.
DEFINE_STATIC_QUIC_TAG(BWS3);  // QUIC Initial CWND - Control.
DEFINE_STATIC_QUIC_TAG(BWS4);  // QUIC Initial CWND - Enabled.
DEFINE_STATIC_QUIC_TAG(BWS5);  // QUIC Initial CWND up and down
DEFINE_STATIC_QUIC_TAG(BWS6);  // QUIC Initial CWND - Enabled
                               // with 0.5 * default
                               // multiplier.
DEFINE_STATIC_QUIC_TAG(BWP0);  // QUIC Initial CWND - SPDY
                               // priority 0.
DEFINE_STATIC_QUIC_TAG(BWP1);  // QUIC Initial CWND - SPDY
                               // priorities 0 and 1.
DEFINE_STATIC_QUIC_TAG(BWP2);  // QUIC Initial CWND - SPDY
                               // priorities 0, 1 and 2.
DEFINE_STATIC_QUIC_TAG(BWP3);  // QUIC Initial CWND - SPDY
                               // priorities 0, 1, 2 and 3.
DEFINE_STATIC_QUIC_TAG(BWP4);  // QUIC Initial CWND - SPDY
                               // priorities >= 0, 1, 2, 3 and
                               // 4.
DEFINE_STATIC_QUIC_TAG(BWG4);  // QUIC Initial CWND -
                               // Bandwidth model 1.
DEFINE_STATIC_QUIC_TAG(BWG7);  // QUIC Initial CWND -
                               // Bandwidth model 2.
DEFINE_STATIC_QUIC_TAG(BWG8);  // QUIC Initial CWND -
                               // Bandwidth model 3.
DEFINE_STATIC_QUIC_TAG(BWS7);  // QUIC Initial CWND - Enabled
                               // with 0.75 * default
                               // multiplier.
DEFINE_STATIC_QUIC_TAG(BWM3);  // Consider overshooting if
                               // bytes lost after bandwidth
                               // resumption * 3 > IW.
DEFINE_STATIC_QUIC_TAG(BWM4);  // Consider overshooting if
                               // bytes lost after bandwidth
                               // resumption * 4 > IW.
DEFINE_STATIC_QUIC_TAG(ICW1);  // Max initial congestion window
                               // 100.
DEFINE_STATIC_QUIC_TAG(DTOS);  // Enable overshooting
                               // detection.

DEFINE_STATIC_QUIC_TAG(FIDT);  // Extend idle timer by PTO
                               // instead of the whole idle
                               // timeout.

DEFINE_STATIC_QUIC_TAG(3AFF);  // 3 anti amplification factor.
DEFINE_STATIC_QUIC_TAG(10AF);  // 10 anti amplification factor.

// Enable path MTU discovery experiment.
DEFINE_STATIC_QUIC_TAG(MTUH);  // High-target MTU discovery.
DEFINE_STATIC_QUIC_TAG(MTUL);  // Low-target MTU discovery.

DEFINE_STATIC_QUIC_TAG(NSLC);  // Always send connection close
                               // for idle timeout.

// Enable application-driven pacing experiment.
DEFINE_STATIC_QUIC_TAG(ADP0);  // Enable App-Driven Pacing.

// Proof types (i.e. certificate types)
// NOTE: although it would be silly to do so, specifying both kX509 and kX59R
// is allowed and is equivalent to specifying only kX509.
DEFINE_STATIC_QUIC_TAG(X509);  // X.509 certificate, all key
                               // types
DEFINE_STATIC_QUIC_TAG(X59R);  // X.509 certificate, RSA keys
                               // only
DEFINE_STATIC_QUIC_TAG(CHID);  // Channel ID.

// Client hello tags
DEFINE_STATIC_QUIC_TAG(VER);   // Version
DEFINE_STATIC_QUIC_TAG(NONC);  // The client's nonce
DEFINE_STATIC_QUIC_TAG(NONP);  // The client's proof nonce
DEFINE_STATIC_QUIC_TAG(KEXS);  // Key exchange methods
DEFINE_STATIC_QUIC_TAG(AEAD);  // Authenticated
                               // encryption algorithms
DEFINE_STATIC_QUIC_TAG(COPT);  // Connection options
DEFINE_STATIC_QUIC_TAG(CLOP);  // Client connection options
DEFINE_STATIC_QUIC_TAG(ICSL);  // Idle network timeout
DEFINE_STATIC_QUIC_TAG(MIDS);  // Max incoming bidi streams
DEFINE_STATIC_QUIC_TAG(MIUS);  // Max incoming unidi streams
DEFINE_STATIC_QUIC_TAG(ADE);   // Ack Delay Exponent (IETF
                               // QUIC ACK Frame Only).
DEFINE_STATIC_QUIC_TAG(IRTT);  // Estimated initial RTT in us.
DEFINE_STATIC_QUIC_TAG(TRTT);  // If server receives an rtt
                               // from an address token, set
                               // it as the initial rtt.
DEFINE_STATIC_QUIC_TAG(SNI);   // Server name
                               // indication
DEFINE_STATIC_QUIC_TAG(PUBS);  // Public key values
DEFINE_STATIC_QUIC_TAG(SCID);  // Server config id
DEFINE_STATIC_QUIC_TAG(OBIT);  // Server orbit.
DEFINE_STATIC_QUIC_TAG(PDMD);  // Proof demand.
DEFINE_STATIC_QUIC_TAG(PROF);  // Proof (signature).
DEFINE_STATIC_QUIC_TAG(CCRT);  // Cached certificate
DEFINE_STATIC_QUIC_TAG(EXPY);  // Expiry
DEFINE_STATIC_QUIC_TAG(STTL);  // Server Config TTL
DEFINE_STATIC_QUIC_TAG(SFCW);  // Initial stream flow control
                               // receive window.
DEFINE_STATIC_QUIC_TAG(CFCW);  // Initial session/connection
                               // flow control receive window.
DEFINE_STATIC_QUIC_TAG(UAID);  // Client's User Agent ID.
DEFINE_STATIC_QUIC_TAG(XLCT);  // Expected leaf certificate.

DEFINE_STATIC_QUIC_TAG(QNZ2);  // Turn off QUIC crypto 0-RTT.

DEFINE_STATIC_QUIC_TAG(MAD);  // Max Ack Delay (IETF QUIC)

DEFINE_STATIC_QUIC_TAG(IGNP);  // Do not use PING only packet
                               // for RTT measure or
                               // congestion control.

DEFINE_STATIC_QUIC_TAG(SRWP);  // Enable retransmittable on
                               // wire PING (ROWP) on the
                               // server side.
DEFINE_STATIC_QUIC_TAG(ROWF);  // Send first 1-RTT packet on
                               // ROWP timeout.
DEFINE_STATIC_QUIC_TAG(ROWR);  // Send random bytes on ROWP
                               // timeout.
// Selective Resumption variants.
DEFINE_STATIC_QUIC_TAG(GSR0);
DEFINE_STATIC_QUIC_TAG(GSR1);
DEFINE_STATIC_QUIC_TAG(GSR2);
DEFINE_STATIC_QUIC_TAG(GSR3);

DEFINE_STATIC_QUIC_TAG(NRES);  // No resumption

DEFINE_STATIC_QUIC_TAG(INVC);  // Send connection close for
                               // INVALID_VERSION

DEFINE_STATIC_QUIC_TAG(MPQC);  // Multi-port QUIC connection
DEFINE_STATIC_QUIC_TAG(MPQM);  // Enable multi-port QUIC
                               // migration

// Client Hints triggers.
DEFINE_STATIC_QUIC_TAG(GWCH);
DEFINE_STATIC_QUIC_TAG(YTCH);
DEFINE_STATIC_QUIC_TAG(ACH0);

// Client sends these connection options to express the intention of skipping IP
// matching when trying to send a request on active sessions.
DEFINE_STATIC_QUIC_TAG(NOIP);
DEFINE_STATIC_QUIC_TAG(NIPA);  // Aggressively skip IP matching

// Rejection tags
DEFINE_STATIC_QUIC_TAG(RREJ);  // Reasons for server sending

// Server hello tags
DEFINE_STATIC_QUIC_TAG(CADR);  // Client IP address and port
DEFINE_STATIC_QUIC_TAG(ASAD);  // Alternate Server IP address
                               // and port.
DEFINE_STATIC_QUIC_TAG(SRST);  // Stateless reset token used
                               // in IETF public reset packet

// CETV tags
DEFINE_STATIC_QUIC_TAG(CIDK);  // ChannelID key
DEFINE_STATIC_QUIC_TAG(CIDS);  // ChannelID signature

// Public reset tags
DEFINE_STATIC_QUIC_TAG(RNON);  // Public reset nonce proof
DEFINE_STATIC_QUIC_TAG(RSEQ);  // Rejected packet number

// Universal tags
DEFINE_STATIC_QUIC_TAG(PAD);  // Padding

// Client Hello Padding tags, for experiments.
DEFINE_STATIC_QUIC_TAG(CHP1);  // 1-packet padding to CHLO.
DEFINE_STATIC_QUIC_TAG(CHP2);  // 2-packet padding to CHLO.

// Stats collection tags
DEFINE_STATIC_QUIC_TAG(EPID);  // Endpoint identifier.

DEFINE_STATIC_QUIC_TAG(MCS1);
DEFINE_STATIC_QUIC_TAG(MCS2);
DEFINE_STATIC_QUIC_TAG(MCS3);
DEFINE_STATIC_QUIC_TAG(MCS4);
DEFINE_STATIC_QUIC_TAG(MCS5);

// Per-loop stream limit experiments
DEFINE_STATIC_QUIC_TAG(SLP1);  // 1 new request per event loop
DEFINE_STATIC_QUIC_TAG(SLP2);  // 2 new requests per event loop
DEFINE_STATIC_QUIC_TAG(SLPF);  // number of new requests per
                               // event loop according to
                               // internal flag.

DEFINE_STATIC_QUIC_TAG(BSUS);  // Blocks server connection
                               // until the SETTINGS frame
                               // is received.

// Enable Failed Path Probe experiment
DEFINE_STATIC_QUIC_TAG(FPPE);

// Fix timeouts experiment.
DEFINE_STATIC_QUIC_TAG(FTOE);

#undef DEFINE_STATIC_QUIC_TAG

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
inline constexpr QuicTag kServerNonceTag =
    internal::MakeStaticQuicTag("SNO\0");  // The server's nonce
inline constexpr QuicTag kSourceAddressTokenTag =
    internal::MakeStaticQuicTag("STK\0");  // Source-address token
inline constexpr QuicTag kCertificateTag =
    internal::MakeStaticQuicTag("CRT\xFF");  // Certificate chain
inline constexpr QuicTag kCertificateSCTTag = internal::MakeStaticQuicTag(
    "CSCT");  // Signed cert timestamp (RFC6962) of leaf cert.

// Max number of entries in a message.
inline constexpr size_t kMaxEntries = 128;

// Size in bytes of the connection nonce.
inline constexpr size_t kNonceSize = 32;

// Number of bytes in an orbit value.
inline constexpr size_t kOrbitSize = 8;

// kProofSignatureLabel is prepended to the CHLO hash and server configs before
// signing to avoid any cross-protocol attacks on the signature.
inline constexpr char kProofSignatureLabel[] =
    "QUIC CHLO and server config signature";

// kClientHelloMinimumSize is the minimum size of a client hello. Client hellos
// will have PAD tags added in order to ensure this minimum is met and client
// hellos smaller than this will be an error. This minimum size reduces the
// amplification factor of any mirror DoS attack.
//
// A client may pad an inchoate client hello to a size larger than
// kClientHelloMinimumSize to make it more likely to receive a complete
// rejection message.
inline constexpr size_t kClientHelloMinimumSize = 1024;

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CRYPTO_PROTOCOL_H_
