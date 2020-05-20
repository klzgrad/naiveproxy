// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A base class for the toy client, which connects to a specified port and sends
// QUIC request to that endpoint.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SPDY_CLIENT_BASE_H_
#define QUICHE_QUIC_TOOLS_QUIC_SPDY_CLIENT_BASE_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/http/quic_client_push_promise_index.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_session.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/tools/quic_client_base.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class ProofVerifier;
class QuicServerId;
class SessionCache;

class QuicSpdyClientBase : public QuicClientBase,
                           public QuicClientPushPromiseIndex::Delegate,
                           public QuicSpdyStream::Visitor {
 public:
  // A ResponseListener is notified when a complete response is received.
  class ResponseListener {
   public:
    ResponseListener() {}
    virtual ~ResponseListener() {}
    virtual void OnCompleteResponse(
        QuicStreamId id,
        const spdy::SpdyHeaderBlock& response_headers,
        const std::string& response_body) = 0;
  };

  // A piece of data that can be sent multiple times. For example, it can be a
  // HTTP request that is resent after a connect=>version negotiation=>reconnect
  // sequence.
  class QuicDataToResend {
   public:
    // |headers| may be null, since it's possible to send data without headers.
    QuicDataToResend(std::unique_ptr<spdy::SpdyHeaderBlock> headers,
                     quiche::QuicheStringPiece body,
                     bool fin);
    QuicDataToResend(const QuicDataToResend&) = delete;
    QuicDataToResend& operator=(const QuicDataToResend&) = delete;

    virtual ~QuicDataToResend();

    // Must be overridden by specific classes with the actual method for
    // re-sending data.
    virtual void Resend() = 0;

   protected:
    std::unique_ptr<spdy::SpdyHeaderBlock> headers_;
    quiche::QuicheStringPiece body_;
    bool fin_;
  };

  QuicSpdyClientBase(const QuicServerId& server_id,
                     const ParsedQuicVersionVector& supported_versions,
                     const QuicConfig& config,
                     QuicConnectionHelperInterface* helper,
                     QuicAlarmFactory* alarm_factory,
                     std::unique_ptr<NetworkHelper> network_helper,
                     std::unique_ptr<ProofVerifier> proof_verifier,
                     std::unique_ptr<SessionCache> session_cache);
  QuicSpdyClientBase(const QuicSpdyClientBase&) = delete;
  QuicSpdyClientBase& operator=(const QuicSpdyClientBase&) = delete;

  ~QuicSpdyClientBase() override;

  // QuicSpdyStream::Visitor
  void OnClose(QuicSpdyStream* stream) override;

  // A spdy session has to call CryptoConnect on top of the regular
  // initialization.
  void InitializeSession() override;

  // Sends an HTTP request and does not wait for response before returning.
  void SendRequest(const spdy::SpdyHeaderBlock& headers,
                   quiche::QuicheStringPiece body,
                   bool fin);

  // Sends an HTTP request and waits for response before returning.
  void SendRequestAndWaitForResponse(const spdy::SpdyHeaderBlock& headers,
                                     quiche::QuicheStringPiece body,
                                     bool fin);

  // Sends a request simple GET for each URL in |url_list|, and then waits for
  // each to complete.
  void SendRequestsAndWaitForResponse(const std::vector<std::string>& url_list);

  // Returns a newly created QuicSpdyClientStream.
  QuicSpdyClientStream* CreateClientStream();

  // Returns a the session used for this client downcasted to a
  // QuicSpdyClientSession.
  QuicSpdyClientSession* client_session();

  QuicClientPushPromiseIndex* push_promise_index() {
    return &push_promise_index_;
  }

  bool CheckVary(const spdy::SpdyHeaderBlock& client_request,
                 const spdy::SpdyHeaderBlock& promise_request,
                 const spdy::SpdyHeaderBlock& promise_response) override;
  void OnRendezvousResult(QuicSpdyStream*) override;

  // If the crypto handshake has not yet been confirmed, adds the data to the
  // queue of data to resend if the client receives a stateless reject.
  // Otherwise, deletes the data.
  void MaybeAddQuicDataToResend(
      std::unique_ptr<QuicDataToResend> data_to_resend);

  void set_store_response(bool val) { store_response_ = val; }

  int latest_response_code() const;
  const std::string& latest_response_headers() const;
  const std::string& preliminary_response_headers() const;
  const spdy::SpdyHeaderBlock& latest_response_header_block() const;
  const std::string& latest_response_body() const;
  const std::string& latest_response_trailers() const;

  void set_response_listener(std::unique_ptr<ResponseListener> listener) {
    response_listener_ = std::move(listener);
  }

  void set_drop_response_body(bool drop_response_body) {
    drop_response_body_ = drop_response_body;
  }
  bool drop_response_body() const { return drop_response_body_; }

  // Set the max promise id for the client session.
  // TODO(b/151641466): Rename this method.
  void SetMaxAllowedPushId(QuicStreamId max) { max_allowed_push_id_ = max; }

  // Disables the use of the QPACK dynamic table and of blocked streams.
  // Must be called before InitializeSession().
  void disable_qpack_dynamic_table() { disable_qpack_dynamic_table_ = true; }

  bool EarlyDataAccepted() override;
  bool ReceivedInchoateReject() override;

 protected:
  int GetNumSentClientHellosFromSession() override;
  int GetNumReceivedServerConfigUpdatesFromSession() override;

  // Takes ownership of |connection|.
  std::unique_ptr<QuicSession> CreateQuicClientSession(
      const quic::ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection) override;

  void ClearDataToResend() override;

  void ResendSavedData() override;

  void AddPromiseDataToResend(const spdy::SpdyHeaderBlock& headers,
                              quiche::QuicheStringPiece body,
                              bool fin);
  bool HasActiveRequests() override;

 private:
  // Specific QuicClient class for storing data to resend.
  class ClientQuicDataToResend : public QuicDataToResend {
   public:
    ClientQuicDataToResend(std::unique_ptr<spdy::SpdyHeaderBlock> headers,
                           quiche::QuicheStringPiece body,
                           bool fin,
                           QuicSpdyClientBase* client)
        : QuicDataToResend(std::move(headers), body, fin), client_(client) {
      DCHECK(headers_);
      DCHECK(client);
    }

    ClientQuicDataToResend(const ClientQuicDataToResend&) = delete;
    ClientQuicDataToResend& operator=(const ClientQuicDataToResend&) = delete;
    ~ClientQuicDataToResend() override {}

    void Resend() override;

   private:
    QuicSpdyClientBase* client_;
  };

  void SendRequestInternal(spdy::SpdyHeaderBlock sanitized_headers,
                           quiche::QuicheStringPiece body,
                           bool fin);

  // Index of pending promised streams. Must outlive |session_|.
  QuicClientPushPromiseIndex push_promise_index_;

  // If true, store the latest response code, headers, and body.
  bool store_response_;
  // HTTP response code from most recent response.
  int latest_response_code_;
  // HTTP/2 headers from most recent response.
  std::string latest_response_headers_;
  // preliminary 100 Continue HTTP/2 headers from most recent response, if any.
  std::string preliminary_response_headers_;
  // HTTP/2 headers from most recent response.
  spdy::SpdyHeaderBlock latest_response_header_block_;
  // Body of most recent response.
  std::string latest_response_body_;
  // HTTP/2 trailers from most recent response.
  std::string latest_response_trailers_;

  // Listens for full responses.
  std::unique_ptr<ResponseListener> response_listener_;

  // Keeps track of any data that must be resent upon a subsequent successful
  // connection, in case the client receives a stateless reject.
  std::vector<std::unique_ptr<QuicDataToResend>> data_to_resend_on_connect_;

  std::unique_ptr<ClientQuicDataToResend> push_promise_data_to_resend_;

  bool drop_response_body_ = false;

  // The max promise id to set on the client session when created.
  QuicStreamId max_allowed_push_id_;

  bool disable_qpack_dynamic_table_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SPDY_CLIENT_BASE_H_
