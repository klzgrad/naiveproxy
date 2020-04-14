// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_H_
#define QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_H_

#include <cstdint>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_compressed_certs_cache.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_handshaker.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

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
  virtual bool ZeroRttAttempted() const = 0;
  virtual const CachedNetworkParameters* PreviousCachedNetworkParams()
      const = 0;
  virtual void SetPreviousCachedNetworkParams(
      CachedNetworkParameters cached_network_params) = 0;

  // NOTE: Indicating that the Expect-CT header should be sent here presents
  // a layering violation to some extent. The Expect-CT header only applies to
  // HTTP connections, while this class can be used for non-HTTP applications.
  // However, it is exposed here because that is the only place where the
  // configuration for the certificate used in the connection is accessible.
  virtual bool ShouldSendExpectCTHeader() const = 0;
};

class QUIC_EXPORT_PRIVATE QuicCryptoServerStream
    : public QuicCryptoServerStreamBase {
 public:
  // QuicCryptoServerStream creates a HandshakerInterface at construction time
  // based on the QuicTransportVersion of the connection. Different
  // HandshakerInterfaces provide implementations of different crypto handshake
  // protocols. Currently QUIC crypto is the only protocol implemented; a future
  // HandshakerInterface will use TLS as the handshake protocol.
  // QuicCryptoServerStream delegates all of its public methods to its
  // HandshakerInterface.
  //
  // This setup of the crypto stream delegating its implementation to the
  // handshaker results in the handshaker reading and writing bytes on the
  // crypto stream, instead of the handshake rpassing the stream bytes to send.
  class QUIC_EXPORT_PRIVATE HandshakerInterface {
   public:
    virtual ~HandshakerInterface() {}

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
    virtual bool ZeroRttAttempted() const = 0;
    virtual void SetPreviousCachedNetworkParams(
        CachedNetworkParameters cached_network_params) = 0;
    virtual void OnPacketDecrypted(EncryptionLevel level) = 0;

    // NOTE: Indicating that the Expect-CT header should be sent here presents a
    // layering violation to some extent. The Expect-CT header only applies to
    // HTTP connections, while this class can be used for non-HTTP applications.
    // However, it is exposed here because that is the only place where the
    // configuration for the certificate used in the connection is accessible.
    virtual bool ShouldSendExpectCTHeader() const = 0;

    // Returns true once any encrypter (initial/0RTT or final/1RTT) has been set
    // for the connection.
    virtual bool encryption_established() const = 0;

    // Returns true once 1RTT keys are available.
    virtual bool one_rtt_keys_available() const = 0;

    // Returns the parameters negotiated in the crypto handshake.
    virtual const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
        const = 0;

    // Used by QuicCryptoStream to parse data received on this stream.
    virtual CryptoMessageParser* crypto_message_parser() = 0;

    // Get current handshake state.
    virtual HandshakeState GetHandshakeState() const = 0;

    // Used by QuicCryptoStream to know how much unprocessed data can be
    // buffered at each encryption level.
    virtual size_t BufferSizeLimitForLevel(EncryptionLevel level) const = 0;
  };

  class QUIC_EXPORT_PRIVATE Helper {
   public:
    virtual ~Helper() {}

    // Returns true if |message|, which was received on |self_address| is
    // acceptable according to the visitor's policy. Otherwise, returns false
    // and populates |error_details|.
    virtual bool CanAcceptClientHello(const CryptoHandshakeMessage& message,
                                      const QuicSocketAddress& client_address,
                                      const QuicSocketAddress& peer_address,
                                      const QuicSocketAddress& self_address,
                                      std::string* error_details) const = 0;
  };

  QuicCryptoServerStream(const QuicCryptoServerStream&) = delete;
  QuicCryptoServerStream& operator=(const QuicCryptoServerStream&) = delete;

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
  bool ZeroRttAttempted() const override;
  void SetPreviousCachedNetworkParams(
      CachedNetworkParameters cached_network_params) override;
  bool ShouldSendExpectCTHeader() const override;

  bool encryption_established() const override;
  bool one_rtt_keys_available() const override;
  const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const override;
  CryptoMessageParser* crypto_message_parser() override;
  void OnPacketDecrypted(EncryptionLevel level) override;
  void OnOneRttPacketAcknowledged() override {}
  void OnHandshakeDoneReceived() override;
  HandshakeState GetHandshakeState() const override;
  size_t BufferSizeLimitForLevel(EncryptionLevel level) const override;
  void OnSuccessfulVersionNegotiation(
      const ParsedQuicVersion& version) override;

 protected:
  QUIC_EXPORT_PRIVATE friend std::unique_ptr<QuicCryptoServerStreamBase>
  CreateCryptoServerStream(const QuicCryptoServerConfig* crypto_config,
                           QuicCompressedCertsCache* compressed_certs_cache,
                           QuicSession* session,
                           Helper* helper);

  QuicCryptoServerStream(const QuicCryptoServerConfig* crypto_config,
                         QuicCompressedCertsCache* compressed_certs_cache,
                         QuicSession* session,
                         Helper* helper);
  // Provided so that subclasses can provide their own handshaker.
  // set_handshaker can only be called if this QuicCryptoServerStream's
  // handshaker hasn't been set yet. If set_handshaker is called outside of
  // OnSuccessfulVersionNegotiation, then that method must be overridden to not
  // set a handshaker.
  QuicCryptoServerStream(const QuicCryptoServerConfig* crypto_config,
                         QuicCompressedCertsCache* compressed_certs_cache,
                         QuicSession* session,
                         Helper* helper,
                         std::unique_ptr<HandshakerInterface> handshaker);
  void set_handshaker(std::unique_ptr<HandshakerInterface> handshaker);
  HandshakerInterface* handshaker() const;

  const QuicCryptoServerConfig* crypto_config() const;
  QuicCompressedCertsCache* compressed_certs_cache() const;
  Helper* helper() const;

 private:
  std::unique_ptr<HandshakerInterface> handshaker_;
  // Latched value of quic_create_server_handshaker_in_constructor flag.
  bool create_handshaker_in_constructor_;

  // Arguments from QuicCryptoServerStream constructor that might need to be
  // passed to the HandshakerInterface constructor in its late construction.
  const QuicCryptoServerConfig* crypto_config_;
  QuicCompressedCertsCache* compressed_certs_cache_;
  Helper* helper_;
};

// Creates an appropriate QuicCryptoServerStream for the provided parameters,
// including the version used by |session|. |crypto_config|, |session|, and
// |helper| must all outlive the stream. The caller takes ownership of the
// returned object.
QUIC_EXPORT_PRIVATE std::unique_ptr<QuicCryptoServerStreamBase>
CreateCryptoServerStream(const QuicCryptoServerConfig* crypto_config,
                         QuicCompressedCertsCache* compressed_certs_cache,
                         QuicSession* session,
                         QuicCryptoServerStream::Helper* helper);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_H_
