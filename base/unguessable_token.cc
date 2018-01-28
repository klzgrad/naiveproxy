// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/unguessable_token.h"

#include "base/format_macros.h"
#include "base/rand_util.h"
#include "base/strings/stringprintf.h"

namespace base {

UnguessableToken::UnguessableToken(uint64_t high, uint64_t low)
    : high_(high), low_(low) {}

std::string UnguessableToken::ToString() const {
  return base::StringPrintf("(%08" PRIX64 "%08" PRIX64 ")", high_, low_);
}

// static
UnguessableToken UnguessableToken::Create() {
  UnguessableToken token;
  // Use base::RandBytes instead of crypto::RandBytes, because crypto calls the
  // base version directly, and to prevent the dependency from base/ to crypto/.
  base::RandBytes(&token, sizeof(token));
  return token;
}

// static
UnguessableToken UnguessableToken::Deserialize(uint64_t high, uint64_t low) {
  // Receiving a zeroed out UnguessableToken from another process means that it
  // was never initialized via Create(). Treat this case as a security issue.
  DCHECK(!(high == 0 && low == 0));
  return UnguessableToken(high, low);
}

std::ostream& operator<<(std::ostream& out, const UnguessableToken& token) {
  return out << token.ToString();
}

}  // namespace base
