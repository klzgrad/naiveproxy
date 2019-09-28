// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "base/i18n/icu_util.h"
#include "url/gurl.h"

struct TestCase {
  TestCase() { CHECK(base::i18n::InitializeICU()); }

  // used by ICU integration.
  base::AtExitManager at_exit_manager;
};

TestCase* test_case = new TestCase();

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1)
    return 0;

  base::StringPiece string_piece_input(reinterpret_cast<const char*>(data),
                                       size);
  GURL url_from_string_piece(string_piece_input);

  // Test for StringPiece16 if size is even.
  if (size % 2 == 0) {
    base::StringPiece16 string_piece_input16(
        reinterpret_cast<const base::char16*>(data), size / 2);

    GURL url_from_string_piece16(string_piece_input16);
  }

  // Resolve relative url tests.
  size_t size_t_bytes = sizeof(size_t);
  if (size < size_t_bytes + 1) {
    return 0;
  }
  size_t relative_size =
      *reinterpret_cast<const size_t*>(data) % (size - size_t_bytes);
  std::string relative_string(
      reinterpret_cast<const char*>(data + size_t_bytes), relative_size);
  base::StringPiece string_piece_part_input(
      reinterpret_cast<const char*>(data + size_t_bytes + relative_size),
      size - relative_size - size_t_bytes);
  GURL url_from_string_piece_part(string_piece_part_input);
  url_from_string_piece_part.Resolve(relative_string);

  if (relative_size % 2 == 0) {
    base::string16 relative_string16(
        reinterpret_cast<const base::char16*>(data + size_t_bytes),
        relative_size / 2);
    url_from_string_piece_part.Resolve(relative_string16);
  }
  return 0;
}
