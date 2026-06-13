// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_KEY_EXCHANGE_H_
#define QUICHE_QUIC_CORE_CRYPTO_KEY_EXCHANGE_H_

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Interface for a Diffie-Hellman key exchange with an asynchronous interface.
// This allows for implementations which hold the private key locally, as well
// as ones which make an RPC to an external key-exchange service.
class QUICHE_EXPORT AsynchronousKeyExchange {
 public:
  virtual ~AsynchronousKeyExchange() = default;

  // Callback base class for receiving the results of an async call to
  // CalculateSharedKeys.
  class QUICHE_EXPORT Callback {
   public:
    Callback() = default;
    virtual ~Callback() = default;

    // Invoked upon completion of CalculateSharedKeysAsync.
    //
    // |ok| indicates whether the operation completed successfully.  If false,
    // then the value pointed to by |shared_key| passed in to
    // CalculateSharedKeyAsync is undefined.
    virtual void Run(bool ok) = 0;

   private:
    Callback(const Callback&) = delete;
    Callback& operator=(const Callback&) = delete;
  };

  // CalculateSharedKey computes the shared key between a private key which is
  // conceptually owned by this object (though it may not be physically located
  // in this process) and a public value from the peer.  Callers should expect
  // that |callback| might be invoked synchronously.  Results will be written
  // into |*shared_key|.
  virtual void CalculateSharedKeyAsync(
      absl::string_view peer_public_value, std::string* shared_key,
      std::unique_ptr<Callback> callback) const = 0;

  // Tag indicating the key-exchange algorithm this object will use.
  virtual QuicTag type() const = 0;
};

// Interface for a Diffie-Hellman key exchange with both synchronous and
// asynchronous interfaces.  Only implementations which hold the private key
// locally should implement this interface.
class QUICHE_EXPORT SynchronousKeyExchange : public AsynchronousKeyExchange {
 public:
  virtual ~SynchronousKeyExchange() = default;

  // AyncKeyExchange API.  Note that this method is marked 'final.'  Subclasses
  // should implement CalculateSharedKeySync only.
  void CalculateSharedKeyAsync(absl::string_view peer_public_value,
                               std::string* shared_key,
                               std::unique_ptr<Callback> callback) const final {
    const bool ok = CalculateSharedKeySync(peer_public_value, shared_key);
    callback->Run(ok);
  }

  // CalculateSharedKey computes the shared key between a local private key and
  // a public value from the peer.  Results will be written into |*shared_key|.
  virtual bool CalculateSharedKeySync(absl::string_view peer_public_value,
                                      std::string* shared_key) const = 0;

  // public_value returns the local public key which can be sent to a peer in
  // order to complete a key exchange. The returned absl::string_view is
  // a reference to a member of this object and is only valid for as long as it
  // exists.
  virtual absl::string_view public_value() const = 0;
};

// Create a SynchronousKeyExchange object which will use a keypair generated
// from |private_key|, and a key-exchange algorithm specified by |type|, which
// must be one of {kC255, kC256}.  Returns nullptr if |private_key| or |type| is
// invalid.
std::unique_ptr<SynchronousKeyExchange> CreateLocalSynchronousKeyExchange(
    QuicTag type, absl::string_view private_key);

// Create a SynchronousKeyExchange object which will use a keypair generated
// from |rand|, and a key-exchange algorithm specified by |type|, which must be
// one of {kC255, kC256}.  Returns nullptr if |type| is invalid.
std::unique_ptr<SynchronousKeyExchange> CreateLocalSynchronousKeyExchange(
    QuicTag type, QuicRandom* rand);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_KEY_EXCHANGE_H_
