// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/http/http_frames.h"
#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_fallthrough.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

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
      current_push_id_length_(0),
      remaining_push_id_length_(0),
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
    RaiseError(QUIC_HTTP_FRAME_TOO_LARGE, "Frame is too large.");
    return false;
  }

  // Calling the following visitor methods does not require parsing of any
  // frame payload.
  bool continue_processing = true;
  const QuicByteCount header_length =
      current_length_field_length_ + current_type_field_length_;

  switch (current_frame_type_) {
    case static_cast<uint64_t>(HttpFrameType::DATA):
      continue_processing =
          visitor_->OnDataFrameStart(header_length, current_frame_length_);
      break;
    case static_cast<uint64_t>(HttpFrameType::HEADERS):
      continue_processing =
          visitor_->OnHeadersFrameStart(header_length, current_frame_length_);
      break;
    case static_cast<uint64_t>(HttpFrameType::CANCEL_PUSH):
      break;
    case static_cast<uint64_t>(HttpFrameType::SETTINGS):
      continue_processing = visitor_->OnSettingsFrameStart(header_length);
      break;
    case static_cast<uint64_t>(HttpFrameType::PUSH_PROMISE):
      // This edge case needs to be handled here, because ReadFramePayload()
      // does not get called if |current_frame_length_| is zero.
      if (current_frame_length_ == 0) {
        RaiseError(QUIC_HTTP_FRAME_ERROR,
                   "PUSH_PROMISE frame with empty payload.");
        return false;
      }
      continue_processing = visitor_->OnPushPromiseFrameStart(header_length);
      break;
    case static_cast<uint64_t>(HttpFrameType::GOAWAY):
      break;
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID):
      break;
    case static_cast<uint64_t>(HttpFrameType::PRIORITY_UPDATE):
      continue_processing = visitor_->OnPriorityUpdateFrameStart(header_length);
      break;
    default:
      continue_processing = visitor_->OnUnknownFrameStart(
          current_frame_type_, header_length, current_frame_length_);
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
      quiche::QuicheStringPiece payload;
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
      quiche::QuicheStringPiece payload;
      bool success = reader->ReadStringPiece(&payload, bytes_to_read);
      DCHECK(success);
      DCHECK(!payload.empty());
      continue_processing = visitor_->OnHeadersFramePayload(payload);
      remaining_frame_length_ -= payload.length();
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
      PushId push_id;
      if (current_frame_length_ == remaining_frame_length_) {
        // A new Push Promise frame just arrived.
        DCHECK_EQ(0u, current_push_id_length_);
        current_push_id_length_ = reader->PeekVarInt62Length();
        if (current_push_id_length_ > remaining_frame_length_) {
          RaiseError(QUIC_HTTP_FRAME_ERROR,
                     "Unable to read PUSH_PROMISE push_id.");
          return false;
        }
        if (current_push_id_length_ > reader->BytesRemaining()) {
          // Not all bytes of push id is present yet, buffer push id.
          DCHECK_EQ(0u, remaining_push_id_length_);
          remaining_push_id_length_ = current_push_id_length_;
          BufferPushId(reader);
          break;
        }
        bool success = reader->ReadVarInt62(&push_id);
        DCHECK(success);
        remaining_frame_length_ -= current_push_id_length_;
        if (!visitor_->OnPushPromiseFramePushId(
                push_id, current_push_id_length_,
                current_frame_length_ - current_push_id_length_)) {
          continue_processing = false;
          current_push_id_length_ = 0;
          break;
        }
        current_push_id_length_ = 0;
      } else if (remaining_push_id_length_ > 0) {
        // Waiting for more bytes on push id.
        BufferPushId(reader);
        if (remaining_push_id_length_ != 0) {
          break;
        }
        QuicDataReader push_id_reader(push_id_buffer_.data(),
                                      current_push_id_length_);

        bool success = push_id_reader.ReadVarInt62(&push_id);
        DCHECK(success);
        if (!visitor_->OnPushPromiseFramePushId(
                push_id, current_push_id_length_,
                current_frame_length_ - current_push_id_length_)) {
          continue_processing = false;
          current_push_id_length_ = 0;
          break;
        }
        current_push_id_length_ = 0;
      }

      // Read Push Promise headers.
      DCHECK_LT(remaining_frame_length_, current_frame_length_);
      QuicByteCount bytes_to_read = std::min<QuicByteCount>(
          remaining_frame_length_, reader->BytesRemaining());
      if (bytes_to_read == 0) {
        break;
      }
      quiche::QuicheStringPiece payload;
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
      BufferFramePayload(reader);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::PRIORITY_UPDATE): {
      // TODO(bnc): Avoid buffering if the entire frame is present, and
      // instead parse directly out of |reader|.
      BufferFramePayload(reader);
      break;
    }
    default: {
      QuicByteCount bytes_to_read = std::min<QuicByteCount>(
          remaining_frame_length_, reader->BytesRemaining());
      quiche::QuicheStringPiece payload;
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
    case static_cast<uint64_t>(HttpFrameType::CANCEL_PUSH): {
      CancelPushFrame frame;
      QuicDataReader reader(buffer_.data(), current_frame_length_);
      if (!reader.ReadVarInt62(&frame.push_id)) {
        RaiseError(QUIC_HTTP_FRAME_ERROR,
                   "Unable to read CANCEL_PUSH push_id.");
        return false;
      }
      if (!reader.IsDoneReading()) {
        RaiseError(QUIC_HTTP_FRAME_ERROR,
                   "Superfluous data in CANCEL_PUSH frame.");
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
      QuicDataReader reader(buffer_.data(), current_frame_length_);
      GoAwayFrame frame;
      static_assert(!std::is_same<decltype(frame.stream_id), uint64_t>::value,
                    "Please remove local |stream_id| variable and pass "
                    "&frame.stream_id directly to ReadVarInt62() when changing "
                    "QuicStreamId from uint32_t to uint64_t.");
      uint64_t stream_id;
      if (!reader.ReadVarInt62(&stream_id)) {
        RaiseError(QUIC_HTTP_FRAME_ERROR, "Unable to read GOAWAY stream_id.");
        return false;
      }
      if (!reader.IsDoneReading()) {
        RaiseError(QUIC_HTTP_FRAME_ERROR, "Superfluous data in GOAWAY frame.");
        return false;
      }
      frame.stream_id = static_cast<QuicStreamId>(stream_id);
      continue_processing = visitor_->OnGoAwayFrame(frame);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID): {
      QuicDataReader reader(buffer_.data(), current_frame_length_);
      MaxPushIdFrame frame;
      if (!reader.ReadVarInt62(&frame.push_id)) {
        RaiseError(QUIC_HTTP_FRAME_ERROR,
                   "Unable to read MAX_PUSH_ID push_id.");
        return false;
      }
      if (!reader.IsDoneReading()) {
        RaiseError(QUIC_HTTP_FRAME_ERROR,
                   "Superfluous data in MAX_PUSH_ID frame.");
        return false;
      }
      continue_processing = visitor_->OnMaxPushIdFrame(frame);
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::PRIORITY_UPDATE): {
      // TODO(bnc): Avoid buffering if the entire frame is present, and
      // instead parse directly out of |reader|.
      PriorityUpdateFrame frame;
      QuicDataReader reader(buffer_.data(), current_frame_length_);
      if (!ParsePriorityUpdateFrame(&reader, &frame)) {
        return false;
      }
      continue_processing = visitor_->OnPriorityUpdateFrame(frame);
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
  quiche::QuicheStringPiece payload;
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
  QuicByteCount bytes_to_read = std::min<QuicByteCount>(
      remaining_type_field_length_, reader->BytesRemaining());
  bool success =
      reader->ReadBytes(type_buffer_.data() + current_type_field_length_ -
                            remaining_type_field_length_,
                        bytes_to_read);
  DCHECK(success);
  remaining_type_field_length_ -= bytes_to_read;
}

void HttpDecoder::BufferPushId(QuicDataReader* reader) {
  DCHECK_LE(remaining_push_id_length_, current_frame_length_);
  QuicByteCount bytes_to_read = std::min<QuicByteCount>(
      reader->BytesRemaining(), remaining_push_id_length_);
  bool success =
      reader->ReadBytes(push_id_buffer_.data() + current_push_id_length_ -
                            remaining_push_id_length_,
                        bytes_to_read);
  DCHECK(success);
  remaining_push_id_length_ -= bytes_to_read;
  remaining_frame_length_ -= bytes_to_read;
}

void HttpDecoder::RaiseError(QuicErrorCode error, std::string error_detail) {
  state_ = STATE_ERROR;
  error_ = error;
  error_detail_ = std::move(error_detail);
  visitor_->OnError(this);
}

bool HttpDecoder::ParseSettingsFrame(QuicDataReader* reader,
                                     SettingsFrame* frame) {
  while (!reader->IsDoneReading()) {
    uint64_t id;
    if (!reader->ReadVarInt62(&id)) {
      RaiseError(QUIC_HTTP_FRAME_ERROR, "Unable to read setting identifier.");
      return false;
    }
    uint64_t content;
    if (!reader->ReadVarInt62(&content)) {
      RaiseError(QUIC_HTTP_FRAME_ERROR, "Unable to read setting value.");
      return false;
    }
    auto result = frame->values.insert({id, content});
    if (!result.second) {
      RaiseError(QUIC_HTTP_DUPLICATE_SETTING_IDENTIFIER,
                 "Duplicate setting identifier.");
      return false;
    }
  }
  return true;
}

bool HttpDecoder::ParsePriorityUpdateFrame(QuicDataReader* reader,
                                           PriorityUpdateFrame* frame) {
  uint8_t prioritized_element_type;
  if (!reader->ReadUInt8(&prioritized_element_type)) {
    RaiseError(QUIC_HTTP_FRAME_ERROR,
               "Unable to read prioritized element type.");
    return false;
  }

  if (prioritized_element_type != REQUEST_STREAM &&
      prioritized_element_type != PUSH_STREAM) {
    RaiseError(QUIC_HTTP_FRAME_ERROR, "Invalid prioritized element type.");
    return false;
  }

  frame->prioritized_element_type =
      static_cast<PrioritizedElementType>(prioritized_element_type);

  if (!reader->ReadVarInt62(&frame->prioritized_element_id)) {
    RaiseError(QUIC_HTTP_FRAME_ERROR, "Unable to read prioritized element id.");
    return false;
  }

  quiche::QuicheStringPiece priority_field_value =
      reader->ReadRemainingPayload();
  frame->priority_field_value =
      std::string(priority_field_value.data(), priority_field_value.size());

  return true;
}

QuicByteCount HttpDecoder::MaxFrameLength(uint64_t frame_type) {
  switch (frame_type) {
    case static_cast<uint64_t>(HttpFrameType::CANCEL_PUSH):
      return sizeof(PushId);
    case static_cast<uint64_t>(HttpFrameType::SETTINGS):
      // This limit is arbitrary.
      return 1024 * 1024;
    case static_cast<uint64_t>(HttpFrameType::GOAWAY):
      return VARIABLE_LENGTH_INTEGER_LENGTH_8;
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID):
      return sizeof(PushId);
    case static_cast<uint64_t>(HttpFrameType::PRIORITY_UPDATE):
      // This limit is arbitrary.
      return 1024 * 1024;
    default:
      // Other frames require no data buffering, so it's safe to have no limit.
      return std::numeric_limits<QuicByteCount>::max();
  }
}

}  // namespace quic
