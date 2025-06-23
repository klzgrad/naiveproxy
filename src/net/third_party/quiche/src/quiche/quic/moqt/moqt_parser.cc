// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_parser.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "absl/base/casts.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/fixed_array.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_stream.h"

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
      MoqtDataStreamType::kStreamHeaderFetch, MoqtDataStreamType::kPadding};
  for (MoqtDataStreamType type : kAllowedStreamTypes) {
    if (static_cast<uint64_t>(type) == value) {
      return true;
    }
  }
  return false;
}

std::optional<uint64_t> ReadVarInt62FromStream(quiche::ReadStream& stream,
                                               bool& fin_read) {
  fin_read = false;

  quiche::ReadStream::PeekResult peek_result = stream.PeekNextReadableRegion();
  if (peek_result.peeked_data.empty()) {
    if (peek_result.fin_next) {
      fin_read = stream.SkipBytes(0);
      QUICHE_DCHECK(fin_read);
    }
    return std::nullopt;
  }
  char first_byte = peek_result.peeked_data[0];
  size_t varint_size =
      1 << ((absl::bit_cast<uint8_t>(first_byte) & 0b11000000) >> 6);
  if (stream.ReadableBytes() < varint_size) {
    return std::nullopt;
  }

  char buffer[8];
  absl::Span<char> bytes_to_read =
      absl::MakeSpan(buffer).subspan(0, varint_size);
  quiche::ReadStream::ReadResult read_result = stream.Read(bytes_to_read);
  QUICHE_DCHECK_EQ(read_result.bytes_read, varint_size);
  fin_read = read_result.fin;

  quiche::QuicheDataReader reader(buffer, read_result.bytes_read);
  uint64_t result;
  bool success = reader.ReadVarInt62(&result);
  QUICHE_DCHECK(success);
  QUICHE_DCHECK(reader.IsDoneReading());
  return result;
}

// Reads from |reader| to list. Returns false if there is a read error.
bool ParseKeyValuePairList(quic::QuicDataReader& reader,
                           KeyValuePairList& list) {
  list.clear();
  uint64_t num_params;
  if (!reader.ReadVarInt62(&num_params)) {
    return false;
  }
  for (uint64_t i = 0; i < num_params; ++i) {
    uint64_t type;
    if (!reader.ReadVarInt62(&type)) {
      return false;
    }
    if (type % 2 == 1) {
      absl::string_view bytes;
      if (!reader.ReadStringPieceVarInt62(&bytes)) {
        return false;
      }
      list.insert(type, bytes);
      continue;
    }
    uint64_t value;
    if (!reader.ReadVarInt62(&value)) {
      return false;
    }
    list.insert(type, value);
  }
  return true;
}

void KeyValuePairListToMoqtSessionParameters(const KeyValuePairList& parameters,
                                             MoqtSessionParameters& out) {
  parameters.ForEach(
      [&](uint64_t key, uint64_t value) {
        SetupParameter parameter = static_cast<SetupParameter>(key);
        switch (parameter) {
          case SetupParameter::kMaxRequestId:
            out.max_request_id = value;
            break;
          case SetupParameter::kMaxAuthTokenCacheSize:
            out.max_auth_token_cache_size = value;
            break;
          case SetupParameter::kSupportObjectAcks:
            out.support_object_acks = (value == 1);
            break;
          default:
            break;
        }
        return true;
      },
      [&](uint64_t key, absl::string_view value) {
        SetupParameter parameter = static_cast<SetupParameter>(key);
        switch (parameter) {
          case SetupParameter::kPath:
            out.path = value;
            break;
          default:
            break;
        }
        return true;
      });
}

}  // namespace

