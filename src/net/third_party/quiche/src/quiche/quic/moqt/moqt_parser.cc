// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_parser.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace moqt {

// The buffering philosophy is complicated, to minimize copying. Here is an
// overview:
// If the entire message body is present (except for OBJECT payload), it is
// parsed and delivered. If not, the partial body is buffered. (requiring a
// copy).
// Any OBJECT payload is always delivered to the application without copying.
// If something has been buffered, when more data arrives copy just enough of it
// to finish parsing that thing, then resume normal processing.
void MoqtParser::ProcessData(absl::string_view data, bool fin) {
  if (no_more_data_) {
    ParseError("Data after end of stream");
  }
  if (processing_) {
    return;
  }
  processing_ = true;
  auto on_return = absl::MakeCleanup([&] { processing_ = false; });
  // Check for early fin
  if (fin) {
    no_more_data_ = true;
    if (ObjectPayloadInProgress() &&
        payload_length_remaining_ > data.length()) {
      ParseError("End of stream before complete OBJECT PAYLOAD");
      return;
    }
    if (!buffered_message_.empty() && data.empty()) {
      ParseError("End of stream before complete message");
      return;
    }
  }
  std::optional<quic::QuicDataReader> reader = std::nullopt;
  size_t original_buffer_size = buffered_message_.size();
  // There are three cases: the parser has already delivered an OBJECT header
  // and is now delivering payload; part of a message is in the buffer; or
  // no message is in progress.
  if (ObjectPayloadInProgress()) {
    // This is additional payload for an OBJECT.
    QUICHE_DCHECK(buffered_message_.empty());
    if (!object_metadata_->payload_length.has_value()) {
      // Deliver the data and exit.
      visitor_.OnObjectMessage(*object_metadata_, data, fin);
      if (fin) {
        object_metadata_.reset();
      }
      return;
    }
    if (data.length() < payload_length_remaining_) {
      // Does not finish the payload; deliver and exit.
      visitor_.OnObjectMessage(*object_metadata_, data, false);
      payload_length_remaining_ -= data.length();
      return;
    }
    // Finishes the payload. Deliver and continue.
    reader.emplace(data);
    visitor_.OnObjectMessage(*object_metadata_,
                             data.substr(0, payload_length_remaining_), true);
    reader->Seek(payload_length_remaining_);
    payload_length_remaining_ = 0;  // Expect a new object.
  } else if (!buffered_message_.empty()) {
    absl::StrAppend(&buffered_message_, data);
    reader.emplace(buffered_message_);
  } else {
    // No message in progress.
    reader.emplace(data);
  }
  size_t total_processed = 0;
  while (!reader->IsDoneReading()) {
    size_t message_len = ProcessMessage(reader->PeekRemainingPayload(), fin);
    if (message_len == 0) {
      if (reader->BytesRemaining() > kMaxMessageHeaderSize) {
        ParseError(MoqtError::kGenericError,
                   "Cannot parse non-OBJECT messages > 2KB");
        return;
      }
      if (fin) {
        ParseError("FIN after incomplete message");
        return;
      }
      if (buffered_message_.empty()) {
        // If the buffer is not empty, |data| has already been copied there.
        absl::StrAppend(&buffered_message_, reader->PeekRemainingPayload());
      }
      break;
    }
    // A message was successfully processed.
    total_processed += message_len;
    reader->Seek(message_len);
  }
  if (original_buffer_size > 0) {
    buffered_message_.erase(0, total_processed);
  }
}

