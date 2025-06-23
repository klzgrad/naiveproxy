// NOLINTBEGIN
// clang-format off
// DO NOT EDIT.

// This file intentionally does not have header guards, it is intended to be
// included multiple times, each time with a different definition of
// QUICHE_FLAG.

#if defined(QUICHE_FLAG)

QUICHE_FLAG(bool, quiche_reloadable_flag_enable_h3_origin_frame, false, true, "If true, enables support for parsing HTTP/3 ORIGIN frames.")
QUICHE_FLAG(bool, quiche_reloadable_flag_enable_tls_trust_anchor_ids, false, false, "When true, QUIC client and server will support TLS Trust Anchor IDs.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_act_upon_invalid_header, true, true, "If true, reject or send error response code upon receiving invalid request or response headers.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_add_stream_info_to_idle_close_detail, false, true, "If true, include stream information in idle timeout connection close detail.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_allow_client_enabled_bbr_v2, true, true, "If true, allow client to enable BBRv2 on server via connection option 'B2ON'.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_bbr2_extra_acked_window, false, true, "When true, the BBR4 copt sets the extra_acked window to 20 RTTs and BBR5 sets it to 40 RTTs.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_bbr2_probe_two_rounds, true, true, "When true, the BB2U copt causes BBR2 to wait two rounds with out draining the queue before exiting PROBE_UP and BB2S has the same effect in STARTUP.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_bbr2_simplify_inflight_hi, true, true, "When true, the BBHI copt causes QUIC BBRv2 to use a simpler algorithm for raising inflight_hi in PROBE_UP.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_block_until_settings_received_copt, true, true, "If enabled and a BSUS connection is received, blocks server connections until SETTINGS frame is received.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_can_send_ack_frequency, false, false, "If true, ack frequency frame can be sent from server to client.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_conservative_bursts, false, false, "If true, set burst token to 2 in cwnd bootstrapping experiment.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_conservative_cwnd_and_pacing_gains, false, false, "If true, uses conservative cwnd gain and pacing gain when cwnd gets bootstrapped.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_default_enable_5rto_blackhole_detection2, true, true, "If true, default-enable 5RTO blachole detection.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_default_to_bbr, true, false, "When true, defaults to BBR congestion control instead of Cubic.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_default_to_bbr_v2, false, false, "If true, use BBRv2 as the default congestion controller. Takes precedence over --quic_default_to_bbr.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_deliver_stop_sending_to_zombie_streams, true, true, "If true, deliver STOP_SENDING to zombie streams.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_disable_batch_write, false, false, "If true, round-robin stream writes instead of batching in QuicWriteBlockedList.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_disable_server_blackhole_detection, false, false, "If true, disable blackhole detection on server side.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_disable_version_draft_29, false, false, "If true, disable QUIC version h3-29.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_disable_version_q046, false, true, "If true, disable QUIC version Q046.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_disable_version_rfcv1, false, false, "If true, disable QUIC version h3 (RFCv1).")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_discard_initial_packet_with_key_dropped, false, true, "If true, discard INITIAL packet if the key has been dropped.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_enable_disable_resumption, true, true, "If true, disable resumption when receiving NRES connection option.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_enable_mtu_discovery_at_server, false, false, "If true, QUIC will default enable MTU discovery at server, with a target of 1450 bytes.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_enable_server_on_wire_ping, true, true, "If true, enable server retransmittable on wire PING.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_enable_version_rfcv2, false, false, "When true, support RFC9369.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_fin_before_completed_http_headers, false, true, "If true, close the connection with error if FIN is received before finish receiving the whole HTTP headers.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_fix_timeouts, true, true, "If true, postpone setting handshake timeout to infinite to handshake complete.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_heapless_key_derivation, false, false, "If true, QUIC key derivation uses heapless crypto utils.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_heapless_obfuscator, false, true, "If true, generates QUIC initial obfuscators with no heap allocations.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_heapless_static_parser, false, false, "If true, stops parsing immediately on unknown version, to avoid a potential malloc when parsing the connection ID")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_ignore_gquic_probing, true, true, "If true, QUIC server will not respond to gQUIC probing packet(PING + PADDING) but treat it as a regular packet.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_limit_new_streams_per_loop_2, true, true, "If true, when the peer sends connection options \\\'SLP1\\\', \\\'SLP2\\\' and \\\'SLPF\\\', internet facing GFEs will only allow a limited number of new requests to be processed per event loop, and postpone the rest to the following event loops. Also guard QuicConnection to iterate through all decrypters at each encryption level to get cipher id for a request.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_no_path_degrading_before_handshake_confirmed, true, true, "If true, an endpoint does not detect path degrading or blackholing until handshake gets confirmed.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_no_write_control_frame_upon_connection_close, false, true, "If trrue, early return before write control frame in OnCanWrite() if the connection is already closed.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_no_write_control_frame_upon_connection_close2, false, false, "If true, QuicSession will block outgoing control frames when the connection is closed.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_notify_ack_listener_earlier, true, true, "If true, call QuicAckListenerInterface::OnPacketAcked() before moving the stream to closed stream list.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_notify_stream_soon_to_destroy, true, true, "If true, notify each QUIC stream before it gets destroyed and update ACK listener before that.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_on_packet_header_return_connected, false, true, "If true, QuicConnection::OnPacketHeader will return connected_ at the end of the function.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_optimize_qpack_blocking_manager, false, false, "If true, optimize qpack_blocking_manager for CPU efficiency.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_pacing_remove_non_initial_burst, false, false, "If true, remove the non-initial burst in QUIC PacingSender.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_parse_cert_compression_algos_from_chlo, true, true, "If true, parse offered cert compression algorithms from received CHLOs.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_priority_respect_incremental, false, false, "If true, respect the incremental parameter of each stream in QuicWriteBlockedList.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_receive_ack_frequency, false, false, "When true, advertises support for ACK_FREQUENCY and IMMEDIATE_ACK from draft-ietf-quic-ack-frequency-10 and processes them correctly.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_record_tos_byte, false, false, "If true, record TOS byte at QuicPacketReader.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_require_handshake_confirmation, true, true, "If true, require handshake confirmation for QUIC connections, functionally disabling 0-rtt handshakes.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_test_peer_addr_change_after_normalize, false, false, "If true, QuicConnection::ProcessValidatedPacket will use normalized address to test peer address changes.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_testonly_default_false, false, false, "A testonly reloadable flag that will always default to false.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_testonly_default_true, true, true, "A testonly reloadable flag that will always default to true.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_use_received_client_addresses_cache, true, true, "If true, use a LRU cache to record client addresses of packets received on server's original address.")
QUICHE_FLAG(bool, quiche_restart_flag_quic_support_release_time_for_gso, false, false, "If true, QuicGsoBatchWriter will support release time if it is available and the process has the permission to do so.")
QUICHE_FLAG(bool, quiche_restart_flag_quic_testonly_default_false, false, false, "A testonly restart flag that will always default to false.")
QUICHE_FLAG(bool, quiche_restart_flag_quic_testonly_default_true, true, true, "A testonly restart flag that will always default to true.")

#endif
// clang-format on
// NOLINTEND
