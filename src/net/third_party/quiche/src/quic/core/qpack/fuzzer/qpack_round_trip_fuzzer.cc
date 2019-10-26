// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/http/quic_header_list.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoded_headers_accumulator.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_test_utils.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_stream_sender_delegate.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_utils.h"
#include "net/third_party/quiche/src/quic/core/qpack/value_splitting_header_list.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_fuzzed_data_provider.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack_encoder_peer.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"

namespace quic {
namespace test {

// Class to hold QpackEncoder and its DecoderStreamErrorDelegate.
class EncodingEndpoint {
 public:
  EncodingEndpoint(uint64_t maximum_dynamic_table_capacity,
                   uint64_t maximum_blocked_streams)
      : encoder_(&decoder_stream_error_delegate) {
    encoder_.SetMaximumDynamicTableCapacity(maximum_dynamic_table_capacity);
    encoder_.SetMaximumBlockedStreams(maximum_blocked_streams);
  }

  ~EncodingEndpoint() {
    // Every reference should be acknowledged.
    CHECK_EQ(std::numeric_limits<uint64_t>::max(),
             QpackEncoderPeer::smallest_blocking_index(&encoder_));
  }

  void set_qpack_stream_sender_delegate(QpackStreamSenderDelegate* delegate) {
    encoder_.set_qpack_stream_sender_delegate(delegate);
  }

  void SetDynamicTableCapacity(uint64_t maximum_dynamic_table_capacity) {
    encoder_.SetDynamicTableCapacity(maximum_dynamic_table_capacity);
  }

  QpackStreamReceiver* decoder_stream_receiver() {
    return encoder_.decoder_stream_receiver();
  }

  std::string EncodeHeaderList(QuicStreamId stream_id,
                               const spdy::SpdyHeaderBlock& header_list) {
    return encoder_.EncodeHeaderList(stream_id, header_list);
  }

 private:
  // DecoderStreamErrorDelegate implementation that crashes on error.
  class CrashingDecoderStreamErrorDelegate
      : public QpackEncoder::DecoderStreamErrorDelegate {
   public:
    ~CrashingDecoderStreamErrorDelegate() override = default;

    void OnDecoderStreamError(QuicStringPiece error_message) override {
      CHECK(false) << error_message;
    }
  };

  CrashingDecoderStreamErrorDelegate decoder_stream_error_delegate;
  QpackEncoder encoder_;
};

// Class that receives all header blocks from the encoding endpoint and passes
// them to the decoding endpoint, with delay determined by fuzzer data,
// preserving order within each stream but not among streams.
class DelayedHeaderBlockTransmitter {
 public:
  class Visitor {
   public:
    virtual ~Visitor() = default;

    // If decoding of the previous header block is still in progress, then
    // DelayedHeaderBlockTransmitter will not start transmitting the next header
    // block.
    virtual bool IsDecodingInProgressOnStream(QuicStreamId stream_id) = 0;

    // Called when a header block starts.
    virtual void OnHeaderBlockStart(QuicStreamId stream_id) = 0;
    // Called when part or all of a header block is transmitted.
    virtual void OnHeaderBlockFragment(QuicStreamId stream_id,
                                       QuicStringPiece data) = 0;
    // Called when transmission of a header block is complete.
    virtual void OnHeaderBlockEnd(QuicStreamId stream_id) = 0;
  };

  DelayedHeaderBlockTransmitter(Visitor* visitor,
                                QuicFuzzedDataProvider* provider)
      : visitor_(visitor), provider_(provider) {}

  ~DelayedHeaderBlockTransmitter() { CHECK(header_blocks_.empty()); }

  // Enqueues |encoded_header_block| for delayed transmission.
  void SendEncodedHeaderBlock(QuicStreamId stream_id,
                              std::string encoded_header_block) {
    auto it = header_blocks_.lower_bound(stream_id);
    if (it == header_blocks_.end() || it->first != stream_id) {
      it = header_blocks_.insert(it, {stream_id, {}});
    }
    CHECK_EQ(stream_id, it->first);
    it->second.push(HeaderBlock(std::move(encoded_header_block)));
  }

