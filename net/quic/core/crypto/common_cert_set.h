// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_COMMON_CERT_SET_H_
#define NET_QUIC_CORE_CRYPTO_COMMON_CERT_SET_H_

#include <cstdint>

#include "base/compiler_specific.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

// CommonCertSets is an interface to an object that contains a number of common
// certificate sets and can match against them.
class QUIC_EXPORT_PRIVATE CommonCertSets {
 public:
  virtual ~CommonCertSets();

  // GetInstanceQUIC returns the standard QUIC common certificate sets.
  static const CommonCertSets* GetInstanceQUIC();

  // GetCommonHashes returns a QuicStringPiece containing the hashes of common
  // sets supported by this object. The 64-bit hashes are concatenated in the
  // QuicStringPiece.
  virtual QuicStringPiece GetCommonHashes() const = 0;

  // GetCert returns a specific certificate (at index |index|) in the common
  // set identified by |hash|. If no such certificate is known, an empty
  // QuicStringPiece is returned.
  virtual QuicStringPiece GetCert(uint64_t hash, uint32_t index) const = 0;

  // MatchCert tries to find |cert| in one of the common certificate sets
  // identified by |common_set_hashes|. On success it puts the hash of the
  // set in |out_hash|, the index of |cert| in the set in |out_index| and
  // returns true. Otherwise it returns false.
  virtual bool MatchCert(QuicStringPiece cert,
                         QuicStringPiece common_set_hashes,
                         uint64_t* out_hash,
                         uint32_t* out_index) const = 0;
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_COMMON_CERT_SET_H_
