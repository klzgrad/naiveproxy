// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/token.h"

#include <inttypes.h>

#include "base/rand_util.h"
#include "base/strings/stringprintf.h"

namespace base {

// static
Token Token::CreateRandom() {
  Token token;

  // Use base::RandBytes instead of crypto::RandBytes, because crypto calls the
  // base version directly, and to prevent the dependency from base/ to crypto/.
  base::RandBytes(&token, sizeof(token));
  return token;
}

std::string Token::ToString() const {
  return base::StringPrintf("%016" PRIX64 "%016" PRIX64, high_, low_);
}

}  // namespace base