  // Release some (possibly none) header block data.
  void MaybeTransmitSomeData() {
    if (header_blocks_.empty()) {
      return;
    }

    auto index =
        provider_->ConsumeIntegralInRange<size_t>(0, header_blocks_.size() - 1);
    auto it = header_blocks_.begin();
    std::advance(it, index);
    const QuicStreamId stream_id = it->first;

    // Do not start new header block if processing of previous header block is
    // blocked.
    if (visitor_->IsDecodingInProgressOnStream(stream_id)) {
      return;
    }

    auto& header_block_queue = it->second;
    HeaderBlock& header_block = header_block_queue.front();

    if (header_block.ConsumedLength() == 0) {
      visitor_->OnHeaderBlockStart(stream_id);
    }

    DCHECK_NE(0u, header_block.RemainingLength());

    size_t length = provider_->ConsumeIntegralInRange<size_t>(
        1, header_block.RemainingLength());
    visitor_->OnHeaderBlockFragment(stream_id, header_block.Consume(length));

    DCHECK_NE(0u, header_block.ConsumedLength());

    if (header_block.RemainingLength() == 0) {
      visitor_->OnHeaderBlockEnd(stream_id);

      header_block_queue.pop();
      if (header_block_queue.empty()) {
        header_blocks_.erase(it);
      }
    }
  }

  // Release all header block data.  Must be called before destruction.  All
  // encoder stream data must have been released before calling Flush() so that
  // all header blocks can be decoded synchronously.
  void Flush() {
    while (!header_blocks_.empty()) {
      auto it = header_blocks_.begin();
      const QuicStreamId stream_id = it->first;

      auto& header_block_queue = it->second;
      HeaderBlock& header_block = header_block_queue.front();

      if (header_block.ConsumedLength() == 0) {
        CHECK(!visitor_->IsDecodingInProgressOnStream(stream_id));
        visitor_->OnHeaderBlockStart(stream_id);
      }

      DCHECK_NE(0u, header_block.RemainingLength());

      visitor_->OnHeaderBlockFragment(stream_id,
                                      header_block.ConsumeRemaining());

      DCHECK_NE(0u, header_block.ConsumedLength());
      DCHECK_EQ(0u, header_block.RemainingLength());

      visitor_->OnHeaderBlockEnd(stream_id);
      CHECK(!visitor_->IsDecodingInProgressOnStream(stream_id));

      header_block_queue.pop();
      if (header_block_queue.empty()) {
        header_blocks_.erase(it);
      }
    }
  }

 private:
  // Helper class that allows the header block to be consumed in parts.
  class HeaderBlock {
   public:
    explicit HeaderBlock(std::string data)
        : data_(std::move(data)), offset_(0) {
      // Valid QPACK header block cannot be empty.
      DCHECK(!data_.empty());
    }

    size_t ConsumedLength() const { return offset_; }

    size_t RemainingLength() const { return data_.length() - offset_; }

    QuicStringPiece Consume(size_t length) {
      DCHECK_NE(0u, length);
      DCHECK_LE(length, RemainingLength());

      QuicStringPiece consumed = QuicStringPiece(&data_[offset_], length);
      offset_ += length;
      return consumed;
    }

    QuicStringPiece ConsumeRemaining() { return Consume(RemainingLength()); }

   private:
    // Complete header block.
    const std::string data_;

    // Offset of the part not consumed yet.  Same as number of consumed bytes.
    size_t offset_;
  };

  Visitor* const visitor_;
  QuicFuzzedDataProvider* const provider_;

  std::map<QuicStreamId, std::queue<HeaderBlock>> header_blocks_;
};

// Class to decode and verify a header block, and in case of blocked decoding,
// keep necessary decoding context while waiting for decoding to complete.
class VerifyingDecoder : public QpackDecodedHeadersAccumulator::Visitor {
 public:
  class Visitor {
   public:
    virtual ~Visitor() = default;

    // Called when header block is decoded, either synchronously or
    // asynchronously.  Might destroy VerifyingDecoder.
    virtual void OnHeaderBlockDecoded(QuicStreamId stream_id) = 0;
  };

  VerifyingDecoder(QuicStreamId stream_id,
                   Visitor* visitor,
                   QpackDecoder* qpack_decoder,
                   QuicHeaderList expected_header_list)
      : stream_id_(stream_id),
        visitor_(visitor),
        accumulator_(
            stream_id,
            qpack_decoder,
            this,
            /* max_header_list_size = */ std::numeric_limits<size_t>::max()),
        expected_header_list_(std::move(expected_header_list)) {}

  VerifyingDecoder(const VerifyingDecoder&) = delete;
  VerifyingDecoder& operator=(const VerifyingDecoder&) = delete;
  // VerifyingDecoder must not be moved because it passes |this| to
  // |accumulator_| upon construction.
  VerifyingDecoder(VerifyingDecoder&&) = delete;
  VerifyingDecoder& operator=(VerifyingDecoder&&) = delete;

  virtual ~VerifyingDecoder() = default;

