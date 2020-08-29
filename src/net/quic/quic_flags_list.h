// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file intentionally does not have header guards, it's included
// inside a macro to generate values. The following line silences a
// presubmit warning that would otherwise be triggered by this:
// no-include-guard-because-multiply-included
// NOLINT(build/header_guard)

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

// If true, require handshake confirmation for QUIC connections, functionally
// disabling 0-rtt handshakes.
// TODO(rtenneti): Enable this flag after CryptoServerTest's are fixed.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_require_handshake_confirmation,
          false)

// If true, disable pacing in QUIC.
QUIC_FLAG(bool, FLAGS_quic_disable_pacing_for_perf_tests, false)

// If true, enforce that QUIC CHLOs fit in one packet.
QUIC_FLAG(bool, FLAGS_quic_enforce_single_packet_chlo, true)

// If true, allows packets to be buffered in anticipation of a future CHLO, and
// allow CHLO packets to be buffered until next iteration of the event loop.
QUIC_FLAG(bool, FLAGS_quic_allow_chlo_buffering, true)

// If greater than zero, mean RTT variation is multiplied by the specified
// factor and added to the congestion window limit.
QUIC_FLAG(double, FLAGS_quic_bbr_rtt_variation_weight, 0.0f)

// Congestion window gain for QUIC BBR during PROBE_BW phase.
QUIC_FLAG(double, FLAGS_quic_bbr_cwnd_gain, 2.0f)

// If true, adjust congestion window when doing bandwidth resumption in BBR.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_fix_bbr_cwnd_in_bandwidth_resumption,
          true)

// When true, defaults to BBR congestion control instead of Cubic.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_default_to_bbr, false)

// If true, use BBRv2 as the default congestion controller.
// Takes precedence over --quic_default_to_bbr.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_default_to_bbr_v2, false)

// If buffered data in QUIC stream is less than this threshold, buffers all
// provided data or asks upper layer for more data.
QUIC_FLAG(uint32_t, FLAGS_quic_buffered_data_threshold, 8192u)

// Max size of data slice in bytes for QUIC stream send buffer.
QUIC_FLAG(uint32_t, FLAGS_quic_send_buffer_max_data_slice_size, 4096u)

// Anti-amplification factor. Before address validation, server will
// send no more than factor times bytes received.
QUIC_FLAG(int32_t, FLAGS_quic_anti_amplification_factor, 3)

// When true, set the initial congestion control window from connection options
// in QuicSentPacketManager rather than TcpCubicSenderBytes.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_unified_iw_options, false)

// Number of packets that the pacing sender allows in bursts during pacing.
QUIC_FLAG(int32_t, FLAGS_quic_lumpy_pacing_size, 2)

// Congestion window fraction that the pacing sender allows in bursts during
// pacing.
QUIC_FLAG(double, FLAGS_quic_lumpy_pacing_cwnd_fraction, 0.25f)

// Default enables QUIC ack decimation and adds a connection option to disable
// it.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_ack_decimation, true)

// If true, QUIC offload pacing when using USPS as egress method.
QUIC_FLAG(bool, FLAGS_quic_restart_flag_quic_offload_pacing_to_usps2, false)

// Max time that QUIC can pace packets into the future in ms.
QUIC_FLAG(int32_t, FLAGS_quic_max_pace_time_into_future_ms, 10)

// Smoothed RTT fraction that a connection can pace packets into the future.
QUIC_FLAG(double, FLAGS_quic_pace_time_into_future_srtt_fraction, 0.125f)

// Mechanism to override version label and ALPN for IETF interop.
QUIC_FLAG(int32_t, FLAGS_quic_ietf_draft_version, 0)

// If true, stop resetting ideal_next_packet_send_time_ in pacing sender.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_donot_reset_ideal_next_packet_send_time,
    false)

// When true and the BBR9 connection option is present, BBR only considers
// bandwidth samples app-limited if they're not filling the pipe.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_flexible_app_limited, false)

