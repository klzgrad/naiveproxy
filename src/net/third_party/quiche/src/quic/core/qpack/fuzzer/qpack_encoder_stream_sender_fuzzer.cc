// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_stream_sender.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_fuzzed_data_provider.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_encoder_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_test_utils.h"

namespace quic {
namespace test {

// This fuzzer exercises QpackEncoderStreamSender.
// TODO(bnc): Encoded data could be fed into QpackEncoderStreamReceiver and
// decoded instructions directly compared to input.  Figure out how to get gMock
// enabled for cc_fuzz_target target types.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  NoopQpackStreamSenderDelegate delegate;
  QpackEncoderStreamSender sender;
  sender.set_qpack_stream_sender_delegate(&delegate);

  QuicFuzzedDataProvider provider(data, size);
  // Limit string literal length to 2 kB for efficiency.
  const uint16_t kMaxStringLength = 2048;

  while (provider.remaining_bytes() != 0) {
    switch (provider.ConsumeIntegral<uint8_t>() % 4) {
      case 0: {
        bool is_static = provider.ConsumeBool();
        uint64_t name_index = provider.ConsumeIntegral<uint64_t>();
        uint16_t value_length =
            provider.ConsumeIntegralInRange<uint16_t>(0, kMaxStringLength);
        std::string value = provider.ConsumeRandomLengthString(value_length);

        sender.SendInsertWithNameReference(is_static, name_index, value);
        break;
      }
      case 1: {
        uint16_t name_length =
            provider.ConsumeIntegralInRange<uint16_t>(0, kMaxStringLength);
        std::string name = provider.ConsumeRandomLengthString(name_length);
        uint16_t value_length =
            provider.ConsumeIntegralInRange<uint16_t>(0, kMaxStringLength);
        std::string value = provider.ConsumeRandomLengthString(value_length);
        sender.SendInsertWithoutNameReference(name, value);
        break;
      }
      case 2: {
        uint64_t index = provider.ConsumeIntegral<uint64_t>();
        sender.SendDuplicate(index);
        break;
      }
      case 3: {
        uint64_t capacity = provider.ConsumeIntegral<uint64_t>();
        sender.SendSetDynamicTableCapacity(capacity);
        break;
      }
    }
  }

  return 0;
}

}  // namespace test
}  // namespace quic