void MoqtControlParser::ReadAndDispatchMessages() {
  if (no_more_data_) {
    ParseError("Data after end of stream");
    return;
  }
  if (processing_) {
    return;
  }
  processing_ = true;
  auto on_return = absl::MakeCleanup([&] { processing_ = false; });
  while (!no_more_data_) {
    bool fin_read = false;
    // Read the message type.
    if (!message_type_.has_value()) {
      message_type_ = ReadVarInt62FromStream(stream_, fin_read);
      if (fin_read) {
        ParseError("FIN on control stream");
        return;
      }
      if (!message_type_.has_value()) {
        return;
      }
    }
    QUICHE_DCHECK(message_type_.has_value());

    // Read the message length.
    if (!message_size_.has_value()) {
      if (stream_.ReadableBytes() < 2) {
        return;
      }
      std::array<char, 2> size_bytes;
      quiche::ReadStream::ReadResult result =
          stream_.Read(absl::MakeSpan(size_bytes));
      if (result.bytes_read != 2) {
        ParseError(MoqtError::kInternalError,
                   "Stream returned incorrect ReadableBytes");
        return;
      }
      if (result.fin) {
        ParseError("FIN on control stream");
        return;
      }
      message_size_ = static_cast<uint16_t>(size_bytes[0]) << 8 |
                      static_cast<uint16_t>(size_bytes[1]);
      if (*message_size_ > kMaxMessageHeaderSize) {
        ParseError(MoqtError::kInternalError,
                   absl::StrCat("Cannot parse control messages more than ",
                                kMaxMessageHeaderSize, " bytes"));
        return;
      }
    }
    QUICHE_DCHECK(message_size_.has_value());

    // Read the message if it's fully received.
    //
    // CAUTION: if the flow control windows are too low, and
    // kMaxMessageHeaderSize is too high, this will cause a deadlock.
    if (stream_.ReadableBytes() < *message_size_) {
      return;
    }
    absl::FixedArray<char> message(*message_size_);
    quiche::ReadStream::ReadResult result =
        stream_.Read(absl::MakeSpan(message));
    if (result.bytes_read != *message_size_) {
      ParseError("Stream returned incorrect ReadableBytes");
      return;
    }
    if (result.fin) {
      ParseError("FIN on control stream");
      return;
    }

    ProcessMessage(absl::string_view(message.data(), message.size()),
                   static_cast<MoqtMessageType>(*message_type_));
    message_type_.reset();
    message_size_.reset();
  }
}

size_t MoqtControlParser::ProcessMessage(absl::string_view data,
                                         MoqtMessageType message_type) {
  quic::QuicDataReader reader(data);
  size_t bytes_read;
  switch (message_type) {
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
    case MoqtMessageType::kMaxRequestId:
      bytes_read = ProcessMaxRequestId(reader);
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
    case MoqtMessageType::kRequestsBlocked:
      bytes_read = ProcessRequestsBlocked(reader);
      break;
    case moqt::MoqtMessageType::kObjectAck:
      bytes_read = ProcessObjectAck(reader);
      break;
    default:
      ParseError("Unknown message type");
      bytes_read = 0;
      break;
  }
  if (bytes_read != data.size() || bytes_read == 0) {
    ParseError("Message length does not match payload length");
    return 0;
  }
  return bytes_read;
}

