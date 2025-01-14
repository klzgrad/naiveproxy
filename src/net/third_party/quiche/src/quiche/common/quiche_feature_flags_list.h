// NOLINTBEGIN
// clang-format off
// DO NOT EDIT.

// This file intentionally does not have header guards, it is intended to be
// included multiple times, each time with a different definition of
// QUICHE_FLAG.

#if defined(QUICHE_FLAG)

QUICHE_FLAG(bool, quiche_reloadable_flag_enable_h3_origin_frame, false, true, "If true, enables support for parsing HTTP/3 ORIGIN frames.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_act_upon_invalid_header, true, true, "If true, reject or send error response code upon receiving invalid request or response headers.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_add_stream_info_to_idle_close_detail, false, true, "If true, include stream information in idle timeout connection close detail.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_allow_client_enabled_bbr_v2, true, true, "If true, allow client to enable BBRv2 on server via connection option 'B2ON'.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_allow_host_in_request2, true, true, "If true, requests with a host header will be allowed.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_avoid_nested_close_connection, false, true, "If true, if QuicConnection::CloseConnection triggers a nested call to itself, make the nested call a no-op.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_bbr2_extra_acked_window, false, true, "When true, the BBR4 copt sets the extra_acked window to 20 RTTs and BBR5 sets it to 40 RTTs.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_bbr2_probe_two_rounds, true, true, "When true, the BB2U copt causes BBR2 to wait two rounds with out draining the queue before exiting PROBE_UP and BB2S has the same effect in STARTUP.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_bbr2_simplify_inflight_hi, true, true, "When true, the BBHI copt causes QUIC BBRv2 to use a simpler algorithm for raising inflight_hi in PROBE_UP.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_block_until_settings_received_copt, true, true, "If enabled and a BSUS connection is received, blocks server connections until SETTINGS frame is received.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_can_send_ack_frequency, true, true, "If true, ack frequency frame can be sent from server to client.")
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
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_ecn_in_first_ack, false, false, "When true, reports ECN in counts in the ACK of the a client initial that goes in the buffered packet store.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_enable_disable_resumption, true, true, "If true, disable resumption when receiving NRES connection option.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_enable_mtu_discovery_at_server, false, false, "If true, QUIC will default enable MTU discovery at server, with a target of 1450 bytes.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_enable_server_on_wire_ping, true, true, "If true, enable server retransmittable on wire PING.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_enable_version_rfcv2, false, false, "When true, support RFC9369.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_fix_timeouts, true, true, "If true, postpone setting handshake timeout to infinite to handshake complete.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_gfe_allow_alps_new_codepoint, true, true, "If true, allow quic to use new ALPS codepoint to negotiate during handshake for H3 if client sends new ALPS codepoint.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_ignore_gquic_probing, true, true, "If true, QUIC server will not respond to gQUIC probing packet(PING + PADDING) but treat it as a regular packet.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_limit_new_streams_per_loop_2, true, true, "If true, when the peer sends connection options \\\'SLP1\\\', \\\'SLP2\\\' and \\\'SLPF\\\', internet facing GFEs will only allow a limited number of new requests to be processed per event loop, and postpone the rest to the following event loops. Also guard QuicConnection to iterate through all decrypters at each encryption level to get cipher id for a request.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_no_path_degrading_before_handshake_confirmed, true, true, "If true, an endpoint does not detect path degrading or blackholing until handshake gets confirmed.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_no_write_control_frame_upon_connection_close, false, true, "If trrue, early return before write control frame in OnCanWrite() if the connection is already closed.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_no_write_control_frame_upon_connection_close2, false, false, "If true, QuicSession will block outgoing control frames when the connection is closed.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_optimize_qpack_blocking_manager, false, false, "If true, optimize qpack_blocking_manager for CPU efficiency.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_pacing_remove_non_initial_burst, false, false, "If true, remove the non-initial burst in QUIC PacingSender.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_parse_cert_compression_algos_from_chlo, true, true, "If true, parse offered cert compression algorithms from received CHLOs.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_priority_respect_incremental, false, false, "If true, respect the incremental parameter of each stream in QuicWriteBlockedList.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_require_handshake_confirmation, true, true, "If true, require handshake confirmation for QUIC connections, functionally disabling 0-rtt handshakes.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_send_placeholder_ticket_when_encrypt_ticket_fails, false, true, "If true, when TicketCrypter fails to encrypt a session ticket, quic::TlsServerHandshaker will send a placeholder ticket, instead of an empty one, to the client.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_stop_reading_also_stops_header_decompression, true, true, "If true, QUIC stream will not continue decompressing buffer headers after StopReading() called.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_test_peer_addr_change_after_normalize, false, false, "If true, QuicConnection::ProcessValidatedPacket will use normalized address to test peer address changes.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_testonly_default_false, false, false, "A testonly reloadable flag that will always default to false.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_testonly_default_true, true, true, "A testonly reloadable flag that will always default to true.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_use_alarm_multiplexer, false, false, "Manages all of the connection alarms via QuicAlarmMultiplexer.")
QUICHE_FLAG(bool, quiche_reloadable_flag_quic_use_received_client_addresses_cache, true, true, "If true, use a LRU cache to record client addresses of packets received on server's original address.")
QUICHE_FLAG(bool, quiche_restart_flag_quic_support_ect1, false, false, "When true, allows sending of QUIC packets marked ECT(1). A different flag (TBD) will actually utilize this capability to send ECT(1).")
QUICHE_FLAG(bool, quiche_restart_flag_quic_support_flow_label2, false, false, "If true, QUIC will support reading and writing IPv6 flow labels.")
QUICHE_FLAG(bool, quiche_restart_flag_quic_support_release_time_for_gso, false, false, "If true, QuicGsoBatchWriter will support release time if it is available and the process has the permission to do so.")
QUICHE_FLAG(bool, quiche_restart_flag_quic_testonly_default_false, false, false, "A testonly restart flag that will always default to false.")
QUICHE_FLAG(bool, quiche_restart_flag_quic_testonly_default_true, true, true, "A testonly restart flag that will always default to true.")
QUICHE_FLAG(bool, quiche_restart_flag_quic_use_new_qpack_blocking_manager, false, true, "If true, QUIC will use NewQpackBlockingManager instead of QpackBlockingManager.")

#endif
// clang-format on
// NOLINTEND