// When the STMP connection option is sent by the client, timestamps in the QUIC
// ACK frame are sent and processed.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_send_timestamps, false)

// When in STARTUP and recovery, do not add bytes_acked to QUIC BBR's CWND in
// CalculateCongestionWindow()
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_bbr_no_bytes_acked_in_startup_recovery,
    false)

// If true and using Leto for QUIC shared-key calculations, GFE will react to a
// failure to contact Leto by sending a REJ containing a fallback ServerConfig,
// allowing the client to continue the handshake.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_send_quic_fallback_server_config_on_leto_error,
    false)

// If true, GFE will not request private keys when fetching QUIC ServerConfigs
// from Leto.
QUIC_FLAG(bool,
          FLAGS_quic_restart_flag_dont_fetch_quic_private_keys_from_leto,
          false)

// In v44 and above, where STOP_WAITING is never sent, close the connection if
// it's received.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_do_not_accept_stop_waiting,
          false)

// If true, set burst token to 2 in cwnd bootstrapping experiment.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_conservative_bursts, false)

// If true, export number of packets written per write operation histogram.")
QUIC_FLAG(bool, FLAGS_quic_export_server_num_packets_per_write_histogram, false)

// If true, uses conservative cwnd gain and pacing gain.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_conservative_cwnd_and_pacing_gains,
          false)

// If true, use predictable version negotiation versions.
QUIC_FLAG(bool, FLAGS_quic_disable_version_negotiation_grease_randomness, false)

// Maximum number of tracked packets.
QUIC_FLAG(int64_t, FLAGS_quic_max_tracked_packet_count, 10000)

// If true, HTTP request header names sent from QuicSpdyClientBase(and
// descendents) will be automatically converted to lower case.
QUIC_FLAG(bool, FLAGS_quic_client_convert_http_header_name_to_lowercase, true)

// If true, allow client to enable BBRv2 on server via connection option 'B2ON'.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_allow_client_enabled_bbr_v2,
          false)

// If true, QuicFramer::WriteClientVersionNegotiationProbePacket uses
// length-prefixed connection IDs.
QUIC_FLAG(bool, FLAGS_quic_prober_uses_length_prefixed_connection_ids, false)

// The maximum amount of CRYPTO frame data that can be buffered.
QUIC_FLAG(int32_t, FLAGS_quic_max_buffered_crypto_bytes, 16 * 1024)

// If the bandwidth during ack aggregation is smaller than (estimated
// bandwidth * this flag), consider the current aggregation completed
// and starts a new one.
QUIC_FLAG(double, FLAGS_quic_ack_aggregation_bandwidth_threshold, 1.0)

// If set to non-zero, the maximum number of consecutive pings that can be sent
// with aggressive initial retransmittable on wire timeout if there is no new
// data received. After which, the timeout will be exponentially back off until
// exceeds the default ping timeout.
QUIC_FLAG(int32_t,
          FLAGS_quic_max_aggressive_retransmittable_on_wire_ping_count,
          0)

// If true, re-calculate pacing rate when cwnd gets bootstrapped.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_fix_pacing_rate, true)

// The maximum congestion window in packets.
QUIC_FLAG(int32_t, FLAGS_quic_max_congestion_window, 2000)

// If true, do not inject bandwidth in BbrSender::AdjustNetworkParameters.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr_donot_inject_bandwidth,
          true)

// The default minimum duration for BBRv2-native probes, in milliseconds.
QUIC_FLAG(int32_t, FLAGS_quic_bbr2_default_probe_bw_base_duration_ms, 2000)

// The default upper bound of the random amount of BBRv2-native
// probes, in milliseconds.
QUIC_FLAG(int32_t, FLAGS_quic_bbr2_default_probe_bw_max_rand_duration_ms, 1000)

// The default period for entering PROBE_RTT, in milliseconds.
QUIC_FLAG(int32_t, FLAGS_quic_bbr2_default_probe_rtt_period_ms, 10000)

