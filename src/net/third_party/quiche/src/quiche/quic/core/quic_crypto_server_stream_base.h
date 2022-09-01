// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_BASE_H_
#define QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_BASE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "quiche/quic/core/crypto/crypto_handshake.h"
#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_crypto_handshaker.h"
#include "quiche/quic/core/quic_crypto_stream.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/platform/api/quic_export.h"

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

  // Disables TLS resumption, should be called as early as possible.
  // Return true if resumption is disabled.
  // Return false if nothing happened, typically it means it is called too late.
  virtual bool DisableResumption() = 0;

  // Returns true if the connection was a successful 0-RTT resumption.
  virtual bool IsZeroRtt() const = 0;

  // Returns true if the connection was the result of a resumption handshake,
  // whether 0-RTT or not.
  virtual bool IsResumption() const = 0;

  // Returns true if the client attempted a resumption handshake, whether or not
  // the resumption actually occurred.
  virtual bool ResumptionAttempted() const = 0;

  // Returns true if the client attempted to use early data, as indicated by the
  // "early_data" TLS extension. TLS only.
  virtual bool EarlyDataAttempted() const = 0;

  // NOTE: Indicating that the Expect-CT header should be sent here presents
  // a layering violation to some extent. The Expect-CT header only applies to
  // HTTP connections, while this class can be used for non-HTTP applications.
  // However, it is exposed here because that is the only place where the
  // configuration for the certificate used in the connection is accessible.
  virtual bool ShouldSendExpectCTHeader() const = 0;

  // Return true if a cert was picked that matched the SNI hostname.
  virtual bool DidCertMatchSni() const = 0;

  // Returns the Details from the latest call to ProofSource::GetProof or
  // ProofSource::ComputeTlsSignature. Returns nullptr if no such call has been
  // made. The Details are owned by the QuicCryptoServerStreamBase and the
  // pointer is only valid while the owning object is still valid.
  virtual const ProofSource::Details* ProofSourceDetails() const = 0;

  bool ExportKeyingMaterial(absl::string_view /*label*/,
                            absl::string_view /*context*/,
                            size_t /*result_len*/,
                            std::string* /*result*/) override {
    QUICHE_NOTREACHED();
    return false;
  }
};

// Creates an appropriate QuicCryptoServerStream for the provided parameters,
// including the version used by |session|. |crypto_config|, |session|, and
// |helper| must all outlive the stream. The caller takes ownership of the
// returned object.
QUIC_EXPORT_PRIVATE std::unique_ptr<QuicCryptoServerStreamBase>
CreateCryptoServerStream(const QuicCryptoServerConfig* crypto_config,
                         QuicCompressedCertsCache* compressed_certs_cache,
                         QuicSession* session,
                         QuicCryptoServerStreamBase::Helper* helper);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CRYPTO_SERVER_STREAM_BASE_H_
