// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/crypto/scoped_evp_aead_ctx.h"

namespace quic {

ScopedEVPAEADCtx::ScopedEVPAEADCtx() {
  ctx_.aead = nullptr;
  ctx_.aead_state = nullptr;
}

ScopedEVPAEADCtx::~ScopedEVPAEADCtx() {
  if (ctx_.aead != nullptr) {
    EVP_AEAD_CTX_cleanup(&ctx_);
  }
}

EVP_AEAD_CTX* ScopedEVPAEADCtx::get() {
  return &ctx_;
}

}  // namespace quic
