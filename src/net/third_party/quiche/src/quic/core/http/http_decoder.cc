// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/http/http_frames.h"
#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_fallthrough.h"

namespace quic {

HttpDecoder::HttpDecoder(Visitor* visitor)
    : visitor_(visitor),
      state_(STATE_READING_FRAME_TYPE),
      current_frame_type_(0),
      current_length_field_length_(0),
      remaining_length_field_length_(0),
      current_frame_length_(0),
      remaining_frame_length_(0),
      current_type_field_length_(0),
      remaining_type_field_length_(0),
      error_(QUIC_NO_ERROR),
      error_detail_("") {
  DCHECK(visitor_);
}

HttpDecoder::~HttpDecoder() {}

QuicByteCount HttpDecoder::ProcessInput(const char* data, QuicByteCount len) {
  DCHECK_EQ(QUIC_NO_ERROR, error_);
  DCHECK_NE(STATE_ERROR, state_);

  QuicDataReader reader(data, len);
  bool continue_processing = true;
  while (continue_processing &&
         (reader.BytesRemaining() != 0 || state_ == STATE_FINISH_PARSING)) {
    // |continue_processing| must have been set to false upon error.
    DCHECK_EQ(QUIC_NO_ERROR, error_);
    DCHECK_NE(STATE_ERROR, state_);

    switch (state_) {
      case STATE_READING_FRAME_TYPE:
        ReadFrameType(&reader);
        break;
      case STATE_READING_FRAME_LENGTH:
        continue_processing = ReadFrameLength(&reader);
        break;
      case STATE_READING_FRAME_PAYLOAD:
        continue_processing = ReadFramePayload(&reader);
        break;
      case STATE_FINISH_PARSING:
        continue_processing = FinishParsing();
        break;
      case STATE_ERROR:
        break;
      default:
        QUIC_BUG << "Invalid state: " << state_;
    }
  }

  return len - reader.BytesRemaining();
}

void HttpDecoder::ReadFrameType(QuicDataReader* reader) {
  DCHECK_NE(0u, reader->BytesRemaining());
  if (current_type_field_length_ == 0) {
    // A new frame is coming.
    current_type_field_length_ = reader->PeekVarInt62Length();
    DCHECK_NE(0u, current_type_field_length_);
    if (current_type_field_length_ > reader->BytesRemaining()) {
      // Buffer a new type field.
      remaining_type_field_length_ = current_type_field_length_;
      BufferFrameType(reader);
      return;
    }
    // The reader has all type data needed, so no need to buffer.
    bool success = reader->ReadVarInt62(&current_frame_type_);
    DCHECK(success);
  } else {
    // Buffer the existing type field.
    BufferFrameType(reader);
    // The frame is still not buffered completely.
    if (remaining_type_field_length_ != 0) {
      return;
    }
    QuicDataReader type_reader(type_buffer_.data(), current_type_field_length_);
    bool success = type_reader.ReadVarInt62(&current_frame_type_);
    DCHECK(success);
  }

  state_ = STATE_READING_FRAME_LENGTH;
}

bool HttpDecoder::ReadFrameLength(QuicDataReader* reader) {
  DCHECK_NE(0u, reader->BytesRemaining());
  if (current_length_field_length_ == 0) {
    // A new frame is coming.
    current_length_field_length_ = reader->PeekVarInt62Length();
    DCHECK_NE(0u, current_length_field_length_);
    if (current_length_field_length_ > reader->BytesRemaining()) {
      // Buffer a new length field.
      remaining_length_field_length_ = current_length_field_length_;
      BufferFrameLength(reader);
      return true;
    }
    // The reader has all length data needed, so no need to buffer.
    bool success = reader->ReadVarInt62(&current_frame_length_);
    DCHECK(success);
  } else {
    // Buffer the existing length field.
    BufferFrameLength(reader);
    // The frame is still not buffered completely.
    if (remaining_length_field_length_ != 0) {
      return true;
    }
    QuicDataReader length_reader(length_buffer_.data(),
                                 current_length_field_length_);
    bool success = length_reader.ReadVarInt62(&current_frame_length_);
    DCHECK(success);
  }

  if (current_frame_length_ > MaxFrameLength(current_frame_type_)) {
    // TODO(bnc): Signal HTTP_EXCESSIVE_LOAD or similar to peer.
    RaiseError(QUIC_INTERNAL_ERROR, "Frame is too large");
    visitor_->OnError(this);
    return false;
  }

  // Calling the following visitor methods does not require parsing of any
  // frame payload.
  bool continue_processing = true;
  auto frame_meta = Http3FrameLengths(
      current_length_field_length_ + current_type_field_length_,
      current_frame_length_);

  switch (current_frame_type_) {
    case static_cast<uint64_t>(HttpFrameType::DATA):
      continue_processing = visitor_->OnDataFrameStart(frame_meta);
      break;
    case static_cast<uint64_t>(HttpFrameType::HEADERS):
      continue_processing = visitor_->OnHeadersFrameStart(frame_meta);
      break;
    case static_cast<uint64_t>(HttpFrameType::PRIORITY):
      continue_processing = visitor_->OnPriorityFrameStart(frame_meta);
      break;
    case static_cast<uint64_t>(HttpFrameType::CANCEL_PUSH):
      break;
    case static_cast<uint64_t>(HttpFrameType::SETTINGS):
      continue_processing = visitor_->OnSettingsFrameStart(frame_meta);
      break;
    case static_cast<uint64_t>(HttpFrameType::PUSH_PROMISE):
      break;
    case static_cast<uint64_t>(HttpFrameType::GOAWAY):
      break;
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID):
      break;
    case static_cast<uint64_t>(HttpFrameType::DUPLICATE_PUSH):
      break;
    default:
      continue_processing =
          visitor_->OnUnknownFrameStart(current_frame_type_, frame_meta);
      break;
  }

