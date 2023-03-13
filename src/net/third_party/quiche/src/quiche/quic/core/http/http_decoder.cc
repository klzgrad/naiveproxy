// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/http_decoder.h"

#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "quiche/http2/http2_constants.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

namespace {

// Limit on the payload length for frames that are buffered by HttpDecoder.
// If a frame header indicating a payload length exceeding this limit is
// received, HttpDecoder closes the connection.  Does not apply to frames that
// are not buffered here but each payload fragment is immediately passed to
// Visitor, like HEADERS, DATA, and unknown frames.
constexpr QuicByteCount kPayloadLengthLimit = 1024 * 1024;

}  // anonymous namespace

HttpDecoder::HttpDecoder(Visitor* visitor) : HttpDecoder(visitor, Options()) {}
HttpDecoder::HttpDecoder(Visitor* visitor, Options options)
    : visitor_(visitor),
      allow_web_transport_stream_(options.allow_web_transport_stream),
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
  QUICHE_DCHECK(visitor_);
}

HttpDecoder::~HttpDecoder() {}

// static
bool HttpDecoder::DecodeSettings(const char* data, QuicByteCount len,
                                 SettingsFrame* frame) {
  QuicDataReader reader(data, len);
  uint64_t frame_type;
  if (!reader.ReadVarInt62(&frame_type)) {
    QUIC_DLOG(ERROR) << "Unable to read frame type.";
    return false;
  }

  if (frame_type != static_cast<uint64_t>(HttpFrameType::SETTINGS)) {
    QUIC_DLOG(ERROR) << "Invalid frame type " << frame_type;
    return false;
  }

  absl::string_view frame_contents;
  if (!reader.ReadStringPieceVarInt62(&frame_contents)) {
    QUIC_DLOG(ERROR) << "Failed to read SETTINGS frame contents";
    return false;
  }

  QuicDataReader frame_reader(frame_contents);

  while (!frame_reader.IsDoneReading()) {
    uint64_t id;
    if (!frame_reader.ReadVarInt62(&id)) {
      QUIC_DLOG(ERROR) << "Unable to read setting identifier.";
      return false;
    }
    uint64_t content;
    if (!frame_reader.ReadVarInt62(&content)) {
      QUIC_DLOG(ERROR) << "Unable to read setting value.";
      return false;
    }
    auto result = frame->values.insert({id, content});
    if (!result.second) {
      QUIC_DLOG(ERROR) << "Duplicate setting identifier.";
      return false;
    }
  }
  return true;
}

QuicByteCount HttpDecoder::ProcessInput(const char* data, QuicByteCount len) {
  QUICHE_DCHECK_EQ(QUIC_NO_ERROR, error_);
  QUICHE_DCHECK_NE(STATE_ERROR, state_);

  QuicDataReader reader(data, len);
  bool continue_processing = true;
  // BufferOrParsePayload() and FinishParsing() may need to be called even if
  // there is no more data so that they can finish processing the current frame.
  while (continue_processing && (reader.BytesRemaining() != 0 ||
                                 state_ == STATE_BUFFER_OR_PARSE_PAYLOAD ||
                                 state_ == STATE_FINISH_PARSING)) {
    // |continue_processing| must have been set to false upon error.
    QUICHE_DCHECK_EQ(QUIC_NO_ERROR, error_);
    QUICHE_DCHECK_NE(STATE_ERROR, state_);

    switch (state_) {
      case STATE_READING_FRAME_TYPE:
        continue_processing = ReadFrameType(&reader);
        break;
      case STATE_READING_FRAME_LENGTH:
        continue_processing = ReadFrameLength(&reader);
        break;
      case STATE_BUFFER_OR_PARSE_PAYLOAD:
        continue_processing = BufferOrParsePayload(&reader);
        break;
      case STATE_READING_FRAME_PAYLOAD:
        continue_processing = ReadFramePayload(&reader);
        break;
      case STATE_FINISH_PARSING:
        continue_processing = FinishParsing();
        break;
      case STATE_PARSING_NO_LONGER_POSSIBLE:
        continue_processing = false;
        QUIC_BUG(HttpDecoder PARSING_NO_LONGER_POSSIBLE)
            << "HttpDecoder called after an indefinite-length frame has been "
               "received";
        RaiseError(QUIC_INTERNAL_ERROR,
                   "HttpDecoder called after an indefinite-length frame has "
                   "been received");
        break;
      case STATE_ERROR:
        break;
      default:
        QUIC_BUG(quic_bug_10411_1) << "Invalid state: " << state_;
    }
  }

  return len - reader.BytesRemaining();
}

