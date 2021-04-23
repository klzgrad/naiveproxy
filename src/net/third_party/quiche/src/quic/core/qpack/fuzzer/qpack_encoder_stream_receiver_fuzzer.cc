// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/qpack/qpack_encoder_stream_receiver.h"

#include <cstddef>
#include <cstdint>

#include "absl/strings/string_view.h"
#include "quic/platform/api/quic_fuzzed_data_provider.h"
#include "quic/platform/api/quic_logging.h"

namespace quic {
namespace test {
namespace {

// A QpackEncoderStreamReceiver::Delegate implementation that ignores all
// decoded instructions but keeps track of whether an error has been detected.
class NoOpDelegate : public QpackEncoderStreamReceiver::Delegate {
 public:
  NoOpDelegate() : error_detected_(false) {}
  ~NoOpDelegate() override = default;

  void OnInsertWithNameReference(bool /*is_static*/,
                                 uint64_t /*name_index*/,
                                 absl::string_view /*value*/) override {}
  void OnInsertWithoutNameReference(absl::string_view /*name*/,
                                    absl::string_view /*value*/) override {}
  void OnDuplicate(uint64_t /*index*/) override {}
  void OnSetDynamicTableCapacity(uint64_t /*capacity*/) override {}
  void OnErrorDetected(QuicErrorCode /*error_code*/,
                       absl::string_view /*error_message*/) override {
    error_detected_ = true;
  }

  bool error_detected() const { return error_detected_; }

 private:
  bool error_detected_;
};

}  // namespace

// This fuzzer exercises QpackEncoderStreamReceiver.
// Note that since string literals may be encoded with or without Huffman
// encoding, one could not expect identical encoded data if the decoded
// instructions were fed into QpackEncoderStreamSender.  Therefore there is no
// point in extending this fuzzer into a round-trip test.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  NoOpDelegate delegate;
  QpackEncoderStreamReceiver receiver(&delegate);

  QuicFuzzedDataProvider provider(data, size);

  while (!delegate.error_detected() && provider.remaining_bytes() != 0) {
    // Process up to 64 kB fragments at a time.  Too small upper bound might not
    // provide enough coverage, too large might make fuzzing too inefficient.
    size_t fragment_size = provider.ConsumeIntegralInRange<uint16_t>(
        1, std::numeric_limits<uint16_t>::max());
    receiver.Decode(provider.ConsumeRandomLengthString(fragment_size));
  }

  return 0;
}

}  // namespace test
}  // namespace quic