  remaining_frame_length_ = current_frame_length_;
  state_ = (remaining_frame_length_ == 0) ? STATE_FINISH_PARSING
                                          : STATE_READING_FRAME_PAYLOAD;
  return continue_processing;
}

bool HttpDecoder::ReadFramePayload(QuicDataReader* reader) {
  DCHECK_NE(0u, reader->BytesRemaining());
  DCHECK_NE(0u, remaining_frame_length_);

  bool continue_processing = true;

  switch (current_frame_type_) {
    case static_cast<uint64_t>(HttpFrameType::DATA): {
      QuicByteCount bytes_to_read = std::min<QuicByteCount>(
          remaining_frame_length_, reader->BytesRemaining());
      QuicStringPiece payload;
      bool success = reader->ReadStringPiece(&payload, bytes_to_read);
      DCHECK(success);
      DCHECK(!payload.empty());
      continue_processing = visitor_->OnDataFramePayload(payload);
      remaining_frame_length_ -= payload.length();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::HEADERS): {
      QuicByteCount bytes_to_read = std::min<QuicByteCount>(
          remaining_frame_length_, reader->BytesRemaining());
      QuicStringPiece payload;
      bool success = reader->ReadStringPiece(&payload, bytes_to_read);
      DCHECK(success);
      DCHECK(!payload.empty());
      continue_processing = visitor_->OnHeadersFramePayload(payload);
      remaining_frame_length_ -= payload.length();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::PRIORITY): {
      // TODO(rch): avoid buffering if the entire frame is present, and
      // instead parse directly out of |reader|.
      BufferFramePayload(reader);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::CANCEL_PUSH): {
      BufferFramePayload(reader);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::SETTINGS): {
      BufferFramePayload(reader);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::PUSH_PROMISE): {
      if (current_frame_length_ == remaining_frame_length_) {
        QuicByteCount bytes_remaining = reader->BytesRemaining();
        PushId push_id;
        // TODO(rch): Handle partial delivery of this field.
        if (!reader->ReadVarInt62(&push_id)) {
          RaiseError(QUIC_INTERNAL_ERROR, "Unable to read push_id");
          return false;
        }
        remaining_frame_length_ -= bytes_remaining - reader->BytesRemaining();
        if (!visitor_->OnPushPromiseFrameStart(
                push_id, Http3FrameLengths(current_length_field_length_ +
                                               current_type_field_length_,
                                           current_frame_length_))) {
          continue_processing = false;
          break;
        }
      }
      DCHECK_LT(remaining_frame_length_, current_frame_length_);
      QuicByteCount bytes_to_read = std::min<QuicByteCount>(
          remaining_frame_length_, reader->BytesRemaining());
      if (bytes_to_read == 0) {
        break;
      }
      QuicStringPiece payload;
      bool success = reader->ReadStringPiece(&payload, bytes_to_read);
      DCHECK(success);
      DCHECK(!payload.empty());
      continue_processing = visitor_->OnPushPromiseFramePayload(payload);
      remaining_frame_length_ -= payload.length();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::GOAWAY): {
      BufferFramePayload(reader);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID): {
      // TODO(rch): Handle partial delivery.
      BufferFramePayload(reader);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::DUPLICATE_PUSH): {
      BufferFramePayload(reader);
      break;
    }
    default: {
      QuicByteCount bytes_to_read = std::min<QuicByteCount>(
          remaining_frame_length_, reader->BytesRemaining());
      QuicStringPiece payload;
      bool success = reader->ReadStringPiece(&payload, bytes_to_read);
      DCHECK(success);
      DCHECK(!payload.empty());
      continue_processing = visitor_->OnUnknownFramePayload(payload);
      remaining_frame_length_ -= payload.length();
      break;
    }
  }

  if (remaining_frame_length_ == 0) {
    state_ = STATE_FINISH_PARSING;
  }

  return continue_processing;
}

