// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_chaos_protector.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/frames/quic_crypto_frame.h"
#include "quiche/quic/core/frames/quic_frame.h"
#include "quiche/quic/core/frames/quic_padding_frame.h"
#include "quiche/quic/core/frames/quic_ping_frame.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace quic {

QuicChaosProtector::QuicChaosProtector(size_t packet_size,
                                       EncryptionLevel level,
                                       QuicFramer* framer, QuicRandom* random)
    : packet_size_(packet_size),
      level_(level),
      framer_(framer),
      random_(random) {
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
  SplitCryptoFrame();
  AddPingFrames();
  SpreadPadding();
  ReorderFrames();
  return BuildPacket(header, buffer);
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
  size_t length =
      framer_->BuildDataPacket(header, frames_, buffer, packet_size_, level_);
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
