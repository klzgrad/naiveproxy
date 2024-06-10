// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_TEST_TOOLS_MOCK_SPDY_HEADERS_HANDLER_H_
#define QUICHE_SPDY_TEST_TOOLS_MOCK_SPDY_HEADERS_HANDLER_H_

#include <cstddef>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/spdy/core/spdy_headers_handler_interface.h"

namespace spdy {
namespace test {

class QUICHE_NO_EXPORT MockSpdyHeadersHandler
    : public SpdyHeadersHandlerInterface {
 public:
  MockSpdyHeadersHandler();
  ~MockSpdyHeadersHandler() override;

  MOCK_METHOD(void, OnHeaderBlockStart, (), (override));
  MOCK_METHOD(void, OnHeader, (absl::string_view key, absl::string_view value),
              (override));
  MOCK_METHOD(void, OnHeaderBlockEnd,
              (size_t uncompressed_header_bytes,
               size_t compressed_header_bytes),
              (override));
};

}  // namespace test
}  // namespace spdy

#endif  // QUICHE_SPDY_TEST_TOOLS_MOCK_SPDY_HEADERS_HANDLER_H_
