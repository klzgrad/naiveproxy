// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/http_constants.h"

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
    RETURN_STRING_LITERAL(SETTINGS_H3_DATAGRAM_DRAFT04);
    RETURN_STRING_LITERAL(SETTINGS_H3_DATAGRAM);
    RETURN_STRING_LITERAL(SETTINGS_WEBTRANS_DRAFT00);
    RETURN_STRING_LITERAL(SETTINGS_ENABLE_CONNECT_PROTOCOL);
    RETURN_STRING_LITERAL(SETTINGS_ENABLE_METADATA);
  }
  return absl::StrCat("UNSUPPORTED_SETTINGS_TYPE(", identifier, ")");
}

ABSL_CONST_INIT const absl::string_view kUserAgentHeaderName = "user-agent";

#undef RETURN_STRING_LITERAL  // undef for jumbo builds

}  // namespace quic
