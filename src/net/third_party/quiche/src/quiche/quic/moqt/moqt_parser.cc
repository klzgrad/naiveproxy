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
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace {

uint64_t SignedVarintUnserializedForm(uint64_t value) {
  if (value & 0x01) {
    return -(value >> 1);
  }
  return value >> 1;
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
bool ParseKeyValuePairList(quic::QuicDataReader& reader,
                           KeyValuePairList& list) {
  list.clear();
  uint64_t num_params;
  if (!reader.ReadVarInt62(&num_params)) {
    return false;
  }
  uint64_t type = 0;
  for (uint64_t i = 0; i < num_params; ++i) {
    uint64_t type_diff;
    if (!reader.ReadVarInt62(&type_diff)) {
      return false;
    }
    type += type_diff;
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

bool ParseKeyValuePairListWithNoPrefix(quic::QuicDataReader& reader,
                                       KeyValuePairList& list) {
  list.clear();
  uint64_t type = 0;
  while (reader.BytesRemaining() > 0) {
    uint64_t type_diff;
    if (!reader.ReadVarInt62(&type_diff)) {
      return false;
    }
    type += type_diff;
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

MoqtError ParseAuthTokenParameter(absl::string_view field,
                                  std::vector<AuthToken>& out) {
  quic::QuicDataReader reader(field);
  AuthTokenAliasType alias_type;
  uint64_t alias;
  AuthTokenType type;
  absl::string_view token;
  uint64_t value;
  if (!reader.ReadVarInt62(&value)) {
    return MoqtError::kKeyValueFormattingError;
  }
  alias_type = static_cast<AuthTokenAliasType>(value);
  switch (alias_type) {
    case AuthTokenAliasType::kUseValue:
      if (!reader.ReadVarInt62(&value) ||
          value > AuthTokenType::kMaxAuthTokenType) {
        return MoqtError::kKeyValueFormattingError;
      }
      type = static_cast<AuthTokenType>(value);
      token = reader.PeekRemainingPayload();
      out.push_back(AuthToken(type, token));
      break;
    case AuthTokenAliasType::kUseAlias:
      if (!reader.ReadVarInt62(&value)) {
        return MoqtError::kKeyValueFormattingError;
      }
      out.push_back(AuthToken(value, alias_type));
      break;
    case AuthTokenAliasType::kRegister:
      if (!reader.ReadVarInt62(&alias) || !reader.ReadVarInt62(&value)) {
        return MoqtError::kKeyValueFormattingError;
      }
      type = static_cast<AuthTokenType>(value);
      token = reader.PeekRemainingPayload();
      out.push_back(AuthToken(alias, type, token));
      break;
    case AuthTokenAliasType::kDelete:
      if (!reader.ReadVarInt62(&alias)) {
        return MoqtError::kKeyValueFormattingError;
      }
      out.push_back(AuthToken(alias, alias_type));
      break;
    default:  // invalid alias type
      return MoqtError::kKeyValueFormattingError;
  }
  return MoqtError::kNoError;
}

MoqtError ParseLocation(absl::string_view field, Location& out) {
  quic::QuicDataReader reader(field);
  if (!reader.ReadVarInt62(&out.group) || !reader.ReadVarInt62(&out.object)) {
    return MoqtError::kKeyValueFormattingError;
  }
  return MoqtError::kNoError;
}

MoqtError ParseSubscriptionFilter(absl::string_view field,
                                  std::optional<SubscriptionFilter>& out) {
  quic::QuicDataReader reader(field);
  uint64_t value;
  if (!reader.ReadVarInt62(&value)) {
    return MoqtError::kKeyValueFormattingError;
  }
  uint64_t group, object;
  switch (static_cast<MoqtFilterType>(value)) {
    case MoqtFilterType::kLargestObject:
    case MoqtFilterType::kNextGroupStart:
      out.emplace(static_cast<MoqtFilterType>(value));
      break;
    case MoqtFilterType::kAbsoluteStart:
      if (!reader.ReadVarInt62(&group) || !reader.ReadVarInt62(&object)) {
        return MoqtError::kKeyValueFormattingError;
      }
      out.emplace(Location(group, object));
      break;
    case MoqtFilterType::kAbsoluteRange:
      if (!reader.ReadVarInt62(&group) || !reader.ReadVarInt62(&object) ||
          !reader.ReadVarInt62(&value)) {
        return MoqtError::kKeyValueFormattingError;
      }
      if (value < group) {  // end before start
        return MoqtError::kProtocolViolation;
      }
      out.emplace(Location(group, object), value);
      break;
    default:  // invalid filter type
      return MoqtError::kProtocolViolation;
  }
  return MoqtError::kNoError;
}

}  // namespace

MoqtError SetupParameters::FromKeyValuePairList(const KeyValuePairList& list) {
  MoqtError error = MoqtError::kNoError;
  // If this callback returns false without explicitly setting an error, then
  // the error is a kProtocolViolation.
  bool result = list.ForEach(
      [&](uint64_t key, std::variant<uint64_t, absl::string_view> value) {
        switch (static_cast<SetupParameter>(key)) {
          case SetupParameter::kMaxRequestId:
            if (max_request_id.has_value()) {
              return false;
            }
            max_request_id = std::get<uint64_t>(value);
            break;
          case SetupParameter::kMaxAuthTokenCacheSize:
            if (max_auth_token_cache_size.has_value()) {
              return false;
            }
            max_auth_token_cache_size = std::get<uint64_t>(value);
            break;
          case SetupParameter::kPath:
            if (path.has_value()) {
              return false;
            }
            if (!http2::adapter::HeaderValidator::IsValidPath(
                    std::get<absl::string_view>(value),
                    /*allow_fragment=*/false)) {
              error = MoqtError::kMalformedPath;
              return false;
            }
            path = std::get<absl::string_view>(value);
            break;
          case SetupParameter::kAuthorizationToken:
            error = ParseAuthTokenParameter(std::get<absl::string_view>(value),
                                            authorization_tokens);
            if (error != MoqtError::kNoError) {
              return false;
            }
            break;
          case SetupParameter::kAuthority:
            if (!http2::adapter::HeaderValidator::IsValidAuthority(
                    std::get<absl::string_view>(value))) {
              error = MoqtError::kMalformedAuthority;
              return false;
            }
            authority = std::get<absl::string_view>(value);
            break;
          case SetupParameter::kMoqtImplementation:
            if (moqt_implementation.has_value()) {
              return false;
            }
            QUICHE_LOG(INFO) << "Peer MOQT implementation: "
                             << std::get<absl::string_view>(value);
            moqt_implementation = std::get<absl::string_view>(value);
            break;
          case SetupParameter::kSupportObjectAcks:
            if (support_object_acks.has_value()) {
              return false;
            }
            if (std::get<uint64_t>(value) > 1) {
              error = MoqtError::kKeyValueFormattingError;
              return false;
            }
            support_object_acks = (std::get<uint64_t>(value) == 1);
            break;
          default:
            break;
        }
        return true;
      });
  if (!result && error == MoqtError::kNoError) {
    return MoqtError::kProtocolViolation;
  }
  return error;
}

MoqtError MessageParameters::FromKeyValuePairList(
    const KeyValuePairList& list) {
  MoqtError error = MoqtError::kNoError;
  bool error_occurred = !list.ForEach(
      [&](uint64_t key, std::variant<uint64_t, absl::string_view> value) {
        switch (static_cast<MessageParameter>(key)) {
          case MessageParameter::kDeliveryTimeout:
            if (delivery_timeout.has_value() ||
                std::get<uint64_t>(value) == 0) {
              return false;
            }
            delivery_timeout = quic::QuicTimeDelta::TryFromMilliseconds(
                                   std::get<uint64_t>(value))
                                   .value_or(quic::QuicTimeDelta::Infinite());
            break;
          case MessageParameter::kAuthorizationToken:
            error = ParseAuthTokenParameter(std::get<absl::string_view>(value),
                                            authorization_tokens);
            if (error != MoqtError::kNoError) {
              return false;
            }
            break;
          case MessageParameter::kExpires:
            if (expires.has_value()) {
              return false;
            }
            expires = quic::QuicTimeDelta::TryFromMilliseconds(
                          std::get<uint64_t>(value))
                          .value_or(quic::QuicTimeDelta::Infinite());
            if (expires->IsZero()) {
              expires = quic::QuicTimeDelta::Infinite();
            }
            break;
          case MessageParameter::kLargestObject:
            if (largest_object.has_value()) {
              return false;
            }
            largest_object = Location();
            error = ParseLocation(std::get<absl::string_view>(value),
                                  *largest_object);
            if (error != MoqtError::kNoError) {
              return false;
            }
            break;
          case MessageParameter::kForward:
            if (forward_has_value() || std::get<uint64_t>(value) > 1) {
              return false;
            }
            set_forward(std::get<uint64_t>(value) != 0);
            break;
          case MessageParameter::kSubscriberPriority:
            if (subscriber_priority.has_value() ||
                std::get<uint64_t>(value) > kMaxPriority) {
              return false;
            }
            subscriber_priority =
                static_cast<MoqtPriority>(std::get<uint64_t>(value));
            break;
          case MessageParameter::kSubscriptionFilter:
            if (subscription_filter.has_value()) {
              // TODO(martinduke): Support multiple subscription filters.
              return false;
            }
            error = ParseSubscriptionFilter(std::get<absl::string_view>(value),
                                            subscription_filter);
            if (error != MoqtError::kNoError) {
              return false;
            }
            break;
          case MessageParameter::kGroupOrder:
            if (group_order.has_value() ||
                std::get<uint64_t>(value) > kMaxMoqtDeliveryOrder ||
                std::get<uint64_t>(value) < kMinMoqtDeliveryOrder) {
              return false;
            }
            group_order =
                static_cast<MoqtDeliveryOrder>(std::get<uint64_t>(value));
            break;
          case MessageParameter::kNewGroupRequest:
            if (new_group_request.has_value()) {
              return false;
            }
            new_group_request = std::get<uint64_t>(value);
            break;
          case MessageParameter::kOackWindowSize:
            if (oack_window_size.has_value()) {
              return false;
            }
            oack_window_size = quic::QuicTimeDelta::FromMicroseconds(
                std::get<uint64_t>(value));
            break;
          default:
            // Unknown MessageParameters not allowed!
            return false;
        }
        return true;
      });
  if (error_occurred && error == MoqtError::kNoError) {
    // Illegal duplicate parameter.
    return MoqtError::kProtocolViolation;
  }
  return error;
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
    case MoqtMessageType::kRequestOk:
      bytes_read = ProcessRequestOk(reader);
      break;
    case MoqtMessageType::kRequestError:
      bytes_read = ProcessRequestError(reader);
      break;
    case MoqtMessageType::kSubscribe:
      bytes_read = ProcessSubscribe(reader);
      break;
    case MoqtMessageType::kSubscribeOk:
      bytes_read = ProcessSubscribeOk(reader);
      break;
    case MoqtMessageType::kUnsubscribe:
      bytes_read = ProcessUnsubscribe(reader);
      break;
    case MoqtMessageType::kPublishDone:
      bytes_read = ProcessPublishDone(reader);
      break;
    case MoqtMessageType::kRequestUpdate:
      bytes_read = ProcessRequestUpdate(reader);
      break;
    case MoqtMessageType::kPublishNamespace:
      bytes_read = ProcessPublishNamespace(reader);
      break;
    case MoqtMessageType::kPublishNamespaceDone:
      bytes_read = ProcessPublishNamespaceDone(reader);
      break;
    case MoqtMessageType::kNamespace:
      bytes_read = ProcessNamespace(reader);
      break;
    case MoqtMessageType::kNamespaceDone:
      bytes_read = ProcessNamespaceDone(reader);
      break;
    case MoqtMessageType::kPublishNamespaceCancel:
      bytes_read = ProcessPublishNamespaceCancel(reader);
      break;
    case MoqtMessageType::kTrackStatus:
      bytes_read = ProcessTrackStatus(reader);
      break;
    case MoqtMessageType::kGoAway:
      bytes_read = ProcessGoAway(reader);
      break;
    case MoqtMessageType::kSubscribeNamespace:
      bytes_read = ProcessSubscribeNamespace(reader);
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
    case MoqtMessageType::kRequestsBlocked:
      bytes_read = ProcessRequestsBlocked(reader);
      break;
    case MoqtMessageType::kPublish:
      bytes_read = ProcessPublish(reader);
      break;
    case MoqtMessageType::kPublishOk:
      bytes_read = ProcessPublishOk(reader);
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
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (!FillAndValidateSetupParameters(parameters, setup.parameters,
                                      MoqtMessageType::kClientSetup)) {
    return 0;
  }
  // TODO(martinduke): Validate construction of the PATH (Sec 8.3.2.1)
  visitor_.OnClientSetupMessage(setup);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessServerSetup(quic::QuicDataReader& reader) {
  MoqtServerSetup setup;
  KeyValuePairList parameters;
  if (!ParseKeyValuePairList(reader, parameters)) {
    return 0;
  }
  if (!FillAndValidateSetupParameters(parameters, setup.parameters,
                                      MoqtMessageType::kServerSetup)) {
    return 0;
  }
  visitor_.OnServerSetupMessage(setup);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribe(quic::QuicDataReader& reader,
                                           MoqtMessageType message_type) {
  MoqtSubscribe subscribe;
  if (!reader.ReadVarInt62(&subscribe.request_id) ||
      !ReadFullTrackName(reader, subscribe.full_track_name)) {
    return 0;
  }
  if (!FillAndValidateMessageParameters(reader, subscribe.parameters)) {
    return 0;
  }
  if (message_type == MoqtMessageType::kTrackStatus) {
    visitor_.OnTrackStatusMessage(subscribe);
  } else {
    visitor_.OnSubscribeMessage(subscribe);
  }
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeOk(quic::QuicDataReader& reader) {
  MoqtSubscribeOk subscribe_ok;
  if (!reader.ReadVarInt62(&subscribe_ok.request_id) ||
      !reader.ReadVarInt62(&subscribe_ok.track_alias)) {
    return 0;
  }
  KeyValuePairList pairs;
  if (!ParseKeyValuePairList(reader, pairs)) {
    return 0;
  }
  MoqtError error = subscribe_ok.parameters.FromKeyValuePairList(pairs);
  if (error != MoqtError::kNoError) {
    ParseError(error, "Failed to parse SUBSCRIBE_OK message parameters");
    return 0;
  }
  if (!ParseKeyValuePairListWithNoPrefix(reader, subscribe_ok.extensions)) {
    return 0;
  }
  if (!subscribe_ok.extensions.Validate()) {
    ParseError("Invalid SUBSCRIBE_OK track extensions");
    return 0;
  }
  visitor_.OnSubscribeOkMessage(subscribe_ok);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessRequestError(quic::QuicDataReader& reader) {
  MoqtRequestError request_error;
  uint64_t error_code;
  uint64_t raw_interval;
  if (!reader.ReadVarInt62(&request_error.request_id) ||
      !reader.ReadVarInt62(&error_code) ||
      !reader.ReadVarInt62(&raw_interval) ||
      !reader.ReadStringVarInt62(request_error.reason_phrase)) {
    return 0;
  }
  request_error.error_code = static_cast<RequestErrorCode>(error_code);
  request_error.retry_interval =
      (raw_interval == 0)
          ? std::nullopt
          : std::make_optional(
                quic::QuicTimeDelta::FromMilliseconds(raw_interval - 1));
  visitor_.OnRequestErrorMessage(request_error);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessUnsubscribe(quic::QuicDataReader& reader) {
  MoqtUnsubscribe unsubscribe;
  if (!reader.ReadVarInt62(&unsubscribe.request_id)) {
    return 0;
  }
  visitor_.OnUnsubscribeMessage(unsubscribe);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessPublishDone(quic::QuicDataReader& reader) {
  MoqtPublishDone publish_done;
  uint64_t value;
  if (!reader.ReadVarInt62(&publish_done.request_id) ||
      !reader.ReadVarInt62(&value) ||
      !reader.ReadVarInt62(&publish_done.stream_count) ||
      !reader.ReadStringVarInt62(publish_done.error_reason)) {
    return 0;
  }
  publish_done.status_code = static_cast<PublishDoneCode>(value);
  visitor_.OnPublishDoneMessage(publish_done);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessRequestUpdate(quic::QuicDataReader& reader) {
  MoqtRequestUpdate request_update;
  if (!reader.ReadVarInt62(&request_update.request_id) ||
      !reader.ReadVarInt62(&request_update.existing_request_id)) {
    return 0;
  }
  if (!FillAndValidateMessageParameters(reader, request_update.parameters)) {
    return 0;
  }
  visitor_.OnRequestUpdateMessage(request_update);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessPublishNamespace(
    quic::QuicDataReader& reader) {
  MoqtPublishNamespace publish_namespace;
  if (!reader.ReadVarInt62(&publish_namespace.request_id) ||
      !ReadTrackNamespace(reader, publish_namespace.track_namespace)) {
    return 0;
  }
  if (!FillAndValidateMessageParameters(reader, publish_namespace.parameters)) {
    return 0;
  }
  visitor_.OnPublishNamespaceMessage(publish_namespace);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessNamespace(quic::QuicDataReader& reader) {
  MoqtNamespace _namespace;
  if (!ReadTrackNamespace(reader, _namespace.track_namespace_suffix)) {
    return 0;
  }
  visitor_.OnNamespaceMessage(_namespace);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessNamespaceDone(quic::QuicDataReader& reader) {
  MoqtNamespaceDone namespace_done;
  if (!ReadTrackNamespace(reader, namespace_done.track_namespace_suffix)) {
    return 0;
  }
  visitor_.OnNamespaceDoneMessage(namespace_done);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessRequestOk(quic::QuicDataReader& reader) {
  MoqtRequestOk request_ok;
  if (!reader.ReadVarInt62(&request_ok.request_id)) {
    return 0;
  }
  if (!FillAndValidateMessageParameters(reader, request_ok.parameters)) {
    return 0;
  }
  visitor_.OnRequestOkMessage(request_ok);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessPublishNamespaceDone(
    quic::QuicDataReader& reader) {
  MoqtPublishNamespaceDone pn_done;
  if (!reader.ReadVarInt62(&pn_done.request_id)) {
    return 0;
  }
  visitor_.OnPublishNamespaceDoneMessage(pn_done);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessPublishNamespaceCancel(
    quic::QuicDataReader& reader) {
  MoqtPublishNamespaceCancel publish_namespace_cancel;
  uint64_t error_code;
  if (!reader.ReadVarInt62(&publish_namespace_cancel.request_id) ||
      !reader.ReadVarInt62(&error_code) ||
      !reader.ReadStringVarInt62(publish_namespace_cancel.error_reason)) {
    return 0;
  }
  publish_namespace_cancel.error_code =
      static_cast<RequestErrorCode>(error_code);
  visitor_.OnPublishNamespaceCancelMessage(publish_namespace_cancel);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessTrackStatus(quic::QuicDataReader& reader) {
  return ProcessSubscribe(reader, MoqtMessageType::kTrackStatus);
}

size_t MoqtControlParser::ProcessGoAway(quic::QuicDataReader& reader) {
  MoqtGoAway goaway;
  if (!reader.ReadStringVarInt62(goaway.new_session_uri)) {
    return 0;
  }
  visitor_.OnGoAwayMessage(goaway);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessSubscribeNamespace(
    quic::QuicDataReader& reader) {
  MoqtSubscribeNamespace subscribe_namespace;
  uint64_t raw_option;
  if (!reader.ReadVarInt62(&subscribe_namespace.request_id) ||
      !ReadTrackNamespace(reader, subscribe_namespace.track_namespace_prefix) ||
      !reader.ReadVarInt62(&raw_option)) {
    return 0;
  }
  if (raw_option > kMaxSubscribeOption) {
    ParseError("Invalid SUBSCRIBE_NAMESPACE option");
    return 0;
  }
  subscribe_namespace.subscribe_options =
      static_cast<SubscribeNamespaceOption>(raw_option);
  if (!FillAndValidateMessageParameters(reader,
                                        subscribe_namespace.parameters)) {
    return 0;
  }
  visitor_.OnSubscribeNamespaceMessage(subscribe_namespace);
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
  uint64_t type;
  if (!reader.ReadVarInt62(&fetch.request_id) || !reader.ReadVarInt62(&type)) {
    return 0;
  }
  switch (static_cast<FetchType>(type)) {
    case FetchType::kAbsoluteJoining: {
      uint64_t joining_request_id;
      uint64_t joining_start;
      if (!reader.ReadVarInt62(&joining_request_id) ||
          !reader.ReadVarInt62(&joining_start)) {
        return 0;
      }
      fetch.fetch = JoiningFetchAbsolute{joining_request_id, joining_start};
      break;
    }
    case FetchType::kRelativeJoining: {
      uint64_t joining_request_id;
      uint64_t joining_start;
      if (!reader.ReadVarInt62(&joining_request_id) ||
          !reader.ReadVarInt62(&joining_start)) {
        return 0;
      }
      fetch.fetch = JoiningFetchRelative{joining_request_id, joining_start};
      break;
    }
    case FetchType::kStandalone: {
      fetch.fetch = StandaloneFetch();
      StandaloneFetch& standalone_fetch =
          std::get<StandaloneFetch>(fetch.fetch);
      if (!ReadFullTrackName(reader, standalone_fetch.full_track_name) ||
          !reader.ReadVarInt62(&standalone_fetch.start_location.group) ||
          !reader.ReadVarInt62(&standalone_fetch.start_location.object) ||
          !reader.ReadVarInt62(&standalone_fetch.end_location.group) ||
          !reader.ReadVarInt62(&standalone_fetch.end_location.object)) {
        return 0;
      }
      if (standalone_fetch.end_location.object == 0) {
        standalone_fetch.end_location.object = kMaxObjectId;
      } else {
        --standalone_fetch.end_location.object;
      }
      if (standalone_fetch.end_location < standalone_fetch.start_location) {
        ParseError("End object comes before start object in FETCH");
        return 0;
      }
      break;
    }
    default:
      ParseError("Invalid FETCH type");
      return 0;
  }
  if (!FillAndValidateMessageParameters(reader, fetch.parameters)) {
    return 0;
  }
  visitor_.OnFetchMessage(fetch);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessFetchOk(quic::QuicDataReader& reader) {
  MoqtFetchOk fetch_ok;
  uint8_t end_of_track;
  if (!reader.ReadVarInt62(&fetch_ok.request_id) ||
      !reader.ReadUInt8(&end_of_track) ||
      !reader.ReadVarInt62(&fetch_ok.end_location.group) ||
      !reader.ReadVarInt62(&fetch_ok.end_location.object)) {
    return 0;
  }
  if (end_of_track > 0x01) {
    ParseError("Invalid end of track value in FETCH_OK");
    return 0;
  }
  if (fetch_ok.end_location.object == 0) {
    fetch_ok.end_location.object = kMaxObjectId;
  } else {
    --fetch_ok.end_location.object;
  }
  fetch_ok.end_of_track = end_of_track == 1;
  if (!FillAndValidateMessageParameters(reader, fetch_ok.parameters)) {
    return 0;
  }
  if (!ParseKeyValuePairListWithNoPrefix(reader, fetch_ok.extensions)) {
    return 0;
  }
  if (!fetch_ok.extensions.Validate()) {
    ParseError("Invalid FETCH_OK track extensions");
    return 0;
  }
  visitor_.OnFetchOkMessage(fetch_ok);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessFetchCancel(quic::QuicDataReader& reader) {
  MoqtFetchCancel fetch_cancel;
  if (!reader.ReadVarInt62(&fetch_cancel.request_id)) {
    return 0;
  }
  visitor_.OnFetchCancelMessage(fetch_cancel);
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

size_t MoqtControlParser::ProcessPublish(quic::QuicDataReader& reader) {
  MoqtPublish publish;
  QUICHE_DCHECK(reader.PreviouslyReadPayload().empty());
  if (!reader.ReadVarInt62(&publish.request_id) ||
      !ReadFullTrackName(reader, publish.full_track_name) ||
      !reader.ReadVarInt62(&publish.track_alias)) {
    return 0;
  }
  if (!FillAndValidateMessageParameters(reader, publish.parameters)) {
    return 0;
  }
  if (!ParseKeyValuePairListWithNoPrefix(reader, publish.extensions)) {
    return 0;
  }
  if (!publish.extensions.Validate()) {
    ParseError("Invalid PUBLISH track extensions");
    return 0;
  }
  visitor_.OnPublishMessage(publish);
  return reader.PreviouslyReadPayload().length();
}

size_t MoqtControlParser::ProcessPublishOk(quic::QuicDataReader& reader) {
  MoqtPublishOk publish_ok;
  if (!reader.ReadVarInt62(&publish_ok.request_id)) {
    return 0;
  }
  if (!FillAndValidateMessageParameters(reader, publish_ok.parameters)) {
    return 0;
  }
  visitor_.OnPublishOkMessage(publish_ok);
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
                                           TrackNamespace& track_namespace) {
  QUICHE_DCHECK(track_namespace.empty());
  uint64_t num_elements;
  if (!reader.ReadVarInt62(&num_elements)) {
    return false;
  }
  if (num_elements == 0 || num_elements > kMaxNamespaceElements) {
    ParseError(MoqtError::kProtocolViolation,
               "Invalid number of namespace elements");
    return false;
  }
  absl::FixedArray<absl::string_view> elements(num_elements);
  for (uint64_t i = 0; i < num_elements; ++i) {
    if (!reader.ReadStringPieceVarInt62(&elements[i])) {
      return false;
    }
  }
  if (!track_namespace.Append(elements)) {
    ParseError(MoqtError::kProtocolViolation, "Track namespace is too large");
    return false;
  }
  return true;
}

bool MoqtControlParser::ReadFullTrackName(quic::QuicDataReader& reader,
                                          FullTrackName& full_track_name) {
  QUICHE_DCHECK(!full_track_name.IsValid());
  TrackNamespace track_namespace;
  if (!ReadTrackNamespace(reader, track_namespace)) {
    return false;
  }
  absl::string_view name;
  if (!reader.ReadStringPieceVarInt62(&name)) {
    return false;
  }
  absl::StatusOr<FullTrackName> full_track_name_or =
      FullTrackName::Create(std::move(track_namespace), std::string(name));
  if (!full_track_name_or.ok()) {
    ParseError(MoqtError::kProtocolViolation,
               full_track_name_or.status().message());
    return false;
  }
  full_track_name = *std::move(full_track_name_or);
  return true;
}

bool MoqtControlParser::FillAndValidateSetupParameters(
    const KeyValuePairList& in, SetupParameters& out,
    MoqtMessageType message_type) {
  MoqtError error = out.FromKeyValuePairList(in);
  if (error != MoqtError::kNoError) {
    absl::string_view error_message = (error == MoqtError::kProtocolViolation)
                                          ? "Duplicate Setup Parameter"
                                          : "Setup Parameter parsing error";
    ParseError(error, error_message);
    return false;
  }
  error =
      SetupParametersAllowedByMessage(out, message_type, uses_web_transport_);
  if (error != MoqtError::kNoError) {
    ParseError(error, "");
    return false;
  }
  return true;
}

bool MoqtControlParser::FillAndValidateMessageParameters(
    quic::QuicDataReader& reader, MessageParameters& out) {
  KeyValuePairList pairs;
  if (!ParseKeyValuePairList(reader, pairs)) {
    return false;
  }
  MoqtError error = out.FromKeyValuePairList(pairs);
  if (error != MoqtError::kNoError) {
    absl::string_view error_message = (error == MoqtError::kProtocolViolation)
                                          ? "Duplicate Message Parameter"
                                          : "Message Parameter parsing error";
    ParseError(error, error_message);
    return false;
  }
  // All parameter types are allowed in all messages.
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
