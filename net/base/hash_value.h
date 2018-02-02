// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_HASH_VALUE_H_
#define NET_BASE_HASH_VALUE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include "base/strings/string_piece.h"
#include "build/build_config.h"
#include "net/base/net_export.h"

namespace net {

struct NET_EXPORT SHA256HashValue {
  unsigned char data[32];
};

inline bool operator==(const SHA256HashValue& lhs, const SHA256HashValue& rhs) {
  return memcmp(lhs.data, rhs.data, sizeof(lhs.data)) == 0;
}

inline bool operator!=(const SHA256HashValue& lhs, const SHA256HashValue& rhs) {
  return !(lhs == rhs);
}

enum HashValueTag {
  HASH_VALUE_SHA256,
};

class NET_EXPORT HashValue {
 public:
  explicit HashValue(const SHA256HashValue& hash);
  explicit HashValue(HashValueTag tag) : tag(tag) {}
  HashValue() : tag(HASH_VALUE_SHA256) {}

  // Serializes/Deserializes hashes in the form of
  // <hash-name>"/"<base64-hash-value>
  // (eg: "sha256/...")
  // This format may be persisted to permanent storage, so
  // care should be taken before changing the serialization.
  //
  // This format is used for:
  //   - net_internals display/setting public-key pins
  //   - logging public-key pins
  //   - serializing public-key pins

  // Deserializes a HashValue from a string. On error, returns
  // false and MAY change the contents of HashValue to contain invalid data.
  bool FromString(const base::StringPiece input);

  // Serializes the HashValue to a string. If an invalid HashValue
  // is supplied (eg: an unknown hash tag), returns "unknown"/<base64>
  std::string ToString() const;

  size_t size() const;
  unsigned char* data();
  const unsigned char* data() const;

  HashValueTag tag;

 private:
  union {
    SHA256HashValue sha256;
  } fingerprint;
};

inline bool operator==(const HashValue& lhs, const HashValue& rhs) {
  return lhs.tag == rhs.tag && memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
}

inline bool operator!=(const HashValue& lhs, const HashValue& rhs) {
  return !(lhs == rhs);
}

typedef std::vector<HashValue> HashValueVector;


class SHA256HashValueLessThan {
 public:
  bool operator()(const SHA256HashValue& lhs,
                  const SHA256HashValue& rhs) const {
    return memcmp(lhs.data, rhs.data, sizeof(lhs.data)) < 0;
  }
};

// IsSHA256HashInSortedArray returns true iff |hash| is in |array|, a sorted
// array of SHA256 hashes.
bool IsSHA256HashInSortedArray(const HashValue& hash,
                               const SHA256HashValue* array,
                               size_t array_len);

// IsAnySHA256HashInSortedArray returns true iff any value in |hashes| is in
// |array|, a sorted array of SHA256 hashes.
bool IsAnySHA256HashInSortedArray(const HashValueVector& hashes,
                                  const SHA256HashValue* list,
                                  size_t list_length);

}  // namespace net

#endif  // NET_BASE_HASH_VALUE_H_