size_t MoqtParser::ProcessMessage(absl::string_view data, bool fin) {
  uint64_t value;
  quic::QuicDataReader reader(data);
  if (ObjectStreamInitialized() && !ObjectPayloadInProgress()) {
    // This is a follow-on object in a stream.
    return ProcessObject(reader,
                         GetMessageTypeForForwardingPreference(
                             object_metadata_->forwarding_preference),
                         fin);
  }
  if (!reader.ReadVarInt62(&value)) {
    return 0;
  }
  auto type = static_cast<MoqtMessageType>(value);
  switch (type) {
    case MoqtMessageType::kObjectStream:
    case MoqtMessageType::kObjectPreferDatagram:
    case MoqtMessageType::kStreamHeaderTrack:
    case MoqtMessageType::kStreamHeaderGroup:
      return ProcessObject(reader, type, fin);
    case MoqtMessageType::kClientSetup:
      return ProcessClientSetup(reader);
    case MoqtMessageType::kServerSetup:
      return ProcessServerSetup(reader);
    case MoqtMessageType::kSubscribe:
      return ProcessSubscribe(reader);
    case MoqtMessageType::kSubscribeOk:
      return ProcessSubscribeOk(reader);
    case MoqtMessageType::kSubscribeError:
      return ProcessSubscribeError(reader);
    case MoqtMessageType::kUnsubscribe:
      return ProcessUnsubscribe(reader);
    case MoqtMessageType::kSubscribeFin:
      return ProcessSubscribeFin(reader);
    case MoqtMessageType::kSubscribeRst:
      return ProcessSubscribeRst(reader);
    case MoqtMessageType::kAnnounce:
      return ProcessAnnounce(reader);
    case MoqtMessageType::kAnnounceOk:
      return ProcessAnnounceOk(reader);
    case MoqtMessageType::kAnnounceError:
      return ProcessAnnounceError(reader);
    case MoqtMessageType::kUnannounce:
      return ProcessUnannounce(reader);
    case MoqtMessageType::kGoAway:
      return ProcessGoAway(reader);
    default:
      ParseError("Unknown message type");
      return 0;
  }
}

