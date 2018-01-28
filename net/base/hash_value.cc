// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/hash_value.h"

#include <stdlib.h>
#include <algorithm>

#include "base/base64.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "crypto/sha2.h"

namespace net {

namespace {

// LessThan comparator for use with std::binary_search() in determining
// whether a SHA-256 HashValue appears within a sorted array of
// SHA256HashValues.
struct SHA256ToHashValueComparator {
  bool operator()(const SHA256HashValue& lhs, const HashValue& rhs) const {
    DCHECK_EQ(HASH_VALUE_SHA256, rhs.tag);
    return memcmp(lhs.data, rhs.data(), rhs.size()) < 0;
  }

  bool operator()(const HashValue& lhs, const SHA256HashValue& rhs) const {
    DCHECK_EQ(HASH_VALUE_SHA256, lhs.tag);
    return memcmp(lhs.data(), rhs.data, lhs.size()) < 0;
  }
};

}  // namespace


HashValue::HashValue(const SHA256HashValue& hash)
    : HashValue(HASH_VALUE_SHA256) {
  fingerprint.sha256 = hash;
}

bool HashValue::FromString(const base::StringPiece value) {
  base::StringPiece base64_str;
  if (value.starts_with("sha256/")) {
    tag = HASH_VALUE_SHA256;
    base64_str = value.substr(7);
  } else {
    return false;
  }

  std::string decoded;
  if (!base::Base64Decode(base64_str, &decoded) || decoded.size() != size())
    return false;

  memcpy(data(), decoded.data(), size());
  return true;
}

std::string HashValue::ToString() const {
  std::string base64_str;
  base::Base64Encode(base::StringPiece(reinterpret_cast<const char*>(data()),
                                       size()), &base64_str);
  switch (tag) {
  case HASH_VALUE_SHA256:
    return std::string("sha256/") + base64_str;
  default:
    NOTREACHED() << "Unknown HashValueTag " << tag;
    return std::string("unknown/" + base64_str);
  }
}

size_t HashValue::size() const {
  switch (tag) {
    case HASH_VALUE_SHA256:
      return sizeof(fingerprint.sha256.data);
    default:
      NOTREACHED() << "Unknown HashValueTag " << tag;
      // While an invalid tag should not happen, return a non-zero length
      // to avoid compiler warnings when the result of size() is
      // used with functions like memset.
      return sizeof(fingerprint.sha256.data);
  }
}

unsigned char* HashValue::data() {
  return const_cast<unsigned char*>(const_cast<const HashValue*>(this)->data());
}

const unsigned char* HashValue::data() const {
  switch (tag) {
    case HASH_VALUE_SHA256:
      return fingerprint.sha256.data;
    default:
      NOTREACHED() << "Unknown HashValueTag " << tag;
      return NULL;
  }
}

bool IsSHA256HashInSortedArray(const HashValue& hash,
                               const SHA256HashValue* array,
                               size_t array_len) {
  return std::binary_search(array, array + array_len, hash,
                            SHA256ToHashValueComparator());
}

bool IsAnySHA256HashInSortedArray(const HashValueVector& hashes,
                                  const SHA256HashValue* list,
                                  size_t list_length) {
  for (const auto& hash : hashes) {
    if (hash.tag != HASH_VALUE_SHA256)
      continue;

    if (IsSHA256HashInSortedArray(hash, list, list_length))
      return true;
  }
  return false;
}

}  // namespace net
