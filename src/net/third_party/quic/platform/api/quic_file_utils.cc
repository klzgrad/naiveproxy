// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/api/quic_file_utils.h"

#include "net/third_party/quic/platform/impl/quic_file_utils_impl.h"

namespace quic {

// Traverses the directory |dirname| and retuns all of the files
// it contains.
std::vector<QuicString> ReadFileContents(const QuicString& dirname) {
  return ReadFileContentsImpl(dirname);
}

// Reads the contents of |filename| as a string into |contents|.
void ReadFileContents(QuicStringPiece filename, QuicString* contents) {
  ReadFileContentsImpl(filename, contents);
}

}  // namespace quic
