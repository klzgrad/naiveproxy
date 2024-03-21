// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_chaos_protector.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/frames/quic_crypto_frame.h"
#include "quiche/quic/core/frames/quic_frame.h"
#include "quiche/quic/core/frames/quic_padding_frame.h"
#include "quiche/quic/core/frames/quic_ping_frame.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_stream_frame_data_producer.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

QuicChaosProtector::QuicChaosProtector(const QuicCryptoFrame& crypto_frame,
                                       int num_padding_bytes,
                                       size_t packet_size, QuicFramer* framer,
                                       QuicRandom* random)
    : packet_size_(packet_size),
      crypto_data_length_(crypto_frame.data_length),
      crypto_buffer_offset_(crypto_frame.offset),
      level_(crypto_frame.level),
      remaining_padding_bytes_(num_padding_bytes),
      framer_(framer),
      random_(random) {
  QUICHE_DCHECK_NE(framer_, nullptr);
  QUICHE_DCHECK_NE(framer_->data_producer(), nullptr);
  QUICHE_DCHECK_NE(random_, nullptr);
}

QuicChaosProtector::~QuicChaosProtector() { DeleteFrames(&frames_); }

std::optional<size_t> QuicChaosProtector::BuildDataPacket(
    const QuicPacketHeader& header, char* buffer) {
  if (!CopyCryptoDataToLocalBuffer()) {
    return std::nullopt;
  }
  SplitCryptoFrame();
  AddPingFrames();
  SpreadPadding();
  ReorderFrames();
  return BuildPacket(header, buffer);
}

WriteStreamDataResult QuicChaosProtector::WriteStreamData(
    QuicStreamId id, QuicStreamOffset offset, QuicByteCount data_length,
    QuicDataWriter* /*writer*/) {
  QUIC_BUG(chaos stream) << "This should never be called; id " << id
                         << " offset " << offset << " data_length "
                         << data_length;
  return STREAM_MISSING;
}

bool QuicChaosProtector::WriteCryptoData(EncryptionLevel level,
                                         QuicStreamOffset offset,
                                         QuicByteCount data_length,
                                         QuicDataWriter* writer) {
  if (level != level_) {
    QUIC_BUG(chaos bad level) << "Unexpected " << level << " != " << level_;
    return false;
  }
  // This is `offset + data_length > buffer_offset_ + buffer_length_`
  // but with integer overflow protection.
  if (offset < crypto_buffer_offset_ || data_length > crypto_data_length_ ||
      offset - crypto_buffer_offset_ > crypto_data_length_ - data_length) {
    QUIC_BUG(chaos bad lengths)
        << "Unexpected buffer_offset_ " << crypto_buffer_offset_ << " offset "
        << offset << " buffer_length_ " << crypto_data_length_
        << " data_length " << data_length;
    return false;
  }
  writer->WriteBytes(&crypto_data_buffer_[offset - crypto_buffer_offset_],
                     data_length);
  return true;
}

bool QuicChaosProtector::CopyCryptoDataToLocalBuffer() {
  crypto_frame_buffer_ = std::make_unique<char[]>(packet_size_);
  frames_.push_back(QuicFrame(
      new QuicCryptoFrame(level_, crypto_buffer_offset_, crypto_data_length_)));
  // We use |framer_| to serialize the CRYPTO frame in order to extract its
  // data from the crypto data producer. This ensures that we reuse the
  // usual serialization code path, but has the downside that we then need to
  // parse the offset and length in order to skip over those fields.
  QuicDataWriter writer(packet_size_, crypto_frame_buffer_.get());
  if (!framer_->AppendCryptoFrame(*frames_.front().crypto_frame, &writer)) {
    QUIC_BUG(chaos write crypto data);
    return false;
  }
  QuicDataReader reader(crypto_frame_buffer_.get(), writer.length());
  uint64_t parsed_offset, parsed_length;
  if (!reader.ReadVarInt62(&parsed_offset) ||
      !reader.ReadVarInt62(&parsed_length)) {
    QUIC_BUG(chaos parse crypto frame);
    return false;
  }

  absl::string_view crypto_data = reader.ReadRemainingPayload();
  crypto_data_buffer_ = crypto_data.data();

  QUICHE_DCHECK_EQ(parsed_offset, crypto_buffer_offset_);
  QUICHE_DCHECK_EQ(parsed_length, crypto_data_length_);
  QUICHE_DCHECK_EQ(parsed_length, crypto_data.length());

  return true;
}