  // QpackDecodedHeadersAccumulator::Visitor implementation.
  void OnHeadersDecoded(QuicHeaderList headers) override {
    // Verify headers.
    CHECK(expected_header_list_ == headers);

    // Might destroy |this|.
    visitor_->OnHeaderBlockDecoded(stream_id_);
  }

  void OnHeaderDecodingError() override {
    CHECK(false) << accumulator_.error_message();
  }

  void Decode(QuicStringPiece data) {
    const bool success = accumulator_.Decode(data);
    CHECK(success) << accumulator_.error_message();
  }

  void EndHeaderBlock() {
    QpackDecodedHeadersAccumulator::Status status =
        accumulator_.EndHeaderBlock();

    CHECK(status != QpackDecodedHeadersAccumulator::Status::kError)
        << accumulator_.error_message();

    if (status == QpackDecodedHeadersAccumulator::Status::kBlocked) {
      return;
    }

    CHECK(status == QpackDecodedHeadersAccumulator::Status::kSuccess);

    // Compare resulting header list to original.
    CHECK(expected_header_list_ == accumulator_.quic_header_list());
    // Might destroy |this|.
    visitor_->OnHeaderBlockDecoded(stream_id_);
  }

 private:
  QuicStreamId stream_id_;
  Visitor* const visitor_;
  QpackDecodedHeadersAccumulator accumulator_;
  QuicHeaderList expected_header_list_;
};

// Class that holds QpackDecoder and its EncoderStreamErrorDelegate, and creates
// and keeps VerifyingDecoders for each received header block until decoding is
// complete.
class DecodingEndpoint : public DelayedHeaderBlockTransmitter::Visitor,
                         public VerifyingDecoder::Visitor {
 public:
  DecodingEndpoint(uint64_t maximum_dynamic_table_capacity,
                   uint64_t maximum_blocked_streams)
      : decoder_(maximum_dynamic_table_capacity,
                 maximum_blocked_streams,
                 &encoder_stream_error_delegate_) {}

  ~DecodingEndpoint() override {
    // All decoding must have been completed.
    CHECK(expected_header_lists_.empty());
    CHECK(verifying_decoders_.empty());
  }

  void set_qpack_stream_sender_delegate(QpackStreamSenderDelegate* delegate) {
    decoder_.set_qpack_stream_sender_delegate(delegate);
  }

  QpackStreamReceiver* encoder_stream_receiver() {
    return decoder_.encoder_stream_receiver();
  }

  void AddExpectedHeaderList(QuicStreamId stream_id,
                             QuicHeaderList expected_header_list) {
    auto it = expected_header_lists_.lower_bound(stream_id);
    if (it == expected_header_lists_.end() || it->first != stream_id) {
      it = expected_header_lists_.insert(it, {stream_id, {}});
    }
    CHECK_EQ(stream_id, it->first);
    it->second.push(std::move(expected_header_list));
  }

  // VerifyingDecoder::Visitor implementation.
  void OnHeaderBlockDecoded(QuicStreamId stream_id) override {
    auto result = verifying_decoders_.erase(stream_id);
    CHECK_EQ(1u, result);
  }

  // DelayedHeaderBlockTransmitter::Visitor implementation.
  bool IsDecodingInProgressOnStream(QuicStreamId stream_id) override {
    return verifying_decoders_.find(stream_id) != verifying_decoders_.end();
  }

  void OnHeaderBlockStart(QuicStreamId stream_id) override {
    CHECK(!IsDecodingInProgressOnStream(stream_id));
    auto it = expected_header_lists_.find(stream_id);
    CHECK(it != expected_header_lists_.end());

    auto& header_list_queue = it->second;
    QuicHeaderList expected_header_list = std::move(header_list_queue.front());

    header_list_queue.pop();
    if (header_list_queue.empty()) {
      expected_header_lists_.erase(it);
    }

    auto verifying_decoder = QuicMakeUnique<VerifyingDecoder>(
        stream_id, this, &decoder_, std::move(expected_header_list));
    auto result =
        verifying_decoders_.insert({stream_id, std::move(verifying_decoder)});
    CHECK(result.second);
  }

  void OnHeaderBlockFragment(QuicStreamId stream_id,
                             QuicStringPiece data) override {
    auto it = verifying_decoders_.find(stream_id);
    CHECK(it != verifying_decoders_.end());
    it->second->Decode(data);
  }

  void OnHeaderBlockEnd(QuicStreamId stream_id) override {
    auto it = verifying_decoders_.find(stream_id);
    CHECK(it != verifying_decoders_.end());
    it->second->EndHeaderBlock();
  }

 private:
  // EncoderStreamErrorDelegate implementation that crashes on error.
  class CrashingEncoderStreamErrorDelegate
      : public QpackDecoder::EncoderStreamErrorDelegate {
   public:
    ~CrashingEncoderStreamErrorDelegate() override = default;

    void OnEncoderStreamError(QuicStringPiece error_message) override {
      CHECK(false) << error_message;
    }
  };

  CrashingEncoderStreamErrorDelegate encoder_stream_error_delegate_;
  QpackDecoder decoder_;

  // Expected header lists in order for each stream.
  std::map<QuicStreamId, std::queue<QuicHeaderList>> expected_header_lists_;

  // A VerifyingDecoder object keeps context necessary for asynchronously
  // decoding blocked header blocks.  It is destroyed as soon as it signals that
  // decoding is completed, which might happen synchronously within an
  // EndHeaderBlock() call.
  std::map<QuicStreamId, std::unique_ptr<VerifyingDecoder>> verifying_decoders_;
};

// Class that receives encoder stream data from the encoder and passes it to the
// decoder, or receives decoder stream data from the decoder and passes it to
// the encoder, with delay determined by fuzzer data.
class DelayedStreamDataTransmitter : public QpackStreamSenderDelegate {
 public:
  DelayedStreamDataTransmitter(QpackStreamReceiver* receiver,
                               QuicFuzzedDataProvider* provider)
      : receiver_(receiver), provider_(provider) {}

