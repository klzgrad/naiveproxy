// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HANDSHAKER_DELEGATE_INTERFACE_H_
#define QUICHE_QUIC_CORE_HANDSHAKER_DELEGATE_INTERFACE_H_

#include "quiche/quic/core/crypto/transport_parameters.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"

namespace quic {

class QuicDecrypter;
class QuicEncrypter;

// Pure virtual class to get notified when particular handshake events occurred.
class QUICHE_EXPORT HandshakerDelegateInterface {
 public:
  virtual ~HandshakerDelegateInterface() {}

  // Called when new decryption key of |level| is available. Returns true if
  // decrypter is set successfully, otherwise, returns false.
  virtual bool OnNewDecryptionKeyAvailable(
      EncryptionLevel level, std::unique_ptr<QuicDecrypter> decrypter,
      bool set_alternative_decrypter, bool latch_once_used) = 0;

  // Called when new encryption key of |level| is available.
  virtual void OnNewEncryptionKeyAvailable(
      EncryptionLevel level, std::unique_ptr<QuicEncrypter> encrypter) = 0;

  // Called to set default encryption level to |level|. Only used in QUIC
  // crypto.
  virtual void SetDefaultEncryptionLevel(EncryptionLevel level) = 0;

  // Called when both 1-RTT read and write keys are available. Only used in TLS
  // handshake.
  virtual void OnTlsHandshakeComplete() = 0;

  // Called on the client side when handshake state change to
  // HANDSHAKE_CONFIRMED. Only used in TLS handshake.
  virtual void OnTlsHandshakeConfirmed() = 0;

  // Called to discard old decryption keys to stop processing packets of
  // encryption |level|.
  virtual void DiscardOldDecryptionKey(EncryptionLevel level) = 0;

  // Called to discard old encryption keys (and neuter obsolete data).
  // TODO(fayang): consider to combine this with DiscardOldDecryptionKey.
  virtual void DiscardOldEncryptionKey(EncryptionLevel level) = 0;

  // Called to neuter ENCRYPTION_INITIAL data (without discarding initial keys).
  virtual void NeuterUnencryptedData() = 0;

  // Called to neuter data of HANDSHAKE_DATA packet number space. Only used in
  // QUIC crypto. This is called 1) when a client switches to forward secure
  // encryption level and 2) a server successfully processes a forward secure
  // packet.
  virtual void NeuterHandshakeData() = 0;

  // Called when 0-RTT data is rejected by the server. This is only called in
  // TLS handshakes and only called on clients.
  virtual void OnZeroRttRejected(int reason) = 0;

  // Fills in |params| with values from the delegate's QuicConfig.
  // Returns whether the operation succeeded.
  virtual bool FillTransportParameters(TransportParameters* params) = 0;

  // Read |params| and apply the values to the delegate's QuicConfig.
  // On failure, returns a QuicErrorCode and saves a detailed error in
  // |error_details|.
  virtual QuicErrorCode ProcessTransportParameters(
      const TransportParameters& params, bool is_resumption,
      std::string* error_details) = 0;

  // Called at the end of an handshake operation callback.
  virtual void OnHandshakeCallbackDone() = 0;

  // Whether a packet flusher is currently attached.
  virtual bool PacketFlusherAttached() const = 0;

  // Get the QUIC version currently in use. tls_handshaker needs this to pass
  // to crypto_utils to apply version-dependent HKDF labels.
  virtual ParsedQuicVersion parsed_version() const = 0;

  // Called after an ClientHelloInner is encrypted and sent as a client.
  virtual void OnEncryptedClientHelloSent(
      absl::string_view client_hello) const = 0;

  // Called after an ClientHelloInner is received and decrypted as a server.
  virtual void OnEncryptedClientHelloReceived(
      absl::string_view client_hello) const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HANDSHAKER_DELEGATE_INTERFACE_H_
