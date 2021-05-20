// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_TEST_UTILS_H_
#define QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_TEST_UTILS_H_

#include <cstddef>
#include <functional>

#include "absl/strings/string_view.h"
#include "quic/core/qpack/qpack_stream_sender_delegate.h"
#include "quic/platform/api/quic_test.h"

namespace quic {
namespace test {

// Called repeatedly to determine the size of each fragment when encoding or
// decoding.  Must return a positive value.
using FragmentSizeGenerator = std::function<size_t()>;

enum class FragmentMode {
  kSingleChunk,
  kOctetByOctet,
};

FragmentSizeGenerator FragmentModeToFragmentSizeGenerator(
    FragmentMode fragment_mode);

// Mock QpackUnidirectionalStreamSenderDelegate implementation.
class MockQpackStreamSenderDelegate : public QpackStreamSenderDelegate {
 public:
  ~MockQpackStreamSenderDelegate() override = default;

  MOCK_METHOD(void, WriteStreamData, (absl::string_view data), (override));
};

class NoopQpackStreamSenderDelegate : public QpackStreamSenderDelegate {
 public:
  ~NoopQpackStreamSenderDelegate() override = default;

  void WriteStreamData(absl::string_view /*data*/) override {}
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_TEST_UTILS_H_
