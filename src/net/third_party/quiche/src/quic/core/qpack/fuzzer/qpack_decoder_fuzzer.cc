// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include "absl/strings/string_view.h"
#include "quic/core/qpack/qpack_decoder.h"
#include "quic/platform/api/quic_fuzzed_data_provider.h"
#include "quic/test_tools/qpack/qpack_decoder_test_utils.h"
#include "quic/test_tools/qpack/qpack_test_utils.h"

namespace quic {
namespace test {

struct DecoderAndHandler {
  std::unique_ptr<QpackProgressiveDecoder> decoder;
  std::unique_ptr<QpackProgressiveDecoder::HeadersHandlerInterface> handler;
};

using DecoderAndHandlerMap = std::map<QuicStreamId, DecoderAndHandler>;

// Class that sets externally owned |error_detected| to true
// on encoder stream error.
class ErrorDelegate : public QpackDecoder::EncoderStreamErrorDelegate {
 public:
  ErrorDelegate(bool* error_detected) : error_detected_(error_detected) {}
  ~ErrorDelegate() override = default;

  void OnEncoderStreamError(QuicErrorCode /*error_code*/,
                            absl::string_view /*error_message*/) override {
    *error_detected_ = true;
  }

 private:
  bool* const error_detected_;
};

// Class that destroys DecoderAndHandler when decoding completes, and sets
// externally owned |error_detected| to true on encoder stream error.
class HeadersHandler : public QpackProgressiveDecoder::HeadersHandlerInterface {
 public:
  HeadersHandler(QuicStreamId stream_id,
                 DecoderAndHandlerMap* processing_decoders,
                 bool* error_detected)
      : stream_id_(stream_id),
        processing_decoders_(processing_decoders),
        error_detected_(error_detected) {}
  ~HeadersHandler() override = default;

  void OnHeaderDecoded(absl::string_view /*name*/,
                       absl::string_view /*value*/) override {}

  // Remove DecoderAndHandler from |*processing_decoders|.
  void OnDecodingCompleted() override {
    // Will delete |this|.
    size_t result = processing_decoders_->erase(stream_id_);
    QUICHE_CHECK_EQ(1u, result);
  }

  void OnDecodingErrorDetected(absl::string_view /*error_message*/) override {
    *error_detected_ = true;
  }

 private:
  const QuicStreamId stream_id_;
  DecoderAndHandlerMap* const processing_decoders_;
  bool* const error_detected_;
};

// This fuzzer exercises QpackDecoder.  It should be able to cover all possible
// code paths.  There is no point in encoding QpackDecoder's output to turn this
// into a roundtrip test, because the same header list can be encoded in many
// different ways, so the output could not be expected to match the original
// input.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  QuicFuzzedDataProvider provider(data, size);

  // Maximum 256 byte dynamic table.  Such a small size helps test draining
  // entries and eviction.
  const uint64_t maximum_dynamic_table_capacity =
      provider.ConsumeIntegral<uint8_t>();
  // Maximum 256 blocked streams.
  const uint64_t maximum_blocked_streams = provider.ConsumeIntegral<uint8_t>();

  // |error_detected| will be set to true if an error is encountered either in a
  // header block or on the encoder stream.
  bool error_detected = false;

  ErrorDelegate encoder_stream_error_delegate(&error_detected);
  QpackDecoder decoder(maximum_dynamic_table_capacity, maximum_blocked_streams,
                       &encoder_stream_error_delegate);

  NoopQpackStreamSenderDelegate decoder_stream_sender_delegate;
  decoder.set_qpack_stream_sender_delegate(&decoder_stream_sender_delegate);

  // Decoders still reading the header block, with corresponding handlers.
  DecoderAndHandlerMap reading_decoders;

  // Decoders still processing the completely read header block,
  // with corresponding handlers.
  DecoderAndHandlerMap processing_decoders;

  // Maximum 256 data fragments to limit runtime and memory usage.
  auto fragment_count = provider.ConsumeIntegral<uint8_t>();
  while (fragment_count > 0 && !error_detected &&
         provider.remaining_bytes() > 0) {
    --fragment_count;
    switch (provider.ConsumeIntegralInRange<uint8_t>(0, 3)) {
      // Feed encoder stream data to QpackDecoder.
      case 0: {
        size_t fragment_size = provider.ConsumeIntegral<uint8_t>();
        std::string data = provider.ConsumeRandomLengthString(fragment_size);
        decoder.encoder_stream_receiver()->Decode(data);

        continue;
      }

      // Create new progressive decoder.
      case 1: {
        QuicStreamId stream_id = provider.ConsumeIntegral<uint8_t>();
        if (reading_decoders.find(stream_id) != reading_decoders.end() ||
            processing_decoders.find(stream_id) != processing_decoders.end()) {
          continue;
        }

        DecoderAndHandler decoder_and_handler;
        decoder_and_handler.handler = std::make_unique<HeadersHandler>(
            stream_id, &processing_decoders, &error_detected);
        decoder_and_handler.decoder = decoder.CreateProgressiveDecoder(
            stream_id, decoder_and_handler.handler.get());
        reading_decoders.insert({stream_id, std::move(decoder_and_handler)});

        continue;
      }

      // Feed header block data to existing decoder.
      case 2: {
        if (reading_decoders.empty()) {
          continue;
        }

        auto it = reading_decoders.begin();
        auto distance = provider.ConsumeIntegralInRange<uint8_t>(
            0, reading_decoders.size() - 1);
        std::advance(it, distance);

        size_t fragment_size = provider.ConsumeIntegral<uint8_t>();
        std::string data = provider.ConsumeRandomLengthString(fragment_size);
        it->second.decoder->Decode(data);

        continue;
      }

      // End header block.
      case 3: {
        if (reading_decoders.empty()) {
          continue;
        }

        auto it = reading_decoders.begin();
        auto distance = provider.ConsumeIntegralInRange<uint8_t>(
            0, reading_decoders.size() - 1);
        std::advance(it, distance);

        QpackProgressiveDecoder* decoder = it->second.decoder.get();

        // Move DecoderAndHandler to |reading_decoders| first, because
        // EndHeaderBlock() might synchronously call OnDecodingCompleted().
        QuicStreamId stream_id = it->first;
        processing_decoders.insert({stream_id, std::move(it->second)});
        reading_decoders.erase(it);

        decoder->EndHeaderBlock();

        continue;
      }
    }
  }

  return 0;
}

}  // namespace test
}  // namespace quic