  ~DelayedStreamDataTransmitter() { CHECK(stream_data.empty()); }

  // QpackStreamSenderDelegate implementation.
  void WriteStreamData(QuicStringPiece data) override {
    stream_data.push(std::string(data.data(), data.size()));
  }

  // Release some (possibly none) delayed stream data.
  void MaybeTransmitSomeData() {
    auto count = provider_->ConsumeIntegral<uint8_t>();
    while (!stream_data.empty() && count > 0) {
      receiver_->Decode(stream_data.front());
      stream_data.pop();
      --count;
    }
  }

  // Release all delayed stream data.  Must be called before destruction.
  void Flush() {
    while (!stream_data.empty()) {
      receiver_->Decode(stream_data.front());
      stream_data.pop();
    }
  }

 private:
  QpackStreamReceiver* const receiver_;
  QuicFuzzedDataProvider* const provider_;
  QuicQueue<std::string> stream_data;
};

// Generate header list using fuzzer data.
spdy::SpdyHeaderBlock GenerateHeaderList(QuicFuzzedDataProvider* provider) {
  spdy::SpdyHeaderBlock header_list;
  uint8_t header_count = provider->ConsumeIntegral<uint8_t>();
  for (uint8_t header_index = 0; header_index < header_count; ++header_index) {
    if (provider->remaining_bytes() == 0) {
      // Do not add more headers if there is no more fuzzer data.
      break;
    }

    std::string name;
    std::string value;
    switch (provider->ConsumeIntegral<uint8_t>()) {
      case 0:
        // Static table entry with no header value.
        name = ":authority";
        break;
      case 1:
        // Static table entry with no header value, using non-empty header
        // value.
        name = ":authority";
        value = "www.example.org";
        break;
      case 2:
        // Static table entry with header value, using that header value.
        name = ":accept-encoding";
        value = "gzip, deflate";
        break;
      case 3:
        // Static table entry with header value, using empty header value.
        name = ":accept-encoding";
        break;
      case 4:
        // Static table entry with header value, using different, non-empty
        // header value.
        name = ":accept-encoding";
        value = "brotli";
        break;
      case 5:
        // Header name that has multiple entries in the static table,
        // using header value from one of them.
        name = ":method";
        value = "GET";
        break;
      case 6:
        // Header name that has multiple entries in the static table,
        // using empty header value.
        name = ":method";
        break;
      case 7:
        // Header name that has multiple entries in the static table,
        // using different, non-empty header value.
        name = ":method";
        value = "CONNECT";
        break;
      case 8:
        // Header name not in the static table, empty header value.
        name = "foo";
        value = "";
        break;
      case 9:
        // Header name not in the static table, non-empty fixed header value.
        name = "foo";
        value = "bar";
        break;
      case 10:
        // Header name not in the static table, fuzzed header value.
        name = "foo";
        value = provider->ConsumeRandomLengthString(128);
        break;
      case 11:
        // Another header name not in the static table, empty header value.
        name = "bar";
        value = "";
        break;
      case 12:
        // Another header name not in the static table, non-empty fixed header
        // value.
        name = "bar";
        value = "baz";
        break;
      case 13:
        // Another header name not in the static table, fuzzed header value.
        name = "bar";
        value = provider->ConsumeRandomLengthString(128);
        break;
      default:
        // Fuzzed header name and header value.
        name = provider->ConsumeRandomLengthString(128);
        value = provider->ConsumeRandomLengthString(128);
    }

    header_list.AppendValueOrAddHeader(name, value);
  }

  return header_list;
}

// Splits |*header_list| header values along '\0' or ';' separators.
QuicHeaderList SplitHeaderList(const spdy::SpdyHeaderBlock& header_list) {
  QuicHeaderList split_header_list;
  split_header_list.set_max_header_list_size(
      std::numeric_limits<size_t>::max());
  split_header_list.OnHeaderBlockStart();

  size_t total_size = 0;
  ValueSplittingHeaderList splitting_header_list(&header_list);
  for (const auto& header : splitting_header_list) {
    split_header_list.OnHeader(header.first, header.second);
    total_size += header.first.size() + header.second.size();
  }

  split_header_list.OnHeaderBlockEnd(total_size, total_size);

  return split_header_list;
}

// This fuzzer exercises QpackEncoder and QpackDecoder.  It should be able to
// cover all possible code paths of QpackEncoder.  However, since the resulting
// header block is always valid and is encoded in a particular way, this fuzzer
// is not expected to cover all code paths of QpackDecoder.  On the other hand,
// encoding then decoding is expected to result in the original header list, and
// this fuzzer checks for that.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  QuicFuzzedDataProvider provider(data, size);