bool HttpDecoder::ReadFrameType(QuicDataReader* reader) {
  QUICHE_DCHECK_NE(0u, reader->BytesRemaining());
  if (current_type_field_length_ == 0) {
    // A new frame is coming.
    current_type_field_length_ = reader->PeekVarInt62Length();
    QUICHE_DCHECK_NE(0u, current_type_field_length_);
    if (current_type_field_length_ > reader->BytesRemaining()) {
      // Buffer a new type field.
      remaining_type_field_length_ = current_type_field_length_;
      BufferFrameType(reader);
      return true;
    }
    // The reader has all type data needed, so no need to buffer.
    bool success = reader->ReadVarInt62(&current_frame_type_);
    QUICHE_DCHECK(success);
  } else {
    // Buffer the existing type field.
    BufferFrameType(reader);
    // The frame is still not buffered completely.
    if (remaining_type_field_length_ != 0) {
      return true;
    }
    QuicDataReader type_reader(type_buffer_.data(), current_type_field_length_);
    bool success = type_reader.ReadVarInt62(&current_frame_type_);
    QUICHE_DCHECK(success);
  }

  // https://tools.ietf.org/html/draft-ietf-quic-http-31#section-7.2.8
  // specifies that the following frames are treated as errors.
  if (current_frame_type_ ==
          static_cast<uint64_t>(http2::Http2FrameType::PRIORITY) ||
      current_frame_type_ ==
          static_cast<uint64_t>(http2::Http2FrameType::PING) ||
      current_frame_type_ ==
          static_cast<uint64_t>(http2::Http2FrameType::WINDOW_UPDATE) ||
      current_frame_type_ ==
          static_cast<uint64_t>(http2::Http2FrameType::CONTINUATION)) {
    RaiseError(QUIC_HTTP_RECEIVE_SPDY_FRAME,
               absl::StrCat("HTTP/2 frame received in a HTTP/3 connection: ",
                            current_frame_type_));
    return false;
  }

  if (current_frame_type_ ==
      static_cast<uint64_t>(HttpFrameType::CANCEL_PUSH)) {
    RaiseError(QUIC_HTTP_FRAME_ERROR, "CANCEL_PUSH frame received.");
    return false;
  }
  if (current_frame_type_ ==
      static_cast<uint64_t>(HttpFrameType::PUSH_PROMISE)) {
    RaiseError(QUIC_HTTP_FRAME_ERROR, "PUSH_PROMISE frame received.");
    return false;
  }

  state_ = STATE_READING_FRAME_LENGTH;
  return true;
}

bool HttpDecoder::ReadFrameLength(QuicDataReader* reader) {
  QUICHE_DCHECK_NE(0u, reader->BytesRemaining());
  if (current_length_field_length_ == 0) {
    // A new frame is coming.
    current_length_field_length_ = reader->PeekVarInt62Length();
    QUICHE_DCHECK_NE(0u, current_length_field_length_);
    if (current_length_field_length_ > reader->BytesRemaining()) {
      // Buffer a new length field.
      remaining_length_field_length_ = current_length_field_length_;
      BufferFrameLength(reader);
      return true;
    }
    // The reader has all length data needed, so no need to buffer.
    bool success = reader->ReadVarInt62(&current_frame_length_);
    QUICHE_DCHECK(success);
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
    QUICHE_DCHECK(success);
  }

  // WEBTRANSPORT_STREAM frames are indefinitely long, and thus require
  // special handling; the number after the frame type is actually the
  // WebTransport session ID, and not the length.
  if (allow_web_transport_stream_ &&
      current_frame_type_ ==
          static_cast<uint64_t>(HttpFrameType::WEBTRANSPORT_STREAM)) {
    visitor_->OnWebTransportStreamFrameType(
        current_length_field_length_ + current_type_field_length_,
        current_frame_length_);
    state_ = STATE_PARSING_NO_LONGER_POSSIBLE;
    return false;
  }

  if (IsFrameBuffered() &&
      current_frame_length_ > MaxFrameLength(current_frame_type_)) {
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
      QUICHE_NOTREACHED();
      break;
    case static_cast<uint64_t>(HttpFrameType::SETTINGS):
      continue_processing = visitor_->OnSettingsFrameStart(header_length);
      break;
    case static_cast<uint64_t>(HttpFrameType::PUSH_PROMISE):
      QUICHE_NOTREACHED();
      break;
    case static_cast<uint64_t>(HttpFrameType::GOAWAY):
      break;
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID):
      break;
    case static_cast<uint64_t>(HttpFrameType::PRIORITY_UPDATE_REQUEST_STREAM):
      continue_processing = visitor_->OnPriorityUpdateFrameStart(header_length);
      break;
    case static_cast<uint64_t>(HttpFrameType::ACCEPT_CH):
      continue_processing = visitor_->OnAcceptChFrameStart(header_length);
      break;
    default:
      continue_processing = visitor_->OnUnknownFrameStart(
          current_frame_type_, header_length, current_frame_length_);
      break;
  }

  remaining_frame_length_ = current_frame_length_;

  if (IsFrameBuffered()) {
    state_ = STATE_BUFFER_OR_PARSE_PAYLOAD;
    return continue_processing;
  }

  state_ = (remaining_frame_length_ == 0) ? STATE_FINISH_PARSING
                                          : STATE_READING_FRAME_PAYLOAD;
  return continue_processing;
}

