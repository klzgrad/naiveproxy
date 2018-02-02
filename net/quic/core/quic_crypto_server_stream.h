// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_H_
#define NET_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/macros.h"
#include "net/quic/core/crypto/crypto_handshake.h"
#include "net/quic/core/crypto/quic_compressed_certs_cache.h"
#include "net/quic/core/crypto/quic_crypto_server_config.h"
#include "net/quic/core/proto/source_address_token.pb.h"
#include "net/quic/core/quic_config.h"
#include "net/quic/core/quic_crypto_handshaker.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/core/quic_session.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

class CachedNetworkParameters;
class CryptoHandshakeMessage;
class QuicCryptoServerConfig;
class QuicCryptoServerStreamBase;

// TODO(alyssar) see what can be moved out of QuicCryptoServerStream with
// various code and test refactoring.
class QUIC_EXPORT_PRIVATE QuicCryptoServerStreamBase : public QuicCryptoStream {
 public:
  explicit QuicCryptoServerStreamBase(QuicSession* session);

  ~QuicCryptoServerStreamBase() override {}

  // Cancel any outstanding callbacks, such as asynchronous validation of client
  // hello.
  virtual void CancelOutstandingCallbacks() = 0;

  // GetBase64SHA256ClientChannelID sets |*output| to the base64 encoded,
  // SHA-256 hash of the client's ChannelID key and returns true, if the client
  // presented a ChannelID. Otherwise it returns false.
  virtual bool GetBase64SHA256ClientChannelID(std::string* output) const = 0;

  virtual int NumServerConfigUpdateMessagesSent() const = 0;

  // Sends the latest server config and source-address token to the client.
  virtual void SendServerConfigUpdate(
      const CachedNetworkParameters* cached_network_params) = 0;

  // These are all accessors and setters to their respective counters.
  virtual uint8_t NumHandshakeMessages() const = 0;
  virtual uint8_t NumHandshakeMessagesWithServerNonces() const = 0;
  virtual bool UseStatelessRejectsIfPeerSupported() const = 0;
  virtual bool PeerSupportsStatelessRejects() const = 0;
  virtual bool ZeroRttAttempted() const = 0;
  virtual void SetPeerSupportsStatelessRejects(bool set) = 0;
  virtual const CachedNetworkParameters* PreviousCachedNetworkParams()
      const = 0;
  virtual void SetPreviousCachedNetworkParams(
      CachedNetworkParameters cached_network_params) = 0;

  // Checks the options on the handshake-message to see whether the
  // peer supports stateless-rejects.
  static bool DoesPeerSupportStatelessRejects(
      const CryptoHandshakeMessage& message);
};