size_t MoqtParser::ProcessObject(quic::QuicDataReader& reader,
                                 MoqtMessageType type, bool fin) {
  size_t processed_data;
  QUICHE_DCHECK(!ObjectPayloadInProgress());
  if (!ObjectStreamInitialized()) {
    // nothing has been processed on the stream.
    object_metadata_ = MoqtObject();
    if (!reader.ReadVarInt62(&object_metadata_->subscribe_id) ||
        !reader.ReadVarInt62(&object_metadata_->track_alias)) {
      object_metadata_.reset();
      return 0;
    }
    if (type != MoqtMessageType::kStreamHeaderTrack &&
        !reader.ReadVarInt62(&object_metadata_->group_id)) {
      object_metadata_.reset();
      return 0;
    }
    if (type != MoqtMessageType::kStreamHeaderTrack &&
        type != MoqtMessageType::kStreamHeaderGroup &&
        !reader.ReadVarInt62(&object_metadata_->object_id)) {
      object_metadata_.reset();
      return 0;
    }
    if (!reader.ReadVarInt62(&object_metadata_->object_send_order)) {
      object_metadata_.reset();
      return 0;
    }
    object_metadata_->forwarding_preference = GetForwardingPreference(type);
  }
  // At this point, enough data has been processed to store in object_metadata_,
  // even if there's nothing else in the buffer.
  processed_data = reader.PreviouslyReadPayload().length();
  QUICHE_DCHECK(payload_length_remaining_ == 0);
  switch (type) {
    case MoqtMessageType::kStreamHeaderTrack:
      if (!reader.ReadVarInt62(&object_metadata_->group_id)) {
        return processed_data;
      }
      [[fallthrough]];
    case MoqtMessageType::kStreamHeaderGroup: {
      uint64_t length;
      if (!reader.ReadVarInt62(&object_metadata_->object_id) ||
          !reader.ReadVarInt62(&length)) {
        return processed_data;
      }
      object_metadata_->payload_length = length;
      break;
    }
    default:
      break;
  }
  bool has_length = object_metadata_->payload_length.has_value();
  bool received_complete_message = false;
  size_t payload_to_draw = reader.BytesRemaining();
  if (fin && has_length &&
      *object_metadata_->payload_length > reader.BytesRemaining()) {
    ParseError("Received FIN mid-payload");
    return processed_data;
  }
  received_complete_message =
      fin || (has_length &&
              *object_metadata_->payload_length <= reader.BytesRemaining());
  if (received_complete_message && has_length &&
      *object_metadata_->payload_length < reader.BytesRemaining()) {
    payload_to_draw = *object_metadata_->payload_length;
  }
  // The error case where there's a fin before the explicit length is complete
  // is handled in ProcessData() in two separate places. Even though the
  // message is "done" if fin regardless of has_length, it's bad to report to
  // the application that the object is done if it hasn't reached the promised
  // length.
  visitor_.OnObjectMessage(
      *object_metadata_,
      reader.PeekRemainingPayload().substr(0, payload_to_draw),
      received_complete_message);
  reader.Seek(payload_to_draw);
  payload_length_remaining_ =
      has_length ? *object_metadata_->payload_length - payload_to_draw : 0;
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessClientSetup(quic::QuicDataReader& reader) {
  MoqtClientSetup setup;
  uint64_t number_of_supported_versions;
  if (!reader.ReadVarInt62(&number_of_supported_versions)) {
    return 0;
  }
  uint64_t version;
  for (uint64_t i = 0; i < number_of_supported_versions; ++i) {
    if (!reader.ReadVarInt62(&version)) {
      return 0;
    }
    setup.supported_versions.push_back(static_cast<MoqtVersion>(version));
  }
  uint64_t num_params;
  if (!reader.ReadVarInt62(&num_params)) {
    return 0;
  }
  // Parse parameters
  for (uint64_t i = 0; i < num_params; ++i) {
    uint64_t type;
    absl::string_view value;
    if (!ReadParameter(reader, type, value)) {
      return 0;
    }
    auto key = static_cast<MoqtSetupParameter>(type);
    switch (key) {
      case MoqtSetupParameter::kRole:
        if (setup.role.has_value()) {
          ParseError("ROLE parameter appears twice in SETUP");
          return 0;
        }
        uint64_t index;
        StringViewToVarInt(value, index);
        setup.role = static_cast<MoqtRole>(index);
        break;
      case MoqtSetupParameter::kPath:
        if (uses_web_transport_) {
          ParseError(
              "WebTransport connection is using PATH parameter in SETUP");
          return 0;
        }
        if (setup.path.has_value()) {
          ParseError("PATH parameter appears twice in CLIENT_SETUP");
          return 0;
        }
        setup.path = value;
        break;
      default:
        // Skip over the parameter.
        break;
    }
  }
  if (!setup.role.has_value()) {
    ParseError("ROLE parameter missing from CLIENT_SETUP message");
    return 0;
  }
  if (!uses_web_transport_ && !setup.path.has_value()) {
    ParseError("PATH SETUP parameter missing from Client message over QUIC");
    return 0;
  }
  visitor_.OnClientSetupMessage(setup);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessServerSetup(quic::QuicDataReader& reader) {
  MoqtServerSetup setup;
  uint64_t version;
  if (!reader.ReadVarInt62(&version)) {
    return 0;
  }
  setup.selected_version = static_cast<MoqtVersion>(version);
  uint64_t num_params;
  if (!reader.ReadVarInt62(&num_params)) {
    return 0;
  }
  // Parse parameters
  for (uint64_t i = 0; i < num_params; ++i) {
    uint64_t type;
    absl::string_view value;
    if (!ReadParameter(reader, type, value)) {
      return 0;
    }
    auto key = static_cast<MoqtSetupParameter>(type);
    switch (key) {
      case MoqtSetupParameter::kRole:
        if (setup.role.has_value()) {
          ParseError("ROLE parameter appears twice in SETUP");
          return 0;
        }
        uint64_t index;
        StringViewToVarInt(value, index);
        setup.role = static_cast<MoqtRole>(index);
        break;
      case MoqtSetupParameter::kPath:
        ParseError("PATH parameter in SERVER_SETUP");
        return 0;
      default:
        // Skip over the parameter.
        break;
    }
  }
  visitor_.OnServerSetupMessage(setup);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessSubscribe(quic::QuicDataReader& reader) {
  MoqtSubscribe subscribe_request;
  if (!reader.ReadVarInt62(&subscribe_request.subscribe_id) ||
      !reader.ReadVarInt62(&subscribe_request.track_alias) ||
      !reader.ReadStringPieceVarInt62(&subscribe_request.track_namespace) ||
      !reader.ReadStringPieceVarInt62(&subscribe_request.track_name) ||
      !ReadLocation(reader, subscribe_request.start_group)) {
    return 0;
  }
  if (!subscribe_request.start_group.has_value()) {
    ParseError("START_GROUP must not be None in SUBSCRIBE");
    return 0;
  }
  if (!ReadLocation(reader, subscribe_request.start_object)) {
    return 0;
  }
  if (!subscribe_request.start_object.has_value()) {
    ParseError("START_OBJECT must not be None in SUBSCRIBE");
    return 0;
  }
  if (!ReadLocation(reader, subscribe_request.end_group) ||
      !ReadLocation(reader, subscribe_request.end_object)) {
    return 0;
  }
  if (subscribe_request.end_group.has_value() !=
      subscribe_request.end_object.has_value()) {
    ParseError(
        "SUBSCRIBE end_group and end_object must be both None "
        "or both non_None");
    return 0;
  }
  uint64_t num_params;
  if (!reader.ReadVarInt62(&num_params)) {
    return 0;
  }
  for (uint64_t i = 0; i < num_params; ++i) {
    uint64_t type;
    absl::string_view value;
    if (!ReadParameter(reader, type, value)) {
      return 0;
    }
    auto key = static_cast<MoqtTrackRequestParameter>(type);
    switch (key) {
      case MoqtTrackRequestParameter::kAuthorizationInfo:
        if (subscribe_request.authorization_info.has_value()) {
          ParseError(
              "AUTHORIZATION_INFO parameter appears twice in "
              "SUBSCRIBE_REQUEST");
          return 0;
        }
        subscribe_request.authorization_info = value;
        break;
      default:
        // Skip over the parameter.
        break;
    }
  }
  visitor_.OnSubscribeMessage(subscribe_request);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessSubscribeOk(quic::QuicDataReader& reader) {
  MoqtSubscribeOk subscribe_ok;
  uint64_t milliseconds;
  if (!reader.ReadVarInt62(&subscribe_ok.subscribe_id) ||
      !reader.ReadVarInt62(&milliseconds)) {
    return 0;
  }
  subscribe_ok.expires = quic::QuicTimeDelta::FromMilliseconds(milliseconds);
  visitor_.OnSubscribeOkMessage(subscribe_ok);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessSubscribeError(quic::QuicDataReader& reader) {
  MoqtSubscribeError subscribe_error;
  uint64_t error_code;
  if (!reader.ReadVarInt62(&subscribe_error.subscribe_id) ||
      !reader.ReadVarInt62(&error_code) ||
      !reader.ReadStringPieceVarInt62(&subscribe_error.reason_phrase) ||
      !reader.ReadVarInt62(&subscribe_error.track_alias)) {
    return 0;
  }
  subscribe_error.error_code = static_cast<SubscribeErrorCode>(error_code);
  visitor_.OnSubscribeErrorMessage(subscribe_error);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessUnsubscribe(quic::QuicDataReader& reader) {
  MoqtUnsubscribe unsubscribe;
  if (!reader.ReadVarInt62(&unsubscribe.subscribe_id)) {
    return 0;
  }
  visitor_.OnUnsubscribeMessage(unsubscribe);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessSubscribeFin(quic::QuicDataReader& reader) {
  MoqtSubscribeFin subscribe_fin;
  if (!reader.ReadVarInt62(&subscribe_fin.subscribe_id) ||
      !reader.ReadVarInt62(&subscribe_fin.final_group) ||
      !reader.ReadVarInt62(&subscribe_fin.final_object)) {
    return 0;
  }
  visitor_.OnSubscribeFinMessage(subscribe_fin);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessSubscribeRst(quic::QuicDataReader& reader) {
  MoqtSubscribeRst subscribe_rst;
  if (!reader.ReadVarInt62(&subscribe_rst.subscribe_id) ||
      !reader.ReadVarInt62(&subscribe_rst.error_code) ||
      !reader.ReadStringPieceVarInt62(&subscribe_rst.reason_phrase) ||
      !reader.ReadVarInt62(&subscribe_rst.final_group) ||
      !reader.ReadVarInt62(&subscribe_rst.final_object)) {
    return 0;
  }
  visitor_.OnSubscribeRstMessage(subscribe_rst);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessAnnounce(quic::QuicDataReader& reader) {
  MoqtAnnounce announce;
  if (!reader.ReadStringPieceVarInt62(&announce.track_namespace)) {
    return 0;
  }
  uint64_t num_params;
  if (!reader.ReadVarInt62(&num_params)) {
    return 0;
  }
  for (uint64_t i = 0; i < num_params; ++i) {
    uint64_t type;
    absl::string_view value;
    if (!ReadParameter(reader, type, value)) {
      return 0;
    }
    auto key = static_cast<MoqtTrackRequestParameter>(type);
    switch (key) {
      case MoqtTrackRequestParameter::kAuthorizationInfo:
        if (announce.authorization_info.has_value()) {
          ParseError("AUTHORIZATION_INFO parameter appears twice in ANNOUNCE");
          return 0;
        }
        announce.authorization_info = value;
        break;
      default:
        // Skip over the parameter.
        break;
    }
  }
  visitor_.OnAnnounceMessage(announce);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessAnnounceOk(quic::QuicDataReader& reader) {
  MoqtAnnounceOk announce_ok;
  if (!reader.ReadStringPieceVarInt62(&announce_ok.track_namespace)) {
    return 0;
  }
  visitor_.OnAnnounceOkMessage(announce_ok);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessAnnounceError(quic::QuicDataReader& reader) {
  MoqtAnnounceError announce_error;
  if (!reader.ReadStringPieceVarInt62(&announce_error.track_namespace)) {
    return 0;
  }
  if (!reader.ReadVarInt62(&announce_error.error_code)) {
    return 0;
  }
  if (!reader.ReadStringPieceVarInt62(&announce_error.reason_phrase)) {
    return 0;
  }
  visitor_.OnAnnounceErrorMessage(announce_error);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessUnannounce(quic::QuicDataReader& reader) {
  MoqtUnannounce unannounce;
  if (!reader.ReadStringPieceVarInt62(&unannounce.track_namespace)) {
    return 0;
  }
  visitor_.OnUnannounceMessage(unannounce);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtParser::ProcessGoAway(quic::QuicDataReader& reader) {
  MoqtGoAway goaway;
  if (!reader.ReadStringPieceVarInt62(&goaway.new_session_uri)) {
    return 0;
  }
  visitor_.OnGoAwayMessage(goaway);
  return reader.PreviouslyReadPayload().length();
}

void MoqtParser::ParseError(absl::string_view reason) {
  ParseError(MoqtError::kProtocolViolation, reason);
}

void MoqtParser::ParseError(MoqtError error_code, absl::string_view reason) {
  if (parsing_error_) {
    return;  // Don't send multiple parse errors.
  }
  no_more_data_ = true;
  parsing_error_ = true;
  visitor_.OnParsingError(error_code, reason);
}

bool MoqtParser::ReadVarIntPieceVarInt62(quic::QuicDataReader& reader,
                                         uint64_t& result) {
  uint64_t length;
  if (!reader.ReadVarInt62(&length)) {
    return false;
  }
  uint64_t actual_length = static_cast<uint64_t>(reader.PeekVarInt62Length());
  if (length != actual_length) {
    ParseError("Parameter VarInt has length field mismatch");
    return false;
  }
  if (!reader.ReadVarInt62(&result)) {
    return false;
  }
  return true;
}

bool MoqtParser::ReadLocation(quic::QuicDataReader& reader,
                              std::optional<MoqtSubscribeLocation>& loc) {
  uint64_t ui64;
  if (!reader.ReadVarInt62(&ui64)) {
    return false;
  }
  auto mode = static_cast<MoqtSubscribeLocationMode>(ui64);
  if (mode == MoqtSubscribeLocationMode::kNone) {
    loc = std::nullopt;
    return true;
  }
  if (!reader.ReadVarInt62(&ui64)) {
    return false;
  }
  switch (mode) {
    case MoqtSubscribeLocationMode::kAbsolute:
      loc = MoqtSubscribeLocation(true, ui64);
      break;
    case MoqtSubscribeLocationMode::kRelativePrevious:
      loc = MoqtSubscribeLocation(false, -1 * static_cast<int64_t>(ui64));
      break;
    case MoqtSubscribeLocationMode::kRelativeNext:
      loc = MoqtSubscribeLocation(false, static_cast<int64_t>(ui64) + 1);
      break;
    default:
      ParseError("Unknown location mode");
      return false;
  }
  return true;
}

bool MoqtParser::ReadParameter(quic::QuicDataReader& reader, uint64_t& type,
                               absl::string_view& value) {
  if (!reader.ReadVarInt62(&type)) {
    return false;
  }
  return reader.ReadStringPieceVarInt62(&value);
}

bool MoqtParser::StringViewToVarInt(absl::string_view& sv, uint64_t& vi) {
  quic::QuicDataReader reader(sv);
  if (static_cast<size_t>(reader.PeekVarInt62Length()) != sv.length()) {
    ParseError(MoqtError::kParameterLengthMismatch,
               "Parameter length does not match varint encoding");
    return false;
  }
  reader.ReadVarInt62(&vi);
  return true;
}

}  // namespace moqt