bool HttpDecoder::IsFrameBuffered() {
  switch (current_frame_type_) {
    case static_cast<uint64_t>(HttpFrameType::SETTINGS):
      return true;
    case static_cast<uint64_t>(HttpFrameType::GOAWAY):
      return true;
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID):
      return true;
    case static_cast<uint64_t>(HttpFrameType::PRIORITY_UPDATE_REQUEST_STREAM):
      return true;
    case static_cast<uint64_t>(HttpFrameType::ACCEPT_CH):
      return true;
  }

  // Other defined frame types as well as unknown frames are not buffered.
  return false;
}

bool HttpDecoder::ReadFramePayload(QuicDataReader* reader) {
  QUICHE_DCHECK(!IsFrameBuffered());
  QUICHE_DCHECK_NE(0u, reader->BytesRemaining());
  QUICHE_DCHECK_NE(0u, remaining_frame_length_);

  bool continue_processing = true;

  switch (current_frame_type_) {
    case static_cast<uint64_t>(HttpFrameType::DATA): {
      QuicByteCount bytes_to_read = std::min<QuicByteCount>(
          remaining_frame_length_, reader->BytesRemaining());
      absl::string_view payload;
      bool success = reader->ReadStringPiece(&payload, bytes_to_read);
      QUICHE_DCHECK(success);
      QUICHE_DCHECK(!payload.empty());
      continue_processing = visitor_->OnDataFramePayload(payload);
      remaining_frame_length_ -= payload.length();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::HEADERS): {
      QuicByteCount bytes_to_read = std::min<QuicByteCount>(
          remaining_frame_length_, reader->BytesRemaining());
      absl::string_view payload;
      bool success = reader->ReadStringPiece(&payload, bytes_to_read);
      QUICHE_DCHECK(success);
      QUICHE_DCHECK(!payload.empty());
      continue_processing = visitor_->OnHeadersFramePayload(payload);
      remaining_frame_length_ -= payload.length();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::CANCEL_PUSH): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::SETTINGS): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::PUSH_PROMISE): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::GOAWAY): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::PRIORITY_UPDATE_REQUEST_STREAM): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::ACCEPT_CH): {
      QUICHE_NOTREACHED();
      break;
    }
    default: {
      continue_processing = HandleUnknownFramePayload(reader);
      break;
    }
  }

  if (remaining_frame_length_ == 0) {
    state_ = STATE_FINISH_PARSING;
  }

  return continue_processing;
}

bool HttpDecoder::FinishParsing() {
  QUICHE_DCHECK(!IsFrameBuffered());
  QUICHE_DCHECK_EQ(0u, remaining_frame_length_);

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
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::SETTINGS): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::PUSH_PROMISE): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::GOAWAY): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::PRIORITY_UPDATE_REQUEST_STREAM): {
      QUICHE_NOTREACHED();
      break;
    }
    case static_cast<uint64_t>(HttpFrameType::ACCEPT_CH): {
      QUICHE_NOTREACHED();
      break;
    }
    default:
      continue_processing = visitor_->OnUnknownFrameEnd();
  }

  ResetForNextFrame();
  return continue_processing;
}

