// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/crypto_framer.h"

#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/quic_data_reader.h"
#include "net/quic/core/quic_data_writer.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_str_cat.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

namespace {

const size_t kQuicTagSize = sizeof(QuicTag);
const size_t kCryptoEndOffsetSize = sizeof(uint32_t);
const size_t kNumEntriesSize = sizeof(uint16_t);

// OneShotVisitor is a framer visitor that records a single handshake message.
class OneShotVisitor : public CryptoFramerVisitorInterface {
 public:
  OneShotVisitor() : error_(false) {}

  void OnError(CryptoFramer* framer) override { error_ = true; }

  void OnHandshakeMessage(const CryptoHandshakeMessage& message) override {
    out_.reset(new CryptoHandshakeMessage(message));
  }

  bool error() const { return error_; }

  std::unique_ptr<CryptoHandshakeMessage> release() { return std::move(out_); }

 private:
  std::unique_ptr<CryptoHandshakeMessage> out_;
  bool error_;
};

}  // namespace

CryptoFramer::CryptoFramer()
    : visitor_(nullptr), error_detail_(""), num_entries_(0), values_len_(0) {
  Clear();
}

CryptoFramer::~CryptoFramer() {}

// static
std::unique_ptr<CryptoHandshakeMessage> CryptoFramer::ParseMessage(
    QuicStringPiece in,
    Perspective perspective) {
  OneShotVisitor visitor;
  CryptoFramer framer;

  framer.set_visitor(&visitor);
  if (!framer.ProcessInput(in, perspective) || visitor.error() ||
      framer.InputBytesRemaining()) {
    return nullptr;
  }

  return visitor.release();
}

QuicErrorCode CryptoFramer::error() const {
  return error_;
}

const std::string& CryptoFramer::error_detail() const {
  return error_detail_;
}

bool CryptoFramer::ProcessInput(QuicStringPiece input,
                                Perspective perspective) {
  DCHECK_EQ(QUIC_NO_ERROR, error_);
  if (error_ != QUIC_NO_ERROR) {
    return false;
  }
  error_ = Process(input, perspective);
  if (error_ != QUIC_NO_ERROR) {
    DCHECK(!error_detail_.empty());
    visitor_->OnError(this);
    return false;
  }

  return true;
}

size_t CryptoFramer::InputBytesRemaining() const {
  return buffer_.length();
}

// static
QuicData* CryptoFramer::ConstructHandshakeMessage(
    const CryptoHandshakeMessage& message,
    Perspective perspective) {
  size_t num_entries = message.tag_value_map().size();
  size_t pad_length = 0;
  bool need_pad_tag = false;
  bool need_pad_value = false;

  size_t len = message.size();
  if (len < message.minimum_size()) {
    need_pad_tag = true;
    need_pad_value = true;
    num_entries++;

    size_t delta = message.minimum_size() - len;
    const size_t overhead = kQuicTagSize + kCryptoEndOffsetSize;
    if (delta > overhead) {
      pad_length = delta - overhead;
    }
    len += overhead + pad_length;
  }

  if (num_entries > kMaxEntries) {
    return nullptr;
  }

  std::unique_ptr<char[]> buffer(new char[len]);
  QuicDataWriter writer(len, buffer.get(), HOST_BYTE_ORDER);
  if (!writer.WriteTag(message.tag())) {
    DCHECK(false) << "Failed to write message tag.";
    return nullptr;
  }
  if (!writer.WriteUInt16(static_cast<uint16_t>(num_entries))) {
    DCHECK(false) << "Failed to write size.";
    return nullptr;
  }
  if (!writer.WriteUInt16(0)) {
    DCHECK(false) << "Failed to write padding.";
    return nullptr;
  }

  uint32_t end_offset = 0;
  // Tags and offsets
  for (QuicTagValueMap::const_iterator it = message.tag_value_map().begin();
       it != message.tag_value_map().end(); ++it) {
    if (it->first == kPAD && need_pad_tag) {
      // Existing PAD tags are only checked when padding needs to be added
      // because parts of the code may need to reserialize received messages
      // and those messages may, legitimately include padding.
      DCHECK(false) << "Message needed padding but already contained a PAD tag";
      return nullptr;
    }

    if (it->first > kPAD && need_pad_tag) {
      need_pad_tag = false;
      if (!WritePadTag(&writer, pad_length, &end_offset)) {
        return nullptr;
      }
    }

    if (!writer.WriteTag(it->first)) {
      DCHECK(false) << "Failed to write tag.";
      return nullptr;
    }
    end_offset += it->second.length();
    if (!writer.WriteUInt32(end_offset)) {
      DCHECK(false) << "Failed to write end offset.";
      return nullptr;
    }
  }

  if (need_pad_tag) {
    if (!WritePadTag(&writer, pad_length, &end_offset)) {
      return nullptr;
    }
  }

  // Values
  for (QuicTagValueMap::const_iterator it = message.tag_value_map().begin();
       it != message.tag_value_map().end(); ++it) {
    if (it->first > kPAD && need_pad_value) {
      need_pad_value = false;
      if (!writer.WriteRepeatedByte('-', pad_length)) {
        DCHECK(false) << "Failed to write padding.";
        return nullptr;
      }
    }

    if (!writer.WriteBytes(it->second.data(), it->second.length())) {
      DCHECK(false) << "Failed to write value.";
      return nullptr;
    }
  }

  if (need_pad_value) {
    if (!writer.WriteRepeatedByte('-', pad_length)) {
      DCHECK(false) << "Failed to write padding.";
      return nullptr;
    }
  }

  return new QuicData(buffer.release(), len, true);
}

