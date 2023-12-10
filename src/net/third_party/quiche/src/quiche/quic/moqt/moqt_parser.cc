// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_parser.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"

namespace moqt {

namespace {

// Minus the type, length, and payload, an OBJECT consists of 4 Varints.
constexpr size_t kMaxObjectHeaderSize = 32;

}  // namespace

// The buffering philosophy is complicated, to minimize copying. Here is an
// overview:
// If the message type is present, this is stored in message_type_. If part of
// the message type varint is partially present, that is buffered (requiring a
// copy).
// Same for message length.
// If the entire message body is present (except for OBJECT payload), it is
// parsed and delivered. If not, the partial body is buffered. (requiring a
// copy).
// Any OBJECT payload is always delivered to the application without copying.
// If something has been buffered, when more data arrives copy just enough of it
// to finish parsing that thing, then resume normal processing.
void MoqtParser::ProcessData(absl::string_view data, bool end_of_stream) {
  if (no_more_data_) {
    if (!data.empty() || !end_of_stream) {
      ParseError("Data after end of stream");
    }
    return;
  }
  if (processing_) {
    return;
  }
  processing_ = true;
  auto on_return = absl::MakeCleanup([&] { processing_ = false; });
  no_more_data_ = end_of_stream;
  quic::QuicDataReader reader(data);
  if (!MaybeMergeDataWithBuffer(reader, end_of_stream)) {
    return;
  }
  if (end_of_stream && reader.IsDoneReading() && object_metadata_.has_value()) {
    // A FIN arrives while delivering OBJECT payload.
    visitor_.OnObjectMessage(object_metadata_.value(), data, true);
    EndOfMessage();
  }
  while (!reader.IsDoneReading()) {
    absl::optional<size_t> processed;
    if (!GetMessageTypeAndLength(reader)) {
      absl::StrAppend(&buffered_message_, reader.PeekRemainingPayload());
      break;
    }
    // Cursor is at start of the message.
    if (end_of_stream && NoMessageLength()) {
      *message_length_ = reader.BytesRemaining();
    }
    if (*message_type_ != MoqtMessageType::kObject &&
        *message_type_ != MoqtMessageType::kGoAway) {
      // Parse OBJECT in case the message is very large. GOAWAY is length zero,
      // so always process.
      if (NoMessageLength()) {
        // Can't parse it yet.
        absl::StrAppend(&buffered_message_, reader.PeekRemainingPayload());
        break;
      }
      if (*message_length_ > kMaxMessageHeaderSize) {
        ParseError("Message too long");
        return;
      }
      if (*message_length_ > reader.BytesRemaining()) {
        // There definitely isn't enough to process the message.
        absl::StrAppend(&buffered_message_, reader.PeekRemainingPayload());
        break;
      }
    }
    processed = ProcessMessage(FetchMessage(reader));
    if (!processed.has_value()) {
      if (*message_type_ == MoqtMessageType::kObject &&
          (NoMessageLength() || reader.BytesRemaining() < *message_length_)) {
        // The parser can attempt to process OBJECT before receiving the whole
        // message length. If it doesn't parse the varints, it will buffer the
        // message.
        absl::StrAppend(&buffered_message_, reader.PeekRemainingPayload());
        break;
      }
      // Non-OBJECT or OBJECT with the complete specified length, but the data
      // was not parseable.
      ParseError("Not able to parse message given specified length");
      return;
    }
    if (*processed == *message_length_) {
      EndOfMessage();
    } else {
      if (*message_type_ != MoqtMessageType::kObject) {
        // Partial processing of non-OBJECT is not allowed.
        ParseError("Specified message length too long");
        return;
      }
      // This is a partially processed OBJECT payload.
      if (!NoMessageLength()) {
        *message_length_ -= *processed;
      }
    }
    if (!reader.Seek(*processed)) {
      QUICHE_DCHECK(false);
      ParseError("Internal Error");
    }
  }
  if (end_of_stream &&
      (!buffered_message_.empty() || object_metadata_.has_value() ||
       message_type_.has_value() || message_length_.has_value())) {
    // If the stream is ending, there should be no message in progress.
    ParseError("Incomplete message at end of stream");
  }
}

bool MoqtParser::MaybeMergeDataWithBuffer(quic::QuicDataReader& reader,
                                          bool end_of_stream) {
  // Copy as much information as necessary from |data| to complete the
  // message or OBJECT header. Minimize unnecessary copying!
  if (buffered_message_.empty()) {
    return true;
  }
  quic::QuicDataReader buffer(buffered_message_);
  if (!message_length_.has_value()) {
    // The buffer contains part of the message type or length.
    if (buffer.BytesRemaining() > buffer.PeekVarInt62Length()) {
      ParseError("Internal Error");
      QUICHE_DCHECK(false);
      return false;
    }
    size_t bytes_needed = buffer.PeekVarInt62Length() - buffer.BytesRemaining();
    if (bytes_needed > reader.BytesRemaining()) {
      // Not enough to complete!
      absl::StrAppend(&buffered_message_, reader.PeekRemainingPayload());
      return false;
    }
    absl::StrAppend(&buffered_message_,
                    reader.PeekRemainingPayload().substr(0, bytes_needed));
    if (!reader.Seek(bytes_needed)) {
      QUICHE_DCHECK(false);
      ParseError("Internal Error");
      return false;
    }
    quic::QuicDataReader new_buffer(buffered_message_);
    uint64_t value;
    if (!new_buffer.ReadVarInt62(&value)) {
      QUICHE_DCHECK(false);
      ParseError("Internal Error");
      return false;
    }
    if (message_type_.has_value()) {
      message_length_ = value;
    } else {
      message_type_ = static_cast<MoqtMessageType>(value);
    }
    // GOAWAY is special. Report the message as soon as the type and length
    // are complete.
    if (message_type_.has_value() && message_length_.has_value() &&
        *message_type_ == MoqtMessageType::kGoAway) {
      ProcessGoAway(new_buffer.PeekRemainingPayload());
      EndOfMessage();
      return false;
    }
    // Proceed to normal parsing.
    buffered_message_.clear();
    return true;
  }
  // It's a partially buffered message
  if (NoMessageLength()) {
    if (end_of_stream) {
      message_length_ = buffer.BytesRemaining() + reader.BytesRemaining();
    } else if (*message_type_ != MoqtMessageType::kObject) {
      absl::StrAppend(&buffered_message_, reader.PeekRemainingPayload());
      return false;
    }
  }
  if (*message_type_ == MoqtMessageType::kObject) {
    // OBJECT is a special case. Append up to KMaxObjectHeaderSize bytes to the
    // buffer and see if that allows parsing.
    QUICHE_DCHECK(!object_metadata_.has_value());
    size_t original_buffer_size = buffer.BytesRemaining();
    size_t bytes_to_pull = reader.BytesRemaining();
    // No check for *message_length_ == 0 below! Mutants will complain if there
    // is a check. If message_length_ < original_buffer_size, the second
    // argument will be a very large unsigned integer, which will be irrelevant
    // due to std::min.
    bytes_to_pull = std::min(reader.BytesRemaining(),
                             *message_length_ - original_buffer_size);
    // Mutants complains that the line below doesn't fail any tests. This is a
    // performance optimization to avoid copying large amounts of object payload
    // into the buffer when only the OBJECT header will be processed. There is
    // no observable behavior change if this line is removed.
    bytes_to_pull = std::min(bytes_to_pull, kMaxObjectHeaderSize);
    absl::StrAppend(&buffered_message_,
                    reader.PeekRemainingPayload().substr(0, bytes_to_pull));
    absl::optional<size_t> processed =
        ProcessObjectVarints(absl::string_view(buffered_message_));
    if (!processed.has_value()) {
      if ((!NoMessageLength() &&
           buffered_message_.length() == *message_length_) ||
          buffered_message_.length() > kMaxObjectHeaderSize) {
        ParseError("Not able to parse buffered message given specified length");
      }
      return false;
    }
    if (*processed > 0 && !reader.Seek(*processed - original_buffer_size)) {
      ParseError("Internal Error");
      return false;
    }
    if (*processed == *message_length_) {
      // This covers an edge case where the peer has sent an OBJECT message with
      // no content.
      visitor_.OnObjectMessage(object_metadata_.value(), absl::string_view(),
                               true);
      EndOfMessage();
      return true;
    }
    if (!NoMessageLength()) {
      *message_length_ -= *processed;
    }
    // Object payload is never processed in the buffer.
    buffered_message_.clear();
    return true;
  }
  size_t bytes_to_pull =
      (buffer.BytesRemaining() + reader.BytesRemaining() < *message_length_)
          ? reader.BytesRemaining()
          : *message_length_ - buffer.BytesRemaining();
  absl::StrAppend(&buffered_message_,
                  reader.PeekRemainingPayload().substr(0, bytes_to_pull));
  if (!reader.Seek(bytes_to_pull)) {
    QUICHE_DCHECK(false);
    ParseError("Internal Error");
    return false;
  }
  if (buffered_message_.length() < *message_length_) {
    // Not enough bytes present.
    return false;
  }
  absl::optional<size_t> processed =
      ProcessMessage(absl::string_view(buffered_message_));
  if (!processed.has_value()) {
    ParseError("Not able to parse buffered message given specified length");
    return false;
  }
  if (*processed != *message_length_) {
    ParseError("Buffered message length too long for message contents");
    return false;
  }
  EndOfMessage();
  return true;
}

absl::optional<size_t> MoqtParser::ProcessMessage(absl::string_view data) {
  switch (*message_type_) {
    case MoqtMessageType::kObject:
      return ProcessObject(data);
    case MoqtMessageType::kSetup:
      return ProcessSetup(data);
    case MoqtMessageType::kSubscribeRequest:
      return ProcessSubscribeRequest(data);
    case MoqtMessageType::kSubscribeOk:
      return ProcessSubscribeOk(data);
    case MoqtMessageType::kSubscribeError:
      return ProcessSubscribeError(data);
    case MoqtMessageType::kUnsubscribe:
      return ProcessUnsubscribe(data);
    case MoqtMessageType::kAnnounce:
      return ProcessAnnounce(data);
    case MoqtMessageType::kAnnounceOk:
      return ProcessAnnounceOk(data);
    case MoqtMessageType::kAnnounceError:
      return ProcessAnnounceError(data);
    case MoqtMessageType::kUnannounce:
      return ProcessUnannounce(data);
    case MoqtMessageType::kGoAway:
      return ProcessGoAway(data);
    default:
      ParseError("Unknown message type");
      return absl::nullopt;
  }
}

absl::optional<size_t> MoqtParser::ProcessObjectVarints(
    absl::string_view data) {
  if (object_metadata_.has_value()) {
    return 0;
  }
  object_metadata_ = MoqtObject();
  quic::QuicDataReader reader(data);
  if (reader.ReadVarInt62(&object_metadata_->track_id) &&
      reader.ReadVarInt62(&object_metadata_->group_sequence) &&
      reader.ReadVarInt62(&object_metadata_->object_sequence) &&
      reader.ReadVarInt62(&object_metadata_->object_send_order)) {
    return reader.PreviouslyReadPayload().length();
  }
  object_metadata_ = absl::nullopt;
  QUICHE_DCHECK(reader.PreviouslyReadPayload().length() < kMaxObjectHeaderSize);
  return absl::nullopt;
}

absl::optional<size_t> MoqtParser::ProcessObject(absl::string_view data) {
  quic::QuicDataReader reader(data);
  size_t payload_length = *message_length_;
  absl::optional<size_t> processed = ProcessObjectVarints(data);
  if (!processed.has_value() && !object_metadata_.has_value()) {
    // Could not obtain the whole object header.
    return absl::nullopt;
  }
  if (!reader.Seek(*processed)) {
    ParseError("Internal Error");
    return absl::nullopt;
  }
  if (payload_length != 0) {
    payload_length -= *processed;
  }
  QUICHE_DCHECK(NoMessageLength() || reader.BytesRemaining() <= payload_length);
  visitor_.OnObjectMessage(
      object_metadata_.value(), reader.PeekRemainingPayload(),
      reader.BytesRemaining() == payload_length && !NoMessageLength());
  return data.length();
}

absl::optional<size_t> MoqtParser::ProcessSetup(absl::string_view data) {
  MoqtSetup setup;
  quic::QuicDataReader reader(data);
  uint64_t number_of_supported_versions;
  if (perspective_ == quic::Perspective::IS_SERVER) {
    if (!reader.ReadVarInt62(&number_of_supported_versions)) {
      return absl::nullopt;
    }
  } else {
    number_of_supported_versions = 1;
  }
  uint64_t value;
  for (uint64_t i = 0; i < number_of_supported_versions; ++i) {
    if (!reader.ReadVarInt62(&value)) {
      return absl::nullopt;
    }
    setup.supported_versions.push_back(static_cast<MoqtVersion>(value));
  }
  // Parse parameters
  while (!reader.IsDoneReading()) {
    if (!reader.ReadVarInt62(&value)) {
      return absl::nullopt;
    }
    auto parameter_key = static_cast<MoqtSetupParameter>(value);
    absl::string_view field;
    switch (parameter_key) {
      case MoqtSetupParameter::kRole:
        if (setup.role.has_value()) {
          ParseError("ROLE parameter appears twice in SETUP");
          return absl::nullopt;
        }
        if (!ReadIntegerPieceVarInt62(reader, value)) {
          return absl::nullopt;
        }
        setup.role = static_cast<MoqtRole>(value);
        break;
      case MoqtSetupParameter::kPath:
        if (uses_web_transport_) {
          ParseError(
              "WebTransport connection is using PATH parameter in SETUP");
          return absl::nullopt;
        }
        if (perspective_ == quic::Perspective::IS_CLIENT) {
          ParseError("PATH parameter sent by server in SETUP");
          return absl::nullopt;
        }
        if (setup.path.has_value()) {
          ParseError("PATH parameter appears twice in SETUP");
          return absl::nullopt;
        }
        if (!reader.ReadStringPieceVarInt62(&field)) {
          return absl::nullopt;
        }
        setup.path = field;
        break;
      default:
        // Skip over the parameter.
        if (!reader.ReadStringPieceVarInt62(&field)) {
          return absl::nullopt;
        }
        break;
    }
  }
  if (perspective_ == quic::Perspective::IS_SERVER) {
    if (!setup.role.has_value()) {
      ParseError("ROLE SETUP parameter missing from Client message");
      return absl::nullopt;
    }
    if (!uses_web_transport_ && !setup.path.has_value()) {
      ParseError("PATH SETUP parameter missing from Client message over QUIC");
      return absl::nullopt;
    }
  }
  visitor_.OnSetupMessage(setup);
  return reader.PreviouslyReadPayload().length();
}

absl::optional<size_t> MoqtParser::ProcessSubscribeRequest(
    absl::string_view data) {
  MoqtSubscribeRequest subscribe_request;
  quic::QuicDataReader reader(data);
  absl::string_view field;
  if (!reader.ReadStringPieceVarInt62(&subscribe_request.full_track_name)) {
    return absl::nullopt;
  }
  uint64_t value;
  while (!reader.IsDoneReading()) {
    if (!reader.ReadVarInt62(&value)) {
      return absl::nullopt;
    }
    auto parameter_key = static_cast<MoqtTrackRequestParameter>(value);
    switch (parameter_key) {
      case MoqtTrackRequestParameter::kGroupSequence:
        if (subscribe_request.group_sequence.has_value()) {
          ParseError(
              "GROUP_SEQUENCE parameter appears twice in SUBSCRIBE_REQUEST");
          return absl::nullopt;
        }
        if (!ReadIntegerPieceVarInt62(reader, value)) {
          return absl::nullopt;
        }
        subscribe_request.group_sequence = value;
        break;
      case MoqtTrackRequestParameter::kObjectSequence:
        if (subscribe_request.object_sequence.has_value()) {
          ParseError(
              "OBJECT_SEQUENCE parameter appears twice in SUBSCRIBE_REQUEST");
          return absl::nullopt;
        }
        if (!ReadIntegerPieceVarInt62(reader, value)) {
          return absl::nullopt;
        }
        subscribe_request.object_sequence = value;
        break;
      case MoqtTrackRequestParameter::kAuthorizationInfo:
        if (subscribe_request.authorization_info.has_value()) {
          ParseError(
              "AUTHORIZATION_INFO parameter appears twice in "
              "SUBSCRIBE_REQUEST");
          return absl::nullopt;
        }
        if (!reader.ReadStringPieceVarInt62(&field)) {
          return absl::nullopt;
        }
        subscribe_request.authorization_info = field;
        break;
      default:
        // Skip over the parameter.
        if (!reader.ReadStringPieceVarInt62(&field)) {
          return absl::nullopt;
        }
        break;
    }
  }
  if (reader.IsDoneReading()) {
    visitor_.OnSubscribeRequestMessage(subscribe_request);
  }
  return reader.PreviouslyReadPayload().length();
}

absl::optional<size_t> MoqtParser::ProcessSubscribeOk(absl::string_view data) {
  MoqtSubscribeOk subscribe_ok;
  quic::QuicDataReader reader(data);
  if (!reader.ReadStringPieceVarInt62(&subscribe_ok.full_track_name)) {
    return absl::nullopt;
  }
  if (!reader.ReadVarInt62(&subscribe_ok.track_id)) {
    return absl::nullopt;
  }
  uint64_t milliseconds;
  if (!reader.ReadVarInt62(&milliseconds)) {
    return absl::nullopt;
  }
  subscribe_ok.expires = quic::QuicTimeDelta::FromMilliseconds(milliseconds);
  if (reader.IsDoneReading()) {
    visitor_.OnSubscribeOkMessage(subscribe_ok);
  }
  return reader.PreviouslyReadPayload().length();
}

absl::optional<size_t> MoqtParser::ProcessSubscribeError(
    absl::string_view data) {
  MoqtSubscribeError subscribe_error;
  quic::QuicDataReader reader(data);
  if (!reader.ReadStringPieceVarInt62(&subscribe_error.full_track_name)) {
    return absl::nullopt;
  }
  if (!reader.ReadVarInt62(&subscribe_error.error_code)) {
    return absl::nullopt;
  }
  if (!reader.ReadStringPieceVarInt62(&subscribe_error.reason_phrase)) {
    return absl::nullopt;
  }
  if (reader.IsDoneReading()) {
    visitor_.OnSubscribeErrorMessage(subscribe_error);
  }
  return reader.PreviouslyReadPayload().length();
}

absl::optional<size_t> MoqtParser::ProcessUnsubscribe(absl::string_view data) {
  MoqtUnsubscribe unsubscribe;
  quic::QuicDataReader reader(data);
  if (!reader.ReadStringPieceVarInt62(&unsubscribe.full_track_name)) {
    return absl::nullopt;
  }
  if (reader.IsDoneReading()) {
    visitor_.OnUnsubscribeMessage(unsubscribe);
  }
  return reader.PreviouslyReadPayload().length();
}

absl::optional<size_t> MoqtParser::ProcessAnnounce(absl::string_view data) {
  MoqtAnnounce announce;
  quic::QuicDataReader reader(data);
  absl::string_view field;
  if (!reader.ReadStringPieceVarInt62(&field)) {
    return absl::nullopt;
  }
  announce.track_namespace = field;
  bool saw_group_sequence = false, saw_object_sequence = false;
  while (!reader.IsDoneReading()) {
    uint64_t value;
    if (!reader.ReadVarInt62(&value)) {
      return absl::nullopt;
    }
    auto parameter_key = static_cast<MoqtTrackRequestParameter>(value);
    switch (parameter_key) {
      case MoqtTrackRequestParameter::kGroupSequence:
        // Not used, but check for duplicates.
        if (saw_group_sequence) {
          ParseError("GROUP_SEQUENCE parameter appears twice in ANNOUNCE");
          return absl::nullopt;
        }
        if (!reader.ReadStringPieceVarInt62(&field)) {
          return absl::nullopt;
        }
        saw_group_sequence = true;
        break;
      case MoqtTrackRequestParameter::kObjectSequence:
        // Not used, but check for duplicates.
        if (saw_object_sequence) {
          ParseError("OBJECT_SEQUENCE parameter appears twice in ANNOUNCE");
          return absl::nullopt;
        }
        if (!reader.ReadStringPieceVarInt62(&field)) {
          return absl::nullopt;
        }
        saw_object_sequence = true;
        break;
      case MoqtTrackRequestParameter::kAuthorizationInfo:
        if (announce.authorization_info.has_value()) {
          ParseError("AUTHORIZATION_INFO parameter appears twice in ANNOUNCE");
          return absl::nullopt;
        }
        if (!reader.ReadStringPieceVarInt62(&field)) {
          return absl::nullopt;
        }
        announce.authorization_info = field;
        break;
      default:
        // Skip over the parameter.
        if (!reader.ReadStringPieceVarInt62(&field)) {
          return absl::nullopt;
        }
        break;
    }
  }
  if (reader.IsDoneReading()) {
    visitor_.OnAnnounceMessage(announce);
  }
  return reader.PreviouslyReadPayload().length();
}

absl::optional<size_t> MoqtParser::ProcessAnnounceOk(absl::string_view data) {
  MoqtAnnounceOk announce_ok;
  quic::QuicDataReader reader(data);
  if (!reader.ReadStringPieceVarInt62(&announce_ok.track_namespace)) {
    return absl::nullopt;
  }
  if (reader.IsDoneReading()) {
    visitor_.OnAnnounceOkMessage(announce_ok);
  }
  return reader.PreviouslyReadPayload().length();
}

absl::optional<size_t> MoqtParser::ProcessAnnounceError(
    absl::string_view data) {
  MoqtAnnounceError announce_error;
  quic::QuicDataReader reader(data);
  if (!reader.ReadStringPieceVarInt62(&announce_error.track_namespace)) {
    return absl::nullopt;
  }
  if (!reader.ReadVarInt62(&announce_error.error_code)) {
    return absl::nullopt;
  }
  if (!reader.ReadStringPieceVarInt62(&announce_error.reason_phrase)) {
    return absl::nullopt;
  }
  if (reader.IsDoneReading()) {
    visitor_.OnAnnounceErrorMessage(announce_error);
  }
  return reader.PreviouslyReadPayload().length();
}

absl::optional<size_t> MoqtParser::ProcessUnannounce(absl::string_view data) {
  MoqtUnannounce unannounce;
  quic::QuicDataReader reader(data);
  if (!reader.ReadStringPieceVarInt62(&unannounce.track_namespace)) {
    return absl::nullopt;
  }
  if (reader.IsDoneReading()) {
    visitor_.OnUnannounceMessage(unannounce);
  }
  return reader.PreviouslyReadPayload().length();
}

absl::optional<size_t> MoqtParser::ProcessGoAway(absl::string_view data) {
  if (!data.empty()) {
    // GOAWAY can only be followed by end_of_stream. Anything else is an error.
    ParseError("GOAWAY has data following");
    return absl::nullopt;
  }
  visitor_.OnGoAwayMessage();
  return 0;
}

bool MoqtParser::GetMessageTypeAndLength(quic::QuicDataReader& reader) {
  if (!message_type_.has_value()) {
    uint64_t value;
    if (!reader.ReadVarInt62(&value)) {
      return false;
    }
    message_type_ = static_cast<MoqtMessageType>(value);
  }
  if (!message_length_.has_value()) {
    uint64_t value;
    if (!reader.ReadVarInt62(&value)) {
      return false;
    }
    message_length_ = value;
  }
  return true;
}

void MoqtParser::EndOfMessage() {
  buffered_message_.clear();
  message_type_ = absl::nullopt;
  message_length_ = absl::nullopt;
  object_metadata_ = absl::nullopt;
}

absl::string_view MoqtParser::FetchMessage(quic::QuicDataReader& reader) {
  if (message_length_ == 0) {
    return reader.PeekRemainingPayload();
  }
  if (message_length_ > reader.BytesRemaining()) {
    QUICHE_DCHECK(message_type_ == MoqtMessageType::kObject);
    return reader.PeekRemainingPayload();
  }
  return reader.PeekRemainingPayload().substr(0, *message_length_);
}

void MoqtParser::ParseError(absl::string_view reason) {
  if (parsing_error_) {
    return;  // Don't send multiple parse errors.
  }
  no_more_data_ = true;
  parsing_error_ = true;
  visitor_.OnParsingError(reason);
}

bool MoqtParser::ReadIntegerPieceVarInt62(quic::QuicDataReader& reader,
                                          uint64_t& result) {
  absl::string_view field;
  if (!reader.ReadStringPieceVarInt62(&field)) {
    return false;
  }
  if (field.size() > sizeof(uint64_t)) {
    ParseError("Cannot parse explicit length integers longer than 8 bytes");
    return false;
  }
  result = 0;
  memcpy((uint8_t*)&result + sizeof(result) - field.size(), field.data(),
         field.size());
  result = quiche::QuicheEndian::NetToHost64(result);
  return true;
}

}  // namespace moqt