size_t MoqtControlParser::ProcessClientSetup(quic::QuicDataReader& reader) {
  MoqtClientSetup setup;
  setup.parameters.using_webtrans = uses_web_transport_;
  setup.parameters.perspective = quic::Perspective::IS_CLIENT;
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
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  MoqtError error = ValidateSetupParameters(parameters, uses_web_transport_,
                                            quic::Perspective::IS_SERVER);
  if (error != MoqtError::kNoError) {
    ParseError(error, "Client SETUP contains invalid parameters");
    return 0;
  }
  KeyValuePairListToMoqtSessionParameters(parameters, setup.parameters);
  // TODO(martinduke): Validate construction of the PATH (Sec 8.3.2.1)
  visitor_.OnClientSetupMessage(setup);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessServerSetup(quic::QuicDataReader& reader) {
  MoqtServerSetup setup;
  setup.parameters.using_webtrans = uses_web_transport_;
  setup.parameters.perspective = quic::Perspective::IS_SERVER;
  uint64_t version;
  if (!reader.ReadVarInt62(&version)) {
    return 0;
  }
  setup.selected_version = static_cast<MoqtVersion>(version);
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  MoqtError error = ValidateSetupParameters(parameters, uses_web_transport_,
                                            quic::Perspective::IS_CLIENT);
  if (error != MoqtError::kNoError) {
    ParseError(error, "Server SETUP contains invalid parameters");
    return 0;
  }
  KeyValuePairListToMoqtSessionParameters(parameters, setup.parameters);
  visitor_.OnServerSetupMessage(setup);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribe(quic::QuicDataReader& reader) {
  MoqtSubscribe subscribe;
  uint64_t filter, group, object;
  uint8_t group_order, forward;
  absl::string_view track_name;
  if (!reader.ReadVarInt62(&subscribe.request_id) ||
      !reader.ReadVarInt62(&subscribe.track_alias) ||
      !ReadTrackNamespace(reader, subscribe.full_track_name) ||
      !reader.ReadStringPieceVarInt62(&track_name) ||
      !reader.ReadUInt8(&subscribe.subscriber_priority) ||
      !reader.ReadUInt8(&group_order) || !reader.ReadUInt8(&forward) ||
      !reader.ReadVarInt62(&filter)) {
    return 0;
  }
  subscribe.full_track_name.AddElement(track_name);
  if (!ParseDeliveryOrder(group_order, subscribe.group_order)) {
    ParseError("Invalid group order value in SUBSCRIBE");
    return 0;
  }
  if (forward > 1) {
    ParseError("Invalid forward value in SUBSCRIBE");
    return 0;
  }
  subscribe.forward = (forward == 1);
  subscribe.filter_type = static_cast<MoqtFilterType>(filter);
  switch (subscribe.filter_type) {
    case MoqtFilterType::kNextGroupStart:
    case MoqtFilterType::kLatestObject:
      break;
    case MoqtFilterType::kAbsoluteStart:
    case MoqtFilterType::kAbsoluteRange:
      if (!reader.ReadVarInt62(&group) || !reader.ReadVarInt62(&object)) {
        return 0;
      }
      subscribe.start = Location(group, object);
      if (subscribe.filter_type == MoqtFilterType::kAbsoluteStart) {
        break;
      }
      if (!reader.ReadVarInt62(&group)) {
        return 0;
      }
      subscribe.end_group = group;
      if (*subscribe.end_group < subscribe.start->group) {
        ParseError("End group is less than start group");
        return 0;
      }
      break;
    default:
      ParseError("Invalid filter type");
      return 0;
  }
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kSubscribe)) {
    ParseError("SUBSCRIBE contains invalid parameters");
    return 0;
  }
  if (!KeyValuePairListToVersionSpecificParameters(parameters,
                                                   subscribe.parameters)) {
    return 0;
  }
  visitor_.OnSubscribeMessage(subscribe);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeOk(quic::QuicDataReader& reader) {
  MoqtSubscribeOk subscribe_ok;
  uint64_t milliseconds;
  uint8_t group_order;
  uint8_t content_exists;
  if (!reader.ReadVarInt62(&subscribe_ok.request_id) ||
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
    subscribe_ok.largest_location = Location();
    if (!reader.ReadVarInt62(&subscribe_ok.largest_location->group) ||
        !reader.ReadVarInt62(&subscribe_ok.largest_location->object)) {
      return 0;
    }
  }
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kSubscribeOk)) {
    ParseError("SUBSCRIBE_OK contains invalid parameters");
    return 0;
  }
  if (!KeyValuePairListToVersionSpecificParameters(parameters,
                                                   subscribe_ok.parameters)) {
    return 0;
  }
  visitor_.OnSubscribeOkMessage(subscribe_ok);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeError(quic::QuicDataReader& reader) {
  MoqtSubscribeError subscribe_error;
  uint64_t error_code;
  if (!reader.ReadVarInt62(&subscribe_error.request_id) ||
      !reader.ReadVarInt62(&error_code) ||
      !reader.ReadStringVarInt62(subscribe_error.reason_phrase) ||
      !reader.ReadVarInt62(&subscribe_error.track_alias)) {
    return 0;
  }
  subscribe_error.error_code = static_cast<RequestErrorCode>(error_code);
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
  uint64_t value;
  if (!reader.ReadVarInt62(&subscribe_done.subscribe_id) ||
      !reader.ReadVarInt62(&value) ||
      !reader.ReadVarInt62(&subscribe_done.stream_count) ||
      !reader.ReadStringVarInt62(subscribe_done.reason_phrase)) {
    return 0;
  }
  subscribe_done.status_code = static_cast<SubscribeDoneCode>(value);
  visitor_.OnSubscribeDoneMessage(subscribe_done);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeUpdate(quic::QuicDataReader& reader) {
  MoqtSubscribeUpdate subscribe_update;
  uint64_t start_group, start_object, end_group;
  uint8_t forward;
  if (!reader.ReadVarInt62(&subscribe_update.request_id) ||
      !reader.ReadVarInt62(&start_group) ||
      !reader.ReadVarInt62(&start_object) || !reader.ReadVarInt62(&end_group) ||
      !reader.ReadUInt8(&subscribe_update.subscriber_priority) ||
      !reader.ReadUInt8(&forward)) {
    return 0;
  }
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kSubscribeUpdate)) {
    ParseError("SUBSCRIBE_UPDATE contains invalid parameters");
    return 0;
  }
  if (!KeyValuePairListToVersionSpecificParameters(
          parameters, subscribe_update.parameters)) {
    return 0;
  }
  subscribe_update.start = Location(start_group, start_object);
  if (end_group > 0) {
    subscribe_update.end_group = end_group - 1;
    if (subscribe_update.end_group < start_group) {
      ParseError("End group is less than start group");
      return 0;
    }
  }
  if (forward > 1) {
    ParseError("Invalid forward value in SUBSCRIBE_UPDATE");
    return 0;
  }
  subscribe_update.forward = (forward == 1);
  visitor_.OnSubscribeUpdateMessage(subscribe_update);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessAnnounce(quic::QuicDataReader& reader) {
  MoqtAnnounce announce;
  if (!ReadTrackNamespace(reader, announce.track_namespace)) {
    return 0;
  }
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kAnnounce)) {
    ParseError("ANNOUNCE contains invalid parameters");
    return 0;
  }
  if (!KeyValuePairListToVersionSpecificParameters(parameters,
                                                   announce.parameters)) {
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
  if (!reader.ReadVarInt62(&error_code) ||
      !reader.ReadStringVarInt62(announce_error.reason_phrase)) {
    return 0;
  }
  announce_error.error_code = static_cast<RequestErrorCode>(error_code);
  visitor_.OnAnnounceErrorMessage(announce_error);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessAnnounceCancel(quic::QuicDataReader& reader) {
  MoqtAnnounceCancel announce_cancel;
  if (!ReadTrackNamespace(reader, announce_cancel.track_namespace)) {
    return 0;
  }
  uint64_t error_code;
  if (!reader.ReadVarInt62(&error_code) ||
      !reader.ReadStringVarInt62(announce_cancel.reason_phrase)) {
    return 0;
  }
  announce_cancel.error_code = static_cast<RequestErrorCode>(error_code);
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
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (!ValidateVersionSpecificParameters(
          parameters, MoqtMessageType::kTrackStatusRequest)) {
    ParseError("TRACK_STATUS_REQUEST message contains invalid parameters");
    return 0;
  }
  if (!KeyValuePairListToVersionSpecificParameters(
          parameters, track_status_request.parameters)) {
    return 0;
  }
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
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kTrackStatus)) {
    ParseError("TRACK_STATUS message contains invalid parameters");
    return 0;
  }
  if (!KeyValuePairListToVersionSpecificParameters(parameters,
                                                   track_status.parameters)) {
    return 0;
  }
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
  MoqtSubscribeAnnounces subscribe_announces;
  if (!ReadTrackNamespace(reader, subscribe_announces.track_namespace)) {
    return 0;
  }
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (!ValidateVersionSpecificParameters(
          parameters, MoqtMessageType::kSubscribeAnnounces)) {
    ParseError("SUBSCRIBE_ANNOUNCES message contains invalid parameters");
    return 0;
  }
  if (!KeyValuePairListToVersionSpecificParameters(
          parameters, subscribe_announces.parameters)) {
    return 0;
  }
  visitor_.OnSubscribeAnnouncesMessage(subscribe_announces);
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
      static_cast<RequestErrorCode>(error_code);
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

