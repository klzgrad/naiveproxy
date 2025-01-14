// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_parser.h"

#include <array>
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
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace moqt {

namespace {

bool ParseDeliveryOrder(uint8_t raw_value,
                        std::optional<MoqtDeliveryOrder>& output) {
  switch (raw_value) {
    case 0x00:
      output = std::nullopt;
      return true;
    case 0x01:
      output = MoqtDeliveryOrder::kAscending;
      return true;
    case 0x02:
      output = MoqtDeliveryOrder::kDescending;
      return true;
    default:
      return false;
  }
}

uint64_t SignedVarintUnserializedForm(uint64_t value) {
  if (value & 0x01) {
    return -(value >> 1);
  }
  return value >> 1;
}

bool IsAllowedStreamType(uint64_t value) {
  constexpr std::array kAllowedStreamTypes = {
      MoqtDataStreamType::kStreamHeaderSubgroup,
      MoqtDataStreamType::kStreamHeaderTrack,
      MoqtDataStreamType::kStreamHeaderFetch, MoqtDataStreamType::kPadding};
  for (MoqtDataStreamType type : kAllowedStreamTypes) {
    if (static_cast<uint64_t>(type) == value) {
      return true;
    }
  }
  return false;
}

size_t ParseObjectHeader(quic::QuicDataReader& reader, MoqtObject& object,
                         MoqtDataStreamType type) {
  if (!reader.ReadVarInt62(&object.track_alias)) {
    return 0;
  }
  if (type != MoqtDataStreamType::kStreamHeaderTrack &&
      type != MoqtDataStreamType::kStreamHeaderFetch &&
      !reader.ReadVarInt62(&object.group_id)) {
    return 0;
  }
  if (type == MoqtDataStreamType::kStreamHeaderSubgroup) {
    uint64_t subgroup_id;
    if (!reader.ReadVarInt62(&subgroup_id)) {
      return 0;
    }
    object.subgroup_id = subgroup_id;
  }
  if (type == MoqtDataStreamType::kObjectDatagram &&
      !reader.ReadVarInt62(&object.object_id)) {
    return 0;
  }
  if (type != MoqtDataStreamType::kStreamHeaderFetch &&
      !reader.ReadUInt8(&object.publisher_priority)) {
    return 0;
  }
  uint64_t status = static_cast<uint64_t>(MoqtObjectStatus::kNormal);
  if (type == MoqtDataStreamType::kObjectDatagram &&
      (!reader.ReadVarInt62(&object.payload_length) ||
       (object.payload_length == 0 && !reader.ReadVarInt62(&status)))) {
    return 0;
  }
  object.object_status = IntegerToObjectStatus(status);
  object.forwarding_preference = GetForwardingPreference(type);
  return reader.PreviouslyReadPayload().size();
}

size_t ParseObjectSubheader(quic::QuicDataReader& reader, MoqtObject& object,
                            MoqtDataStreamType type) {
  switch (type) {
    case MoqtDataStreamType::kStreamHeaderTrack:
    case MoqtDataStreamType::kStreamHeaderFetch:
      if (!reader.ReadVarInt62(&object.group_id)) {
        return 0;
      }
      if (type == MoqtDataStreamType::kStreamHeaderFetch) {
        uint64_t value;
        if (!reader.ReadVarInt62(&value)) {
          return 0;
        }
        object.subgroup_id = value;
      }
      [[fallthrough]];

    case MoqtDataStreamType::kStreamHeaderSubgroup: {
      if (!reader.ReadVarInt62(&object.object_id)) {
        return 0;
      }
      if (type == MoqtDataStreamType::kStreamHeaderFetch &&
          !reader.ReadUInt8(&object.publisher_priority)) {
        return 0;
      }
      if (!reader.ReadVarInt62(&object.payload_length)) {
        return 0;
      }
      uint64_t status = static_cast<uint64_t>(MoqtObjectStatus::kNormal);
      if (object.payload_length == 0 && !reader.ReadVarInt62(&status)) {
        return 0;
      }
      object.object_status = IntegerToObjectStatus(status);
      return reader.PreviouslyReadPayload().size();
    }

    default:
      QUICHE_NOTREACHED();
      return 0;
  }
}

}  // namespace

// The buffering philosophy is complicated, to minimize copying. Here is an
// overview:
// If the entire message body is present (except for OBJECT payload), it is
// parsed and delivered. If not, the partial body is buffered. (requiring a
// copy).
// Any OBJECT payload is always delivered to the application without copying.
// If something has been buffered, when more data arrives copy just enough of it
// to finish parsing that thing, then resume normal processing.
void MoqtControlParser::ProcessData(absl::string_view data, bool fin) {
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
    if (!buffered_message_.empty() && data.empty()) {
      ParseError("End of stream before complete message");
      return;
    }
  }
  std::optional<quic::QuicDataReader> reader = std::nullopt;
  size_t original_buffer_size = buffered_message_.size();
  if (!buffered_message_.empty()) {
    absl::StrAppend(&buffered_message_, data);
    reader.emplace(buffered_message_);
  } else {
    // No message in progress.
    reader.emplace(data);
  }
  size_t total_processed = 0;
  while (!reader->IsDoneReading()) {
    size_t message_len = ProcessMessage(reader->PeekRemainingPayload());
    if (message_len == 0) {
      if (reader->BytesRemaining() > kMaxMessageHeaderSize) {
        ParseError(MoqtError::kInternalError,
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

size_t MoqtControlParser::ProcessMessage(absl::string_view data) {
  uint64_t value, length;
  quic::QuicDataReader reader(data);
  if (!reader.ReadVarInt62(&value) || !reader.ReadVarInt62(&length)) {
    return 0;
  }
  if (length > reader.BytesRemaining()) {
    return 0;
  }
  auto type = static_cast<MoqtMessageType>(value);
  size_t message_header_length = reader.PreviouslyReadPayload().length();
  size_t bytes_read;
  switch (type) {
    case MoqtMessageType::kClientSetup:
      bytes_read = ProcessClientSetup(reader);
      break;
    case MoqtMessageType::kServerSetup:
      bytes_read = ProcessServerSetup(reader);
      break;
    case MoqtMessageType::kSubscribe:
      bytes_read = ProcessSubscribe(reader);
      break;
    case MoqtMessageType::kSubscribeOk:
      bytes_read = ProcessSubscribeOk(reader);
      break;
    case MoqtMessageType::kSubscribeError:
      bytes_read = ProcessSubscribeError(reader);
      break;
    case MoqtMessageType::kUnsubscribe:
      bytes_read = ProcessUnsubscribe(reader);
      break;
    case MoqtMessageType::kSubscribeDone:
      bytes_read = ProcessSubscribeDone(reader);
      break;
    case MoqtMessageType::kSubscribeUpdate:
      bytes_read = ProcessSubscribeUpdate(reader);
      break;
    case MoqtMessageType::kAnnounce:
      bytes_read = ProcessAnnounce(reader);
      break;
    case MoqtMessageType::kAnnounceOk:
      bytes_read = ProcessAnnounceOk(reader);
      break;
    case MoqtMessageType::kAnnounceError:
      bytes_read = ProcessAnnounceError(reader);
      break;
    case MoqtMessageType::kAnnounceCancel:
      bytes_read = ProcessAnnounceCancel(reader);
      break;
    case MoqtMessageType::kTrackStatusRequest:
      bytes_read = ProcessTrackStatusRequest(reader);
      break;
    case MoqtMessageType::kUnannounce:
      bytes_read = ProcessUnannounce(reader);
      break;
    case MoqtMessageType::kTrackStatus:
      bytes_read = ProcessTrackStatus(reader);
      break;
    case MoqtMessageType::kGoAway:
      bytes_read = ProcessGoAway(reader);
      break;
    case MoqtMessageType::kSubscribeAnnounces:
      bytes_read = ProcessSubscribeAnnounces(reader);
      break;
    case MoqtMessageType::kSubscribeAnnouncesOk:
      bytes_read = ProcessSubscribeAnnouncesOk(reader);
      break;
    case MoqtMessageType::kSubscribeAnnouncesError:
      bytes_read = ProcessSubscribeAnnouncesError(reader);
      break;
    case MoqtMessageType::kUnsubscribeAnnounces:
      bytes_read = ProcessUnsubscribeAnnounces(reader);
      break;
    case MoqtMessageType::kMaxSubscribeId:
      bytes_read = ProcessMaxSubscribeId(reader);
      break;
    case MoqtMessageType::kFetch:
      bytes_read = ProcessFetch(reader);
      break;
    case MoqtMessageType::kFetchCancel:
      bytes_read = ProcessFetchCancel(reader);
      break;
    case MoqtMessageType::kFetchOk:
      bytes_read = ProcessFetchOk(reader);
      break;
    case MoqtMessageType::kFetchError:
      bytes_read = ProcessFetchError(reader);
      break;
    case moqt::MoqtMessageType::kObjectAck:
      bytes_read = ProcessObjectAck(reader);
      break;
    default:
      ParseError("Unknown message type");
      bytes_read = 0;
      break;
  }
  if ((bytes_read - message_header_length) != length) {
    ParseError("Message length does not match payload length");
    return 0;
  }
  return bytes_read;
}

size_t MoqtControlParser::ProcessClientSetup(quic::QuicDataReader& reader) {
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
        if (!StringViewToVarInt(value, index)) {
          return 0;
        }
        if (index > static_cast<uint64_t>(MoqtRole::kRoleMax)) {
          ParseError("Invalid ROLE parameter");
          return 0;
        }
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
      case MoqtSetupParameter::kMaxSubscribeId:
        if (setup.max_subscribe_id.has_value()) {
          ParseError("MAX_SUBSCRIBE_ID parameter appears twice in SETUP");
          return 0;
        }
        uint64_t max_id;
        if (!StringViewToVarInt(value, max_id)) {
          ParseError("MAX_SUBSCRIBE_ID parameter is not a valid varint");
          return 0;
        }
        setup.max_subscribe_id = max_id;
        break;
      case MoqtSetupParameter::kSupportObjectAcks:
        uint64_t flag;
        if (!StringViewToVarInt(value, flag) || flag > 1) {
          ParseError("Invalid kSupportObjectAcks value");
          return 0;
        }
        setup.supports_object_ack = static_cast<bool>(flag);
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

size_t MoqtControlParser::ProcessServerSetup(quic::QuicDataReader& reader) {
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
        if (!StringViewToVarInt(value, index)) {
          return 0;
        }
        if (index > static_cast<uint64_t>(MoqtRole::kRoleMax)) {
          ParseError("Invalid ROLE parameter");
          return 0;
        }
        setup.role = static_cast<MoqtRole>(index);
        break;
      case MoqtSetupParameter::kPath:
        ParseError("PATH parameter in SERVER_SETUP");
        return 0;
      case MoqtSetupParameter::kMaxSubscribeId:
        if (setup.max_subscribe_id.has_value()) {
          ParseError("MAX_SUBSCRIBE_ID parameter appears twice in SETUP");
          return 0;
        }
        uint64_t max_id;
        if (!StringViewToVarInt(value, max_id)) {
          ParseError("MAX_SUBSCRIBE_ID parameter is not a valid varint");
          return 0;
        }
        setup.max_subscribe_id = max_id;
        break;
      case MoqtSetupParameter::kSupportObjectAcks:
        uint64_t flag;
        if (!StringViewToVarInt(value, flag) || flag > 1) {
          ParseError("Invalid kSupportObjectAcks value");
          return 0;
        }
        setup.supports_object_ack = static_cast<bool>(flag);
        break;
      default:
        // Skip over the parameter.
        break;
    }
  }
  if (!setup.role.has_value()) {
    ParseError("ROLE parameter missing from SERVER_SETUP message");
    return 0;
  }
  visitor_.OnServerSetupMessage(setup);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribe(quic::QuicDataReader& reader) {
  MoqtSubscribe subscribe_request;
  uint64_t filter, group, object;
  uint8_t group_order;
  absl::string_view track_name;
  if (!reader.ReadVarInt62(&subscribe_request.subscribe_id) ||
      !reader.ReadVarInt62(&subscribe_request.track_alias) ||
      !ReadTrackNamespace(reader, subscribe_request.full_track_name) ||
      !reader.ReadStringPieceVarInt62(&track_name) ||
      !reader.ReadUInt8(&subscribe_request.subscriber_priority) ||
      !reader.ReadUInt8(&group_order) || !reader.ReadVarInt62(&filter)) {
    return 0;
  }
  subscribe_request.full_track_name.AddElement(track_name);
  if (!ParseDeliveryOrder(group_order, subscribe_request.group_order)) {
    ParseError("Invalid group order value in SUBSCRIBE message");
    return 0;
  }
  MoqtFilterType filter_type = static_cast<MoqtFilterType>(filter);
  switch (filter_type) {
    case MoqtFilterType::kLatestGroup:
      subscribe_request.start_object = 0;
      break;
    case MoqtFilterType::kLatestObject:
      break;
    case MoqtFilterType::kAbsoluteStart:
    case MoqtFilterType::kAbsoluteRange:
      if (!reader.ReadVarInt62(&group) || !reader.ReadVarInt62(&object)) {
        return 0;
      }
      subscribe_request.start_group = group;
      subscribe_request.start_object = object;
      if (filter_type == MoqtFilterType::kAbsoluteStart) {
        break;
      }
      if (!reader.ReadVarInt62(&group) || !reader.ReadVarInt62(&object)) {
        return 0;
      }
      subscribe_request.end_group = group;
      if (subscribe_request.end_group < subscribe_request.start_group) {
        ParseError("End group is less than start group");
        return 0;
      }
      if (object == 0) {
        subscribe_request.end_object = std::nullopt;
      } else {
        subscribe_request.end_object = object - 1;
        if (subscribe_request.start_group == subscribe_request.end_group &&
            subscribe_request.end_object < subscribe_request.start_object) {
          ParseError("End object comes before start object");
          return 0;
        }
      }
      break;
    default:
      ParseError("Invalid filter type");
      return 0;
  }
  if (!ReadSubscribeParameters(reader, subscribe_request.parameters)) {
    return 0;
  }
  visitor_.OnSubscribeMessage(subscribe_request);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeOk(quic::QuicDataReader& reader) {
  MoqtSubscribeOk subscribe_ok;
  uint64_t milliseconds;
  uint8_t group_order;
  uint8_t content_exists;
  if (!reader.ReadVarInt62(&subscribe_ok.subscribe_id) ||
      !reader.ReadVarInt62(&milliseconds) || !reader.ReadUInt8(&group_order) ||
      !reader.ReadUInt8(&content_exists)) {
    return 0;
  }
  if (content_exists > 1) {
    ParseError("SUBSCRIBE_OK ContentExists has invalid value");
    return 0;
  }
  if (group_order != 0x01 && group_order != 0x02) {
    ParseError("Invalid group order value in SUBSCRIBE_OK");
    return 0;
  }
  subscribe_ok.expires = quic::QuicTimeDelta::FromMilliseconds(milliseconds);
  subscribe_ok.group_order = static_cast<MoqtDeliveryOrder>(group_order);
  if (content_exists) {
    subscribe_ok.largest_id = FullSequence();
    if (!reader.ReadVarInt62(&subscribe_ok.largest_id->group) ||
        !reader.ReadVarInt62(&subscribe_ok.largest_id->object)) {
      return 0;
    }
  }
  if (!ReadSubscribeParameters(reader, subscribe_ok.parameters)) {
    return 0;
  }
  if (subscribe_ok.parameters.authorization_info.has_value()) {
    ParseError("SUBSCRIBE_OK has authorization info");
    return 0;
  }
  visitor_.OnSubscribeOkMessage(subscribe_ok);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeError(quic::QuicDataReader& reader) {
  MoqtSubscribeError subscribe_error;
  uint64_t error_code;
  if (!reader.ReadVarInt62(&subscribe_error.subscribe_id) ||
      !reader.ReadVarInt62(&error_code) ||
      !reader.ReadStringVarInt62(subscribe_error.reason_phrase) ||
      !reader.ReadVarInt62(&subscribe_error.track_alias)) {
    return 0;
  }
  subscribe_error.error_code = static_cast<SubscribeErrorCode>(error_code);
  visitor_.OnSubscribeErrorMessage(subscribe_error);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessUnsubscribe(quic::QuicDataReader& reader) {
  MoqtUnsubscribe unsubscribe;
  if (!reader.ReadVarInt62(&unsubscribe.subscribe_id)) {
    return 0;
  }
  visitor_.OnUnsubscribeMessage(unsubscribe);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeDone(quic::QuicDataReader& reader) {
  MoqtSubscribeDone subscribe_done;
  uint8_t content_exists;
  uint64_t value;
  if (!reader.ReadVarInt62(&subscribe_done.subscribe_id) ||
      !reader.ReadVarInt62(&value) ||
      !reader.ReadStringVarInt62(subscribe_done.reason_phrase) ||
      !reader.ReadUInt8(&content_exists)) {
    return 0;
  }
  subscribe_done.status_code = static_cast<SubscribeDoneCode>(value);
  if (content_exists > 1) {
    ParseError("SUBSCRIBE_DONE ContentExists has invalid value");
    return 0;
  }
  if (content_exists == 1) {
    subscribe_done.final_id = FullSequence();
    if (!reader.ReadVarInt62(&subscribe_done.final_id->group) ||
        !reader.ReadVarInt62(&subscribe_done.final_id->object)) {
      return 0;
    }
  }
  visitor_.OnSubscribeDoneMessage(subscribe_done);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeUpdate(quic::QuicDataReader& reader) {
  MoqtSubscribeUpdate subscribe_update;
  uint64_t end_group, end_object;
  if (!reader.ReadVarInt62(&subscribe_update.subscribe_id) ||
      !reader.ReadVarInt62(&subscribe_update.start_group) ||
      !reader.ReadVarInt62(&subscribe_update.start_object) ||
      !reader.ReadVarInt62(&end_group) || !reader.ReadVarInt62(&end_object) ||
      !reader.ReadUInt8(&subscribe_update.subscriber_priority)) {
    return 0;
  }
  if (!ReadSubscribeParameters(reader, subscribe_update.parameters)) {
    return 0;
  }
  if (end_group == 0) {
    // end_group remains nullopt.
    if (end_object > 0) {
      ParseError("SUBSCRIBE_UPDATE has end_object but no end_group");
      return 0;
    }
  } else {
    subscribe_update.end_group = end_group - 1;
    if (subscribe_update.end_group < subscribe_update.start_group) {
      ParseError("End group is less than start group");
      return 0;
    }
  }
  if (end_object > 0) {
    subscribe_update.end_object = end_object - 1;
    if (subscribe_update.end_object.has_value() &&
        subscribe_update.start_group == *subscribe_update.end_group &&
        *subscribe_update.end_object < subscribe_update.start_object) {
      ParseError("End object comes before start object");
      return 0;
    }
  } else {
    subscribe_update.end_object = std::nullopt;
  }
  if (subscribe_update.parameters.authorization_info.has_value()) {
    ParseError("SUBSCRIBE_UPDATE has authorization info");
    return 0;
  }
  visitor_.OnSubscribeUpdateMessage(subscribe_update);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessAnnounce(quic::QuicDataReader& reader) {
  MoqtAnnounce announce;
  if (!ReadTrackNamespace(reader, announce.track_namespace)) {
    return 0;
  }
  if (!ReadSubscribeParameters(reader, announce.parameters)) {
    return 0;
  }
  if (announce.parameters.delivery_timeout.has_value()) {
    ParseError("ANNOUNCE has delivery timeout");
    return 0;
  }
  visitor_.OnAnnounceMessage(announce);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessAnnounceOk(quic::QuicDataReader& reader) {
  MoqtAnnounceOk announce_ok;
  if (!ReadTrackNamespace(reader, announce_ok.track_namespace)) {
    return 0;
  }
  visitor_.OnAnnounceOkMessage(announce_ok);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessAnnounceError(quic::QuicDataReader& reader) {
  MoqtAnnounceError announce_error;
  if (!ReadTrackNamespace(reader, announce_error.track_namespace)) {
    return 0;
  }
  uint64_t error_code;
  if (!reader.ReadVarInt62(&error_code)) {
    return 0;
  }
  announce_error.error_code = static_cast<MoqtAnnounceErrorCode>(error_code);
  if (!reader.ReadStringVarInt62(announce_error.reason_phrase)) {
    return 0;
  }
  visitor_.OnAnnounceErrorMessage(announce_error);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessAnnounceCancel(quic::QuicDataReader& reader) {
  MoqtAnnounceCancel announce_cancel;
  if (!ReadTrackNamespace(reader, announce_cancel.track_namespace)) {
    return 0;
  }
  if (!reader.ReadVarInt62(&announce_cancel.error_code) ||
      !reader.ReadStringVarInt62(announce_cancel.reason_phrase)) {
    return 0;
  }
  visitor_.OnAnnounceCancelMessage(announce_cancel);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessTrackStatusRequest(
    quic::QuicDataReader& reader) {
  MoqtTrackStatusRequest track_status_request;
  if (!ReadTrackNamespace(reader, track_status_request.full_track_name)) {
    return 0;
  }
  absl::string_view name;
  if (!reader.ReadStringPieceVarInt62(&name)) {
    return 0;
  }
  track_status_request.full_track_name.AddElement(name);
  visitor_.OnTrackStatusRequestMessage(track_status_request);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessUnannounce(quic::QuicDataReader& reader) {
  MoqtUnannounce unannounce;
  if (!ReadTrackNamespace(reader, unannounce.track_namespace)) {
    return 0;
  }
  visitor_.OnUnannounceMessage(unannounce);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessTrackStatus(quic::QuicDataReader& reader) {
  MoqtTrackStatus track_status;
  if (!ReadTrackNamespace(reader, track_status.full_track_name)) {
    return 0;
  }
  absl::string_view name;
  if (!reader.ReadStringPieceVarInt62(&name)) {
    return 0;
  }
  track_status.full_track_name.AddElement(name);
  uint64_t value;
  if (!reader.ReadVarInt62(&value) ||
      !reader.ReadVarInt62(&track_status.last_group) ||
      !reader.ReadVarInt62(&track_status.last_object)) {
    return 0;
  }
  track_status.status_code = static_cast<MoqtTrackStatusCode>(value);
  visitor_.OnTrackStatusMessage(track_status);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessGoAway(quic::QuicDataReader& reader) {
  MoqtGoAway goaway;
  if (!reader.ReadStringVarInt62(goaway.new_session_uri)) {
    return 0;
  }
  visitor_.OnGoAwayMessage(goaway);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeAnnounces(
    quic::QuicDataReader& reader) {
  MoqtSubscribeAnnounces subscribe_namespace;
  if (!ReadTrackNamespace(reader, subscribe_namespace.track_namespace)) {
    return 0;
  }
  if (!ReadSubscribeParameters(reader, subscribe_namespace.parameters)) {
    return 0;
  }
  visitor_.OnSubscribeAnnouncesMessage(subscribe_namespace);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeAnnouncesOk(
    quic::QuicDataReader& reader) {
  MoqtSubscribeAnnouncesOk subscribe_namespace_ok;
  if (!ReadTrackNamespace(reader, subscribe_namespace_ok.track_namespace)) {
    return 0;
  }
  visitor_.OnSubscribeAnnouncesOkMessage(subscribe_namespace_ok);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeAnnouncesError(
    quic::QuicDataReader& reader) {
  MoqtSubscribeAnnouncesError subscribe_namespace_error;
  uint64_t error_code;
  if (!ReadTrackNamespace(reader, subscribe_namespace_error.track_namespace) ||
      !reader.ReadVarInt62(&error_code) ||
      !reader.ReadStringVarInt62(subscribe_namespace_error.reason_phrase)) {
    return 0;
  }
  subscribe_namespace_error.error_code =
      static_cast<MoqtAnnounceErrorCode>(error_code);
  visitor_.OnSubscribeAnnouncesErrorMessage(subscribe_namespace_error);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessUnsubscribeAnnounces(
    quic::QuicDataReader& reader) {
  MoqtUnsubscribeAnnounces unsubscribe_namespace;
  if (!ReadTrackNamespace(reader, unsubscribe_namespace.track_namespace)) {
    return 0;
  }
  visitor_.OnUnsubscribeAnnouncesMessage(unsubscribe_namespace);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessMaxSubscribeId(quic::QuicDataReader& reader) {
  MoqtMaxSubscribeId max_subscribe_id;
  if (!reader.ReadVarInt62(&max_subscribe_id.max_subscribe_id)) {
    return 0;
  }
  visitor_.OnMaxSubscribeIdMessage(max_subscribe_id);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessFetch(quic::QuicDataReader& reader) {
  MoqtFetch fetch;
  absl::string_view track_name;
  uint8_t group_order;
  uint64_t end_object;
  if (!reader.ReadVarInt62(&fetch.subscribe_id) ||
      !ReadTrackNamespace(reader, fetch.full_track_name) ||
      !reader.ReadStringPieceVarInt62(&track_name) ||
      !reader.ReadUInt8(&fetch.subscriber_priority) ||
      !reader.ReadUInt8(&group_order) ||
      !reader.ReadVarInt62(&fetch.start_object.group) ||
      !reader.ReadVarInt62(&fetch.start_object.object) ||
      !reader.ReadVarInt62(&fetch.end_group) ||
      !reader.ReadVarInt62(&end_object) ||
      !ReadSubscribeParameters(reader, fetch.parameters)) {
    return 0;
  }
  // Elements that have to be translated from the literal value.
  fetch.full_track_name.AddElement(track_name);
  if (!ParseDeliveryOrder(group_order, fetch.group_order)) {
    ParseError("Invalid group order value in FETCH message");
    return 0;
  }
  fetch.end_object =
      end_object == 0 ? std::optional<uint64_t>() : (end_object - 1);
  if (fetch.end_group < fetch.start_object.group ||
      (fetch.end_group == fetch.start_object.group &&
       fetch.end_object.has_value() &&
       *fetch.end_object < fetch.start_object.object)) {
    ParseError("End object comes before start object in FETCH");
    return 0;
  }
  visitor_.OnFetchMessage(fetch);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessFetchCancel(quic::QuicDataReader& reader) {
  MoqtFetchCancel fetch_cancel;
  if (!reader.ReadVarInt62(&fetch_cancel.subscribe_id)) {
    return 0;
  }
  visitor_.OnFetchCancelMessage(fetch_cancel);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessFetchOk(quic::QuicDataReader& reader) {
  MoqtFetchOk fetch_ok;
  uint8_t group_order;
  if (!reader.ReadVarInt62(&fetch_ok.subscribe_id) ||
      !reader.ReadUInt8(&group_order) ||
      !reader.ReadVarInt62(&fetch_ok.largest_id.group) ||
      !reader.ReadVarInt62(&fetch_ok.largest_id.object) ||
      !ReadSubscribeParameters(reader, fetch_ok.parameters)) {
    return 0;
  }
  if (group_order != 0x01 && group_order != 0x02) {
    ParseError("Invalid group order value in FETCH_OK");
    return 0;
  }
  fetch_ok.group_order = static_cast<MoqtDeliveryOrder>(group_order);
  visitor_.OnFetchOkMessage(fetch_ok);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessFetchError(quic::QuicDataReader& reader) {
  MoqtFetchError fetch_error;
  uint64_t error_code;
  if (!reader.ReadVarInt62(&fetch_error.subscribe_id) ||
      !reader.ReadVarInt62(&error_code) ||
      !reader.ReadStringVarInt62(fetch_error.reason_phrase)) {
    return 0;
  }
  fetch_error.error_code = static_cast<SubscribeErrorCode>(error_code);
  visitor_.OnFetchErrorMessage(fetch_error);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessObjectAck(quic::QuicDataReader& reader) {
  MoqtObjectAck object_ack;
  uint64_t raw_delta;
  if (!reader.ReadVarInt62(&object_ack.subscribe_id) ||
      !reader.ReadVarInt62(&object_ack.group_id) ||
      !reader.ReadVarInt62(&object_ack.object_id) ||
      !reader.ReadVarInt62(&raw_delta)) {
    return 0;
  }
  object_ack.delta_from_deadline = quic::QuicTimeDelta::FromMicroseconds(
      SignedVarintUnserializedForm(raw_delta));
  visitor_.OnObjectAckMessage(object_ack);
  return reader.PreviouslyReadPayload().length();
}

void MoqtControlParser::ParseError(absl::string_view reason) {
  ParseError(MoqtError::kProtocolViolation, reason);
}

void MoqtControlParser::ParseError(MoqtError error_code,
                                   absl::string_view reason) {
  if (parsing_error_) {
    return;  // Don't send multiple parse errors.
  }
  no_more_data_ = true;
  parsing_error_ = true;
  visitor_.OnParsingError(error_code, reason);
}

bool MoqtControlParser::ReadVarIntPieceVarInt62(quic::QuicDataReader& reader,
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

bool MoqtControlParser::ReadParameter(quic::QuicDataReader& reader,
                                      uint64_t& type,
                                      absl::string_view& value) {
  if (!reader.ReadVarInt62(&type)) {
    return false;
  }
  return reader.ReadStringPieceVarInt62(&value);
}

bool MoqtControlParser::ReadSubscribeParameters(
    quic::QuicDataReader& reader, MoqtSubscribeParameters& params) {
  uint64_t num_params;
  if (!reader.ReadVarInt62(&num_params)) {
    return false;
  }
  for (uint64_t i = 0; i < num_params; ++i) {
    uint64_t type;
    absl::string_view value;
    if (!ReadParameter(reader, type, value)) {
      return false;
    }
    uint64_t raw_value;
    auto key = static_cast<MoqtTrackRequestParameter>(type);
    switch (key) {
      case MoqtTrackRequestParameter::kAuthorizationInfo:
        if (params.authorization_info.has_value()) {
          ParseError("AUTHORIZATION_INFO parameter appears twice");
          return false;
        }
        params.authorization_info = value;
        break;
      case moqt::MoqtTrackRequestParameter::kDeliveryTimeout:
        if (params.delivery_timeout.has_value()) {
          ParseError("DELIVERY_TIMEOUT parameter appears twice");
          return false;
        }
        if (!StringViewToVarInt(value, raw_value)) {
          return false;
        }
        params.delivery_timeout =
            quic::QuicTimeDelta::FromMilliseconds(raw_value);
        break;
      case moqt::MoqtTrackRequestParameter::kMaxCacheDuration:
        if (params.max_cache_duration.has_value()) {
          ParseError("MAX_CACHE_DURATION parameter appears twice");
          return false;
        }
        if (!StringViewToVarInt(value, raw_value)) {
          return false;
        }
        params.max_cache_duration =
            quic::QuicTimeDelta::FromMilliseconds(raw_value);
        break;
      case MoqtTrackRequestParameter::kOackWindowSize: {
        if (params.object_ack_window.has_value()) {
          ParseError("OACK_WINDOW_SIZE parameter appears twice in SUBSCRIBE");
          return false;
        }
        if (!StringViewToVarInt(value, raw_value)) {
          ParseError("OACK_WINDOW_SIZE parameter is not a valid varint");
          return false;
        }
        params.object_ack_window =
            quic::QuicTimeDelta::FromMicroseconds(raw_value);
        break;
      }
      default:
        // Skip over the parameter.
        break;
    }
  }
  return true;
}

bool MoqtControlParser::StringViewToVarInt(absl::string_view& sv,
                                           uint64_t& vi) {
  quic::QuicDataReader reader(sv);
  if (static_cast<size_t>(reader.PeekVarInt62Length()) != sv.length()) {
    ParseError(MoqtError::kParameterLengthMismatch,
               "Parameter length does not match varint encoding");
    return false;
  }
  reader.ReadVarInt62(&vi);
  return true;
}

bool MoqtControlParser::ReadTrackNamespace(quic::QuicDataReader& reader,
                                           FullTrackName& full_track_name) {
  QUICHE_DCHECK(full_track_name.empty());
  uint64_t num_elements;
  if (!reader.ReadVarInt62(&num_elements)) {
    return 0;
  }
  for (uint64_t i = 0; i < num_elements; ++i) {
    absl::string_view element;
    if (!reader.ReadStringPieceVarInt62(&element)) {
      return false;
    }
    full_track_name.AddElement(element);
  }
  return true;
}

void MoqtDataParser::ParseError(absl::string_view reason) {
  if (parsing_error_) {
    return;  // Don't send multiple parse errors.
  }
  no_more_data_ = true;
  parsing_error_ = true;
  visitor_.OnParsingError(MoqtError::kProtocolViolation, reason);
}

absl::string_view ParseDatagram(absl::string_view data,
                                MoqtObject& object_metadata) {
  uint64_t value;
  quic::QuicDataReader reader(data);
  if (!reader.ReadVarInt62(&value)) {
    return absl::string_view();
  }
  if (static_cast<MoqtDataStreamType>(value) !=
      MoqtDataStreamType::kObjectDatagram) {
    return absl::string_view();
  }
  size_t processed_data = ParseObjectHeader(
      reader, object_metadata, MoqtDataStreamType::kObjectDatagram);
  if (processed_data == 0) {  // Incomplete header
    return absl::string_view();
  }
  return reader.PeekRemainingPayload();
}

void MoqtDataParser::ProcessData(absl::string_view data, bool fin) {
  if (processing_) {
    QUICHE_BUG(MoqtDataParser_reentry)
        << "Calling ProcessData() when ProcessData() is already in progress.";
    return;
  }
  processing_ = true;
  auto on_return = absl::MakeCleanup([&] { processing_ = false; });

  if (no_more_data_) {
    ParseError("Data after end of stream");
    return;
  }

  // Sad path: there is already data buffered.  Attempt to transfer a small
  // chunk from `data` into the buffer, in hope that it will make the contents
  // of the buffer parsable without any leftover data.  This is a reasonable
  // expectation, since object headers are small, and are often followed by
  // large blobs of data.
  while (!buffered_message_.empty() && !data.empty()) {
    absl::string_view chunk = data.substr(0, chunk_size_);
    absl::StrAppend(&buffered_message_, chunk);
    absl::string_view unprocessed = ProcessDataInner(buffered_message_);
    if (unprocessed.size() >= chunk.size()) {
      // chunk didn't allow any processing at all.
      data.remove_prefix(chunk.size());
    } else {
      buffered_message_.clear();
      data.remove_prefix(chunk.size() - unprocessed.size());
    }
  }

  // Happy path: there is no buffered data.
  if (buffered_message_.empty() && !data.empty()) {
    buffered_message_.assign(ProcessDataInner(data));
  }

  if (fin) {
    if (!buffered_message_.empty() || !metadata_.has_value() ||
        payload_length_remaining_ > 0) {
      ParseError("FIN received at an unexpected point in the stream");
      return;
    }
    no_more_data_ = true;
  }
}

absl::string_view MoqtDataParser::ProcessDataInner(absl::string_view data) {
  quic::QuicDataReader reader(data);
  while (!reader.IsDoneReading()) {
    absl::string_view remainder = reader.PeekRemainingPayload();
    switch (GetNextInput()) {
      case kStreamType: {
        uint64_t value;
        if (!reader.ReadVarInt62(&value)) {
          return remainder;
        }
        if (!IsAllowedStreamType(value)) {
          ParseError(absl::StrCat("Unknown stream type: ", value));
          return "";
        }
        type_ = static_cast<MoqtDataStreamType>(value);
        continue;
      }

      case kHeader: {
        MoqtObject header;
        size_t bytes_read = ParseObjectHeader(reader, header, *type_);
        if (bytes_read == 0) {
          return remainder;
        }
        metadata_ = header;
        continue;
      }

      case kSubheader: {
        size_t bytes_read = ParseObjectSubheader(reader, *metadata_, *type_);
        if (bytes_read == 0) {
          return remainder;
        }
        if (metadata_->object_status ==
            MoqtObjectStatus::kInvalidObjectStatus) {
          ParseError("Invalid object status provided");
          return "";
        }
        payload_length_remaining_ = metadata_->payload_length;
        if (payload_length_remaining_ == 0) {
          visitor_.OnObjectMessage(*metadata_, "", true);
        }
        continue;
      }

      case kData: {
        absl::string_view payload =
            reader.ReadAtMost(payload_length_remaining_);
        visitor_.OnObjectMessage(*metadata_, payload,
                                 payload.size() == payload_length_remaining_);
        payload_length_remaining_ -= payload.size();

        continue;
      }

      case kPadding:
        return "";
    }
  }
  return "";
}

}  // namespace moqt