bool HttpDecoder::FinishParsing() {
  DCHECK_EQ(0u, remaining_frame_length_);

  bool continue_processing = true;

  switch (current_frame_type_) {
    case static_cast<uint64_t>(HttpFrameType::DATA): {
      continue_processing = visitor_->OnDataFrameEnd();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::HEADERS): {
      continue_processing = visitor_->OnHeadersFrameEnd();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::PRIORITY): {
      // TODO(rch): avoid buffering if the entire frame is present, and
      // instead parse directly out of |reader|.
      PriorityFrame frame;
      QuicDataReader reader(buffer_.data(), current_frame_length_);
      if (!ParsePriorityFrame(&reader, &frame)) {
        return false;
      }
      continue_processing = visitor_->OnPriorityFrame(frame);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::CANCEL_PUSH): {
      // TODO(rch): Handle partial delivery.
      CancelPushFrame frame;
      QuicDataReader reader(buffer_.data(), current_frame_length_);
      if (!reader.ReadVarInt62(&frame.push_id)) {
        RaiseError(QUIC_INTERNAL_ERROR, "Unable to read push_id");
        return false;
      }
      continue_processing = visitor_->OnCancelPushFrame(frame);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::SETTINGS): {
      SettingsFrame frame;
      QuicDataReader reader(buffer_.data(), current_frame_length_);
      if (!ParseSettingsFrame(&reader, &frame)) {
        return false;
      }
      continue_processing = visitor_->OnSettingsFrame(frame);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::PUSH_PROMISE): {
      continue_processing = visitor_->OnPushPromiseFrameEnd();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::GOAWAY): {
      // TODO(bnc): Handle partial delivery.
      QuicDataReader reader(buffer_.data(), current_frame_length_);
      GoAwayFrame frame;
      static_assert(!std::is_same<decltype(frame.stream_id), uint64_t>::value,
                    "Please remove local |stream_id| variable and pass "
                    "&frame.stream_id directly to ReadVarInt62() when changing "
                    "QuicStreamId from uint32_t to uint64_t.");
      uint64_t stream_id;
      if (!reader.ReadVarInt62(&stream_id)) {
        RaiseError(QUIC_INTERNAL_ERROR, "Unable to read GOAWAY stream_id");
        return false;
      }
      frame.stream_id = static_cast<QuicStreamId>(stream_id);
      continue_processing = visitor_->OnGoAwayFrame(frame);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID): {
      // TODO(bnc): Handle partial delivery.
      QuicDataReader reader(buffer_.data(), current_frame_length_);
      MaxPushIdFrame frame;
      if (!reader.ReadVarInt62(&frame.push_id)) {
        RaiseError(QUIC_INTERNAL_ERROR, "Unable to read push_id");
        return false;
      }
      continue_processing = visitor_->OnMaxPushIdFrame(frame);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::DUPLICATE_PUSH): {
      // TODO(bnc): Handle partial delivery.
      QuicDataReader reader(buffer_.data(), current_frame_length_);
      DuplicatePushFrame frame;
      if (!reader.ReadVarInt62(&frame.push_id)) {
        RaiseError(QUIC_INTERNAL_ERROR, "Unable to read push_id");
        return false;
      }
      continue_processing = visitor_->OnDuplicatePushFrame(frame);
      break;
    }
    default: {
      continue_processing = visitor_->OnUnknownFrameEnd();
      break;
    }
  }

  current_length_field_length_ = 0;
  current_type_field_length_ = 0;
  state_ = STATE_READING_FRAME_TYPE;
  return continue_processing;
}