void QuicChaosProtector::SplitCryptoFrame() {
  const int max_overhead_of_adding_a_crypto_frame =
      static_cast<int>(QuicFramer::GetMinCryptoFrameSize(
          crypto_buffer_offset_ + crypto_data_length_, crypto_data_length_));
  // Pick a random number of CRYPTO frames to add.
  constexpr uint64_t kMaxAddedCryptoFrames = 10;
  const uint64_t num_added_crypto_frames =
      random_->InsecureRandUint64() % (kMaxAddedCryptoFrames + 1);
  for (uint64_t i = 0; i < num_added_crypto_frames; i++) {
    if (remaining_padding_bytes_ < max_overhead_of_adding_a_crypto_frame) {
      break;
    }
    // Pick a random frame and split it by shrinking the picked frame and
    // moving the second half of its data to a new frame that is then appended
    // to |frames|.
    size_t frame_to_split_index =
        random_->InsecureRandUint64() % frames_.size();
    QuicCryptoFrame* frame_to_split =
        frames_[frame_to_split_index].crypto_frame;
    if (frame_to_split->data_length <= 1) {
      continue;
    }
    const int frame_to_split_old_overhead =
        static_cast<int>(QuicFramer::GetMinCryptoFrameSize(
            frame_to_split->offset, frame_to_split->data_length));
    const QuicPacketLength frame_to_split_new_data_length =
        1 + (random_->InsecureRandUint64() % (frame_to_split->data_length - 1));
    const QuicPacketLength new_frame_data_length =
        frame_to_split->data_length - frame_to_split_new_data_length;
    const QuicStreamOffset new_frame_offset =
        frame_to_split->offset + frame_to_split_new_data_length;
    frame_to_split->data_length -= new_frame_data_length;
    frames_.push_back(QuicFrame(
        new QuicCryptoFrame(level_, new_frame_offset, new_frame_data_length)));
    const int frame_to_split_new_overhead =
        static_cast<int>(QuicFramer::GetMinCryptoFrameSize(
            frame_to_split->offset, frame_to_split->data_length));
    const int new_frame_overhead =
        static_cast<int>(QuicFramer::GetMinCryptoFrameSize(
            new_frame_offset, new_frame_data_length));
    QUICHE_DCHECK_LE(frame_to_split_new_overhead, frame_to_split_old_overhead);
    // Readjust padding based on increased overhead.
    remaining_padding_bytes_ -= new_frame_overhead;
    remaining_padding_bytes_ -= frame_to_split_new_overhead;
    remaining_padding_bytes_ += frame_to_split_old_overhead;
  }
}

void QuicChaosProtector::AddPingFrames() {
  if (remaining_padding_bytes_ == 0) {
    return;
  }
  constexpr uint64_t kMaxAddedPingFrames = 10;
  const uint64_t num_ping_frames =
      random_->InsecureRandUint64() %
      std::min<uint64_t>(kMaxAddedPingFrames, remaining_padding_bytes_);
  for (uint64_t i = 0; i < num_ping_frames; i++) {
    frames_.push_back(QuicFrame(QuicPingFrame()));
  }
  remaining_padding_bytes_ -= static_cast<int>(num_ping_frames);
}

void QuicChaosProtector::ReorderFrames() {
  // Walk the array backwards and swap each frame with a random earlier one.
  for (size_t i = frames_.size() - 1; i > 0; i--) {
    std::swap(frames_[i], frames_[random_->InsecureRandUint64() % (i + 1)]);
  }
}

void QuicChaosProtector::SpreadPadding() {
  for (auto it = frames_.begin(); it != frames_.end(); ++it) {
    const int padding_bytes_in_this_frame =
        random_->InsecureRandUint64() % (remaining_padding_bytes_ + 1);
    if (padding_bytes_in_this_frame <= 0) {
      continue;
    }
    it = frames_.insert(
        it, QuicFrame(QuicPaddingFrame(padding_bytes_in_this_frame)));
    ++it;  // Skip over the padding frame we just added.
    remaining_padding_bytes_ -= padding_bytes_in_this_frame;
  }
  if (remaining_padding_bytes_ > 0) {
    frames_.push_back(QuicFrame(QuicPaddingFrame(remaining_padding_bytes_)));
  }
}

std::optional<size_t> QuicChaosProtector::BuildPacket(
    const QuicPacketHeader& header, char* buffer) {
  QuicStreamFrameDataProducer* original_data_producer =
      framer_->data_producer();
  framer_->set_data_producer(this);

  size_t length =
      framer_->BuildDataPacket(header, frames_, buffer, packet_size_, level_);

  framer_->set_data_producer(original_data_producer);
  if (length == 0) {
    return std::nullopt;
  }
  return length;
}

}  // namespace quic
