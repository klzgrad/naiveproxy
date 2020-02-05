// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HANDSHAKER_DELEGATE_INTERFACE_H_
#define QUICHE_QUIC_CORE_HANDSHAKER_DELEGATE_INTERFACE_H_

#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

class QuicDecrypter;
class QuicEncrypter;

// Pure virtual class to get notified when particular handshake events occurred.
class QUIC_EXPORT_PRIVATE HandshakerDelegateInterface {
 public:
  virtual ~HandshakerDelegateInterface() {}

  // Called when new keys are available.
  virtual void OnNewKeysAvailable(EncryptionLevel level,
                                  std::unique_ptr<QuicDecrypter> decrypter,
                                  bool set_alternative_decrypter,
                                  bool latch_once_used,
                                  std::unique_ptr<QuicEncrypter> encrypter) = 0;

  // Called to set default encryption level to |level|.
  virtual void SetDefaultEncryptionLevel(EncryptionLevel level) = 0;

  // Called to discard old decryption keys to stop processing packets of
  // encryption |level|.
  virtual void DiscardOldDecryptionKey(EncryptionLevel level) = 0;

  // Called to discard old encryption keys (and neuter obsolete data).
  // TODO(fayang): consider to combine this with DiscardOldDecryptionKey.
  virtual void DiscardOldEncryptionKey(EncryptionLevel level) = 0;

  // Called to neuter ENCRYPTION_INITIAL data (without discarding initial keys).
  virtual void NeuterUnencryptedData() = 0;

  // Called to neuter data of HANDSHAKE_DATA packet number space. In QUIC
  // crypto, this is called 1) when a client switches to forward secure
  // encryption level and 2) a server successfully processes a forward secure
  // packet. Temporarily use this method in TLS handshake when both endpoints
  // switch to forward secure encryption level.
  // TODO(fayang): use DiscardOldEncryptionKey instead of this method in TLS
  // handshake when handshake key discarding settles down.
  virtual void NeuterHandshakeData() = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HANDSHAKER_DELEGATE_INTERFACE_H_
