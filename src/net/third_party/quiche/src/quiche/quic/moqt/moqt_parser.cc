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
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/casts.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/fixed_array.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/http2/adapter/header_validator.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_status_utils.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace {

uint64_t SignedVarintUnserializedForm(uint64_t value) {
  if (value & 0x01) {
    return -(value >> 1);
  }
  return value >> 1;
}

absl::Status KeyValueFormatError(absl::string_view message) {
  return MoqtErrorStatusWithCode(message, MoqtError::kKeyValueFormattingError);
}

absl::Status CheckForTrailingData(const quic::QuicDataReader& reader) {
  if (!reader.IsDoneReading()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Control message has excess data of ",
                     reader.BytesRemaining(), " bytes at the end"));
  }
  return absl::OkStatus();
}

// |fin_read| is set to true if there is a FIN anywhere before the end of the
// varint.
std::optional<uint64_t> ReadVarInt62FromStream(webtransport::Stream& stream,
                                               bool& fin_read) {
  fin_read = false;

  webtransport::Stream::PeekResult peek_result =
      stream.PeekNextReadableRegion();
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
    if (peek_result.all_data_received) {
      fin_read = true;
    }
    return std::nullopt;
  }

  char buffer[8];
  absl::Span<char> bytes_to_read =
      absl::MakeSpan(buffer).subspan(0, varint_size);
  webtransport::Stream::ReadResult read_result = stream.Read(bytes_to_read);
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
absl::Status ParseKeyValuePairList(quic::QuicDataReader& reader,
                                   KeyValuePairList& list) {
  list.clear();
  uint64_t num_params;
  if (!reader.ReadVarInt62(&num_params)) {
    return absl::InvalidArgumentError(
        "Unable to parse key-value pair list element count");
  }
  uint64_t type = 0;
  for (uint64_t i = 0; i < num_params; ++i) {
    uint64_t type_diff;
    if (!reader.ReadVarInt62(&type_diff)) {
      return absl::InvalidArgumentError(
          "Unable to parse the key in a key-value pair");
    }
    type += type_diff;
    if (type % 2 == 1) {
      absl::string_view bytes;
      if (!reader.ReadStringPieceVarInt62(&bytes)) {
        return absl::InvalidArgumentError(
            "Unable to read the string value in a key-value pair");
      }
      list.insert(type, bytes);
      continue;
    }
    uint64_t value;
    if (!reader.ReadVarInt62(&value)) {
      return absl::InvalidArgumentError(
          "Unable to read the integer value in a key-value pair");
    }
    list.insert(type, value);
  }
  return absl::OkStatus();
}

absl::Status ParseKeyValuePairListWithNoPrefix(quic::QuicDataReader& reader,
                                               KeyValuePairList& list) {
  list.clear();
  uint64_t type = 0;
  while (reader.BytesRemaining() > 0) {
    uint64_t type_diff;
    if (!reader.ReadVarInt62(&type_diff)) {
      return absl::InvalidArgumentError(
          "Unable to parse the key in a key-value pair");
    }
    type += type_diff;
    if (type % 2 == 1) {
      absl::string_view bytes;
      if (!reader.ReadStringPieceVarInt62(&bytes)) {
        return absl::InvalidArgumentError(
            "Unable to read the string value in a key-value pair");
      }
      list.insert(type, bytes);
      continue;
    }
    uint64_t value;
    if (!reader.ReadVarInt62(&value)) {
      return absl::InvalidArgumentError(
          "Unable to read the integer value in a key-value pair");
    }
    list.insert(type, value);
  }
  return absl::OkStatus();
}

bool ParseAuthTokenParameter(absl::string_view field,
                             std::vector<AuthToken>& out) {
  quic::QuicDataReader reader(field);
  AuthTokenAliasType alias_type;
  uint64_t alias;
  AuthTokenType type;
  absl::string_view token;
  uint64_t value;
  if (!reader.ReadVarInt62(&value)) {
    return false;
  }
  alias_type = static_cast<AuthTokenAliasType>(value);
  switch (alias_type) {
    case AuthTokenAliasType::kUseValue:
      if (!reader.ReadVarInt62(&value) ||
          value > AuthTokenType::kMaxAuthTokenType) {
        return false;
      }
      type = static_cast<AuthTokenType>(value);
      token = reader.PeekRemainingPayload();
      out.push_back(AuthToken(type, token));
      break;
    case AuthTokenAliasType::kUseAlias:
      if (!reader.ReadVarInt62(&value)) {
        return false;
      }
      out.push_back(AuthToken(value, alias_type));
      break;
    case AuthTokenAliasType::kRegister:
      if (!reader.ReadVarInt62(&alias) || !reader.ReadVarInt62(&value)) {
        return false;
      }
      type = static_cast<AuthTokenType>(value);
      token = reader.PeekRemainingPayload();
      out.push_back(AuthToken(alias, type, token));
      break;
    case AuthTokenAliasType::kDelete:
      if (!reader.ReadVarInt62(&alias)) {
        return false;
      }
      out.push_back(AuthToken(alias, alias_type));
      break;
    default:  // invalid alias type
      return false;
  }
  return true;
}

bool ParseLocation(absl::string_view field, Location& out) {
  quic::QuicDataReader reader(field);
  return reader.ReadVarInt62(&out.group) && reader.ReadVarInt62(&out.object) &&
         reader.IsDoneReading();
}