void HttpDecoder::DiscardFramePayload(QuicDataReader* reader) {
  QuicByteCount bytes_to_read = std::min<QuicByteCount>(
      remaining_frame_length_, reader->BytesRemaining());
  QuicStringPiece payload;
  bool success = reader->ReadStringPiece(&payload, bytes_to_read);
  DCHECK(success);
  remaining_frame_length_ -= payload.length();
  if (remaining_frame_length_ == 0) {
    state_ = STATE_READING_FRAME_TYPE;
    current_length_field_length_ = 0;
    current_type_field_length_ = 0;
  }
}

void HttpDecoder::BufferFramePayload(QuicDataReader* reader) {
  if (current_frame_length_ == remaining_frame_length_) {
    buffer_.erase(buffer_.size());
    buffer_.reserve(current_frame_length_);
  }
  QuicByteCount bytes_to_read = std::min<QuicByteCount>(
      remaining_frame_length_, reader->BytesRemaining());
  bool success = reader->ReadBytes(
      &(buffer_[0]) + current_frame_length_ - remaining_frame_length_,
      bytes_to_read);
  DCHECK(success);
  remaining_frame_length_ -= bytes_to_read;
}

void HttpDecoder::BufferFrameLength(QuicDataReader* reader) {
  if (current_length_field_length_ == remaining_length_field_length_) {
    length_buffer_.fill(0);
  }
  QuicByteCount bytes_to_read = std::min<QuicByteCount>(
      remaining_length_field_length_, reader->BytesRemaining());
  bool success =
      reader->ReadBytes(length_buffer_.data() + current_length_field_length_ -
                            remaining_length_field_length_,
                        bytes_to_read);
  DCHECK(success);
  remaining_length_field_length_ -= bytes_to_read;
}

void HttpDecoder::BufferFrameType(QuicDataReader* reader) {
  if (current_type_field_length_ == remaining_type_field_length_) {
    type_buffer_.fill(0);
  }
  QuicByteCount bytes_to_read = std::min<QuicByteCount>(
      remaining_type_field_length_, reader->BytesRemaining());
  bool success =
      reader->ReadBytes(type_buffer_.data() + current_type_field_length_ -
                            remaining_type_field_length_,
                        bytes_to_read);
  DCHECK(success);
  remaining_type_field_length_ -= bytes_to_read;
}

void HttpDecoder::RaiseError(QuicErrorCode error, std::string error_detail) {
  state_ = STATE_ERROR;
  error_ = error;
  error_detail_ = std::move(error_detail);
}

