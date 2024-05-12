// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NOLINT(build/header_guard)
// This file intentionally does not have header guards, it's intended to be
// included multiple times, each time with a different definition of
// QUICHE_PROTOCOL_FLAG.

#if defined(QUICHE_PROTOCOL_FLAG)

QUICHE_PROTOCOL_FLAG(
    bool, quic_allow_chlo_buffering, true,
    "If true, allows packets to be buffered in anticipation of a "
    "future CHLO, and allow CHLO packets to be buffered until next "
    "iteration of the event loop.")

QUICHE_PROTOCOL_FLAG(bool, quic_disable_pacing_for_perf_tests, false,
                     "If true, disable pacing in QUIC")

// Note that single-packet CHLOs are only enforced for Google QUIC versions that
// do not use CRYPTO frames. This currently means only Q043 and Q046. All other
// versions of QUIC (both Google QUIC and IETF) allow multi-packet CHLOs
// regardless of the value of this flag.
QUICHE_PROTOCOL_FLAG(bool, quic_enforce_single_packet_chlo, true,
                     "If true, enforce that sent QUIC CHLOs fit in one packet. "
                     "Only applies to Q043 and Q046.")

// Currently, this number is quite conservative.  At a hypothetical 1000 qps,
// this means that the longest time-wait list we should see is:
//   200 seconds * 1000 qps = 200000.
// Of course, there are usually many queries per QUIC connection, so we allow a
// factor of 3 leeway.
QUICHE_PROTOCOL_FLAG(int64_t, quic_time_wait_list_max_connections, 600000,
                     "Maximum number of connections on the time-wait list.  "
                     "A negative value implies no configured limit.")

QUICHE_PROTOCOL_FLAG(
    int64_t, quic_time_wait_list_seconds, 200,
    "Time period for which a given connection_id should live in "
    "the time-wait state.")

// This number is relatively conservative. For example, there are at most 1K
// queued stateless resets, which consume 1K * 21B = 21KB.
QUICHE_PROTOCOL_FLAG(
    uint64_t, quic_time_wait_list_max_pending_packets, 1024,
    "Upper limit of pending packets in time wait list when writer is blocked.")

// Stop sending a reset if the recorded number of addresses that server has
// recently sent stateless reset to exceeds this limit.
QUICHE_PROTOCOL_FLAG(uint64_t, quic_max_recent_stateless_reset_addresses, 1024,
                     "Max number of recorded recent reset addresses.")

// After this timeout, recent reset addresses will be cleared.
// FLAGS_quic_max_recent_stateless_reset_addresses * (1000ms /
// FLAGS_quic_recent_stateless_reset_addresses_lifetime_ms) is roughly the max
// reset per second. For example, 1024 * (1000ms / 1000ms) = 1K reset per
// second.
QUICHE_PROTOCOL_FLAG(
    uint64_t, quic_recent_stateless_reset_addresses_lifetime_ms, 1000,
    "Max time that a client address lives in recent reset addresses set.")

QUICHE_PROTOCOL_FLAG(
    double, quic_bbr_cwnd_gain, 2.0f,
    "Congestion window gain for QUIC BBR during PROBE_BW phase.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_buffered_data_threshold, 8 * 1024,
    "If buffered data in QUIC stream is less than this "
    "threshold, buffers all provided data or asks upper layer for more data")

QUICHE_PROTOCOL_FLAG(
    uint64_t, quic_send_buffer_max_data_slice_size, 4 * 1024,
    "Max size of data slice in bytes for QUIC stream send buffer.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_lumpy_pacing_size, 2,
    "Number of packets that the pacing sender allows in bursts during "
    "pacing. This flag is ignored if a flow's estimated bandwidth is "
    "lower than 1200 kbps.")

QUICHE_PROTOCOL_FLAG(
    double, quic_lumpy_pacing_cwnd_fraction, 0.25f,
    "Congestion window fraction that the pacing sender allows in bursts "
    "during pacing.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_lumpy_pacing_min_bandwidth_kbps, 1200,
    "The minimum estimated client bandwidth below which the pacing sender will "
    "not allow bursts.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_max_pace_time_into_future_ms, 10,
    "Max time that QUIC can pace packets into the future in ms.")

QUICHE_PROTOCOL_FLAG(
    double, quic_pace_time_into_future_srtt_fraction,
    0.125f,  // One-eighth smoothed RTT
    "Smoothed RTT fraction that a connection can pace packets into the future.")

QUICHE_PROTOCOL_FLAG(
    bool, quic_export_write_path_stats_at_server, false,
    "If true, export detailed write path statistics at server.")

QUICHE_PROTOCOL_FLAG(bool, quic_disable_version_negotiation_grease_randomness,
                     false,
                     "If true, use predictable version negotiation versions.")

QUICHE_PROTOCOL_FLAG(bool, quic_enable_http3_grease_randomness, true,
                     "If true, use random greased settings and frames.")

QUICHE_PROTOCOL_FLAG(int64_t, quic_max_tracked_packet_count, 10000,
                     "Maximum number of tracked packets.")

QUICHE_PROTOCOL_FLAG(
    bool, quic_client_convert_http_header_name_to_lowercase, true,
    "If true, HTTP request header names sent from QuicSpdyClientBase(and "
    "descendants) will be automatically converted to lower case.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_bbr2_default_probe_bw_base_duration_ms, 2000,
    "The default minimum duration for BBRv2-native probes, in milliseconds.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_bbr2_default_probe_bw_max_rand_duration_ms, 1000,
    "The default upper bound of the random amount of BBRv2-native "
    "probes, in milliseconds.")

