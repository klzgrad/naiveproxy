// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/common_cert_set.h"

#include <cstddef>

#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace {

class CommonCertSetsEmpty : public CommonCertSets {
 public:
  // CommonCertSets interface.
  quiche::QuicheStringPiece GetCommonHashes() const override {
    return quiche::QuicheStringPiece();
  }

  quiche::QuicheStringPiece GetCert(uint64_t /* hash */,
                                    uint32_t /* index */) const override {
    return quiche::QuicheStringPiece();
  }

  bool MatchCert(quiche::QuicheStringPiece /* cert */,
                 quiche::QuicheStringPiece /* common_set_hashes */,
                 uint64_t* /* out_hash */,
                 uint32_t* /* out_index */) const override {
    return false;
  }

  CommonCertSetsEmpty() {}
  CommonCertSetsEmpty(const CommonCertSetsEmpty&) = delete;
  CommonCertSetsEmpty& operator=(const CommonCertSetsEmpty&) = delete;
  ~CommonCertSetsEmpty() override {}
};

}  // anonymous namespace

CommonCertSets::~CommonCertSets() {}

// static
const CommonCertSets* CommonCertSets::GetInstanceQUIC() {
  static CommonCertSetsEmpty* certs = new CommonCertSetsEmpty();
  return certs;
}

}  // namespace quic
