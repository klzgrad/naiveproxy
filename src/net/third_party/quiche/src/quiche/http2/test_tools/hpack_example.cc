// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/test_tools/hpack_example.h"

#include <ctype.h>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {
namespace test {
namespace {

void HpackExampleToStringOrDie(absl::string_view example, std::string* output) {
  while (!example.empty()) {
    const char c0 = example[0];
    if (isxdigit(c0)) {
      QUICHE_CHECK_GT(example.size(), 1u) << "Truncated hex byte?";
      const char c1 = example[1];
      QUICHE_CHECK(isxdigit(c1)) << "Found half a byte?";
      *output += absl::HexStringToBytes(example.substr(0, 2));
      example.remove_prefix(2);
      continue;
    }
    if (isspace(c0)) {
      example.remove_prefix(1);
      continue;
    }
    if (!example.empty() && example[0] == '|') {
      // Start of a comment. Skip to end of line or of input.
      auto pos = example.find('\n');
      if (pos == absl::string_view::npos) {
        // End of input.
        break;
      }
      example.remove_prefix(pos + 1);
      continue;
    }
    QUICHE_BUG(http2_bug_107_1)
        << "Can't parse byte " << static_cast<int>(c0)
        << absl::StrCat(" (0x", absl::Hex(c0), ")") << "\nExample: " << example;
  }
  QUICHE_CHECK_LT(0u, output->size()) << "Example is empty.";
}

}  // namespace

std::string HpackExampleToStringOrDie(absl::string_view example) {
  std::string output;
  HpackExampleToStringOrDie(example, &output);
  return output;
}

}  // namespace test
}  // namespace http2
