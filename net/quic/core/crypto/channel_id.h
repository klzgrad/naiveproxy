// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_CHANNEL_ID_H_
#define NET_QUIC_CORE_CRYPTO_CHANNEL_ID_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

// ChannelIDKey is an interface that supports signing with and serializing a
// ChannelID key.
class QUIC_EXPORT_PRIVATE ChannelIDKey {
 public:
  virtual ~ChannelIDKey() {}

  // Sign signs |signed_data| using the ChannelID private key and puts the
  // signature into |out_signature|. It returns true on success.
  virtual bool Sign(QuicStringPiece signed_data,
                    std::string* out_signature) const = 0;

  // SerializeKey returns the serialized ChannelID public key.
  virtual std::string SerializeKey() const = 0;
};

// ChannelIDSourceCallback provides a generic mechanism for a ChannelIDSource
// to call back after an asynchronous GetChannelIDKey operation.
class ChannelIDSourceCallback {
 public:
  virtual ~ChannelIDSourceCallback() {}

  // Run is called on the original thread to mark the completion of an
  // asynchonous GetChannelIDKey operation. If |*channel_id_key| is not nullptr
  // then the channel ID lookup is successful. |Run| may take ownership of
  // |*channel_id_key| by calling |release| on it.
  virtual void Run(std::unique_ptr<ChannelIDKey>* channel_id_key) = 0;
};

// ChannelIDSource is an abstract interface by which a QUIC client can obtain
// a ChannelIDKey for a given hostname.
class QUIC_EXPORT_PRIVATE ChannelIDSource {
 public:
  virtual ~ChannelIDSource() {}

  // GetChannelIDKey looks up the ChannelIDKey for |hostname|. On success it
  // returns QUIC_SUCCESS and stores the ChannelIDKey in |*channel_id_key|,
  // which the caller takes ownership of. On failure, it returns QUIC_FAILURE.
  //
  // This function may also return QUIC_PENDING, in which case the
  // ChannelIDSource will call back, on the original thread, via |callback|
  // when complete. In this case, the ChannelIDSource will take ownership of
  // |callback|.
  virtual QuicAsyncStatus GetChannelIDKey(
      const std::string& hostname,
      std::unique_ptr<ChannelIDKey>* channel_id_key,
      ChannelIDSourceCallback* callback) = 0;
};

// ChannelIDVerifier verifies ChannelID signatures.
class QUIC_EXPORT_PRIVATE ChannelIDVerifier {
 public:
  // kContextStr is prepended to the data to be signed in order to ensure that
  // a ChannelID signature cannot be used in a different context. (The
  // terminating NUL byte is inclued.)
  static const char kContextStr[];
  // kClientToServerStr follows kContextStr to specify that the ChannelID is
  // being used in the client to server direction. (The terminating NUL byte is
  // included.)
  static const char kClientToServerStr[];

  // Verify returns true iff |signature| is a valid signature of |signed_data|
  // by |key|.
  static bool Verify(QuicStringPiece key,
                     QuicStringPiece signed_data,
                     QuicStringPiece signature);

  // FOR TESTING ONLY: VerifyRaw returns true iff |signature| is a valid
  // signature of |signed_data| by |key|. |is_channel_id_signature| indicates
  // whether |signature| is a ChannelID signature (with kContextStr prepended
  // to the data to be signed).
  static bool VerifyRaw(QuicStringPiece key,
                        QuicStringPiece signed_data,
                        QuicStringPiece signature,
                        bool is_channel_id_signature);

 private:
  DISALLOW_COPY_AND_ASSIGN(ChannelIDVerifier);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_CHANNEL_ID_H_
