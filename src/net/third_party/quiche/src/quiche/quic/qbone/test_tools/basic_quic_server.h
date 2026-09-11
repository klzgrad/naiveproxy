// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_TEST_TOOLS_BASIC_QUIC_SERVER_H_
#define QUICHE_QUIC_QBONE_TEST_TOOLS_BASIC_QUIC_SERVER_H_

#include <cstddef>
#include <memory>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_stream_sequencer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/tls_server_handshaker.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_socket_address.h"

namespace quic::test {

// Lightweight QUIC (not necessarily HTTP/3) server for tests and experiments,
// especially for simulating QBONE Terminator functionality. Receives traffic
// over a socket on the given socket address, which may be a loopback interface
// for same-machine (or same-process) testing. Runs on its own dedicated server
// thread for ease of running same-process as a client.
class BasicQuicServer final {
 public:
  // Interface for handling server events. All calls will be made on the server
  // thread.
  class Handler {
   public:
    virtual ~Handler() = default;

    // Called on construction of a new QuicSession. Returns the QuicCryptoStream
    // to use for the session.
    virtual std::unique_ptr<QuicCryptoStream> OnNewSession(
        QuicSession* session, const QuicCryptoServerConfig* crypto_config) {
      return std::make_unique<TlsServerHandshaker>(session, crypto_config);
    }

    virtual void OnSessionEnd(QuicConnectionId server_connection_id) {}

    // See QuicSession::SelectAlpn.
    virtual std::vector<absl::string_view>::const_iterator SelectAlpn(
        QuicConnectionId server_connection_id,
        const std::vector<absl::string_view>& alpns) const {
      return alpns.cbegin();
    }

    // Called on initiation of a new stream from the peer. Returns true iff the
    // stream should be accepted and created.
    virtual bool OnNewStream(QuicConnectionId server_connection_id,
                             QuicStreamId stream_id) {
      return true;
    }

    // Called when new data is available on a stream. Returns the number of
    // bytes that were successfully processed and that should be consumed from
    // stream buffers.
    virtual int OnStreamDataAvailable(
        QuicConnectionId server_connection_id, QuicStreamId stream_id,
        const QuicStreamSequencer& data_sequencer) {
      return data_sequencer.ReadableBytes();
    }

    virtual void OnDatagramReceived(QuicConnectionId server_connection_id,
                                    absl::Span<const std::byte> data) {}
  };

  BasicQuicServer(quiche::QuicheSocketAddress socket_address,
                  std::unique_ptr<ProofSource> absl_nonnull proof_source);
  ~BasicQuicServer();

  absl::Status Start(std::unique_ptr<Handler> absl_nonnull handler);
  absl::Status Stop();

  // The following interactions should be safe from any thread, and will
  // marshall to the server thread as necessary.

  // Not guaranteed to run if the server is stopped.
  void Schedule(quiche::SingleUseCallback<void()> callback);
  // Not guaranteed to ever return if the server is stopped.
  void ScheduleAndWaitForCompletion(quiche::SingleUseCallback<void()> callback);

  absl::StatusOr<quiche::QuicheSocketAddress> bound_address();

  // Returns the number of bytes consumed into the stream (written or buffered).
  absl::StatusOr<int> SendStreamData(QuicConnectionId server_connection_id,
                                     QuicStreamId stream_id,
                                     absl::Span<const std::byte> data,
                                     bool fin = false);
  absl::StatusOr<int> SendStreamData(QuicConnectionId server_connection_id,
                                     QuicStreamId stream_id,
                                     absl::Span<quiche::QuicheMemSlice> data,
                                     bool fin = false);

  absl::Status SendDatagram(QuicConnectionId server_connection_id,
                            absl::Span<const std::byte> data);
  absl::Status SendDatagram(QuicConnectionId server_connection_id,
                            absl::Span<quiche::QuicheMemSlice> data);

 private:
  class ServerThread;

  const quiche::QuicheSocketAddress socket_address_;

  const QuicConfig config_;
  const std::unique_ptr<QuicCryptoServerConfig> crypto_config_;
  std::unique_ptr<ServerThread> server_thread_;
};

}  // namespace quic::test

#endif  // QUICHE_QUIC_QBONE_TEST_TOOLS_BASIC_QUIC_SERVER_H_
