// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CONNECTION_STATS_H_
#define QUICHE_QUIC_CORE_QUIC_CONNECTION_STATS_H_

#include <cstdint>
#include <ostream>

#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_time_accumulator.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Structure to hold stats for a QuicConnection.
struct QUICHE_EXPORT QuicConnectionStats {
  QUICHE_EXPORT friend std::ostream& operator<<(std::ostream& os,
                                                const QuicConnectionStats& s);

  QuicByteCount bytes_sent = 0;  // Includes retransmissions.
  QuicPacketCount packets_sent = 0;
  // Non-retransmitted bytes sent in a stream frame.
  QuicByteCount stream_bytes_sent = 0;
  // Packets serialized and discarded before sending.
  QuicPacketCount packets_discarded = 0;

  // These include version negotiation and public reset packets, which do not
  // have packet numbers or frame data.
  QuicByteCount bytes_received = 0;  // Includes duplicate data for a stream.
  // Includes packets which were not processable.
  QuicPacketCount packets_received = 0;
  // Excludes packets which were not processable.
  QuicPacketCount packets_processed = 0;
  QuicByteCount stream_bytes_received = 0;  // Bytes received in a stream frame.

  QuicByteCount bytes_retransmitted = 0;
  QuicPacketCount packets_retransmitted = 0;

  QuicByteCount bytes_spuriously_retransmitted = 0;
  QuicPacketCount packets_spuriously_retransmitted = 0;
  // Number of packets abandoned as lost by the loss detection algorithm.
  QuicPacketCount packets_lost = 0;
  QuicPacketCount packet_spuriously_detected_lost = 0;

  // The sum of loss detection response times of all lost packets, in number of
  // round trips.
  // Given a packet detected as lost:
  //   T(S)                            T(1Rtt)    T(D)
  //     |_________________________________|_______|
  // Where
  //   T(S) is the time when the packet is sent.
  //   T(1Rtt) is one rtt after T(S), using the rtt at the time of detection.
  //   T(D) is the time of detection, i.e. when the packet is declared as lost.
  // The loss detection response time is defined as
  //     (T(D) - T(S)) / (T(1Rtt) - T(S))
  //
  // The average loss detection response time is this number divided by
  // |packets_lost|. Smaller result means detection is faster.
  float total_loss_detection_response_time = 0.0;

  // Number of times this connection went through the slow start phase.
  uint32_t slowstart_count = 0;
  // Number of round trips spent in slow start.
  uint32_t slowstart_num_rtts = 0;
  // Number of packets sent in slow start.
  QuicPacketCount slowstart_packets_sent = 0;
  // Number of bytes sent in slow start.
  QuicByteCount slowstart_bytes_sent = 0;
  // Number of packets lost exiting slow start.
  QuicPacketCount slowstart_packets_lost = 0;
  // Number of bytes lost exiting slow start.
  QuicByteCount slowstart_bytes_lost = 0;
  // Time spent in slow start. Populated for BBRv1 and BBRv2.
  QuicTimeAccumulator slowstart_duration;

  // Number of PROBE_BW cycles. Populated for BBRv1 and BBRv2.
  uint32_t bbr_num_cycles = 0;
  // Number of PROBE_BW cycles shortened for reno coexistence. BBRv2 only.
  uint32_t bbr_num_short_cycles_for_reno_coexistence = 0;
  // Whether BBR exited STARTUP due to excessive loss. Populated for BBRv1 and
  // BBRv2.
  bool bbr_exit_startup_due_to_loss = false;

  QuicPacketCount packets_dropped = 0;  // Duplicate or less than least unacked.

  // Packets that failed to decrypt when they were first received,
  // before the handshake was complete.
  QuicPacketCount undecryptable_packets_received_before_handshake_complete = 0;

  size_t crypto_retransmit_count = 0;
  // Count of times the loss detection alarm fired.  At least one packet should
  // be lost when the alarm fires.
  size_t loss_timeout_count = 0;
  size_t tlp_count = 0;
  size_t rto_count = 0;  // Count of times the rto timer fired.
  size_t pto_count = 0;

  int64_t min_rtt_us = 0;                 // Minimum RTT in microseconds.
  int64_t srtt_us = 0;                    // Smoothed RTT in microseconds.
  int64_t cwnd_bootstrapping_rtt_us = 0;  // RTT used in cwnd_bootstrapping.
  // The connection's |long_term_mtu_| used for sending packets, populated by
  // QuicConnection::GetStats().
  QuicByteCount egress_mtu = 0;
  // The maximum |long_term_mtu_| the connection ever used.
  QuicByteCount max_egress_mtu = 0;
  // Size of the largest packet received from the peer, populated by
  // QuicConnection::GetStats().
  QuicByteCount ingress_mtu = 0;
  QuicBandwidth estimated_bandwidth = QuicBandwidth::Zero();

  // Reordering stats for received packets.
  // Number of packets received out of packet number order.
  QuicPacketCount packets_reordered = 0;
  // Maximum reordering observed in packet number space.
  QuicPacketCount max_sequence_reordering = 0;
  // Maximum reordering observed in microseconds
  int64_t max_time_reordering_us = 0;

  // Maximum sequence reordering observed from acked packets.
  QuicPacketCount sent_packets_max_sequence_reordering = 0;
  // Number of times that a packet is not detected as lost per reordering_shift,
  // but would have been if the reordering_shift increases by one.
  QuicPacketCount sent_packets_num_borderline_time_reorderings = 0;