size_t MoqtControlParser::ProcessMaxRequestId(quic::QuicDataReader& reader) {
  MoqtMaxRequestId max_request_id;
  if (!reader.ReadVarInt62(&max_request_id.max_request_id)) {
    return 0;
  }
  visitor_.OnMaxRequestIdMessage(max_request_id);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessFetch(quic::QuicDataReader& reader) {
  MoqtFetch fetch;
  absl::string_view track_name;
  uint8_t group_order;
  uint64_t end_object;
  uint64_t type;
  if (!reader.ReadVarInt62(&fetch.fetch_id) ||
      !reader.ReadUInt8(&fetch.subscriber_priority) ||
      !reader.ReadUInt8(&group_order) || !reader.ReadVarInt62(&type)) {
    return 0;
  }
  if (!ParseDeliveryOrder(group_order, fetch.group_order)) {
    ParseError("Invalid group order value in FETCH message");
    return 0;
  }
  switch (static_cast<FetchType>(type)) {
    case FetchType::kJoining: {
      uint64_t joining_subscribe_id;
      uint64_t preceding_group_offset;
      if (!reader.ReadVarInt62(&joining_subscribe_id) ||
          !reader.ReadVarInt62(&preceding_group_offset)) {
        return 0;
      }
      fetch.joining_fetch =
          JoiningFetch{joining_subscribe_id, preceding_group_offset};
      break;
    }
    case FetchType::kStandalone: {
      fetch.joining_fetch = std::nullopt;
      if (!ReadTrackNamespace(reader, fetch.full_track_name) ||
          !reader.ReadStringPieceVarInt62(&track_name) ||
          !reader.ReadVarInt62(&fetch.start_object.group) ||
          !reader.ReadVarInt62(&fetch.start_object.object) ||
          !reader.ReadVarInt62(&fetch.end_group) ||
          !reader.ReadVarInt62(&end_object)) {
        return 0;
      }
      // Elements that have to be translated from the literal value.
      fetch.full_track_name.AddElement(track_name);
      fetch.end_object =
          end_object == 0 ? std::optional<uint64_t>() : (end_object - 1);
      if (fetch.end_group < fetch.start_object.group ||
          (fetch.end_group == fetch.start_object.group &&
           fetch.end_object.has_value() &&
           *fetch.end_object < fetch.start_object.object)) {
        ParseError("End object comes before start object in FETCH");
        return 0;
      }
      break;
    }
    default:
      ParseError("Invalid FETCH type");
      return 0;
  }
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (!ValidateVersionSpecificParameters(parameters, MoqtMessageType::kFetch)) {
    ParseError("FETCH message contains invalid parameters");
    return 0;
  }
  if (!KeyValuePairListToVersionSpecificParameters(parameters,
                                                   fetch.parameters)) {
    return 0;
  };
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
  KeyValuePairList parameters;
  if (!reader.ReadVarInt62(&fetch_ok.subscribe_id) ||
      !reader.ReadUInt8(&group_order) ||
      !reader.ReadVarInt62(&fetch_ok.largest_id.group) ||
      !reader.ReadVarInt62(&fetch_ok.largest_id.object) ||
      !ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (group_order != 0x01 && group_order != 0x02) {
    ParseError("Invalid group order value in FETCH_OK");
    return 0;
  }
  if (!ValidateVersionSpecificParameters(parameters,
                                         MoqtMessageType::kFetchOk)) {
    ParseError("FETCH_OK message contains invalid parameters");
    return 0;
  }
  fetch_ok.group_order = static_cast<MoqtDeliveryOrder>(group_order);
  if (!KeyValuePairListToVersionSpecificParameters(parameters,
                                                   fetch_ok.parameters)) {
    return 0;
  }
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
  fetch_error.error_code = static_cast<RequestErrorCode>(error_code);
  visitor_.OnFetchErrorMessage(fetch_error);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessRequestsBlocked(quic::QuicDataReader& reader) {
  MoqtRequestsBlocked requests_blocked;
  if (!reader.ReadVarInt62(&requests_blocked.max_request_id)) {
    return 0;
  }
  visitor_.OnRequestsBlockedMessage(requests_blocked);
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

bool MoqtControlParser::ReadTrackNamespace(quic::QuicDataReader& reader,
                                           FullTrackName& full_track_name) {
  QUICHE_DCHECK(full_track_name.empty());
  uint64_t num_elements;
  if (!reader.ReadVarInt62(&num_elements)) {
    return false;
  }
  if (num_elements == 0 || num_elements > kMaxNamespaceElements) {
    ParseError(MoqtError::kProtocolViolation,
               "Invalid number of namespace elements");
    return false;
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

// Returns false if there is a protocol violation.
bool MoqtControlParser::KeyValuePairListToVersionSpecificParameters(
    const KeyValuePairList& parameters, VersionSpecificParameters& out) {
  return parameters.ForEach(
      [&](uint64_t key, uint64_t value) {
        VersionSpecificParameter parameter =
            static_cast<VersionSpecificParameter>(key);
        switch (parameter) {
          case VersionSpecificParameter::kDeliveryTimeout:
            out.delivery_timeout = quic::QuicTimeDelta::FromMilliseconds(value);
            break;
          case VersionSpecificParameter::kMaxCacheDuration:
            out.max_cache_duration =
                quic::QuicTimeDelta::FromMilliseconds(value);
            break;
          case VersionSpecificParameter::kOackWindowSize:
            out.oack_window_size = quic::QuicTimeDelta::FromMicroseconds(value);
            break;
          default:
            break;
        }
        return true;
      },
      [&](uint64_t key, absl::string_view value) {
        VersionSpecificParameter parameter =
            static_cast<VersionSpecificParameter>(key);
        switch (parameter) {
          case VersionSpecificParameter::kAuthorizationToken:
            if (!ParseAuthTokenParameter(value, out)) {
              return false;
            }
            break;
          default:
            break;
        }
        return true;
      });
}

bool MoqtControlParser::ParseAuthTokenParameter(
    absl::string_view field, VersionSpecificParameters& out) {
  quic::QuicDataReader reader(field);
  AuthTokenType token_type;
  absl::string_view token;
  uint64_t value;
  if (!reader.ReadVarInt62(&value) || value > AuthTokenAliasType::kMaxValue) {
    ParseError(MoqtError::kKeyValueFormattingError,
               "Invalid Authorization Token Alias type");
    return false;
  }
  AuthTokenAliasType alias_type = static_cast<AuthTokenAliasType>(value);
  switch (alias_type) {
    case AuthTokenAliasType::kUseValue:
      if (!reader.ReadVarInt62(&value)) {
        ParseError(MoqtError::kKeyValueFormattingError,
                   "Malformed Authorization Token Parameter");
        return false;
      }
      if (value > AuthTokenType::kMaxAuthTokenType) {
        ParseError(MoqtError::kKeyValueFormattingError,
                   "Invalid Authorization Token Type");
        return false;
      }
      token_type = static_cast<AuthTokenType>(value);
      token = reader.PeekRemainingPayload();
      break;
    case AuthTokenAliasType::kUseAlias:
      if (!reader.ReadVarInt62(&value)) {
        ParseError(MoqtError::kKeyValueFormattingError,
                   "Malformed Authorization Token Parameter");
        return false;
      }
      // TODO: Implement support for cache_size > 0
      ParseError(MoqtError::kKeyValueFormattingError,
                 "Unknown Auth Token Alias");
      return false;
    case AuthTokenAliasType::kRegister:
      if (!reader.ReadVarInt62(&value)) {
        ParseError(MoqtError::kKeyValueFormattingError,
                   "Malformed Authorization Token Parameter");
        return false;
      }
      if (!reader.ReadVarInt62(&value)) {
        ParseError(MoqtError::kKeyValueFormattingError,
                   "Malformed Authorization Token Parameter");
        return false;
      }
      token_type = static_cast<AuthTokenType>(value);
      token = reader.PeekRemainingPayload();
      if (auth_token_cache_size_ + sizeof(uint64_t) + token.length() >
          max_auth_token_cache_size_) {
        ParseError(MoqtError::kAuthTokenCacheOverflow,
                   "Too many authorization token tags");
        return false;
      }
      break;
      // TODO: Add to the cache.
      // TODO: Check if the alias is already in use.
      QUICHE_NOTREACHED();
      break;
    case AuthTokenAliasType::kDelete:
      if (!reader.ReadVarInt62(&value)) {
        ParseError(MoqtError::kKeyValueFormattingError,
                   "Malformed Authorization Token Parameter");
        return false;
      }
      // TODO: Implement support for cache_size > 0
      ParseError(MoqtError::kKeyValueFormattingError,
                 "Unknown Auth Token Alias");
      return false;
  }
  // Validate cache operations.
  out.authorization_token.push_back(AuthToken(token_type, token));
  return true;
}

void MoqtDataParser::ParseError(absl::string_view reason) {
  if (parsing_error_) {
    return;  // Don't send multiple parse errors.
  }
  next_input_ = kFailed;
  no_more_data_ = true;
  parsing_error_ = true;
  visitor_.OnParsingError(MoqtError::kProtocolViolation, reason);
}

std::optional<absl::string_view> ParseDatagram(absl::string_view data,
                                               MoqtObject& object_metadata) {
  uint64_t type_raw, object_status_raw;
  absl::string_view extensions;
  quic::QuicDataReader reader(data);
  if (!reader.ReadVarInt62(&type_raw) ||
      !reader.ReadVarInt62(&object_metadata.track_alias) ||
      !reader.ReadVarInt62(&object_metadata.group_id) ||
      !reader.ReadVarInt62(&object_metadata.object_id) ||
      !reader.ReadUInt8(&object_metadata.publisher_priority) ||
      !reader.ReadStringPieceVarInt62(&extensions)) {
    return std::nullopt;
  }
  object_metadata.extension_headers = std::string(extensions);
  if (static_cast<MoqtDatagramType>(type_raw) ==
      MoqtDatagramType::kObjectStatus) {
    object_metadata.payload_length = 0;
    if (!reader.ReadVarInt62(&object_status_raw)) {
      return std::nullopt;
    }
    object_metadata.object_status = IntegerToObjectStatus(object_status_raw);
    return "";
  }

  absl::string_view payload;
  if (!reader.ReadStringPieceVarInt62(&payload)) {
    return std::nullopt;
  }
  object_metadata.object_status = MoqtObjectStatus::kNormal;
  object_metadata.payload_length = payload.length();
  return payload;
}

void MoqtDataParser::ReadDataUntil(StopCondition stop_condition) {
  if (processing_) {
    QUICHE_BUG(MoqtDataParser_reentry)
        << "Calling ProcessData() when ProcessData() is already in progress.";
    return;
  }
  processing_ = true;
  auto on_return = absl::MakeCleanup([&] { processing_ = false; });

  State last_state = state();
  for (;;) {
    ParseNextItemFromStream();
    if (state() == last_state || no_more_data_ || stop_condition()) {
      break;
    }
    last_state = state();
  }
}

std::optional<uint64_t> MoqtDataParser::ReadVarInt62NoFin() {
  bool fin_read = false;
  std::optional<uint64_t> result = ReadVarInt62FromStream(stream_, fin_read);
  if (fin_read) {
    ParseError("Unexpected FIN received in the middle of a header");
    return std::nullopt;
  }
  return result;
}

std::optional<uint8_t> MoqtDataParser::ReadUint8NoFin() {
  char buffer[1];
  quiche::ReadStream::ReadResult read_result =
      stream_.Read(absl::MakeSpan(buffer));
  if (read_result.fin) {
    ParseError("Unexpected FIN received in the middle of a header");
    return std::nullopt;
  }
  if (read_result.bytes_read == 0) {
    return std::nullopt;
  }
  return absl::bit_cast<uint8_t>(buffer[0]);
}

void MoqtDataParser::AdvanceParserState() {
  QUICHE_DCHECK(type_ == MoqtDataStreamType::kStreamHeaderSubgroup ||
                type_ == MoqtDataStreamType::kStreamHeaderFetch);
  const bool is_fetch = type_ == MoqtDataStreamType::kStreamHeaderFetch;
  switch (next_input_) {
    // The state table is factored into a separate function (rather than
    // inlined) in order to separate the order of elements from the way they are
    // parsed.
    case kStreamType:
      next_input_ = kTrackAlias;
      break;
    case kTrackAlias:
      next_input_ = kGroupId;
      break;
    case kGroupId:
      next_input_ = kSubgroupId;
      break;
    case kSubgroupId:
      next_input_ = is_fetch ? kObjectId : kPublisherPriority;
      break;
    case kPublisherPriority:
      next_input_ = is_fetch ? kExtensionSize : kObjectId;
      break;
    case kObjectId:
      next_input_ = is_fetch ? kPublisherPriority : kExtensionSize;
      break;
    case kExtensionBody:
      next_input_ = kObjectPayloadLength;
      break;
    case kStatus:
    case kData:
      next_input_ = is_fetch ? kGroupId : kObjectId;
      break;

    case kExtensionSize:        // Either kExtensionBody or
                                // kObjectPayloadLength.
    case kObjectPayloadLength:  // Either kStatus or kData depending on length.
    case kPadding:              // Handled separately.
    case kFailed:               // Should cause parsing to cease.
      QUICHE_NOTREACHED();
      break;
  }
}

void MoqtDataParser::ParseNextItemFromStream() {
  if (CheckForFinWithoutData()) {
    return;
  }
  switch (next_input_) {
    case kStreamType: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        if (!IsAllowedStreamType(*value_read)) {
          ParseError("Invalid stream type supplied");
          return;
        }
        type_ = static_cast<MoqtDataStreamType>(*value_read);
        switch (*type_) {
          case MoqtDataStreamType::kStreamHeaderSubgroup:
          case MoqtDataStreamType::kStreamHeaderFetch:
            AdvanceParserState();
            break;
          case MoqtDataStreamType::kPadding:
            next_input_ = kPadding;
            break;
        }
      }
      return;
    }

    case kTrackAlias: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        metadata_.track_alias = *value_read;
        AdvanceParserState();
      }
      return;
    }

    case kGroupId: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        metadata_.group_id = *value_read;
        AdvanceParserState();
      }
      return;
    }

    case kSubgroupId: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        metadata_.subgroup_id = *value_read;
        AdvanceParserState();
      }
      return;
    }

    case kPublisherPriority: {
      std::optional<uint8_t> value_read = ReadUint8NoFin();
      if (value_read.has_value()) {
        metadata_.publisher_priority = *value_read;
        AdvanceParserState();
      }
      return;
    }

    case kObjectId: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        metadata_.object_id = *value_read;
        AdvanceParserState();
      }
      return;
    }

    case kExtensionSize: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        metadata_.extension_headers.clear();
        payload_length_remaining_ = *value_read;
        next_input_ = (value_read == 0) ? kObjectPayloadLength : kExtensionBody;
      }
      return;
    }

    case kObjectPayloadLength: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        metadata_.payload_length = *value_read;
        payload_length_remaining_ = *value_read;
        if (metadata_.payload_length > 0) {
          metadata_.object_status = MoqtObjectStatus::kNormal;
          next_input_ = kData;
        } else {
          next_input_ = kStatus;
        }
      }
      return;
    }

    case kStatus: {
      bool fin_read = false;
      std::optional<uint64_t> value_read =
          ReadVarInt62FromStream(stream_, fin_read);
      if (value_read.has_value()) {
        metadata_.object_status = IntegerToObjectStatus(*value_read);
        if (metadata_.object_status == MoqtObjectStatus::kInvalidObjectStatus) {
          ParseError("Invalid object status provided");
          return;
        }

        ++num_objects_read_;
        visitor_.OnObjectMessage(metadata_, "", /*end_of_message=*/true);
        AdvanceParserState();
      }
      if (fin_read) {
        no_more_data_ = true;
        return;
      }
      return;
    }

    case kExtensionBody:
    case kData: {
      while (payload_length_remaining_ > 0) {
        quiche::ReadStream::PeekResult peek_result =
            stream_.PeekNextReadableRegion();
        if (!peek_result.has_data()) {
          return;
        }
        if (peek_result.fin_next && payload_length_remaining_ > 0) {
          ParseError("FIN received at an unexpected point in the stream");
          return;
        }

        size_t chunk_size =
            std::min(payload_length_remaining_, peek_result.peeked_data.size());
        payload_length_remaining_ -= chunk_size;
        bool done = payload_length_remaining_ == 0;
        if (next_input_ == kData) {
          visitor_.OnObjectMessage(
              metadata_, peek_result.peeked_data.substr(0, chunk_size), done);
          const bool fin = stream_.SkipBytes(chunk_size);
          if (done) {
            ++num_objects_read_;
            no_more_data_ |= fin;
            AdvanceParserState();
          }
        } else {
          absl::StrAppend(&metadata_.extension_headers,
                          peek_result.peeked_data.substr(0, chunk_size));
          if (stream_.SkipBytes(chunk_size)) {
            ParseError("FIN received at an unexpected point in the stream");
            return;
          }
          if (done) {
            AdvanceParserState();
          }
        }
      }
      return;
    }

    case kPadding:
      no_more_data_ |= stream_.SkipBytes(stream_.ReadableBytes());
      return;

    case kFailed:
      return;
  }
}

void MoqtDataParser::ReadAllData() {
  ReadDataUntil(+[]() { return false; });
}

void MoqtDataParser::ReadStreamType() {
  return ReadDataUntil([this]() { return type_.has_value(); });
}

void MoqtDataParser::ReadTrackAlias() {
  return ReadDataUntil(
      [this]() { return type_.has_value() && next_input_ != kTrackAlias; });
}

void MoqtDataParser::ReadAtMostOneObject() {
  const size_t num_objects_read_initial = num_objects_read_;
  return ReadDataUntil(
      [&]() { return num_objects_read_ != num_objects_read_initial; });
}

bool MoqtDataParser::CheckForFinWithoutData() {
  if (!stream_.PeekNextReadableRegion().fin_next) {
    return false;
  }
  const bool valid_state =
      (type_ == MoqtDataStreamType::kStreamHeaderSubgroup &&
       next_input_ == kObjectId) ||
      (type_ == MoqtDataStreamType::kStreamHeaderFetch &&
       next_input_ == kGroupId);
  if (!valid_state || num_objects_read_ == 0) {
    ParseError("FIN received at an unexpected point in the stream");
    return true;
  }
  return stream_.SkipBytes(0);
}

}  // namespace moqt