void CryptoFramer::Clear() {
  message_.Clear();
  tags_and_lengths_.clear();
  error_ = QUIC_NO_ERROR;
  error_detail_ = "";
  state_ = STATE_READING_TAG;
}

QuicErrorCode CryptoFramer::Process(QuicStringPiece input,
                                    Perspective perspective) {
  // Add this data to the buffer.
  buffer_.append(input.data(), input.length());
  QuicDataReader reader(buffer_.data(), buffer_.length(), HOST_BYTE_ORDER);

  switch (state_) {
    case STATE_READING_TAG:
      if (reader.BytesRemaining() < kQuicTagSize) {
        break;
      }
      QuicTag message_tag;
      reader.ReadTag(&message_tag);
      message_.set_tag(message_tag);
      state_ = STATE_READING_NUM_ENTRIES;
    case STATE_READING_NUM_ENTRIES:
      if (reader.BytesRemaining() < kNumEntriesSize + sizeof(uint16_t)) {
        break;
      }
      reader.ReadUInt16(&num_entries_);
      if (num_entries_ > kMaxEntries) {
        error_detail_ = QuicStrCat(num_entries_, " entries");
        return QUIC_CRYPTO_TOO_MANY_ENTRIES;
      }
      uint16_t padding;
      reader.ReadUInt16(&padding);

      tags_and_lengths_.reserve(num_entries_);
      state_ = STATE_READING_TAGS_AND_LENGTHS;
      values_len_ = 0;
    case STATE_READING_TAGS_AND_LENGTHS: {
      if (reader.BytesRemaining() <
          num_entries_ * (kQuicTagSize + kCryptoEndOffsetSize)) {
        break;
      }

      uint32_t last_end_offset = 0;
      for (unsigned i = 0; i < num_entries_; ++i) {
        QuicTag tag;
        reader.ReadTag(&tag);
        if (i > 0 && tag <= tags_and_lengths_[i - 1].first) {
          if (tag == tags_and_lengths_[i - 1].first) {
            error_detail_ = QuicStrCat("Duplicate tag:", tag);
            return QUIC_CRYPTO_DUPLICATE_TAG;
          }
          error_detail_ = QuicStrCat("Tag ", tag, " out of order");
          return QUIC_CRYPTO_TAGS_OUT_OF_ORDER;
        }

        uint32_t end_offset;
        reader.ReadUInt32(&end_offset);

        if (end_offset < last_end_offset) {
          error_detail_ =
              QuicStrCat("End offset: ", end_offset, " vs ", last_end_offset);
          return QUIC_CRYPTO_TAGS_OUT_OF_ORDER;
        }
        tags_and_lengths_.push_back(std::make_pair(
            tag, static_cast<size_t>(end_offset - last_end_offset)));
        last_end_offset = end_offset;
      }
      values_len_ = last_end_offset;
      state_ = STATE_READING_VALUES;
    }
    case STATE_READING_VALUES:
      if (reader.BytesRemaining() < values_len_) {
        break;
      }
      for (const std::pair<QuicTag, size_t>& item : tags_and_lengths_) {
        QuicStringPiece value;
        reader.ReadStringPiece(&value, item.second);
        message_.SetStringPiece(item.first, value);
      }
      visitor_->OnHandshakeMessage(message_);
      Clear();
      state_ = STATE_READING_TAG;
      break;
  }
  // Save any remaining data.
  buffer_ = reader.PeekRemainingPayload().as_string();
  return QUIC_NO_ERROR;
}

// static
bool CryptoFramer::WritePadTag(QuicDataWriter* writer,
                               size_t pad_length,
                               uint32_t* end_offset) {
  if (!writer->WriteTag(kPAD)) {
    DCHECK(false) << "Failed to write tag.";
    return false;
  }
  *end_offset += pad_length;
  if (!writer->WriteUInt32(*end_offset)) {
    DCHECK(false) << "Failed to write end offset.";
    return false;
  }
  return true;
}

}  // namespace net