void HttpDecoder::ResetForNextFrame() {
  current_length_field_length_ = 0;
  current_type_field_length_ = 0;
  state_ = STATE_READING_FRAME_TYPE;
}

bool HttpDecoder::HandleUnknownFramePayload(QuicDataReader* reader) {
  QuicByteCount bytes_to_read = std::min<QuicByteCount>(
      remaining_frame_length_, reader->BytesRemaining());
  absl::string_view payload;
  bool success = reader->ReadStringPiece(&payload, bytes_to_read);
  QUICHE_DCHECK(success);
  QUICHE_DCHECK(!payload.empty());
  remaining_frame_length_ -= payload.length();
  return visitor_->OnUnknownFramePayload(payload);
}

bool HttpDecoder::BufferOrParsePayload(QuicDataReader* reader) {
  QUICHE_DCHECK(IsFrameBuffered());
  QUICHE_DCHECK_EQ(current_frame_length_,
                   buffer_.size() + remaining_frame_length_);

  if (buffer_.empty() && reader->BytesRemaining() >= current_frame_length_) {
    // |*reader| contains entire payload, which might be empty.
    remaining_frame_length_ = 0;
    QuicDataReader current_payload_reader(reader->PeekRemainingPayload().data(),
                                          current_frame_length_);
    bool continue_processing = ParseEntirePayload(&current_payload_reader);

    reader->Seek(current_frame_length_);
    ResetForNextFrame();
    return continue_processing;
  }

  // Buffer as much of the payload as |*reader| contains.
  QuicByteCount bytes_to_read = std::min<QuicByteCount>(
      remaining_frame_length_, reader->BytesRemaining());
  absl::StrAppend(&buffer_, reader->PeekRemainingPayload().substr(
                                /* pos = */ 0, bytes_to_read));
  reader->Seek(bytes_to_read);
  remaining_frame_length_ -= bytes_to_read;

  QUICHE_DCHECK_EQ(current_frame_length_,
                   buffer_.size() + remaining_frame_length_);

  if (remaining_frame_length_ > 0) {
    QUICHE_DCHECK(reader->IsDoneReading());
    return false;
  }

  QuicDataReader buffer_reader(buffer_);
  bool continue_processing = ParseEntirePayload(&buffer_reader);
  buffer_.clear();

  ResetForNextFrame();
  return continue_processing;
}

bool HttpDecoder::ParseEntirePayload(QuicDataReader* reader) {
  QUICHE_DCHECK(IsFrameBuffered());
  QUICHE_DCHECK_EQ(current_frame_length_, reader->BytesRemaining());
  QUICHE_DCHECK_EQ(0u, remaining_frame_length_);

  switch (current_frame_type_) {
    case static_cast<uint64_t>(HttpFrameType::CANCEL_PUSH): {
      QUICHE_NOTREACHED();
      return false;
    }
    case static_cast<uint64_t>(HttpFrameType::SETTINGS): {
      SettingsFrame frame;
      if (!ParseSettingsFrame(reader, &frame)) {
        return false;
      }
      return visitor_->OnSettingsFrame(frame);
    }
    case static_cast<uint64_t>(HttpFrameType::GOAWAY): {
      GoAwayFrame frame;
      if (!reader->ReadVarInt62(&frame.id)) {
        RaiseError(QUIC_HTTP_FRAME_ERROR, "Unable to read GOAWAY ID.");
        return false;
      }
      if (!reader->IsDoneReading()) {
        RaiseError(QUIC_HTTP_FRAME_ERROR, "Superfluous data in GOAWAY frame.");
        return false;
      }
      return visitor_->OnGoAwayFrame(frame);
    }
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID): {
      uint64_t unused;
      if (!reader->ReadVarInt62(&unused)) {
        RaiseError(QUIC_HTTP_FRAME_ERROR,
                   "Unable to read MAX_PUSH_ID push_id.");
        return false;
      }
      if (!reader->IsDoneReading()) {
        RaiseError(QUIC_HTTP_FRAME_ERROR,
                   "Superfluous data in MAX_PUSH_ID frame.");
        return false;
      }
      return visitor_->OnMaxPushIdFrame();
    }
    case static_cast<uint64_t>(HttpFrameType::PRIORITY_UPDATE_REQUEST_STREAM): {
      PriorityUpdateFrame frame;
      if (!ParsePriorityUpdateFrame(reader, &frame)) {
        return false;
      }
      return visitor_->OnPriorityUpdateFrame(frame);
    }
    case static_cast<uint64_t>(HttpFrameType::ACCEPT_CH): {
      AcceptChFrame frame;
      if (!ParseAcceptChFrame(reader, &frame)) {
        return false;
      }
      return visitor_->OnAcceptChFrame(frame);
    }
    default:
      // Only above frame types are parsed by ParseEntirePayload().
      QUICHE_NOTREACHED();
      return false;
  }
}

