// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/http/http_constants.h"

#include "absl/strings/str_cat.h"

namespace quic {

#define RETURN_STRING_LITERAL(x) \
  case x:                        \
    return #x;

std::string H3SettingsToString(Http3AndQpackSettingsIdentifiers identifier) {
  switch (identifier) {
    RETURN_STRING_LITERAL(SETTINGS_QPACK_MAX_TABLE_CAPACITY);
    RETURN_STRING_LITERAL(SETTINGS_MAX_FIELD_SECTION_SIZE);
    RETURN_STRING_LITERAL(SETTINGS_QPACK_BLOCKED_STREAMS);
  }
  return absl::StrCat("UNSUPPORTED_SETTINGS_TYPE(", identifier, ")");
}

#undef RETURN_STRING_LITERAL  // undef for jumbo builds

}  // namespace quic