// The default loss threshold for QUIC BBRv2, should be a value
// between 0 and 1.
QUIC_FLAG(double, FLAGS_quic_bbr2_default_loss_threshold, 0.02)

// The default minimum number of loss marking events to exit STARTUP.
QUIC_FLAG(int32_t, FLAGS_quic_bbr2_default_startup_full_loss_count, 8)

// The default fraction of unutilized headroom to try to leave in path
// upon high loss.
QUIC_FLAG(double, FLAGS_quic_bbr2_default_inflight_hi_headroom, 0.01)

// If true, disable QUIC version Q043.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_q043, false)

// If true, disable QUIC version Q046.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_q046, false)

// If true, disable QUIC version Q048.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_q048, true)

// If true, disable QUIC version Q049.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_q049, true)

// If true, disable QUIC version Q050.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_q050, false)

// A testonly reloadable flag that will always default to false.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_testonly_default_false, false)

// A testonly reloadable flag that will always default to true.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_testonly_default_true, true)

// A testonly restart flag that will always default to false.
QUIC_FLAG(bool, FLAGS_quic_restart_flag_quic_testonly_default_false, false)

// A testonly restart flag that will always default to true.
QUIC_FLAG(bool, FLAGS_quic_restart_flag_quic_testonly_default_true, true)

// In BBR, slow pacing rate if it is likely causing overshoot.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_bbr_mitigate_overly_large_bandwidth_sample,
    true)

// The default initial value of the max ack height filter's window length.
QUIC_FLAG(int32_t, FLAGS_quic_bbr2_default_initial_ack_height_filter_window, 10)

// The default minimum number of loss marking events to exit PROBE_UP phase.
QUIC_FLAG(double, FLAGS_quic_bbr2_default_probe_bw_full_loss_count, 2)

// When true, ensure the ACK delay is never less than the alarm granularity when
// ACK decimation is enabled.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_ack_delay_alarm_granularity,
          false)

// If true, use predictable grease settings identifiers and values.
QUIC_FLAG(bool, FLAGS_quic_enable_http3_grease_randomness, true)

// If true, disable QUIC version h3-25.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_draft_25, false)

// If true, disable QUIC version h3-27.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_draft_27, false)

// If true, QUIC BBRv2 to take ack height into account when calculating
// queuing_threshold in PROBE_UP.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_bbr2_add_ack_height_to_queueing_threshold,
    true)

// If true, use idle network detector to detect handshake timeout and idle
// network timeout.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_use_idle_network_detector, true)

// If true, server push will be allowed in QUIC versions using HTTP/3.
QUIC_FLAG(bool, FLAGS_quic_enable_http3_server_push, false)

// The divisor that controls how often MAX_STREAMS frames are sent.
QUIC_FLAG(int32_t, FLAGS_quic_max_streams_window_divisor, 2)

// If true, QUIC BBRv2\'s PROBE_BW mode will not reduce cwnd below
// BDP+ack_height.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr2_avoid_too_low_probe_bw_cwnd,
          false)

// When true, the 1RTT and 2RTT connection options decrease the number of round
// trips in BBRv2 STARTUP without a 25% bandwidth increase to 1 or 2 round trips
// respectively.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr2_fewer_startup_round_trips,
          false)

// Replace the usage of ConnectionData::encryption_level in
// quic_time_wait_list_manager with a new TimeWaitAction.
QUIC_FLAG(bool,
          FLAGS_quic_restart_flag_quic_replace_time_wait_list_encryption_level,
          true)

// If true, enables support for TLS resumption in QUIC.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_tls_resumption, false)

// When true, QUIC's BBRv2 ignores inflight_lo in PROBE_BW.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr2_ignore_inflight_lo, false)

// If true, do not change ACK in PostProcessAckFrame if an ACK has been queued.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_donot_change_queued_ack, true)

// If true, reject IETF QUIC connections with invalid SNI.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_tls_enforce_valid_sni, true)

