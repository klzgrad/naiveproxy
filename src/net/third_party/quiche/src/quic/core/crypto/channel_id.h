// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CHANNEL_ID_H_
#define QUICHE_QUIC_CORE_CRYPTO_CHANNEL_ID_H_

#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// ChannelIDVerifier verifies ChannelID signatures.
class QUIC_EXPORT_PRIVATE ChannelIDVerifier {
 public:
  ChannelIDVerifier() = delete;

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
  static bool Verify(quiche::QuicheStringPiece key,
                     quiche::QuicheStringPiece signed_data,
                     quiche::QuicheStringPiece signature);

  // FOR TESTING ONLY: VerifyRaw returns true iff |signature| is a valid
  // signature of |signed_data| by |key|. |is_channel_id_signature| indicates
  // whether |signature| is a ChannelID signature (with kContextStr prepended
  // to the data to be signed).
  static bool VerifyRaw(quiche::QuicheStringPiece key,
                        quiche::QuicheStringPiece signed_data,
                        quiche::QuicheStringPiece signature,
                        bool is_channel_id_signature);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CHANNEL_ID_H_