absl::Status ParseSubscriptionFilter(absl::string_view field,
                                     std::optional<SubscriptionFilter>& out) {
  quic::QuicDataReader reader(field);
  uint64_t value;
  if (!reader.ReadVarInt62(&value)) {
    return KeyValueFormatError("Unable to read subscription filter type");
  }
  uint64_t group, object;
  switch (static_cast<MoqtFilterType>(value)) {
    case MoqtFilterType::kLargestObject:
    case MoqtFilterType::kNextGroupStart:
      out.emplace(static_cast<MoqtFilterType>(value));
      break;
    case MoqtFilterType::kAbsoluteStart:
      if (!reader.ReadVarInt62(&group) || !reader.ReadVarInt62(&object)) {
        return KeyValueFormatError("Invalid AbsoluteStart filter");
      }
      out.emplace(Location(group, object));
      break;
    case MoqtFilterType::kAbsoluteRange:
      if (!reader.ReadVarInt62(&group) || !reader.ReadVarInt62(&object) ||
          !reader.ReadVarInt62(&value)) {
        return KeyValueFormatError("Invalid AbsoluteRange filter");
      }
      if (value < group) {  // end before start
        return absl::InvalidArgumentError(
            "AbsoluteRange filter specified with a start after the end");
      }
      out.emplace(Location(group, object), value);
      break;
    default:  // invalid filter type
      return absl::InvalidArgumentError("Invalid filter type");
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status SetupParameters::FromKeyValuePairList(
    const KeyValuePairList& list) {
  absl::Status status = absl::OkStatus();
  uint64_t last_key;
  bool result = list.ForEach(
      [&](uint64_t key, std::variant<uint64_t, absl::string_view> value) {
        last_key = key;
        switch (static_cast<SetupParameter>(key)) {
          case SetupParameter::kMaxRequestId:
            if (max_request_id.has_value()) {
              status = absl::InvalidArgumentError("Duplicate Setup Parameter");
              return false;
            }
            max_request_id = std::get<uint64_t>(value);
            break;
          case SetupParameter::kMaxAuthTokenCacheSize:
            if (max_auth_token_cache_size.has_value()) {
              status = absl::InvalidArgumentError("Duplicate Setup Parameter");
              return false;
            }
            max_auth_token_cache_size = std::get<uint64_t>(value);
            break;
          case SetupParameter::kPath:
            if (path.has_value()) {
              status = absl::InvalidArgumentError("Duplicate Setup Parameter");
              return false;
            }
            if (!http2::adapter::HeaderValidator::IsValidPath(
                    std::get<absl::string_view>(value),
                    /*allow_fragment=*/false)) {
              status = MoqtErrorStatusWithCode("Malformed path",
                                               MoqtError::kMalformedPath);
              return false;
            }
            path = std::get<absl::string_view>(value);
            break;
          case SetupParameter::kAuthorizationToken:
            if (!ParseAuthTokenParameter(std::get<absl::string_view>(value),
                                         authorization_tokens)) {
              status = KeyValueFormatError("Malformed auth token parameter");
              return false;
            }
            break;
          case SetupParameter::kAuthority:
            if (!http2::adapter::HeaderValidator::IsValidAuthority(
                    std::get<absl::string_view>(value))) {
              status = MoqtErrorStatusWithCode("Invalid authority field",
                                               MoqtError::kMalformedAuthority);
              return false;
            }
            authority = std::get<absl::string_view>(value);
            break;
          case SetupParameter::kMoqtImplementation:
            if (moqt_implementation.has_value()) {
              status = absl::InvalidArgumentError("Duplicate Setup Parameter");
              return false;
            }
            QUICHE_LOG(INFO) << "Peer MOQT implementation: "
                             << std::get<absl::string_view>(value);
            moqt_implementation = std::get<absl::string_view>(value);
            break;
          case SetupParameter::kSupportObjectAcks:
            if (support_object_acks.has_value()) {
              status = absl::InvalidArgumentError("Duplicate Setup Parameter");
              return false;
            }
            if (std::get<uint64_t>(value) > 1) {
              status =
                  KeyValueFormatError("SUPPORT_OBJECT_ACKS has to be 0 or 1");
              return false;
            }
            support_object_acks = (std::get<uint64_t>(value) == 1);
            break;
          default:
            break;
        }
        return true;
      });
  if (!result && status.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse the value for the setup parameter key 0x",
                     absl::Hex(static_cast<uint64_t>(last_key))));
  }
  return status;
}

absl::Status MessageParameters::FromKeyValuePairList(
    const KeyValuePairList& list) {
  absl::Status status = absl::OkStatus();
  uint64_t last_key;
  bool result = list.ForEach([&](uint64_t key,
                                 std::variant<uint64_t, absl::string_view>
                                     value) {
    last_key = key;
    switch (static_cast<MessageParameter>(key)) {
      case MessageParameter::kDeliveryTimeout:
        if (delivery_timeout.has_value()) {
          status = absl::InvalidArgumentError("Duplicate Message Parameter");
          return false;
        }
        if (std::get<uint64_t>(value) == 0) {
          status = absl::InvalidArgumentError("DELIVERY_TIMEOUT cannot be 0");
          return false;
        }
        delivery_timeout =
            quic::QuicTimeDelta::TryFromMilliseconds(std::get<uint64_t>(value))
                .value_or(quic::QuicTimeDelta::Infinite());
        break;
      case MessageParameter::kAuthorizationToken:
        if (!ParseAuthTokenParameter(std::get<absl::string_view>(value),
                                     authorization_tokens)) {
          status = KeyValueFormatError("Malformed auth token parameter");
          return false;
        }
        break;
      case MessageParameter::kExpires:
        if (expires.has_value()) {
          status = absl::InvalidArgumentError("Duplicate Message Parameter");
          return false;
        }
        expires =
            quic::QuicTimeDelta::TryFromMilliseconds(std::get<uint64_t>(value))
                .value_or(quic::QuicTimeDelta::Infinite());
        if (expires->IsZero()) {
          expires = quic::QuicTimeDelta::Infinite();
        }
        break;
      case MessageParameter::kLargestObject:
        if (largest_object.has_value()) {
          status = absl::InvalidArgumentError("Duplicate Message Parameter");
          return false;
        }
        largest_object = Location();
        if (!ParseLocation(std::get<absl::string_view>(value),
                           *largest_object)) {
          status = KeyValueFormatError(
              "Failed to parse location of the largest object");
          return false;
        }
        break;
      case MessageParameter::kForward:
        if (forward_has_value()) {
          status = absl::InvalidArgumentError("Duplicate Message Parameter");
          return false;
        }
        if (std::get<uint64_t>(value) > 1) {
          status = absl::InvalidArgumentError("FORWARD must be 0 or 1");
          return false;
        }
        set_forward(std::get<uint64_t>(value) != 0);
        break;
      case MessageParameter::kSubscriberPriority:
        if (subscriber_priority.has_value()) {
          status = absl::InvalidArgumentError("Duplicate Message Parameter");
          return false;
        }
        if (std::get<uint64_t>(value) > kMaxPriority) {
          status =
              absl::InvalidArgumentError("Subscriber priority exceeds maximum");
          return false;
        }
        subscriber_priority =
            static_cast<MoqtPriority>(std::get<uint64_t>(value));
        break;
      case MessageParameter::kSubscriptionFilter:
        if (subscription_filter.has_value()) {
          status = absl::InvalidArgumentError("Duplicate Message Parameter");
          // TODO(martinduke): Support multiple subscription filters.
          return false;
        }
        status = ParseSubscriptionFilter(std::get<absl::string_view>(value),
                                         subscription_filter);
        if (!status.ok()) {
          return false;
        }
        break;
      case MessageParameter::kGroupOrder:
        if (group_order.has_value()) {
          status = absl::InvalidArgumentError("Duplicate Message Parameter");
          return false;
        }
        if (std::get<uint64_t>(value) > kMaxMoqtDeliveryOrder ||
            std::get<uint64_t>(value) < kMinMoqtDeliveryOrder) {
          status = absl::InvalidArgumentError(
              "GROUP_ORDER is outside the valid range");
          return false;
        }
        group_order = static_cast<MoqtDeliveryOrder>(std::get<uint64_t>(value));
        break;
      case MessageParameter::kNewGroupRequest:
        if (new_group_request.has_value()) {
          status = absl::InvalidArgumentError("Duplicate Message Parameter");
          return false;
        }
        new_group_request = std::get<uint64_t>(value);
        break;
      case MessageParameter::kOackWindowSize:
        if (oack_window_size.has_value()) {
          status = absl::InvalidArgumentError("Duplicate Message Parameter");
          return false;
        }
        oack_window_size =
            quic::QuicTimeDelta::FromMicroseconds(std::get<uint64_t>(value));
        break;
      default:
        // Unknown MessageParameters not allowed!
        status = absl::InvalidArgumentError(
            absl::StrCat("Unknown message parameter 0x",
                         absl::Hex(static_cast<uint64_t>(key))));
        return false;
    }
    return true;
  });
  if (!result && status.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse the value for the message parameter key 0x",
        absl::Hex(static_cast<uint64_t>(last_key))));
  }
  return status;
}

