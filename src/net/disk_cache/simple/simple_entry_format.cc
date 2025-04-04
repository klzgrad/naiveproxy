// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/390223051): Remove C-library calls to fix the errors.
#pragma allow_unsafe_libc_calls
#endif

#include "net/disk_cache/simple/simple_entry_format.h"

#include <cstring>

namespace disk_cache {

SimpleFileHeader::SimpleFileHeader() {
  // Make hashing repeatable: leave no padding bytes untouched.
  std::memset(this, 0, sizeof(*this));
}

SimpleFileEOF::SimpleFileEOF() {
  // Make hashing repeatable: leave no padding bytes untouched.
  std::memset(this, 0, sizeof(*this));
}

SimpleFileSparseRangeHeader::SimpleFileSparseRangeHeader() {
  // Make hashing repeatable: leave no padding bytes untouched.
  std::memset(this, 0, sizeof(*this));
}

}  // namespace disk_cache