  // The following stats are used only in TcpCubicSender.
  // The number of loss events from TCP's perspective.  Each loss event includes
  // one or more lost packets.
  uint32_t tcp_loss_events = 0;

  // Creation time, as reported by the QuicClock.
  QuicTime connection_creation_time = QuicTime::Zero();

  // Handshake completion time.
  QuicTime handshake_completion_time = QuicTime::Zero();

  uint64_t blocked_frames_received = 0;
  uint64_t blocked_frames_sent = 0;

  // Number of connectivity probing packets received by this connection.
  uint64_t num_connectivity_probing_received = 0;

  // Number of PATH_RESPONSE frame received by this connection.
  uint64_t num_path_response_received = 0;

  // Whether a RETRY packet was successfully processed.
  bool retry_packet_processed = false;

  // Number of received coalesced packets.
  uint64_t num_coalesced_packets_received = 0;
  // Number of successfully processed coalesced packets.
  uint64_t num_coalesced_packets_processed = 0;
  // Number of ack aggregation epochs. For the same number of bytes acked, the
  // smaller this value, the more ack aggregation is going on.
  uint64_t num_ack_aggregation_epochs = 0;

  // Whether overshooting is detected (and pacing rate decreases) during start
  // up with network parameters adjusted.
  bool overshooting_detected_with_network_parameters_adjusted = false;

  // Whether there is any non app-limited bandwidth sample.
  bool has_non_app_limited_sample = false;

  // Packet number of first decrypted packet.
  QuicPacketNumber first_decrypted_packet;

  // Max consecutive retransmission timeout before making forward progress.
  size_t max_consecutive_rto_with_forward_progress = 0;

  // Number of times when the connection tries to send data but gets throttled
  // by amplification factor.
  size_t num_amplification_throttling = 0;

  // Number of key phase updates that have occurred. In the case of a locally
  // initiated key update, this is incremented when the keys are updated, before
  // the peer has acknowledged the key update.
  uint32_t key_update_count = 0;

  // Counts the number of undecryptable packets received across all keys. Does
  // not include packets where a decryption key for that level was absent.
  QuicPacketCount num_failed_authentication_packets_received = 0;

  // Counts the number of QUIC+TLS 0-RTT packets received after 0-RTT decrypter
  // was discarded, only on server connections.
  QuicPacketCount
      num_tls_server_zero_rtt_packets_received_after_discarding_decrypter = 0;

  // Counts the number of packets received with each Explicit Congestion
  // Notification (ECN) codepoint, except Not-ECT. There is one counter across
  // all packet number spaces.
  QuicEcnCounts num_ecn_marks_received;

  // Counts the number of ACK frames sent with ECN counts.
  QuicPacketCount num_ack_frames_sent_with_ecn = 0;

  // True if address is validated via decrypting HANDSHAKE or 1-RTT packet.
  bool address_validated_via_decrypting_packet = false;

  // True if address is validated via validating token received in INITIAL
  // packet.
  bool address_validated_via_token = false;

  size_t ping_frames_sent = 0;

  // Number of detected peer address changes which changes to a peer address
  // validated by earlier path validation.
  size_t num_peer_migration_to_proactively_validated_address = 0;
  // Number of detected peer address changes which triggers reverse path
  // validation.
  size_t num_reverse_path_validtion_upon_migration = 0;
  // Number of detected peer migrations which either succeed reverse path
  // validation or no need to be validated.
  size_t num_validated_peer_migration = 0;
  // Number of detected peer migrations which triggered reverse path validation
  // and failed and fell back to the old path.
  size_t num_invalid_peer_migration = 0;
  // Number of detected peer migrations which triggered reverse path validation
  // which was canceled because the peer migrated again. Such migration is also
  // counted as invalid peer migration.
  size_t num_peer_migration_while_validating_default_path = 0;
  // Number of NEW_CONNECTION_ID frames sent.
  size_t num_new_connection_id_sent = 0;
  // Number of RETIRE_CONNECTION_ID frames sent.
  size_t num_retire_connection_id_sent = 0;
  // Number of path degrading.
  size_t num_path_degrading = 0;
  // Number of forward progress made after path degrading.
  size_t num_forward_progress_after_path_degrading = 0;

  bool server_preferred_address_validated = false;
  bool failed_to_validate_server_preferred_address = false;
  // Number of duplicated packets that have been sent to server preferred
  // address while the validation is pending.
  size_t num_duplicated_packets_sent_to_server_preferred_address = 0;

  struct QUICHE_EXPORT TlsServerOperationStats {
    bool success = false;
    // If the operation is performed asynchronously, how long did it take.
    // Zero() for synchronous operations.
    QuicTime::Delta async_latency = QuicTime::Delta::Zero();
  };

  // The TLS server op stats only have values when the corresponding operation
  // is performed by TlsServerHandshaker. If an operation is done within
  // BoringSSL, e.g. ticket decrypted without using
  // TlsServerHandshaker::SessionTicketOpen, it will not be recorded here.
  absl::optional<TlsServerOperationStats> tls_server_select_cert_stats;
  absl::optional<TlsServerOperationStats> tls_server_compute_signature_stats;
  absl::optional<TlsServerOperationStats> tls_server_decrypt_ticket_stats;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONNECTION_STATS_H_
