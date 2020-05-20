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

// If true, enable experiment for testing PCC congestion-control.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_pcc3, false)

// When true, ensure BBR allows at least one MSS to be sent in response to an
// ACK in packet conservation.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_bbr_one_mss_conservation, false)

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

// If true, enables the BBS4 and BBS5 connection options, which reduce BBR's
// pacing rate in STARTUP as more losses occur as a fraction of CWND.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr_startup_rate_reduction,
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

// If true, will negotiate the ACK delay time.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_negotiate_ack_delay_time, false)

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
QUIC_FLAG(double, FLAGS_quic_bbr2_default_loss_threshold, 0.3)

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
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_q048, false)

// If true, disable QUIC version Q049.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_q049, false)

// If true, disable QUIC version Q050.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_disable_version_q050, false)

// If true, enable QUIC version T050.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_t050, true)

// A testonly reloadable flag that will always default to false.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_testonly_default_false, false)

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

// When the EACK connection option is sent by the client, an ack-eliciting frame
// is bundled with ACKs sent after the PTO fires.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bundle_retransmittable_with_pto_ack,
          true)
// If true, use QuicClock::Now() as the source of packet receive time instead of
// WallNow().
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_use_quic_time_for_received_timestamp2,
          true)

// If true, enable QUIC version h3-25.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_enable_version_draft_25_v3,
          true)

// If true, enable QUIC version h3-27.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_enable_version_draft_27, true)

// If true, fix QUIC bandwidth sampler to avoid over estimating bandwidth in
// the presence of ack aggregation.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_avoid_overestimate_bandwidth_with_aggregation,
    true)

// If true, emit more granular errors instead of
// SpdyFramerError::SPDY_DECOMPRESS_FAILURE in Http2DecoderAdapter.
// This flag is duplicated in spdy_flags_impl.h due to mixed usage of flags.
// Please update the flag value in spdy when this flag is flipped.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_spdy_enable_granular_decompress_errors,
          true)

// If true, only do minimum validation of coalesced packets (only validate
// connection ID).
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_minimum_validation_of_coalesced_packets,
    true)

// If true, arm the 1st PTO with earliest in flight sent time.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_arm_pto_with_earliest_sent_time,
          true)

// If true, QuicSession::WritevData() will support writing data at a specified
// encryption level.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_writevdata_at_level, true)

// If true, use standard deviation when calculating PTO timeout.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_use_standard_deviation_for_pto,
          true)

// If true, QUIC BBRv2 to avoid unnecessary PROBE_RTTs after quiescence.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr2_avoid_unnecessary_probe_rtt,
          true)

// If true, use passed in ack_frame to calculate minimum size of the serialized
// ACK frame.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_use_ack_frame_to_get_min_size,
          true)

// If true, skip packet threshold loss detection if largest acked is a runt.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_skip_packet_threshold_loss_detection_with_runt,
    true)

// If true, QUIC BBRv2 to take ack height into account when calculating
// queuing_threshold in PROBE_UP.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_bbr2_add_ack_height_to_queueing_threshold,
    false)

// If true, send PING when PTO skips packet number and there is no data to send.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_send_ping_when_pto_skips_packet_number,
    true)

// If true, QuicSession\'s various write methods will set transmission type.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_write_with_transmission, true)

// If true, fix a bug in QUIC BBR where bandwidth estimate becomes 0 after a
// loss only event.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr_fix_zero_bw_on_loss_only_event,
          true)

// If true, trigger QUIC_BUG in two ShouldCreateIncomingStream() overrides when
// called with locally initiated stream ID.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_create_incoming_stream_bug,
          false)

// If true, quic::BandwidthSampler will start in application limited phase.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bw_sampler_app_limited_starting_value,
          false)

// If true, QUIC connection will ignore one packet write error after MTU probe.
QUIC_FLAG(
    bool,
    FLAGS_quic_reloadable_flag_quic_ignore_one_write_error_after_mtu_probe,
    false)

// If true, send H3 SETTINGs when 1-RTT write key is available (rather then both
// keys are available).
QUIC_FLAG(bool,
          FLAGS_quic_restart_flag_quic_send_settings_on_write_key_available,
          false)

// If true, use blackhole detector in QuicConnection to detect path degrading
// and network blackhole.
QUIC_FLAG(bool, FLAGS_quic_reloadable_flag_quic_use_blackhole_detector, false)

// If true, use idle network detector to detect handshake timeout and idle
// network timeout.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_use_idle_network_detector,
          false)

// If true, when QUIC switches from BbrSender to Bbr2Sender, Bbr2Sender will
// copy the bandwidth sampler states from BbrSender.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr_copy_sampler_state_from_v1_to_v2,
          false)

// If true, QUIC will enable connection options LRTT+BBQ2 by default.
QUIC_FLAG(bool,
          FLAGS_quic_reloadable_flag_quic_bbr_default_exit_startup_on_loss,
          false)
