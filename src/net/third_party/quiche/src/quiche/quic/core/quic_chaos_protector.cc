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
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace quic {

QuicChaosProtectorOld::QuicChaosProtectorOld(
    const QuicCryptoFrame& crypto_frame, int num_padding_bytes,
    size_t packet_size, QuicFramer* framer, QuicRandom* random)
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

QuicChaosProtectorOld::~QuicChaosProtectorOld() { DeleteFrames(&frames_); }

std::optional<size_t> QuicChaosProtectorOld::BuildDataPacket(
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

WriteStreamDataResult QuicChaosProtectorOld::WriteStreamData(
    QuicStreamId id, QuicStreamOffset offset, QuicByteCount data_length,
    QuicDataWriter* /*writer*/) {
  QUIC_BUG(chaos stream) << "This should never be called; id " << id
                         << " offset " << offset << " data_length "
                         << data_length;
  return STREAM_MISSING;
}

bool QuicChaosProtectorOld::WriteCryptoData(EncryptionLevel level,
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

bool QuicChaosProtectorOld::CopyCryptoDataToLocalBuffer() {
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

void QuicChaosProtectorOld::SplitCryptoFrame() {
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

void QuicChaosProtectorOld::AddPingFrames() {
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

void QuicChaosProtectorOld::ReorderFrames() {
  // Walk the array backwards and swap each frame with a random earlier one.
  for (size_t i = frames_.size() - 1; i > 0; i--) {
    std::swap(frames_[i], frames_[random_->InsecureRandUint64() % (i + 1)]);
  }
}

void QuicChaosProtectorOld::SpreadPadding() {
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

std::optional<size_t> QuicChaosProtectorOld::BuildPacket(
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

// End of old code, start of new code.

QuicChaosProtector::QuicChaosProtector(size_t packet_size,
                                       EncryptionLevel level,
                                       QuicFramer* framer, QuicRandom* random)
    : avoid_copy_(GetQuicReloadableFlag(quic_chaos_protector_avoid_copy)),
      packet_size_(packet_size),
      level_(level),
      framer_(framer),
      random_(random) {
  if (avoid_copy_) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_chaos_protector_avoid_copy);
  }
  QUICHE_DCHECK_NE(framer_, nullptr);
  QUICHE_DCHECK_NE(framer_->data_producer(), nullptr);
  QUICHE_DCHECK_NE(random_, nullptr);
}

bool QuicChaosProtector::IngestFrames(const QuicFrames& frames) {
  bool has_crypto_frame = false;
  bool has_padding_frame = false;
  QuicByteCount max_crypto_data;
  for (const QuicFrame& frame : frames) {
    if (frame.type == CRYPTO_FRAME) {
      if (level_ != frame.crypto_frame->level) {
        QUIC_BUG(chaos encryption level)
            << level_ << " != " << frame.crypto_frame->level;
        return false;
      }
      if (!has_crypto_frame) {
        crypto_data_length_ = frame.crypto_frame->data_length;
        crypto_buffer_offset_ = frame.crypto_frame->offset;
        max_crypto_data = crypto_buffer_offset_ + crypto_data_length_;
      } else {
        crypto_buffer_offset_ =
            std::min(crypto_buffer_offset_, frame.crypto_frame->offset);
        max_crypto_data =
            std::max(max_crypto_data, frame.crypto_frame->offset +
                                          frame.crypto_frame->data_length);
        crypto_data_length_ = max_crypto_data - crypto_buffer_offset_;
      }
      has_crypto_frame = true;
      frames_.push_back(QuicFrame(new QuicCryptoFrame(*frame.crypto_frame)));
      continue;
    }
    if (frame.type == PADDING_FRAME) {
      if (has_padding_frame) {
        return false;
      }
      has_padding_frame = true;
      remaining_padding_bytes_ = frame.padding_frame.num_padding_bytes;
      if (remaining_padding_bytes_ <= 0) {
        // Do not perform chaos protection if we do not have a known number of
        // padding bytes to work with.
        return false;
      }
      continue;
    }
    // Copy any other frames unmodified. Note that the buffer allocator is
    // only used for DATAGRAM frames, and those aren't used here, so the
    // buffer allocator is never actually used.
    frames_.push_back(
        CopyQuicFrame(quiche::SimpleBufferAllocator::Get(), frame));
  }
  return has_crypto_frame && has_padding_frame;
}

QuicChaosProtector::~QuicChaosProtector() { DeleteFrames(&frames_); }

std::optional<size_t> QuicChaosProtector::BuildDataPacket(
    const QuicPacketHeader& header, const QuicFrames& frames, char* buffer) {
  if (!IngestFrames(frames)) {
    QUIC_DVLOG(1) << "Failed to ingest frames for initial packet number "
                  << header.packet_number;
    return std::nullopt;
  }
  if (!avoid_copy_) {
    if (!CopyCryptoDataToLocalBuffer()) {
      QUIC_DVLOG(1) << "Failed to copy crypto data to local buffer for initial "
                       "packet number "
                    << header.packet_number;
      return std::nullopt;
    }
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
  if (avoid_copy_) {
    QUIC_BUG(chaos avoid copy WriteCryptoData) << "This should never be called";
    return false;
  }
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
  if (avoid_copy_) {
    QUIC_BUG(chaos avoid copy CopyCryptoDataToLocalBuffer)
        << "This should never be called";
    return false;
  }
  size_t frame_size = QuicDataWriter::GetVarInt62Len(crypto_buffer_offset_) +
                      QuicDataWriter::GetVarInt62Len(crypto_data_length_) +
                      crypto_data_length_;
  crypto_frame_buffer_ = std::make_unique<char[]>(frame_size);
  // We use |framer_| to serialize the CRYPTO frame in order to extract its
  // data from the crypto data producer. This ensures that we reuse the
  // usual serialization code path, but has the downside that we then need to
  // parse the offset and length in order to skip over those fields.
  QuicDataWriter writer(frame_size, crypto_frame_buffer_.get());
  QuicCryptoFrame crypto_frame(level_, crypto_buffer_offset_,
                               crypto_data_length_);
  if (!framer_->AppendCryptoFrame(crypto_frame, &writer)) {
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
  constexpr uint64_t kMinAddedCryptoFrames = 2;
  constexpr uint64_t kMaxAddedCryptoFrames = 10;
  const uint64_t num_added_crypto_frames =
      kMinAddedCryptoFrames +
      random_->InsecureRandUint64() %
          (kMaxAddedCryptoFrames + 1 - kMinAddedCryptoFrames);
  for (uint64_t i = 0; i < num_added_crypto_frames; i++) {
    if (remaining_padding_bytes_ < max_overhead_of_adding_a_crypto_frame) {
      break;
    }
    // Pick a random frame and split it by shrinking the picked frame and
    // moving the second half of its data to a new frame that is then appended
    // to |frames|.
    size_t frame_to_split_index =
        random_->InsecureRandUint64() % frames_.size();
    // Only split CRYPTO frames.
    if (frames_[frame_to_split_index].type != CRYPTO_FRAME) {
      continue;
    }
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
  constexpr uint64_t kMinAddedPingFrames = 2;
  constexpr uint64_t kMaxAddedPingFrames = 10;
  const uint64_t num_ping_frames = std::min<uint64_t>(
      kMinAddedPingFrames + random_->InsecureRandUint64() %
                                (kMaxAddedPingFrames + 1 - kMinAddedPingFrames),
      remaining_padding_bytes_);
  for (uint64_t i = 0; i < num_ping_frames; i++) {
    frames_.push_back(QuicFrame(QuicPingFrame()));
  }
  remaining_padding_bytes_ -= static_cast<int>(num_ping_frames);
}

void QuicChaosProtector::ReorderFrames() {
  // Walk the array backwards and swap each frame with a random earlier one.
  for (size_t i = frames_.size() - 1; i > 0; i--) {
    quic::QuicFrame& lhs = frames_[i];
    quic::QuicFrame& rhs = frames_[random_->InsecureRandUint64() % (i + 1)];
    // Do not swap ACK frames to minimize impact on congestion control.
    if (lhs.type != ACK_FRAME && rhs.type != ACK_FRAME) {
      std::swap(lhs, rhs);
    }
  }
}

void QuicChaosProtector::SpreadPadding() {
  for (auto it = frames_.begin(); it != frames_.end(); ++it) {
    const int padding_bytes_in_this_frame =
        random_->InsecureRandUint64() % (remaining_padding_bytes_ + 1);
    if (padding_bytes_in_this_frame <= 0) {
      continue;
    }
    // Do not add PADDING before ACK to minimize impact on congestion control.
    if (it->type == ACK_FRAME) {
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
  QuicStreamFrameDataProducer* original_data_producer;
  if (!avoid_copy_) {
    original_data_producer = framer_->data_producer();
    framer_->set_data_producer(this);
  }

  size_t length =
      framer_->BuildDataPacket(header, frames_, buffer, packet_size_, level_);

  if (!avoid_copy_) {
    framer_->set_data_producer(original_data_producer);
  }
  if (length == 0) {
    QUIC_DVLOG(1) << "Failed to build data packet for initial packet number "
                  << header.packet_number;
    return std::nullopt;
  }
  QUIC_DVLOG(1) << "Performed chaos protection on initial packet number "
                << header.packet_number << " with length " << length;
  return length;
}

}  // namespace quic