void HttpDecoder::BufferFrameLength(QuicDataReader* reader) {
  QuicByteCount bytes_to_read = std::min<QuicByteCount>(
      remaining_length_field_length_, reader->BytesRemaining());
  bool success =
      reader->ReadBytes(length_buffer_.data() + current_length_field_length_ -
                            remaining_length_field_length_,
                        bytes_to_read);
  QUICHE_DCHECK(success);
  remaining_length_field_length_ -= bytes_to_read;
}

void HttpDecoder::BufferFrameType(QuicDataReader* reader) {
  QuicByteCount bytes_to_read = std::min<QuicByteCount>(
      remaining_type_field_length_, reader->BytesRemaining());
  bool success =
      reader->ReadBytes(type_buffer_.data() + current_type_field_length_ -
                            remaining_type_field_length_,
                        bytes_to_read);
  QUICHE_DCHECK(success);
  remaining_type_field_length_ -= bytes_to_read;
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
  if (!reader->ReadVarInt62(&frame->prioritized_element_id)) {
    RaiseError(QUIC_HTTP_FRAME_ERROR, "Unable to read prioritized element id.");
    return false;
  }

  absl::string_view priority_field_value = reader->ReadRemainingPayload();
  frame->priority_field_value =
      std::string(priority_field_value.data(), priority_field_value.size());

  return true;
}

bool HttpDecoder::ParseAcceptChFrame(QuicDataReader* reader,
                                     AcceptChFrame* frame) {
  absl::string_view origin;
  absl::string_view value;
  while (!reader->IsDoneReading()) {
    if (!reader->ReadStringPieceVarInt62(&origin)) {
      RaiseError(QUIC_HTTP_FRAME_ERROR, "Unable to read ACCEPT_CH origin.");
      return false;
    }
    if (!reader->ReadStringPieceVarInt62(&value)) {
      RaiseError(QUIC_HTTP_FRAME_ERROR, "Unable to read ACCEPT_CH value.");
      return false;
    }
    // Copy data.
    frame->entries.push_back({std::string(origin.data(), origin.size()),
                              std::string(value.data(), value.size())});
  }
  return true;
}

QuicByteCount HttpDecoder::MaxFrameLength(uint64_t frame_type) {
  QUICHE_DCHECK(IsFrameBuffered());

  switch (frame_type) {
    case static_cast<uint64_t>(HttpFrameType::SETTINGS):
      return kPayloadLengthLimit;
    case static_cast<uint64_t>(HttpFrameType::GOAWAY):
      return quiche::VARIABLE_LENGTH_INTEGER_LENGTH_8;
    case static_cast<uint64_t>(HttpFrameType::MAX_PUSH_ID):
      return quiche::VARIABLE_LENGTH_INTEGER_LENGTH_8;
    case static_cast<uint64_t>(HttpFrameType::PRIORITY_UPDATE_REQUEST_STREAM):
      return kPayloadLengthLimit;
    case static_cast<uint64_t>(HttpFrameType::ACCEPT_CH):
      return kPayloadLengthLimit;
    default:
      QUICHE_NOTREACHED();
      return 0;
  }
}

std::string HttpDecoder::DebugString() const {
  return absl::StrCat(
      "HttpDecoder:", "\n  state: ", state_, "\n  error: ", error_,
      "\n  current_frame_type: ", current_frame_type_,
      "\n  current_length_field_length: ", current_length_field_length_,
      "\n  remaining_length_field_length: ", remaining_length_field_length_,
      "\n  current_frame_length: ", current_frame_length_,
      "\n  remaining_frame_length: ", remaining_frame_length_,
      "\n  current_type_field_length: ", current_type_field_length_,
      "\n  remaining_type_field_length: ", remaining_type_field_length_);
}

}  // namespace quic