bool MoqtMessageTypeParser::ReadUntilMessageTypeKnown() {
  if (message_type_.has_value()) {
    return true;
  }
  bool fin_read = false;
  message_type_ = ReadVarInt62FromStream(stream_, fin_read);
  if (fin_read) {
    return false;
  }
  return true;
}

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
      webtransport::Stream::ReadResult result =
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
    webtransport::Stream::ReadResult result =
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

void MoqtControlParser::ProcessMessage(absl::string_view data,
                                       MoqtMessageType message_type) {
  absl::Status status;
  switch (message_type) {
    case MoqtMessageType::kClientSetup:
      status = ProcessClientSetup(data);
      break;
    case MoqtMessageType::kServerSetup:
      status = ProcessServerSetup(data);
      break;
    case MoqtMessageType::kRequestOk:
      status = ProcessRequestOk(data);
      break;
    case MoqtMessageType::kRequestError:
      status = ProcessRequestError(data);
      break;
    case MoqtMessageType::kSubscribe:
      status = ProcessSubscribe(data);
      break;
    case MoqtMessageType::kSubscribeOk:
      status = ProcessSubscribeOk(data);
      break;
    case MoqtMessageType::kUnsubscribe:
      status = ProcessUnsubscribe(data);
      break;
    case MoqtMessageType::kPublishDone:
      status = ProcessPublishDone(data);
      break;
    case MoqtMessageType::kRequestUpdate:
      status = ProcessRequestUpdate(data);
      break;
    case MoqtMessageType::kPublishNamespace:
      status = ProcessPublishNamespace(data);
      break;
    case MoqtMessageType::kPublishNamespaceDone:
      status = ProcessPublishNamespaceDone(data);
      break;
    case MoqtMessageType::kNamespace:
      status = ProcessNamespace(data);
      break;
    case MoqtMessageType::kNamespaceDone:
      status = ProcessNamespaceDone(data);
      break;
    case MoqtMessageType::kPublishNamespaceCancel:
      status = ProcessPublishNamespaceCancel(data);
      break;
    case MoqtMessageType::kTrackStatus:
      status = ProcessTrackStatus(data);
      break;
    case MoqtMessageType::kGoAway:
      status = ProcessGoAway(data);
      break;
    case MoqtMessageType::kSubscribeNamespace:
      status = ProcessSubscribeNamespace(data);
      break;
    case MoqtMessageType::kMaxRequestId:
      status = ProcessMaxRequestId(data);
      break;
    case MoqtMessageType::kFetch:
      status = ProcessFetch(data);
      break;
    case MoqtMessageType::kFetchCancel:
      status = ProcessFetchCancel(data);
      break;
    case MoqtMessageType::kFetchOk:
      status = ProcessFetchOk(data);
      break;
    case MoqtMessageType::kRequestsBlocked:
      status = ProcessRequestsBlocked(data);
      break;
    case MoqtMessageType::kPublish:
      status = ProcessPublish(data);
      break;
    case MoqtMessageType::kPublishOk:
      status = ProcessPublishOk(data);
      break;
    case moqt::MoqtMessageType::kObjectAck:
      status = ProcessObjectAck(data);
      break;
    default:
      ParseError(absl::InvalidArgumentError(
          absl::StrCat("Unknown control message type 0x",
                       absl::Hex(static_cast<uint64_t>(message_type)))));
      return;
  }
  if (!status.ok()) {
    ParseError(
        quiche::AppendToStatus(status, " while parsing a message of type 0x",
                               absl::Hex(static_cast<uint64_t>(message_type))));
  }
}

