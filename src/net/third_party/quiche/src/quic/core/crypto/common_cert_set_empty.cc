// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/crypto/common_cert_set.h"

#include <cstddef>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "quic/core/quic_utils.h"

namespace quic {

namespace {

class CommonCertSetsEmpty : public CommonCertSets {
 public:
  // CommonCertSets interface.
  absl::string_view GetCommonHashes() const override {
    return absl::string_view();
  }

  absl::string_view GetCert(uint64_t /* hash */,
                            uint32_t /* index */) const override {
    return absl::string_view();
  }

  bool MatchCert(absl::string_view /* cert */,
                 absl::string_view /* common_set_hashes */,
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
