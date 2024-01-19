// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_spdy_client_session_base.h"

#include <string>

#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"

using spdy::Http2HeaderBlock;

namespace quic {

QuicSpdyClientSessionBase::QuicSpdyClientSessionBase(
    QuicConnection* connection, QuicSession::Visitor* visitor,
    const QuicConfig& config, const ParsedQuicVersionVector& supported_versions)
    : QuicSpdySession(connection, visitor, config, supported_versions) {}

QuicSpdyClientSessionBase::~QuicSpdyClientSessionBase() {
  DeleteConnection();
}

void QuicSpdyClientSessionBase::OnConfigNegotiated() {
  QuicSpdySession::OnConfigNegotiated();
}

void QuicSpdyClientSessionBase::OnStreamClosed(QuicStreamId stream_id) {
  QuicSpdySession::OnStreamClosed(stream_id);
  if (!VersionUsesHttp3(transport_version())) {
    headers_stream()->MaybeReleaseSequencerBuffer();
  }
}

bool QuicSpdyClientSessionBase::ShouldReleaseHeadersStreamSequencerBuffer() {
  return !HasActiveRequestStreams();
}

bool QuicSpdyClientSessionBase::ShouldKeepConnectionAlive() const {
  return QuicSpdySession::ShouldKeepConnectionAlive() ||
         num_outgoing_draining_streams() > 0;
}

bool QuicSpdyClientSessionBase::OnSettingsFrame(const SettingsFrame& frame) {
  if (!was_zero_rtt_rejected()) {
    if (max_outbound_header_list_size() != std::numeric_limits<size_t>::max() &&
        frame.values.find(SETTINGS_MAX_FIELD_SECTION_SIZE) ==
            frame.values.end()) {
      CloseConnectionWithDetails(
          QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
          "Server accepted 0-RTT but omitted non-default "
          "SETTINGS_MAX_FIELD_SECTION_SIZE");
      return false;
    }

    if (qpack_encoder()->maximum_blocked_streams() != 0 &&
        frame.values.find(SETTINGS_QPACK_BLOCKED_STREAMS) ==
            frame.values.end()) {
      CloseConnectionWithDetails(
          QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
          "Server accepted 0-RTT but omitted non-default "
          "SETTINGS_QPACK_BLOCKED_STREAMS");
      return false;
    }

    if (qpack_encoder()->MaximumDynamicTableCapacity() != 0 &&
        frame.values.find(SETTINGS_QPACK_MAX_TABLE_CAPACITY) ==
            frame.values.end()) {
      CloseConnectionWithDetails(
          QUIC_HTTP_ZERO_RTT_RESUMPTION_SETTINGS_MISMATCH,
          "Server accepted 0-RTT but omitted non-default "
          "SETTINGS_QPACK_MAX_TABLE_CAPACITY");
      return false;
    }
  }

  if (!QuicSpdySession::OnSettingsFrame(frame)) {
    return false;
  }
  std::string settings_frame = HttpEncoder::SerializeSettingsFrame(frame);
  auto serialized_data = std::make_unique<ApplicationState>(
      settings_frame.data(), settings_frame.data() + settings_frame.length());
  GetMutableCryptoStream()->SetServerApplicationStateForResumption(
      std::move(serialized_data));
  return true;
}

}  // namespace quic