QUICHE_PROTOCOL_FLAG(
    double, quic_bbr2_default_probe_rtt_inflight_target_bdp_fraction, 0.5,
    "The default fraction to adjust the target in flight BDP during "
    "PROBE_RTT phase.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_bbr2_default_probe_rtt_period_ms, 10000,
    "The default period for entering PROBE_RTT, in milliseconds.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_bbr2_default_probe_rtt_duration_ms, 200,
    "The default time to spend in PROBE_RTT mode, in milliseconds.")

QUICHE_PROTOCOL_FLAG(
    double, quic_bbr2_default_loss_threshold, 0.02,
    "The default loss threshold for QUIC BBRv2, should be a value "
    "between 0 and 1.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_bbr2_default_startup_full_loss_count, 8,
    "The default minimum number of loss marking events to exit STARTUP.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_bbr2_default_probe_bw_full_loss_count, 2,
    "The default minimum number of loss marking events to exit PROBE_UP phase.")

QUICHE_PROTOCOL_FLAG(
    double, quic_bbr2_default_inflight_hi_headroom, 0.15,
    "The default fraction of unutilized headroom to try to leave in path "
    "upon high loss.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_bbr2_default_initial_ack_height_filter_window, 10,
    "The default initial value of the max ack height filter's window length.")

QUICHE_PROTOCOL_FLAG(
    double, quic_ack_aggregation_bandwidth_threshold, 1.0,
    "If the bandwidth during ack aggregation is smaller than (estimated "
    "bandwidth * this flag), consider the current aggregation completed "
    "and starts a new one.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_anti_amplification_factor, 3,
    "Anti-amplification factor. Before address validation, server will "
    "send no more than factor times bytes received.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_max_buffered_crypto_bytes,
    16 * 1024,  // 16 KB
    "The maximum amount of CRYPTO frame data that can be buffered.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_max_aggressive_retransmittable_on_wire_ping_count, 5,
    "Maximum number of consecutive pings that can be sent with the "
    "aggressive initial retransmittable on the wire timeout if there is "
    "no new stream data received. After this limit, the timeout will be "
    "doubled each ping until it exceeds the default ping timeout.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_max_retransmittable_on_wire_ping_count, 1000,
    "Maximum number of pings that can be sent with the retransmittable "
    "on the wire timeout, over the lifetime of a connection. After this "
    "limit, the timeout will be the default ping timeout.")

QUICHE_PROTOCOL_FLAG(int32_t, quic_max_congestion_window, 2000,
                     "The maximum congestion window in packets.")

QUICHE_PROTOCOL_FLAG(
    int32_t, quic_max_streams_window_divisor, 2,
    "The divisor that controls how often MAX_STREAMS frame is sent.")

QUICHE_PROTOCOL_FLAG(
    uint64_t, quic_key_update_confidentiality_limit, 0,
    "If non-zero and key update is allowed, the maximum number of "
    "packets sent for each key phase before initiating a key update.")

QUICHE_PROTOCOL_FLAG(bool, quic_disable_client_tls_zero_rtt, false,
                     "If true, QUIC client with TLS will not try 0-RTT.")

QUICHE_PROTOCOL_FLAG(bool, quic_disable_server_tls_resumption, false,
                     "If true, QUIC server will disable TLS resumption by not "
                     "issuing or processing session tickets.")

QUICHE_PROTOCOL_FLAG(bool, quic_defer_send_in_response, true,
                     "If true, QUIC servers will defer sending in response to "
                     "incoming packets by default.")

QUICHE_PROTOCOL_FLAG(
    bool, quic_header_size_limit_includes_overhead, true,
    "If true, QUIC QPACK decoder includes 32-bytes overheader per entry while "
    "comparing request/response header size against its upper limit.")

QUICHE_PROTOCOL_FLAG(
    bool, quic_reject_retry_token_in_initial_packet, false,
    "If true, always reject retry_token received in INITIAL packets")

QUICHE_PROTOCOL_FLAG(bool, quic_use_lower_server_response_mtu_for_test, false,
                     "If true, cap server response packet size at 1250.")

QUICHE_PROTOCOL_FLAG(bool, quic_enforce_strict_amplification_factor, false,
                     "If true, enforce strict amplification factor")

QUICHE_PROTOCOL_FLAG(bool, quic_bounded_crypto_send_buffer, false,
                     "If true, close the connection if a crypto send buffer "
                     "exceeds its size limit.")

QUICHE_PROTOCOL_FLAG(bool, quic_interval_set_enable_add_optimization, true,
                     "If true, enable an optimization in QuicIntervalSet")

QUICHE_PROTOCOL_FLAG(bool, quic_server_disable_qpack_dynamic_table, false,
                     "If true, disables use of the QPACK dynamic table in "
                     "servers, both for decoding context (requests) and for "
                     "encoding context (responses).")

QUICHE_PROTOCOL_FLAG(
    bool, quic_enable_chaos_protection, true,
    "If true, use chaos protection to randomize client initials.")

QUICHE_PROTOCOL_FLAG(bool, quic_always_support_server_preferred_address, false,
                     "If false, the kSPAD connection option is required to use "
                     "QUIC server preferred address support.")

QUICHE_PROTOCOL_FLAG(bool, quiche_oghttp2_debug_trace, false,
                     "If true, emits trace logs for HTTP/2 events.")

#endif