absl::Status MoqtControlParser::ProcessClientSetup(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtClientSetup setup;
  KeyValuePairList parameters;
  QUICHE_RETURN_IF_ERROR(ParseKeyValuePairList(reader, parameters));
  QUICHE_RETURN_IF_ERROR(FillAndValidateSetupParameters(
      parameters, setup.parameters, MoqtMessageType::kClientSetup));
  // TODO(martinduke): Validate construction of the PATH (Sec 8.3.2.1)
  visitor_.OnClientSetupMessage(setup);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessServerSetup(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtServerSetup setup;
  KeyValuePairList parameters;
  QUICHE_RETURN_IF_ERROR(ParseKeyValuePairList(reader, parameters));
  QUICHE_RETURN_IF_ERROR(FillAndValidateSetupParameters(
      parameters, setup.parameters, MoqtMessageType::kServerSetup));
  visitor_.OnServerSetupMessage(setup);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessSubscribe(absl::string_view data,
                                                 MoqtMessageType message_type) {
  quic::QuicDataReader reader(data);
  MoqtSubscribe subscribe;
  if (!reader.ReadVarInt62(&subscribe.request_id)) {
    return absl::InvalidArgumentError("Failed to read request ID");
  }
  QUICHE_RETURN_IF_ERROR(ReadFullTrackName(reader, subscribe.full_track_name));
  QUICHE_RETURN_IF_ERROR(
      FillAndValidateMessageParameters(reader, subscribe.parameters));
  if (message_type == MoqtMessageType::kTrackStatus) {
    visitor_.OnTrackStatusMessage(subscribe);
  } else {
    visitor_.OnSubscribeMessage(subscribe);
  }
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessSubscribeOk(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtSubscribeOk subscribe_ok;
  if (!reader.ReadVarInt62(&subscribe_ok.request_id)) {
    return absl::InvalidArgumentError("Failed to read the request ID");
  }
  if (!reader.ReadVarInt62(&subscribe_ok.track_alias)) {
    return absl::InvalidArgumentError("Failed to read the track alias");
  }
  KeyValuePairList pairs;
  QUICHE_RETURN_IF_ERROR(ParseKeyValuePairList(reader, pairs));
  QUICHE_RETURN_IF_ERROR(subscribe_ok.parameters.FromKeyValuePairList(pairs));
  QUICHE_RETURN_IF_ERROR(
      ParseKeyValuePairListWithNoPrefix(reader, subscribe_ok.extensions));
  if (!subscribe_ok.extensions.Validate()) {
    return absl::InvalidArgumentError("Invalid SUBSCRIBE_OK track extensions");
  }
  visitor_.OnSubscribeOkMessage(subscribe_ok);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessRequestError(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtRequestError request_error;
  uint64_t error_code;
  uint64_t raw_interval;
  if (!reader.ReadVarInt62(&request_error.request_id) ||
      !reader.ReadVarInt62(&error_code) ||
      !reader.ReadVarInt62(&raw_interval) ||
      !reader.ReadStringVarInt62(request_error.reason_phrase)) {
    return absl::InvalidArgumentError("Message missing fields");
  }
  request_error.error_code = static_cast<RequestErrorCode>(error_code);
  request_error.retry_interval =
      (raw_interval == 0)
          ? std::nullopt
          : std::make_optional(
                quic::QuicTimeDelta::FromMilliseconds(raw_interval - 1));
  visitor_.OnRequestErrorMessage(request_error);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessUnsubscribe(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtUnsubscribe unsubscribe;
  if (!reader.ReadVarInt62(&unsubscribe.request_id)) {
    return absl::InvalidArgumentError("Message missing fields");
  }
  visitor_.OnUnsubscribeMessage(unsubscribe);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessPublishDone(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtPublishDone publish_done;
  uint64_t value;
  if (!reader.ReadVarInt62(&publish_done.request_id) ||
      !reader.ReadVarInt62(&value) ||
      !reader.ReadVarInt62(&publish_done.stream_count) ||
      !reader.ReadStringVarInt62(publish_done.error_reason)) {
    return absl::InvalidArgumentError("Message missing fields");
  }
  publish_done.status_code = static_cast<PublishDoneCode>(value);
  visitor_.OnPublishDoneMessage(publish_done);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessRequestUpdate(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtRequestUpdate request_update;
  if (!reader.ReadVarInt62(&request_update.request_id) ||
      !reader.ReadVarInt62(&request_update.existing_request_id)) {
    return absl::InvalidArgumentError("Message missing request IDs");
  }
  QUICHE_RETURN_IF_ERROR(
      FillAndValidateMessageParameters(reader, request_update.parameters));
  visitor_.OnRequestUpdateMessage(request_update);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessPublishNamespace(
    absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtPublishNamespace publish_namespace;
  if (!reader.ReadVarInt62(&publish_namespace.request_id)) {
    return absl::InvalidArgumentError("Request ID missing");
  }
  QUICHE_RETURN_IF_ERROR(
      ReadTrackNamespace(reader, publish_namespace.track_namespace));
  QUICHE_RETURN_IF_ERROR(
      FillAndValidateMessageParameters(reader, publish_namespace.parameters));
  visitor_.OnPublishNamespaceMessage(publish_namespace);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessNamespace(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtNamespace _namespace;
  QUICHE_RETURN_IF_ERROR(
      ReadTrackNamespace(reader, _namespace.track_namespace_suffix));
  visitor_.OnNamespaceMessage(_namespace);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessNamespaceDone(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtNamespaceDone namespace_done;
  QUICHE_RETURN_IF_ERROR(
      ReadTrackNamespace(reader, namespace_done.track_namespace_suffix));
  visitor_.OnNamespaceDoneMessage(namespace_done);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessRequestOk(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtRequestOk request_ok;
  if (!reader.ReadVarInt62(&request_ok.request_id)) {
    return absl::InvalidArgumentError("Request ID missing");
  }
  QUICHE_RETURN_IF_ERROR(
      FillAndValidateMessageParameters(reader, request_ok.parameters));
  visitor_.OnRequestOkMessage(request_ok);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessPublishNamespaceDone(
    absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtPublishNamespaceDone pn_done;
  if (!reader.ReadVarInt62(&pn_done.request_id)) {
    return absl::InvalidArgumentError("Request ID missing");
  }
  visitor_.OnPublishNamespaceDoneMessage(pn_done);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessPublishNamespaceCancel(
    absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtPublishNamespaceCancel publish_namespace_cancel;
  uint64_t error_code;
  if (!reader.ReadVarInt62(&publish_namespace_cancel.request_id) ||
      !reader.ReadVarInt62(&error_code) ||
      !reader.ReadStringVarInt62(publish_namespace_cancel.error_reason)) {
    return absl::InvalidArgumentError("Message missing fields");
  }
  publish_namespace_cancel.error_code =
      static_cast<RequestErrorCode>(error_code);
  visitor_.OnPublishNamespaceCancelMessage(publish_namespace_cancel);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessTrackStatus(absl::string_view data) {
  return ProcessSubscribe(data, MoqtMessageType::kTrackStatus);
}

absl::Status MoqtControlParser::ProcessGoAway(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtGoAway goaway;
  if (!reader.ReadStringVarInt62(goaway.new_session_uri)) {
    return absl::InvalidArgumentError("Missing new session URI");
  }
  visitor_.OnGoAwayMessage(goaway);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessSubscribeNamespace(
    absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtSubscribeNamespace subscribe_namespace;
  uint64_t raw_option;
  if (!reader.ReadVarInt62(&subscribe_namespace.request_id)) {
    return absl::InvalidArgumentError("Request ID missing");
  }
  QUICHE_RETURN_IF_ERROR(
      ReadTrackNamespace(reader, subscribe_namespace.track_namespace_prefix));
  if (!reader.ReadVarInt62(&raw_option)) {
    return absl::InvalidArgumentError("SUBSCRIBE_NAMESPACE option missing");
  }
  if (raw_option > kMaxSubscribeOption) {
    return absl::InvalidArgumentError("Invalid SUBSCRIBE_NAMESPACE option");
  }
  subscribe_namespace.subscribe_options =
      static_cast<SubscribeNamespaceOption>(raw_option);
  QUICHE_RETURN_IF_ERROR(
      FillAndValidateMessageParameters(reader, subscribe_namespace.parameters));
  visitor_.OnSubscribeNamespaceMessage(subscribe_namespace);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessMaxRequestId(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtMaxRequestId max_request_id;
  if (!reader.ReadVarInt62(&max_request_id.max_request_id)) {
    return absl::InvalidArgumentError("Max request ID missing");
  }
  visitor_.OnMaxRequestIdMessage(max_request_id);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessFetch(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtFetch fetch;
  uint64_t type;
  if (!reader.ReadVarInt62(&fetch.request_id) || !reader.ReadVarInt62(&type)) {
    return absl::InvalidArgumentError("Message missing fields");
  }
  switch (static_cast<FetchType>(type)) {
    case FetchType::kAbsoluteJoining: {
      uint64_t joining_request_id;
      uint64_t joining_start;
      if (!reader.ReadVarInt62(&joining_request_id) ||
          !reader.ReadVarInt62(&joining_start)) {
        return absl::InvalidArgumentError(
            "Absolute joining parameters invalid");
      }
      fetch.fetch = JoiningFetchAbsolute{joining_request_id, joining_start};
      break;
    }
    case FetchType::kRelativeJoining: {
      uint64_t joining_request_id;
      uint64_t joining_start;
      if (!reader.ReadVarInt62(&joining_request_id) ||
          !reader.ReadVarInt62(&joining_start)) {
        return absl::InvalidArgumentError(
            "Relative joining parameters invalid");
      }
      fetch.fetch = JoiningFetchRelative{joining_request_id, joining_start};
      break;
    }
    case FetchType::kStandalone: {
      fetch.fetch = StandaloneFetch();
      StandaloneFetch& standalone_fetch =
          std::get<StandaloneFetch>(fetch.fetch);
      QUICHE_RETURN_IF_ERROR(
          ReadFullTrackName(reader, standalone_fetch.full_track_name));
      if (!reader.ReadVarInt62(&standalone_fetch.start_location.group) ||
          !reader.ReadVarInt62(&standalone_fetch.start_location.object) ||
          !reader.ReadVarInt62(&standalone_fetch.end_location.group) ||
          !reader.ReadVarInt62(&standalone_fetch.end_location.object)) {
        return absl::InvalidArgumentError(
            "Standalone fetch parameters invalid");
      }
      if (standalone_fetch.end_location.object == 0) {
        standalone_fetch.end_location.object = kMaxObjectId;
      } else {
        --standalone_fetch.end_location.object;
      }
      if (standalone_fetch.end_location < standalone_fetch.start_location) {
        return absl::InvalidArgumentError(
            "End object comes before start object in FETCH");
      }
      break;
    }
    default:
      return absl::InvalidArgumentError("Invalid FETCH type");
  }
  QUICHE_RETURN_IF_ERROR(
      FillAndValidateMessageParameters(reader, fetch.parameters));
  visitor_.OnFetchMessage(fetch);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessFetchOk(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtFetchOk fetch_ok;
  uint8_t end_of_track;
  if (!reader.ReadVarInt62(&fetch_ok.request_id) ||
      !reader.ReadUInt8(&end_of_track) ||
      !reader.ReadVarInt62(&fetch_ok.end_location.group) ||
      !reader.ReadVarInt62(&fetch_ok.end_location.object)) {
    return absl::InvalidArgumentError("Message missing fields");
  }
  if (end_of_track > 0x01) {
    return absl::InvalidArgumentError("Invalid end of track value in FETCH_OK");
  }
  if (fetch_ok.end_location.object == 0) {
    fetch_ok.end_location.object = kMaxObjectId;
  } else {
    --fetch_ok.end_location.object;
  }
  fetch_ok.end_of_track = end_of_track == 1;
  QUICHE_RETURN_IF_ERROR(
      FillAndValidateMessageParameters(reader, fetch_ok.parameters));
  QUICHE_RETURN_IF_ERROR(
      ParseKeyValuePairListWithNoPrefix(reader, fetch_ok.extensions));
  if (!fetch_ok.extensions.Validate()) {
    return absl::InvalidArgumentError("Invalid FETCH_OK track extensions");
  }
  visitor_.OnFetchOkMessage(fetch_ok);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessFetchCancel(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtFetchCancel fetch_cancel;
  if (!reader.ReadVarInt62(&fetch_cancel.request_id)) {
    return absl::InvalidArgumentError("Request ID missing");
  }
  visitor_.OnFetchCancelMessage(fetch_cancel);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessRequestsBlocked(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtRequestsBlocked requests_blocked;
  if (!reader.ReadVarInt62(&requests_blocked.max_request_id)) {
    return absl::InvalidArgumentError("Max request ID missing");
  }
  visitor_.OnRequestsBlockedMessage(requests_blocked);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessPublish(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtPublish publish;
  QUICHE_DCHECK(reader.PreviouslyReadPayload().empty());
  if (!reader.ReadVarInt62(&publish.request_id)) {
    return absl::InvalidArgumentError("Request ID missing");
  }
  QUICHE_RETURN_IF_ERROR(ReadFullTrackName(reader, publish.full_track_name));
  if (!reader.ReadVarInt62(&publish.track_alias)) {
    return absl::InvalidArgumentError("Track alias missing");
  }
  QUICHE_RETURN_IF_ERROR(
      FillAndValidateMessageParameters(reader, publish.parameters));
  QUICHE_RETURN_IF_ERROR(
      ParseKeyValuePairListWithNoPrefix(reader, publish.extensions));
  if (!publish.extensions.Validate()) {
    return absl::InvalidArgumentError("Invalid PUBLISH track extensions");
  }
  visitor_.OnPublishMessage(publish);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessPublishOk(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtPublishOk publish_ok;
  if (!reader.ReadVarInt62(&publish_ok.request_id)) {
    return absl::InvalidArgumentError("Message missing fields");
  }
  QUICHE_RETURN_IF_ERROR(
      FillAndValidateMessageParameters(reader, publish_ok.parameters));
  visitor_.OnPublishOkMessage(publish_ok);
  return CheckForTrailingData(reader);
}

absl::Status MoqtControlParser::ProcessObjectAck(absl::string_view data) {
  quic::QuicDataReader reader(data);
  MoqtObjectAck object_ack;
  uint64_t raw_delta;
  if (!reader.ReadVarInt62(&object_ack.subscribe_id) ||
      !reader.ReadVarInt62(&object_ack.group_id) ||
      !reader.ReadVarInt62(&object_ack.object_id) ||
      !reader.ReadVarInt62(&raw_delta)) {
    return absl::InvalidArgumentError("Message missing fields");
  }
  object_ack.delta_from_deadline = quic::QuicTimeDelta::FromMicroseconds(
      SignedVarintUnserializedForm(raw_delta));
  visitor_.OnObjectAckMessage(object_ack);
  return CheckForTrailingData(reader);
}

void MoqtControlParser::ParseError(absl::string_view reason) {
  ParseError(MoqtError::kProtocolViolation, reason);
}

void MoqtControlParser::ParseError(const absl::Status& status) {
  ParseError(
      GetMoqtErrorForStatus(status).value_or(MoqtError::kProtocolViolation),
      status.message());
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

absl::Status MoqtControlParser::ReadTrackNamespace(
    quic::QuicDataReader& reader, TrackNamespace& track_namespace) {
  QUICHE_DCHECK(track_namespace.empty());
  uint64_t num_elements;
  if (!reader.ReadVarInt62(&num_elements)) {
    return absl::InvalidArgumentError(
        "Unable to parse the number of namespace elements");
  }
  if (num_elements == 0 || num_elements > kMaxNamespaceElements) {
    return absl::InvalidArgumentError("Invalid number of namespace elements");
  }
  absl::FixedArray<absl::string_view> elements(num_elements);
  for (uint64_t i = 0; i < num_elements; ++i) {
    if (!reader.ReadStringPieceVarInt62(&elements[i])) {
      return absl::InvalidArgumentError(
          "Namespace element shorter than specified");
    }
  }
  if (!track_namespace.Append(elements)) {
    return absl::InvalidArgumentError("Track namespace is too large");
  }
  return absl::OkStatus();
}

absl::Status MoqtControlParser::ReadFullTrackName(
    quic::QuicDataReader& reader, FullTrackName& full_track_name) {
  QUICHE_DCHECK(!full_track_name.IsValid());
  TrackNamespace track_namespace;
  QUICHE_RETURN_IF_ERROR(ReadTrackNamespace(reader, track_namespace));
  absl::string_view name;
  if (!reader.ReadStringPieceVarInt62(&name)) {
    return absl::InvalidArgumentError("Unable to parse track name");
  }
  absl::StatusOr<FullTrackName> full_track_name_or =
      FullTrackName::Create(std::move(track_namespace), std::string(name));
  QUICHE_RETURN_IF_ERROR(full_track_name_or.status());
  full_track_name = *std::move(full_track_name_or);
  return absl::OkStatus();
}

absl::Status MoqtControlParser::FillAndValidateSetupParameters(
    const KeyValuePairList& in, SetupParameters& out,
    MoqtMessageType message_type) {
  QUICHE_RETURN_IF_ERROR(out.FromKeyValuePairList(in));
  MoqtError error =
      SetupParametersAllowedByMessage(out, message_type, uses_web_transport_);
  if (error != MoqtError::kNoError) {
    return MoqtErrorStatusWithCode("Setup parameter parsing error", error);
  }
  return absl::OkStatus();
}

absl::Status MoqtControlParser::FillAndValidateMessageParameters(
    quic::QuicDataReader& reader, MessageParameters& out) {
  KeyValuePairList pairs;
  QUICHE_RETURN_IF_ERROR(ParseKeyValuePairList(reader, pairs));
  // All parameter types are allowed in all messages.
  QUICHE_RETURN_IF_ERROR(out.FromKeyValuePairList(pairs));
  return absl::OkStatus();
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
                                               MoqtObject& object_metadata,
                                               bool& use_default_priority) {
  uint64_t type_raw, object_status_raw;
  absl::string_view extensions;
  quic::QuicDataReader reader(data);
  object_metadata = MoqtObject();
  if (!reader.ReadVarInt62(&type_raw) ||
      !reader.ReadVarInt62(&object_metadata.track_alias) ||
      !reader.ReadVarInt62(&object_metadata.group_id)) {
    return std::nullopt;
  }

  std::optional<MoqtDatagramType> datagram_type =
      MoqtDatagramType::FromValue(type_raw);
  if (!datagram_type.has_value()) {
    return std::nullopt;
  }
  if (datagram_type->end_of_group()) {
    object_metadata.object_status = MoqtObjectStatus::kEndOfGroup;
    if (datagram_type->has_status()) {
      QUICHE_BUG(Moqt_invalid_datagram_type)
          << "Invalid datagram type: " << type_raw;
      return std::nullopt;
    }
  } else {
    object_metadata.object_status = MoqtObjectStatus::kNormal;
  }
  if (datagram_type->has_object_id()) {
    if (!reader.ReadVarInt62(&object_metadata.object_id)) {
      return std::nullopt;
    }
  } else {
    object_metadata.object_id = 0;
  }
  object_metadata.subgroup_id = std::nullopt;
  use_default_priority = datagram_type->has_default_priority();
  if (!use_default_priority &&
      !reader.ReadUInt8(&object_metadata.publisher_priority)) {
    return std::nullopt;
  }
  if (datagram_type->has_extension()) {
    if (!reader.ReadStringPieceVarInt62(&extensions)) {
      return std::nullopt;
    }
    if (extensions.empty()) {
      // This is a session error.
      return std::nullopt;
    }
    object_metadata.extension_headers = std::string(extensions);
  }
  if (datagram_type->has_status()) {
    object_metadata.payload_length = 0;
    if (!reader.ReadVarInt62(&object_status_raw)) {
      return std::nullopt;
    }
    object_metadata.object_status = IntegerToObjectStatus(object_status_raw);
    return "";
  }
  absl::string_view payload = reader.ReadRemainingPayload();
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
  if (fin_read) {  // FIN received before a complete varint.
    ParseError("FIN after incomplete message");
    return std::nullopt;
  }
  return result;
}

std::optional<uint8_t> MoqtDataParser::ReadUint8NoFin() {
  char buffer[1];
  webtransport::Stream::ReadResult read_result =
      stream_.Read(absl::MakeSpan(buffer));
  if (read_result.bytes_read == 0) {
    return std::nullopt;
  }
  return absl::bit_cast<uint8_t>(buffer[0]);
}

MoqtDataParser::NextInput MoqtDataParser::AdvanceParserState() {
  if (type_.IsFetch()) {
    switch (next_input_) {
      case kStreamType:
        return kRequestId;
      case kRequestId:
        return kSerializationFlags;
      case kSerializationFlags:
        if (fetch_serialization_.has_group_id()) {
          return kGroupId;
        }
        [[fallthrough]];
      case kGroupId:
        if (fetch_serialization_.is_datagram()) {
          metadata_.subgroup_id = std::nullopt;
        } else {
          if (fetch_serialization_.has_subgroup_id()) {
            return kSubgroupId;
          }
          if (fetch_serialization_.prior_subgroup_id_plus_one()) {
            if (!metadata_.subgroup_id.has_value()) {
              ParseError("reference to subgroup ID of prior datagram");
              return kFailed;
            }
            ++(*metadata_.subgroup_id);
          } else if (fetch_serialization_.zero_subgroup_id()) {
            metadata_.subgroup_id = 0;
          } else if (!metadata_.subgroup_id.has_value()) {
            QUICHE_DCHECK(fetch_serialization_.prior_subgroup_id());
            ParseError("reference to subgroup ID of prior datagram");
            return kFailed;
          }
        }
        [[fallthrough]];
      case kSubgroupId:
        if (fetch_serialization_.has_object_id()) {
          return kObjectId;
        }
        ++metadata_.object_id;
        [[fallthrough]];
      case kObjectId:
        if (fetch_serialization_.end_of_non_existent_range() ||
            fetch_serialization_.end_of_unknown_range()) {
          return kSerializationFlags;
        }
        if (fetch_serialization_.has_priority()) {
          return kPublisherPriority;
        }
        [[fallthrough]];
      case kPublisherPriority:
        if (fetch_serialization_.has_extensions()) {
          return kExtensionSize;
        }
        metadata_.extension_headers = "";
        return kObjectPayloadLength;
      case kExtensionBody:
        return kObjectPayloadLength;
      case kData:
        return kSerializationFlags;
      case kTrackAlias:
      case kObjectPayloadLength:
      case kAwaitingNextByte:
      case kStatus:
      case kFailed:
      case kExtensionSize:
      case kPadding:
        QUICHE_NOTREACHED();
        return next_input_;
    }
  }
  switch (next_input_) {
    // The state table is factored into a separate function (rather than
    // inlined) in order to separate the order of elements from the way they are
    // parsed.
    case kStreamType:
      return kTrackAlias;
    case kTrackAlias:
      return kGroupId;
    case kGroupId:
      if (type_.IsSubgroupPresent()) {
        return kSubgroupId;
      }
      if (type_.SubgroupIsZero()) {
        metadata_.subgroup_id = 0;
      }
      [[fallthrough]];
    case kSubgroupId:
      if (!type_.HasDefaultPriority()) {
        return kPublisherPriority;
      }
      metadata_.publisher_priority = default_publisher_priority_;
      [[fallthrough]];
    case kPublisherPriority:
      return kObjectId;
    case kObjectId:
      if (num_objects_read_ == 0 && type_.SubgroupIsFirstObjectId()) {
        metadata_.subgroup_id = metadata_.object_id;
      }
      if (type_.AreExtensionHeadersPresent()) {
        return kExtensionSize;
      }
      [[fallthrough]];
    case kExtensionBody:
      return kObjectPayloadLength;
    case kStatus:
    case kData:
    case kAwaitingNextByte:
      return kObjectId;
    case kRequestId:
    case kSerializationFlags:
    case kExtensionSize:
    case kObjectPayloadLength:
    case kPadding:
    case kFailed:
      // Other transitions are either Fetch-only or handled in
      // ParseNextItemFromStream.
      QUICHE_NOTREACHED();
      return next_input_;
  }
}

void MoqtDataParser::ParseNextItemFromStream() {
  if (CheckForFinWithoutData()) {
    return;
  }
  switch (next_input_) {
    case kStreamType: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (!value_read.has_value()) {
        return;
      }
      std::optional<MoqtDataStreamType> type =
          MoqtDataStreamType::FromValue(*value_read);
      if (!type.has_value()) {
        ParseError("Invalid stream type supplied");
        return;
      }
      type_ = *type;
      if (type_.IsPadding()) {
        next_input_ = kPadding;
        return;
      }
      if (type_.EndOfGroupInStream()) {
        contains_end_of_group_ = true;
      }
      next_input_ = AdvanceParserState();
      return;
    }

    case kRequestId:
    case kTrackAlias: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        metadata_.track_alias = *value_read;
        next_input_ = AdvanceParserState();
      }
      return;
    }

    case kSerializationFlags: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        std::optional<MoqtFetchSerialization> serialization =
            MoqtFetchSerialization::FromValue(*value_read);
        if (!serialization.has_value()) {
          ParseError("Invalid serialization flags");
          return;
        }
        if (num_objects_read_ == 0 &&
            (serialization->prior_subgroup_id() ||
             serialization->prior_subgroup_id_plus_one() ||
             !serialization->has_object_id() ||
             !serialization->has_group_id() ||
             !serialization->has_priority())) {
          ParseError("Invalid serialization flags for first object");
          return;
        }
        fetch_serialization_ = *serialization;
        next_input_ = AdvanceParserState();
      }
      return;
    }

    case kGroupId: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        if (type_.IsFetch() ||
            !fetch_serialization_.end_of_non_existent_range() ||
            !fetch_serialization_.end_of_unknown_range()) {
          // Do not record range indicator group IDs because it will corrupt
          // references to the previous object.
          metadata_.group_id = *value_read;
        }
        next_input_ = AdvanceParserState();
      }
      return;
    }

    case kSubgroupId: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        metadata_.subgroup_id = *value_read;
        next_input_ = AdvanceParserState();
      }
      return;
    }

    case kPublisherPriority: {
      std::optional<uint8_t> value_read = ReadUint8NoFin();
      if (value_read.has_value()) {
        metadata_.publisher_priority = *value_read;
        next_input_ = AdvanceParserState();
      }
      return;
    }

    case kObjectId: {
      std::optional<uint64_t> value_read = ReadVarInt62NoFin();
      if (value_read.has_value()) {
        if (type_.IsFetch() ||
            !fetch_serialization_.end_of_non_existent_range() ||
            !fetch_serialization_.end_of_unknown_range()) {
          // Do not record range indicator object IDs because it will corrupt
          // references to the previous object.
          if (type_.IsSubgroup() && last_object_id_.has_value()) {
            metadata_.object_id = *value_read + *last_object_id_ + 1;
          } else {
            metadata_.object_id = *value_read;
          }
        }
        last_object_id_ = metadata_.object_id;
        next_input_ = AdvanceParserState();
      }
      // TODO(martinduke): Report something if the fetch serialization is an end
      // of range indicator.
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
          next_input_ = type_.IsFetch() ? kSerializationFlags : kStatus;
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
        // TODO(martinduke): If contains_end_of_group_ && fin_read, the track is
        // malformed. There is no API to signal this to the session yet, but the
        // contains_end_of_group_ logic is likely to substantially change in the
        // spec. Don't bother to signal this for now; just ignore that the
        // stream was supposed to conclude with kEndOfGroup and end it with the
        // encoded status instead.
        visitor_.OnObjectMessage(metadata_, "", /*end_of_message=*/true);
        next_input_ = AdvanceParserState();
      }
      if (fin_read) {
        visitor_.OnFin();
        no_more_data_ = true;
        return;
      }
      return;
    }

    case kExtensionBody:
    case kData: {
      while (payload_length_remaining_ > 0) {
        webtransport::Stream::PeekResult peek_result =
            stream_.PeekNextReadableRegion();
        if (!peek_result.has_data()) {
          return;
        }
        size_t chunk_size =
            std::min(payload_length_remaining_, peek_result.peeked_data.size());
        payload_length_remaining_ -= chunk_size;
        bool done = payload_length_remaining_ == 0;
        if (next_input_ == kData) {
          no_more_data_ = peek_result.all_data_received &&
                          chunk_size == stream_.ReadableBytes();
          if (!done && no_more_data_) {
            ParseError("FIN received at an unexpected point in the stream");
            return;
          }
          if (contains_end_of_group_) {
            if (no_more_data_) {
              metadata_.object_status = MoqtObjectStatus::kEndOfGroup;
            } else if (done) {
              // Don't signal done until the next byte arrives.
              next_input_ = kAwaitingNextByte;
              done = false;
            }
          }
          visitor_.OnObjectMessage(
              metadata_, peek_result.peeked_data.substr(0, chunk_size), done);
          if (done) {
            if (no_more_data_) {
              visitor_.OnFin();
            }
            ++num_objects_read_;
            next_input_ = AdvanceParserState();
          }
          if (stream_.SkipBytes(chunk_size) && !no_more_data_) {
            // Although there was no FIN, SkipBytes() can return true if the
            // stream is reset, probably because OnObjectMessage() caused
            // something to happen to the stream or the session.
            no_more_data_ = true;
            if (!done) {
              ParseError("FIN received at an unexpected point in the stream");
            }
          }
        } else {
          absl::StrAppend(&metadata_.extension_headers,
                          peek_result.peeked_data.substr(0, chunk_size));
          if (stream_.SkipBytes(chunk_size)) {
            ParseError("FIN received at an unexpected point in the stream");
            no_more_data_ = true;
            return;
          }
          if (done) {
            next_input_ = AdvanceParserState();
          }
        }
      }
      return;
    }

    case kAwaitingNextByte: {
      QUICHE_NOTREACHED();  // CheckForFinWithoutData() should have handled it.
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
  return ReadDataUntil([this]() { return next_input_ != kStreamType; });
}

void MoqtDataParser::ReadTrackAlias() {
  return ReadDataUntil([this]() { return next_input_ > kTrackAlias; });
}

void MoqtDataParser::ReadAtMostOneObject() {
  const size_t num_objects_read_initial = num_objects_read_;
  return ReadDataUntil(
      [&]() { return num_objects_read_ != num_objects_read_initial; });
}

bool MoqtDataParser::CheckForFinWithoutData() {
  if (!stream_.PeekNextReadableRegion().fin_next) {
    if (next_input_ == kAwaitingNextByte) {
      // Data arrived; the last object was not EndOfGroup.
      visitor_.OnObjectMessage(metadata_, "", /*end_of_message=*/true);
      next_input_ = AdvanceParserState();
      ++num_objects_read_;
    }
    return false;
  }
  no_more_data_ = true;
  const bool valid_state =
      payload_length_remaining_ == 0 &&
      ((type_.IsSubgroup() && next_input_ == kObjectId) ||
       (type_.IsFetch() && next_input_ == kSerializationFlags));
  if (!valid_state) {
    ParseError("FIN received at an unexpected point in the stream");
    return true;
  }
  if (next_input_ == kAwaitingNextByte) {
    metadata_.object_status = MoqtObjectStatus::kEndOfGroup;
    visitor_.OnObjectMessage(metadata_, "", /*end_of_message=*/true);
  }
  visitor_.OnFin();
  return stream_.SkipBytes(0);
}

}  // namespace moqt