// If true, support for IETF QUIC 0-rtt is enabled.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_zero_rtt_for_tls, true)

// If true, default on PTO which unifies TLP + RTO loss recovery.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_default_on_pto, false)

// When true, QUIC+TLS will not send nor parse the old-format Google-specific
// transport parameters.
QUIC_FLAG(bool,
          FLAGS_quic_restart_flag_quic_google_transport_param_omit_old,
          false)

// When true, QUIC+TLS will send and parse the new-format Google-specific
// transport parameters.
QUIC_FLAG(bool,
          FLAGS_quic_restart_flag_quic_google_transport_param_send_new,
          true)

// If true, check ShouldGeneratePacket for every crypto packet.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_fix_checking_should_generate_packet,
          true)

// If true, notify stream ID manager even connection disconnects.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_notify_stream_id_manager_when_disconnected,
    true)

// If true, return from QuicCryptoStream::WritePendingCryptoRetransmission after
// partial writes.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_fix_write_pending_crypto_retransmission,
    true)

// If true, clear last_inflight_packets_sent_time_ of a packet number space when
// there is no bytes in flight.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_fix_last_inflight_packets_sent_time,
          true)

// If true, QUIC will free writer-allocated packet buffer if writer->WritePacket
// is not called.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_avoid_leak_writer_buffer, false)

// If true, QuicConnection::SendAllPendingAcks will Update instead of Set the
// ack alarm.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_update_ack_alarm_in_send_all_pending_acks,
    true)

// If true, the B2HI connection option limits reduction of inflight_hi to
// (1-Beta)*CWND.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr2_limit_inflight_hi, false)

// When true, always check the amplification limit before writing, not just for
// handshake packets.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_move_amplification_limit, true)

// If true, SendAllPendingAcks always send the earliest ACK.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_always_send_earliest_ack, true)

// If true, check connection level flow control for send control stream and
// qpack streams in QuicSession::WillingAndAbleToWrite.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_fix_willing_and_able_to_write,
          true)

// If true, disable QUIC version h3-T050.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_t050, false)

// If true, do not arm PTO on half RTT packets if they are the only ones in
// flight.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_fix_server_pto_timeout, true)

// If true, default-enable 5RTO blachole detection.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_default_enable_5rto_blackhole_detection2,
    true)

// If true, session does not send duplicate MAX_STREAMS.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_stop_sending_duplicate_max_streams,
          true)

// If true, enable QUIC version h3-29.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_draft_29, true)

// If true, support HANDSHAKE_DONE frame in T050
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_support_handshake_done_in_t050,
          true)

// If true, save user agent into in QuicSession.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_save_user_agent_in_quic_session,
          false)

// When true, QUIC_CRYPTO versions of QUIC will not send the max ACK delay
// unless it is configured to a non-default value.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_dont_send_max_ack_delay_if_default,
          true)

// If true, remove the head of line blocking caused by an unprocessable packet
// in the undecryptable packets list.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_fix_undecryptable_packets, true)

// If true, QUIC client only tries to retransmit data when 1-RTT key is
// available.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_do_not_retransmit_immediately_on_zero_rtt_reject,
    true)

// If true, try to bundle INITIAL data when trying to send INITIAL ACK.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bundle_crypto_data_with_initial_ack,
          true)

// If true, do not use QuicUtil::IsBidirectionalStreamId() to determine gQUIC
// stream type.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_fix_gquic_stream_type, true)

// When true, do not pad the QUIC_CRYPTO CHLO message itself. Note that the
// packet containing the CHLO will still be padded.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_dont_pad_chlo, false)

// If true, include MinPlaintextPacketSize when determine whether removing soft
// limit for crypto frames.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_fix_min_crypto_frame_size, true)

// When true, QuicDispatcher supports decapsulation of Legacy Version
// Encapsulation packets.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_dispatcher_legacy_version_encapsulation,
    false)

// If true, update packet size when the first frame gets queued.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_update_packet_size, false)