  // Maximum 256 byte dynamic table.  Such a small size helps test draining
  // entries and eviction.
  const uint64_t maximum_dynamic_table_capacity =
      provider.ConsumeIntegral<uint8_t>();
  // Maximum 256 blocked streams.
  const uint64_t maximum_blocked_streams = provider.ConsumeIntegral<uint8_t>();

  // Set up encoder.
  EncodingEndpoint encoder(maximum_dynamic_table_capacity,
                           maximum_blocked_streams);

  // Set up decoder.
  DecodingEndpoint decoder(maximum_dynamic_table_capacity,
                           maximum_blocked_streams);

  // Transmit encoder stream data from encoder to decoder.
  DelayedStreamDataTransmitter encoder_stream_transmitter(
      decoder.encoder_stream_receiver(), &provider);
  encoder.set_qpack_stream_sender_delegate(&encoder_stream_transmitter);

  // Use a dynamic table as large as the peer allows.  This sends data on the
  // encoder stream, so it can only be done after delegate is set.
  encoder.SetDynamicTableCapacity(maximum_dynamic_table_capacity);

  // Transmit decoder stream data from encoder to decoder.
  DelayedStreamDataTransmitter decoder_stream_transmitter(
      encoder.decoder_stream_receiver(), &provider);
  decoder.set_qpack_stream_sender_delegate(&decoder_stream_transmitter);

  // Transmit header blocks from encoder to decoder.
  DelayedHeaderBlockTransmitter header_block_transmitter(&decoder, &provider);

  // Maximum 256 header lists to limit runtime and memory usage.
  auto header_list_count = provider.ConsumeIntegral<uint8_t>();
  while (header_list_count > 0 && provider.remaining_bytes() > 0) {
    const QuicStreamId stream_id = provider.ConsumeIntegral<uint8_t>();

    // Generate header list.
    spdy::SpdyHeaderBlock header_list = GenerateHeaderList(&provider);

    // Encode header list.
    std::string encoded_header_block =
        encoder.EncodeHeaderList(stream_id, header_list);

    // TODO(bnc): Randomly cancel the stream.

    // Encoder splits |header_list| header values along '\0' or ';' separators.
    // Do the same here so that we get matching results.
    QuicHeaderList expected_header_list = SplitHeaderList(header_list);
    decoder.AddExpectedHeaderList(stream_id, std::move(expected_header_list));

    header_block_transmitter.SendEncodedHeaderBlock(
        stream_id, std::move(encoded_header_block));

    // Transmit some encoder stream data, decoder stream data, or header blocks
    // on the request stream, repeating a few times.
    for (auto transmit_data_count = provider.ConsumeIntegralInRange(1, 5);
         transmit_data_count > 0; --transmit_data_count) {
      encoder_stream_transmitter.MaybeTransmitSomeData();
      decoder_stream_transmitter.MaybeTransmitSomeData();
      header_block_transmitter.MaybeTransmitSomeData();
    }

    --header_list_count;
  }

  // Release all delayed encoder stream data so that remaining header blocks can
  // be decoded synchronously.
  encoder_stream_transmitter.Flush();
  // Release all delayed header blocks.
  header_block_transmitter.Flush();
  // Release all delayed decoder stream data.
  decoder_stream_transmitter.Flush();

  return 0;
}

}  // namespace test
}  // namespace quic