bool HttpDecoder::ParsePriorityFrame(QuicDataReader* reader,
                                     PriorityFrame* frame) {
  uint8_t flags;
  if (!reader->ReadUInt8(&flags)) {
    // TODO(b/124216424): Use HTTP_MALFORMED_FRAME.
    RaiseError(QUIC_INVALID_FRAME_DATA, "Unable to read PRIORITY frame flags.");
    return false;
  }

  // Assign two most significant bits to prioritized_type.
  frame->prioritized_type = static_cast<PriorityElementType>((flags >> 6) & 3);
  // Assign the next two most significant bits to dependency type.
  frame->dependency_type = static_cast<PriorityElementType>((flags >> 4) & 3);
  frame->exclusive = flags & kPriorityExclusiveBit;
  // TODO(bnc): Close connection with HTTP_MALFORMED_FRAME
  // if lowest three bits are not all zero.

  // TODO(b/137359636): Handle partial delivery.
  if (frame->prioritized_type != ROOT_OF_TREE &&
      !reader->ReadVarInt62(&frame->prioritized_element_id)) {
    // TODO(b/124216424): Use HTTP_MALFORMED_FRAME.
    RaiseError(QUIC_INVALID_FRAME_DATA,
               "Unable to read prioritized_element_id.");
    return false;
  }
  if (frame->dependency_type != ROOT_OF_TREE &&
      !reader->ReadVarInt62(&frame->element_dependency_id)) {
    // TODO(b/124216424): Use HTTP_MALFORMED_FRAME.
    RaiseError(QUIC_INVALID_FRAME_DATA,
               "Unable to read element_dependency_id.");
    return false;
  }
  if (!reader->ReadUInt8(&frame->weight)) {
    // TODO(b/124216424): Use HTTP_MALFORMED_FRAME.
    RaiseError(QUIC_INVALID_FRAME_DATA,
               "Unable to read priority frame weight.");
    return false;
  }
  if (!reader->IsDoneReading()) {
    // TODO(b/124216424): Use HTTP_MALFORMED_FRAME.
    RaiseError(QUIC_INVALID_FRAME_DATA, "Superfluous data in priority frame.");
    return false;
  }
  return true;
}

bool HttpDecoder::ParseSettingsFrame(QuicDataReader* reader,
                                     SettingsFrame* frame) {
  while (!reader->IsDoneReading()) {
    // TODO(bnc): Handle partial delivery of both fields.
    uint64_t id;
    if (!reader->ReadVarInt62(&id)) {
      RaiseError(QUIC_INTERNAL_ERROR,
                 "Unable to read settings frame identifier");
      return false;
    }
    uint64_t content;
    if (!reader->ReadVarInt62(&content)) {
      RaiseError(QUIC_INTERNAL_ERROR, "Unable to read settings frame content");
      return false;
    }
    frame->values[id] = content;
  }
  return true;
}

QuicByteCount HttpDecoder::MaxFrameLength(uint64_t frame_type) {
  switch (frame_type) {
    case static_cast<uint64_t>(HttpFrameType::PRIORITY):
      return kPriorityFirstByteLength + VARIABLE_LENGTH_INTEGER_LENGTH_8 * 2 +
             kPriorityWeightLength;
    case static_cast<uint64_t>(HttpFrameType::CANCEL_PUSH):
      return sizeof(PushId);
    case static_cast<uint64_t>(HttpFrameType::SETTINGS):
      // This limit is arbitrary.
      return 1024 * 1024;
    case static_cast<uint64_t>(HttpFrameType::GOAWAY):
      return sizeof(QuicStreamId);
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID):
      return sizeof(PushId);
    case static_cast<uint64_t>(HttpFrameType::DUPLICATE_PUSH):
      return sizeof(PushId);
    default:
      // Other frames require no data buffering, so it's safe to have no limit.
      return std::numeric_limits<QuicByteCount>::max();
  }
}

}  // namespace quic
