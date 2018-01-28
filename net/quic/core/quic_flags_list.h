// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file intentionally does not have header guards, it's included
// inside a macro to generate values.

// This file contains the list of QUIC protocol flags.

// Time period for which a given connection_id should live in the time-wait
// state.
QUIC_FLAG(int64_t, FLAGS_quic_time_wait_list_seconds, 200)

// Currently, this number is quite conservative.  The max QPS limit for an
// individual server silo is currently set to 1000 qps, though the actual max
// that we see in the wild is closer to 450 qps.  Regardless, this means that
// the longest time-wait list we should see is 200 seconds * 1000 qps, 200000.
// Of course, there are usually many queries per QUIC connection, so we allow a
// factor of 3 leeway.
//
// Maximum number of connections on the time-wait list. A negative value implies
// no configured limit.
QUIC_FLAG(int64_t, FLAGS_quic_time_wait_list_max_connections, 600000)

// Enables server-side support for QUIC stateless rejects.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_enable_quic_stateless_reject_support,
          true)

// If true, require handshake confirmation for QUIC connections, functionally
// disabling 0-rtt handshakes.
// TODO(rtenneti): Enable this flag after CryptoServerTest's are fixed.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_require_handshake_confirmation,
          false)

// If true, disable pacing in QUIC.
QUIC_FLAG(bool, FLAGS_quic_disable_pacing_for_perf_tests, false)

// If true, QUIC will use cheap stateless rejects without creating a full
// connection.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_use_cheap_stateless_rejects,
          true)

// If true, QUIC respect HTTP2 SETTINGS frame rather than always close the
// connection.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_respect_http2_settings_frame,
          true)

// If true, allows packets to be buffered in anticipation of a future CHLO, and
// allow CHLO packets to be buffered until next iteration of the event loop.
QUIC_FLAG(bool, FLAGS_quic_allow_chlo_buffering, true)

// If true, GFE sends SETTINGS_MAX_HEADER_LIST_SIZE to the client at the
// beginning of a connection.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_send_max_header_list_size, true)

// If greater than zero, mean RTT variation is multiplied by the specified
// factor and added to the congestion window limit.
QUIC_FLAG(double, FLAGS_quic_bbr_rtt_variation_weight, 0.0f)

// Congestion window gain for QUIC BBR during PROBE_BW phase.
QUIC_FLAG(double, FLAGS_quic_bbr_cwnd_gain, 2.0f)

// Allows one self address change.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_allow_one_address_change, false)

// Support bandwidth resumption in QUIC BBR.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_bandwidth_resumption, false)

// Add the equivalent number of bytes as 3 TCP TSO segments to QUIC's BBR CWND.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_add_tso_cwnd, false)

// If true, enable version 38 which supports new PADDING frame and respects NSTP
// connection option.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_38, true)

// If true, enable QUIC v39.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_39, true)

// Simplify QUIC\'s adaptive time loss detection to measure the necessary
// reordering window for every spurious retransmit.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_fix_adaptive_time_loss, false)

// If true, enable random padding of size [1, 256] when response body is
// compressed for QUIC version >= 38.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_random_padding, true)

// If true, export a varz mapping QUIC non 0-rtt handshake with corresponding
// frontend service.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_account_handshake, true)

// Allows the 3RTO QUIC connection option to close a QUIC connection after
// 3RTOs if there are no open streams.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_3rtos, false)

// If true, enable experiment for testing PCC congestion-control.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_pcc, false)

// When true, defaults to BBR congestion control instead of Cubic.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_default_to_bbr, false)

// Allow a new rate based recovery in QUIC BBR to be enabled via connection
// option.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_rate_recovery, false)

// If true, allow trailing headers with duplicate keys, and combine the values
// from duplicate keys into a single delimted header.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_handle_duplicate_trailers, true)

// If true, disables support for the packets-based QUIC congestion control
// algorithms.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_packets_based_cc, true)

// When enabled, ack frame uses a deque internally instead of a set.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_frames_deque2, false)

// If true, enable QUIC v42.
QUIC_FLAG(bool, FLAGS_quic_enable_version_42, false)

// When true, allows the LRTT connection option to cause QUIC BBR to exit
// STARTUP when in recovery and there has been no bandwidth increase for 1RTT.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_exit_startup_on_loss, true)

// If buffered data in QUIC stream is less than this threshold, buffers all
// provided data or asks upper layer for more data.
QUIC_FLAG(uint32_t, FLAGS_quic_buffered_data_threshold, 8192u)

// Max size of data slice in bytes for QUIC stream send buffer.
QUIC_FLAG(uint32_t, FLAGS_quic_send_buffer_max_data_slice_size, 4096u)

// If true, QUIC supports both QUIC Crypto and TLS 1.3 for the handshake
// protocol.
QUIC_FLAG(bool, FLAGS_quic_supports_tls_handshake, false)

// Add 4 new ack decimation modes to QUIC that are entirely time based at 1/4
// or 1/8 RTT.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_ack_decimation, false)

// If true, in QUIC, set dispatcher framer\'s version to client packet's
// version in QuicDispatcher::OnStatelessRejectorProcessDone.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_set_version_on_async_get_proof_returns,
    true)

// If true, check for packet number underflow when reading ack blocks.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_sanitize_framer_addrange_input,
          false)

// If true, QUIC v40 is enabled which includes changes to RST_STREAM, ACK
// and STREAM frames match IETF format.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_41, true)

// If true, QUIC can take ownership of data provided in a reference counted
// memory to avoid data copy.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_use_mem_slices, false)

// Allow QUIC to accept initial packet numbers that are random, not 1.
QUIC_FLAG(bool, FLAGS_quic_restart_flag_quic_enable_accept_random_ipn, false)

// Report the more analogous TLS 1.3 cipher suites rather than TLS 1.2 ECDHE_RSA
// ciphers in QuicDecrypters.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_use_tls13_cipher_suites, false)

// If true, read and write QUIC version labels in network byte order.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_use_net_byte_order_version_label,
          false)

// If true, send stateless reset token in SHLO. This token is used in IETF
// public reset packet.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_send_reset_token_in_shlo, false)

// Default enable all cubic fixes in QUIC Cubic by default.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_cubic_fixes, false)
