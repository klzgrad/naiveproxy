// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_TEST_TOOLS_QBONE_BASIC_QUIC_SERVER_HANDLER_H_
#define QUICHE_QUIC_QBONE_TEST_TOOLS_QBONE_BASIC_QUIC_SERVER_HANDLER_H_

#include <cstddef>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_stream_sequencer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/qbone/qbone_control.pb.h"
#include "quiche/quic/qbone/test_tools/basic_quic_server.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic::test {

class QboneBasicQuicServerHandler : public BasicQuicServer::Handler {
 public:
  QboneBasicQuicServerHandler(
      BasicQuicServer* absl_nonnull server ABSL_ATTRIBUTE_LIFETIME_BOUND);

  ~QboneBasicQuicServerHandler() override = default;

  virtual void OnNewConnection(QuicConnectionId server_connection_id) {}
  virtual void OnConnectionClosed(QuicConnectionId server_connection_id) {}
  virtual void OnControlMessageReceived(QuicConnectionId server_connection_id,
                                        const QboneServerRequest& request) = 0;
  virtual void OnControlMessageParseError() {
    QUICHE_CHECK(false) << "Invalid control message received.";
  }
  virtual void OnTunnelPacket(QuicConnectionId server_connection_id,
                              absl::Span<const std::byte> data) = 0;

  absl::Status SendControlMessage(QuicConnectionId server_connection_id,
                                  const QboneClientRequest& request);
  absl::Status SendTunnelPacket(QuicConnectionId server_connection_id,
                                absl::Span<const std::byte> data);

  // BasicQuicServer::Handler:
  void OnSessionEnd(QuicConnectionId server_connection_id) override;
  bool OnNewStream(QuicConnectionId server_connection_id,
                   QuicStreamId stream_id) override;
  int OnStreamDataAvailable(QuicConnectionId server_connection_id,
                            QuicStreamId stream_id,
                            const QuicStreamSequencer& data_sequencer) override;
  void OnDatagramReceived(QuicConnectionId server_connection_id,
                          absl::Span<const std::byte> data) override;

 private:
  class SequencerReader;

  int ParseAndHandleSingleControlRequest(QuicConnectionId server_connection_id,
                                         SequencerReader& reader);

  BasicQuicServer* absl_nonnull server_;

  // Connections registered on establishment of the control stream.
  absl::flat_hash_set<QuicConnectionId> connections_;
};

}  // namespace quic::test

#endif  // QUICHE_QUIC_QBONE_TEST_TOOLS_QBONE_BASIC_QUIC_SERVER_HANDLER_H_
