// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_UNGUESSABLE_TOKEN_H_
#define BASE_UNGUESSABLE_TOKEN_H_

#include <stdint.h>
#include <string.h>
#include <iosfwd>
#include <tuple>

#include "base/base_export.h"
#include "base/hash.h"
#include "base/logging.h"

namespace base {

struct UnguessableTokenHash;

// A UnguessableToken is an 128-bit token generated from a cryptographically
// strong random source. It can be used as part of a larger aggregate type,
// or as an ID in and of itself.
//
// UnguessableToken can be used to implement "Capability-Based Security".
// In other words, UnguessableToken can be used when the resource associated
// with the ID needs to be protected against manipulation by other untrusted
// agents in the system, and there is no other convenient way to verify the
// authority of the agent to do so (because the resource is part of a table
// shared across processes, for instance). In such a scheme, knowledge of the
// token value in and of itself is sufficient proof of authority to carry out
// an operation against the associated resource.
//
// Use Create() for creating new UnguessableTokens.
//
// NOTE: It is illegal to send empty UnguessableTokens across processes, and
// sending/receiving empty tokens should be treated as a security issue.
// If there is a valid scenario for sending "no token" across processes,
// base::Optional should be used instead of an empty token.
class BASE_EXPORT UnguessableToken {
 public:
  // Create a unique UnguessableToken.
  static UnguessableToken Create();

  // Returns a reference to a global null UnguessableToken. This should only be
  // used for functions that need to return a reference to an UnguessableToken,
  // and should not be used as a general-purpose substitute for invoking the
  // default constructor.
  static const UnguessableToken& Null();

  // Return a UnguessableToken built from the high/low bytes provided.
  // It should only be used in deserialization scenarios.
  //
  // NOTE: If the deserialized token is empty, it means that it was never
  // initialized via Create(). This is a security issue, and should be handled.
  static UnguessableToken Deserialize(uint64_t high, uint64_t low);

  // Creates an empty UnguessableToken.
  // Assign to it with Create() before using it.
  constexpr UnguessableToken() = default;

  // NOTE: Serializing an empty UnguessableToken is an illegal operation.
  uint64_t GetHighForSerialization() const {
    DCHECK(!is_empty());
    return high_;
  }

  // NOTE: Serializing an empty UnguessableToken is an illegal operation.
  uint64_t GetLowForSerialization() const {
    DCHECK(!is_empty());
    return low_;
  }

  bool is_empty() const { return high_ == 0 && low_ == 0; }

  // Hex representation of the unguessable token.
  std::string ToString() const;

  explicit operator bool() const { return !is_empty(); }

  bool operator<(const UnguessableToken& other) const {
    return std::tie(high_, low_) < std::tie(other.high_, other.low_);
  }

  bool operator==(const UnguessableToken& other) const {
    return high_ == other.high_ && low_ == other.low_;
  }

  bool operator!=(const UnguessableToken& other) const {
    return !(*this == other);
  }

 private:
  friend struct UnguessableTokenHash;
  UnguessableToken(uint64_t high, uint64_t low);

  // Note: Two uint64_t are used instead of uint8_t[16], in order to have a
  // simpler ToString() and is_empty().
  uint64_t high_ = 0;
  uint64_t low_ = 0;
};

BASE_EXPORT std::ostream& operator<<(std::ostream& out,
                                     const UnguessableToken& token);

// For use in std::unordered_map.
struct UnguessableTokenHash {
  size_t operator()(const base::UnguessableToken& token) const {
    DCHECK(token);
    return base::HashInts64(token.high_, token.low_);
  }
};

}  // namespace base

#endif  // BASE_UNGUESSABLE_TOKEN_H_
