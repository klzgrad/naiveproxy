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

// Adds a QuicPacketNumberQueue that is based on a deque and does not support
// costly AddRange arguments.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_frames_deque3, true)

// If true, enable QUIC v42.
QUIC_FLAG(bool, FLAGS_quic_enable_version_42, false)

// If buffered data in QUIC stream is less than this threshold, buffers all
// provided data or asks upper layer for more data.
QUIC_FLAG(uint32_t, FLAGS_quic_buffered_data_threshold, 8192u)

// Max size of data slice in bytes for QUIC stream send buffer.
QUIC_FLAG(uint32_t, FLAGS_quic_send_buffer_max_data_slice_size, 4096u)

// If true, QUIC supports both QUIC Crypto and TLS 1.3 for the handshake
// protocol.
QUIC_FLAG(bool, FLAGS_quic_supports_tls_handshake, false)

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
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_use_tls13_cipher_suites, true)

// If true, read and write QUIC version labels in network byte order.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_use_net_byte_order_version_label,
          true)

// If true, send stateless reset token in SHLO. This token is used in IETF
// public reset packet.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_send_reset_token_in_shlo, true)

// Default enable all cubic fixes in QUIC Cubic by default.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_cubic_fixes, true)

// If true, enable QUIC v43.
QUIC_FLAG(bool, FLAGS_quic_enable_version_43, false)

// If true, allows one address change when UDP proxying.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_allow_address_change_for_udp_proxy,
          false)

// If true, allow a new BBR connection option to use a slower STARTUP once loss
// occurs
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_slower_startup, true)

// Deprecate QuicAckFrame.largest_observed since it is redundant.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_deprecate_largest_observed,
          true)

// Fully drain the queue in QUIC BBR at least once per cycle(8 rounds) when
// activated by the BBR3 connection option.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_fully_drain_queue, true)

// When true, allows connection options to be sent to completely disable packet
// conservation in QUIC BBR STARTUP or make it more aggressive.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr_conservation_in_startup,
          false)

// Allows increasing the length of time ack aggregation is windowed for to 20
// and 40 RTTs.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr_ack_aggregation_window,
          false)

// If true, OnStreamFrameDiscarded is not called on stream cancellation, and
// canceled stream is immediately closed.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_remove_on_stream_frame_discarded,
          false)

// Explicitly send a connection close if the TLP count is greater than 0 when
// idle timeout occurs.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_explicit_close_after_tlp, false)

// Enables 3 new connection options to make PROBE_RTT more aggressive
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_less_probe_rtt, false)

// If true, server will send a connectivity probing to the source address of the
// received connectivity probing. Otherwise, server will treat connectivity
// probing packet as normal packet
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_server_reply_to_connectivity_probing,
          true)

// If true, truncates QUIC error strings to 256 characters before writing them
// to the wire.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_truncate_long_details, true)

// If true, allow stream data and control frames to be acked multiple times.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_allow_multiple_acks_for_data2,
          false)

// If true, calculate stream sequencer buffer block count in a way that
// guaranteed to be 2048.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_fix_sequencer_buffer_block_count2,
          false)

// If true, use deframer from net/quic/http instead of net/http2.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_hq_deframer, false)
