// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_ZLIB_GOOGLE_COMPRESSION_UTILS_H_
#define THIRD_PARTY_ZLIB_GOOGLE_COMPRESSION_UTILS_H_

#include <string>

#include "base/strings/string_piece.h"

namespace compression {

// Compresses the data in |input| using gzip, storing the result in |output|.
// |input| and |output| are allowed to be the same string (in-place operation).
bool GzipCompress(const std::string& input, std::string* output);

// Uncompresses the data in |input| using gzip, storing the result in |output|.
// |input| and |output| are allowed to be the same string (in-place operation).
bool GzipUncompress(const std::string& input, std::string* output);

// Like the above method, but uses base::StringPiece to avoid allocations if
// needed. |output|'s size must be at least as large as the return value from
// GetUncompressedSize.
bool GzipUncompress(base::StringPiece input, base::StringPiece output);

// Returns the uncompressed size from GZIP-compressed |compressed_data|.
uint32_t GetUncompressedSize(base::StringPiece compressed_data);

}  // namespace compression

#endif  // THIRD_PARTY_ZLIB_GOOGLE_COMPRESSION_UTILS_H_