class QUIC_EXPORT_PRIVATE QuicCryptoServerStream
    : public QuicCryptoServerStreamBase {
 public:
  // QuicCryptoServerStream creates a HandshakerDelegate at construction time
  // based on the QuicTransportVersion of the connection. Different
  // HandshakerDelegates provide implementations of different crypto handshake
  // protocols. Currently QUIC crypto is the only protocol implemented; a future
  // HandshakerDelegate will use TLS as the handshake protocol.
  // QuicCryptoServerStream delegates all of its public methods to its
  // HandshakerDelegate.
  //
  // This setup of the crypto stream delegating its implementation to the
  // handshaker results in the handshaker reading and writing bytes on the
  // crypto stream, instead of the handshake rpassing the stream bytes to send.
  class QUIC_EXPORT_PRIVATE HandshakerDelegate {
   public:
    virtual ~HandshakerDelegate() {}

    // Cancel any outstanding callbacks, such as asynchronous validation of
    // client hello.
    virtual void CancelOutstandingCallbacks() = 0;

    // GetBase64SHA256ClientChannelID sets |*output| to the base64 encoded,
    // SHA-256 hash of the client's ChannelID key and returns true, if the
    // client presented a ChannelID. Otherwise it returns false.
    virtual bool GetBase64SHA256ClientChannelID(std::string* output) const = 0;

    // Sends the latest server config and source-address token to the client.
    virtual void SendServerConfigUpdate(
        const CachedNetworkParameters* cached_network_params) = 0;

    // These are all accessors and setters to their respective counters.
    virtual uint8_t NumHandshakeMessages() const = 0;
    virtual uint8_t NumHandshakeMessagesWithServerNonces() const = 0;
    virtual int NumServerConfigUpdateMessagesSent() const = 0;
    virtual const CachedNetworkParameters* PreviousCachedNetworkParams()
        const = 0;
    virtual bool UseStatelessRejectsIfPeerSupported() const = 0;
    virtual bool PeerSupportsStatelessRejects() const = 0;
    virtual bool ZeroRttAttempted() const = 0;
    virtual void SetPeerSupportsStatelessRejects(
        bool peer_supports_stateless_rejects) = 0;
    virtual void SetPreviousCachedNetworkParams(
        CachedNetworkParameters cached_network_params) = 0;

    // NOTE: Indicating that the Expect-CT header should be sent here presents a
    // layering violation to some extent. The Expect-CT header only applies to
    // HTTP connections, while this class can be used for non-HTTP applications.
    // However, it is exposed here because that is the only place where the
    // configuration for the certificate used in the connection is accessible.
    virtual bool ShouldSendExpectCTHeader() const = 0;

    // Returns true once any encrypter (initial/0RTT or final/1RTT) has been set
    // for the connection.
    virtual bool encryption_established() const = 0;

    // Returns true once the crypto handshake has completed.
    virtual bool handshake_confirmed() const = 0;

    // Returns the parameters negotiated in the crypto handshake.
    virtual const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
        const = 0;

    // Used by QuicCryptoStream to parse data received on this stream.
    virtual CryptoMessageParser* crypto_message_parser() = 0;
  };

  class Helper {
   public:
    virtual ~Helper() {}

    // Given the current connection_id, generates a new ConnectionId to
    // be returned with a stateless reject.
    virtual QuicConnectionId GenerateConnectionIdForReject(
        QuicConnectionId connection_id) const = 0;

    // Returns true if |message|, which was received on |self_address| is
    // acceptable according to the visitor's policy. Otherwise, returns false
    // and populates |error_details|.
    virtual bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                                      const QuicSocketAddress& self_address,
                                      std::string* error_details) const = 0;
  };

  // |crypto_config| must outlive the stream.
  // |session| must outlive the stream.
  // |helper| must outlive the stream.
  QuicCryptoServerStream(const QuicCryptoServerConfig* crypto_config,
                         QuicCompressedCertsCache* compressed_certs_cache,
                         bool use_stateless_rejects_if_peer_supported,
                         QuicSession* session,
                         Helper* helper);

  ~QuicCryptoServerStream() override;

  // From QuicCryptoServerStreamBase
  void CancelOutstandingCallbacks() override;
  bool GetBase64SHA256ClientChannelID(std::string* output) const override;
  void SendServerConfigUpdate(
      const CachedNetworkParameters* cached_network_params) override;
  uint8_t NumHandshakeMessages() const override;
  uint8_t NumHandshakeMessagesWithServerNonces() const override;
  int NumServerConfigUpdateMessagesSent() const override;
  const CachedNetworkParameters* PreviousCachedNetworkParams() const override;
  bool UseStatelessRejectsIfPeerSupported() const override;
  bool PeerSupportsStatelessRejects() const override;
  bool ZeroRttAttempted() const override;
  void SetPeerSupportsStatelessRejects(
      bool peer_supports_stateless_rejects) override;
  void SetPreviousCachedNetworkParams(
      CachedNetworkParameters cached_network_params) override;

  // NOTE: Indicating that the Expect-CT header should be sent here presents
  // a layering violation to some extent. The Expect-CT header only applies to
  // HTTP connections, while this class can be used for non-HTTP applications.
  // However, it is exposed here because that is the only place where the
  // configuration for the certificate used in the connection is accessible.
  bool ShouldSendExpectCTHeader() const;

  bool encryption_established() const override;
  bool handshake_confirmed() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;

 protected:
  // Provided so that subclasses can provide their own handshaker.
  virtual HandshakerDelegate* handshaker() const;

 private:
  std::unique_ptr<HandshakerDelegate> handshaker_;

  DISALLOW_COPY_AND_ASSIGN(QuicCryptoServerStream);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_H_
