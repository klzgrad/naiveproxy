// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_endian.h"

namespace moqt::test {

inline constexpr absl::string_view kDefaultExtensionBlob(
    "\x00\x0c\x01\x03\x66\x6f\x6f", 7);

inline std::vector<MoqtDatagramType> AllMoqtDatagramTypes() {
  std::vector<MoqtDatagramType> types;
  for (bool payload : {false, true}) {
    for (bool extension : {false, true}) {
      for (bool end_of_group : {false, true}) {
        for (bool default_priority : {false, true}) {
          for (bool zero_object_id : {false, true}) {
            types.push_back(MoqtDatagramType(payload, extension, end_of_group,
                                             default_priority, zero_object_id));
          }
        }
      }
    }
  }
  return types;
}

inline std::vector<MoqtFetchSerialization> AllMoqtFetchSerializations() {
  std::vector<MoqtFetchSerialization> serializations;
  for (uint64_t i = 0; i < 128; ++i) {
    std::optional<MoqtFetchSerialization> value =
        MoqtFetchSerialization::FromValue(i);
    if (value.has_value()) {
      serializations.push_back(*value);
    } else {
      break;
    }
  }
  return serializations;
}

inline std::vector<MoqtDataStreamType> AllMoqtDataStreamTypes() {
  std::vector<MoqtDataStreamType> types;
  types.push_back(MoqtDataStreamType::Fetch());
  uint64_t first_object_id = 1;
  for (uint64_t subgroup_id : {0, 1, 2}) {
    for (bool no_extension_headers : {true, false}) {
      for (bool default_priority : {true, false}) {
        for (bool end_of_group : {false, true}) {
          types.push_back(MoqtDataStreamType::Subgroup(
              subgroup_id, first_object_id, no_extension_headers,
              default_priority, end_of_group));
        }
      }
    }
  }
  return types;
}

inline MessageParameters SubscribeForTest() {
  MessageParameters parameters;
  parameters.delivery_timeout = quic::QuicTimeDelta::FromMilliseconds(10000);
  parameters.authorization_tokens.emplace_back(AuthTokenType::kOutOfBand,
                                               "bar");
  parameters.set_forward(true);
  parameters.subscriber_priority = 0x20;
  parameters.subscription_filter.emplace(Location(4, 1));
  parameters.group_order = MoqtDeliveryOrder::kDescending;
  return parameters;
}

constexpr absl::string_view kTestImplementationString =
    "Moq Test Implementation Type";

// Base class containing a wire image and the corresponding structured
// representation of an example of each message. It allows parser and framer
// tests to iterate through all message types without much specialized code.
class QUICHE_NO_EXPORT TestMessageBase {
 public:
  virtual ~TestMessageBase() = default;

  using MessageStructuredData = std::variant<
      MoqtClientSetup, MoqtServerSetup, MoqtObject, MoqtRequestOk,
      MoqtRequestError, MoqtSubscribe, MoqtSubscribeOk, MoqtUnsubscribe,
      MoqtPublishDone, MoqtRequestUpdate, MoqtPublishNamespace,
      MoqtPublishNamespaceDone, MoqtPublishNamespaceCancel, MoqtTrackStatus,
      MoqtGoAway, MoqtSubscribeNamespace, MoqtMaxRequestId, MoqtFetch,
      MoqtFetchCancel, MoqtFetchOk, MoqtRequestsBlocked, MoqtPublish,
      MoqtPublishOk, MoqtNamespace, MoqtNamespaceDone, MoqtObjectAck>;

  // The total actual size of the message.
  size_t total_message_size() const { return wire_image_size_; }

  absl::string_view PacketSample() const {
    return absl::string_view(wire_image_, wire_image_size_);
  }

  void set_wire_image_size(size_t wire_image_size) {
    wire_image_size_ = wire_image_size;
  }

  // Returns a copy of the structured data for the message.
  virtual MessageStructuredData structured_data() const = 0;

  // Compares |values| to the derived class's structured data to make sure
  // they are equal.
  virtual bool EqualFieldValues(MessageStructuredData& values) const = 0;

  // Expand all varints in the message. This is pure virtual because each
  // message has a different layout of varints.
  virtual void ExpandVarints() = 0;

  // This will cause a parsing error. Do not call this on Object Messages.
  void DecreasePayloadLengthByOne() {
    size_t length_offset =
        0x1 << ((static_cast<uint8_t>(wire_image_[0]) & 0xc0) >> 6);
    wire_image_[length_offset + 1]--;
  }
  void IncreasePayloadLengthByOne() {
    size_t length_offset =
        0x1 << ((static_cast<uint8_t>(wire_image_[0]) & 0xc0) >> 6);
    wire_image_[length_offset + 1]++;
    set_wire_image_size(wire_image_size_ + 1);
  }

  // Objects might need a different status if at the end of the stream.
  virtual void MakeObjectEndOfStream() {
    QUIC_LOG(INFO) << "MakeObjectEndOfStream not implemented";
  }

 protected:
  void SetWireImage(uint8_t* wire_image, size_t wire_image_size) {
    memcpy(wire_image_, wire_image, wire_image_size);
    wire_image_size_ = wire_image_size;
  }
  void SetByte(size_t offset, char value) { wire_image_[offset] = value; }

  // Expands all the varints in the message, alternating between making them 2,
  // 4, and 8 bytes long. Updates length fields accordingly.
  // Each character in |varints| corresponds to a byte in the original message
  // payload.
  // If there is a 'v', it is a varint that should be expanded. If '-', skip
  // to the next byte.
  void ExpandVarintsImpl(absl::string_view varints,
                         bool is_control_message = true) {
    int next_varint_len = 2;
    char new_wire_image[kMaxMessageHeaderSize + 1];
    quic::QuicDataReader reader(
        absl::string_view(wire_image_, wire_image_size_));
    quic::QuicDataWriter writer(sizeof(new_wire_image), new_wire_image);
    size_t length_field = 0;
    if (is_control_message) {
      uint8_t type_length = static_cast<uint8_t>(reader.PeekVarInt62Length());
      uint64_t type;
      reader.ReadVarInt62(&type);
      if (type_length == 1) {
        // Expand the message type.
        type_length = next_varint_len;
        writer.WriteVarInt62WithForcedLength(
            type, static_cast<quiche::QuicheVariableLengthIntegerLength>(
                      type_length));
        next_varint_len = 4;
      } else {
        writer.WriteVarInt62(type);
      }
      length_field = writer.length();
      uint16_t size;
      reader.ReadUInt16(&size);
      writer.WriteUInt16(size);
    }
    size_t i = 0;
    while (!reader.IsDoneReading()) {
      if (i >= (varints.length()) || varints[i++] == '-') {
        uint8_t byte;
        reader.ReadUInt8(&byte);
        writer.WriteUInt8(byte);
        continue;
      }
      uint64_t value;
      reader.ReadVarInt62(&value);
      writer.WriteVarInt62WithForcedLength(
          value, static_cast<quiche::QuicheVariableLengthIntegerLength>(
                     next_varint_len));
      next_varint_len *= 2;
      if (next_varint_len == 16) {
        next_varint_len = 2;
      }
    }
    memcpy(wire_image_, new_wire_image, writer.length());
    wire_image_size_ = writer.length();
    if (is_control_message) {  // First byte will be empty.
      quic::QuicDataWriter length_writer(writer.length(),
                                         &wire_image_[length_field]);
      length_writer.WriteUInt16(writer.length() - length_field - 2);
    }
  }

 private:
  char wire_image_[kMaxMessageHeaderSize + 20];
  size_t wire_image_size_;
};

// Base class for the two subtypes of Object Message.
class QUICHE_NO_EXPORT ObjectMessage : public TestMessageBase {
 public:
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::move(std::get<MoqtObject>(values));
    if (cast.track_alias != object_.track_alias) {
      QUIC_LOG(INFO) << "OBJECT Track ID mismatch";
      return false;
    }
    if (cast.group_id != object_.group_id) {
      QUIC_LOG(INFO) << "OBJECT Group Sequence mismatch";
      return false;
    }
    if (cast.object_id != object_.object_id) {
      QUIC_LOG(INFO) << "OBJECT Object Sequence mismatch";
      return false;
    }
    if (cast.publisher_priority != object_.publisher_priority) {
      QUIC_LOG(INFO) << "OBJECT Publisher Priority mismatch";
      return false;
    }
    if (cast.extension_headers != object_.extension_headers) {
      QUIC_LOG(INFO) << "OBJECT Extension Header mismatch";
      return false;
    }
    if (cast.object_status != object_.object_status) {
      QUIC_LOG(INFO) << "OBJECT Object Status mismatch";
      return false;
    }
    if (cast.subgroup_id != object_.subgroup_id) {
      QUIC_LOG(INFO) << "OBJECT Subgroup ID mismatch";
      return false;
    }
    if (cast.payload_length != object_.payload_length) {
      QUIC_LOG(INFO) << "OBJECT Payload Length mismatch";
      return false;
    }
    return true;
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(object_);
  }

 protected:
  MoqtObject object_ = {
      /*track_alias=*/4,
      /*group_id*/ 5,
      /*object_id=*/6,
      /*publisher_priority=*/7,
      std::string(kDefaultExtensionBlob),
      /*object_status=*/MoqtObjectStatus::kNormal,
      /*subgroup_id=*/8,
      /*payload_length=*/3,
  };
};

class QUICHE_NO_EXPORT ObjectDatagramMessage : public ObjectMessage {
 public:
  ObjectDatagramMessage(MoqtDatagramType datagram_type)
      : ObjectMessage(), datagram_type_(datagram_type) {
    // Update ObjectMessage::object_ to match the datagram type.
    if (datagram_type.has_status()) {
      object_.object_status = MoqtObjectStatus::kObjectDoesNotExist;
      object_.payload_length = 0;
    } else {
      object_.object_status = datagram_type.end_of_group()
                                  ? MoqtObjectStatus::kEndOfGroup
                                  : MoqtObjectStatus::kNormal;
      object_.payload_length = 3;
    }
    object_.extension_headers =
        datagram_type.has_extension() ? std::string(kDefaultExtensionBlob) : "";
    object_.object_id = datagram_type.has_object_id() ? 6 : 0;
    object_.subgroup_id = std::nullopt;
    quic::QuicDataWriter writer(sizeof(raw_packet_),
                                reinterpret_cast<char*>(raw_packet_));
    EXPECT_TRUE(writer.WriteVarInt62(datagram_type.value()));
    EXPECT_TRUE(writer.WriteStringPiece(kRawAliasGroup));
    if (datagram_type.has_object_id()) {
      EXPECT_TRUE(writer.WriteStringPiece(kRawObject));
    }
    if (!datagram_type.has_default_priority()) {
      EXPECT_TRUE(writer.WriteStringPiece(kRawPriority));
    }
    if (datagram_type.has_extension()) {
      EXPECT_TRUE(writer.WriteStringPiece(kRawExtensions));
    }
    if (datagram_type.has_status()) {
      EXPECT_TRUE(
          writer.WriteVarInt62(static_cast<uint64_t>(object_.object_status)));
    } else {
      EXPECT_TRUE(writer.WriteStringPiece(kRawPayload));
    }
    EXPECT_LE(writer.length(), kMaxMessageHeaderSize);
    SetWireImage(raw_packet_, writer.length());
  }

  void ExpandVarints() override {
    std::string varints = "vvv";
    if (datagram_type_.has_object_id()) {
      varints += "v";
    }
    if (!datagram_type_.has_default_priority()) {
      varints += "-";  // priority
    }
    if (datagram_type_.has_extension()) {
      varints += "v-------";
    }
    if (datagram_type_.has_status()) {
      varints += "v";
    }
    ExpandVarintsImpl(varints, false);
  }

  MoqtPriority publisher_priority() const { return object_.publisher_priority; }

 private:
  uint8_t raw_packet_[17];
  MoqtDatagramType datagram_type_;
  static constexpr absl::string_view kRawAliasGroup = "\x04\x05";
  static constexpr absl::string_view kRawObject = "\x06";
  static constexpr absl::string_view kRawPriority = "\x07";
  static constexpr absl::string_view kRawExtensions{
      "\x07\x00\x0c\x01\x03\x66\x6f\x6f", 8};  // see kDefaultExtensionBlob
  static constexpr absl::string_view kRawPayload = "foo";
};

// Concatenation of the base header and the object-specific header. Follow-on
// object headers are handled in a different class.
class QUICHE_NO_EXPORT StreamHeaderSubgroupMessage : public ObjectMessage {
 public:
  explicit StreamHeaderSubgroupMessage(MoqtDataStreamType type)
      : ObjectMessage(), type_(type) {
    // Update ObjectMessage from the type;
    if (type.SubgroupIsZero()) {
      object_.subgroup_id = 0;
    } else if (type.SubgroupIsFirstObjectId()) {
      object_.subgroup_id = object_.object_id;
    }
    if (!type.AreExtensionHeadersPresent()) {
      object_.extension_headers = "";
    }
    // Build raw_packet_ from the type.
    quic::QuicDataWriter writer(sizeof(raw_packet_), raw_packet_);
    EXPECT_TRUE(
        writer.WriteVarInt62(type.value()) &&
        writer.WriteBytes(kRawBeginning.data(), kRawBeginning.length()));
    if (type.IsSubgroupPresent()) {
      EXPECT_TRUE(
          writer.WriteBytes(kRawSubgroupId.data(), kRawSubgroupId.length()));
    }
    if (!type.HasDefaultPriority()) {
      EXPECT_TRUE(writer.WriteBytes(kRawPublisherPriority.data(),
                                    kRawPublisherPriority.length()));
    }
    EXPECT_TRUE(writer.WriteBytes(kRawObjectId.data(), kRawObjectId.length()));
    if (type.AreExtensionHeadersPresent()) {
      EXPECT_TRUE(
          writer.WriteBytes(kRawExtensions.data(), kRawExtensions.length()));
    }
    payload_length_offset_ = writer.length();
    EXPECT_TRUE(writer.WriteBytes(kRawPayload.data(), kRawPayload.length()));
    EXPECT_LE(writer.length(), kMaxMessageHeaderSize);
    SetWireImage(reinterpret_cast<uint8_t*>(raw_packet_), writer.length());
  }

  void ExpandVarints() override {
    std::string varints = "vvv";
    if (type_.IsSubgroupPresent()) {
      varints += "v";
    }
    if (!type_.HasDefaultPriority()) {
      varints += "-";  // priority
    }
    varints += "v";  // object ID
    if (type_.AreExtensionHeadersPresent()) {
      varints += "v-------";
    }
    varints += "v---";  // payload with length
    ExpandVarintsImpl(varints, false);
  }

  bool SetPayloadLength(uint8_t payload_length) {
    if (payload_length > 63) {
      // This only supports one-byte varints.
      return false;
    }
    int payload_length_change = payload_length - object_.payload_length;
    object_.payload_length = payload_length;
    raw_packet_[payload_length_offset_] = static_cast<char>(payload_length);
    SetWireImage(reinterpret_cast<uint8_t*>(raw_packet_), total_message_size());
    set_wire_image_size(total_message_size() + payload_length_change);
    return true;
  }

  void MakeObjectEndOfStream() override {
    if (type_.EndOfGroupInStream()) {
      object_.object_status = MoqtObjectStatus::kEndOfGroup;
    }
  }

 private:
  MoqtDataStreamType type_;
  static constexpr absl::string_view kRawBeginning = "\x04\x05";
  // track alias, group ID
  static constexpr absl::string_view kRawSubgroupId = "\x08";
  static constexpr absl::string_view kRawPublisherPriority = "\x07";
  static constexpr absl::string_view kRawObjectId = "\x06";
  static constexpr absl::string_view kRawExtensions{
      "\x07\x00\x0c\x01\x03\x66\x6f\x6f", 8};  // see kDefaultExtensionBlob
  static constexpr absl::string_view kRawPayload = "\x03\x66\x6f\x6f";
  char raw_packet_[18];
  size_t payload_length_offset_;
};

// Used only for tests that process multiple objects on one stream.
class QUICHE_NO_EXPORT StreamMiddlerSubgroupMessage : public ObjectMessage {
 public:
  StreamMiddlerSubgroupMessage(const MoqtDataStreamType type)
      : ObjectMessage(), type_(type) {
    SetWireImage(reinterpret_cast<uint8_t*>(raw_packet_), sizeof(raw_packet_));
    if (type.SubgroupIsZero()) {
      object_.subgroup_id = 0;
    } else if (type.SubgroupIsFirstObjectId()) {
      object_.subgroup_id = 6;  // The object ID in the header.
    }
    object_.object_id = 9;
    quic::QuicDataWriter writer(sizeof(raw_packet_), raw_packet_);
    EXPECT_TRUE(writer.WriteVarInt62(2));  // Object ID delta - 1
    if (type.AreExtensionHeadersPresent()) {
      EXPECT_TRUE(
          writer.WriteBytes(kRawExtensions.data(), kRawExtensions.length()));
    }
    EXPECT_TRUE(writer.WriteBytes(kRawPayload.data(), kRawPayload.length()));
    EXPECT_LE(writer.length(), kMaxMessageHeaderSize);
    SetWireImage(reinterpret_cast<uint8_t*>(raw_packet_), writer.length());
  }

  void ExpandVarints() override {
    if (type_.AreExtensionHeadersPresent()) {
      ExpandVarintsImpl("vv-------v---", false);
    } else {
      ExpandVarintsImpl("vv---", false);
    }
  }

 private:
  MoqtDataStreamType type_;
  static constexpr absl::string_view kRawExtensions{
      "\x07\x00\x0c\x01\x03\x66\x6f\x6f", 8};  // see kDefaultExtensionBlob
  static constexpr absl::string_view kRawPayload = "\x03\x62\x61\x72";
  char raw_packet_[13];
};

class QUICHE_NO_EXPORT StreamHeaderFetchMessage : public ObjectMessage {
 public:
  StreamHeaderFetchMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.subgroup_id = 8;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvvvv-v-------v---", false);
  }

  bool SetPayloadLength(uint8_t payload_length) {
    if (payload_length > 63) {
      // This only supports one-byte varints.
      return false;
    }
    object_.payload_length = payload_length;
    raw_packet_[14] = payload_length;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    return true;
  }

 private:
  uint8_t raw_packet_[19] = {
      0x05,              // type field
      0x04,              // request ID
      0x3f,              // object serialization flag
      0x05, 0x08, 0x06,  // sequence
      0x07, 0x07,        // publisher priority, 7B extensions
      0x00, 0x0c, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // extensions
      0x03, 0x66, 0x6f, 0x6f,                    // payload = "foo"
  };
};

// Used only for tests that process multiple objects on one stream.
class QUICHE_NO_EXPORT StreamMiddlerFetchMessage : public ObjectMessage {
 public:
  StreamMiddlerFetchMessage(MoqtFetchSerialization serialization)
      : ObjectMessage(), serialization_(serialization) {
    size_t length = 0;
    if (serialization.is_datagram()) {  // Two byte varint.
      raw_packet_[length++] = 0x40;
    }
    raw_packet_[length++] = static_cast<uint8_t>(serialization.value());
    if (serialization.has_group_id()) {
      raw_packet_[length++] = 0x06;  // group ID
      object_.group_id = 6;
    }
    if (serialization.zero_subgroup_id()) {
      object_.subgroup_id = 0;
    } else if (serialization.has_subgroup_id()) {
      raw_packet_[length++] = 0x0a;
      object_.subgroup_id = 10;
    } else if (serialization.prior_subgroup_id_plus_one()) {
      if (!object_.subgroup_id.has_value()) {
        QUICHE_BUG(quiche_bug_moqt_prior_subgroup_id_without_previous_subgroup)
            << "prior_subgroup_id_plus_one without previous subgroup ID";
        return;
      }
      ++(*object_.subgroup_id);
    } else if (serialization.is_datagram()) {
      object_.subgroup_id = std::nullopt;
    }  // If prior_subgroup_id, subgroup_id is already set properly.
    if (serialization.has_object_id()) {
      raw_packet_[length++] = 0x0a;
      object_.object_id = 10;
    } else {
      ++object_.object_id;
    }
    if (serialization.has_priority()) {
      raw_packet_[length++] = 0x09;
      object_.publisher_priority = MoqtPriority(0x09);
    }
    if (serialization.has_extensions()) {
      memcpy(&raw_packet_[length], kRawExtensions.data(),
             kRawExtensions.length());
      length += kRawExtensions.length();
    } else {
      object_.extension_headers = "";
    }
    memcpy(&raw_packet_[length], kRawPayload.data(), kRawPayload.length());
    length += kRawPayload.length();

    SetWireImage(raw_packet_, length);
  }

  void ExpandVarints() override {
    std::string varints = "v";
    if (serialization_.has_group_id()) {
      varints += "v";
    }
    if (serialization_.has_subgroup_id()) {
      varints += "v";
    }
    if (serialization_.has_object_id()) {
      varints += "v";
    }
    if (serialization_.has_priority()) {
      varints += "-";
    }
    if (serialization_.has_extensions()) {
      varints += "v-------";
    }
    varints += "v---";
    ExpandVarintsImpl(varints, false);
  }

 private:
  MoqtFetchSerialization serialization_;
  uint8_t raw_packet_[17];
  static constexpr absl::string_view kRawExtensions{
      "\x07\x00\x0c\x01\x03\x66\x6f\x6f", 8};  // see kDefaultExtensionBlob
  static constexpr absl::string_view kRawPayload = "\x03\x62\x61\x72";
};

class QUICHE_NO_EXPORT ClientSetupMessage : public TestMessageBase {
 public:
  explicit ClientSetupMessage(bool webtrans) : TestMessageBase() {
    client_setup_.parameters.moqt_implementation = kTestImplementationString;
    if (webtrans) {
      // Should not send PATH or AUTHORITY.
      client_setup_.parameters.path = std::nullopt;
      client_setup_.parameters.authority = std::nullopt;
      raw_packet_[2] -= 17;   // adjust payload length
      raw_packet_[3] = 0x02;  // only two parameters
      // Move MaxRequestId up in the packet.
      memmove(raw_packet_ + 4, raw_packet_ + 10, 2);
      // Move MoqtImplementation up in the packet.
      memmove(raw_packet_ + 6, raw_packet_ + 23,
              kTestImplementationString.length() + 2);
      raw_packet_[4] = 0x02;  // Diff from 0.
      raw_packet_[6] = 0x05;  // Diff from 2.
      SetWireImage(raw_packet_, sizeof(raw_packet_) - 17);
    } else {
      SetWireImage(raw_packet_, sizeof(raw_packet_));
    }
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtClientSetup>(values);
    if (cast.parameters != client_setup_.parameters) {
      QUIC_LOG(INFO) << "CLIENT_SETUP parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    if (client_setup_.parameters.path.has_value()) {
      ExpandVarintsImpl("vvv----vvvv---------vv---------------------------");
    } else {
      ExpandVarintsImpl("vvvvv---------------------------");
    }
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(client_setup_);
  }

 private:
  // The framer serializes all the integer parameters in order, then all the
  // string parameters in order. Unfortunately, this means that
  // kMoqtImplementation goes last even though it is always present, while
  // kPath and KAuthority aren't.
  uint8_t raw_packet_[53] = {
      0x20, 0x00, 0x32,                    // type, length
      0x04,                                // 4 parameters
      0x01, 0x04, 0x70, 0x61, 0x74, 0x68,  // path = "path"
      0x01, 0x32,                          // max_request_id = 50
      0x03, 0x09, 0x61, 0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x74,
      0x79,  // authority = "authority"
      // moqt_implementation:
      0x02, 0x1c, 0x4d, 0x6f, 0x71, 0x20, 0x54, 0x65, 0x73, 0x74, 0x20, 0x49,
      0x6d, 0x70, 0x6c, 0x65, 0x6d, 0x65, 0x6e, 0x74, 0x61, 0x74, 0x69, 0x6f,
      0x6e, 0x20, 0x54, 0x79, 0x70, 0x65};
  MoqtClientSetup client_setup_ = {
      SetupParameters("path", "authority", 50),
  };
};

class QUICHE_NO_EXPORT ServerSetupMessage : public TestMessageBase {
 public:
  ServerSetupMessage() : TestMessageBase() {
    server_setup_.parameters.moqt_implementation = kTestImplementationString;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtServerSetup>(values);
    if (cast.parameters != server_setup_.parameters) {
      QUIC_LOG(INFO) << "SERVER_SETUP parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(server_setup_);
  }

 private:
  uint8_t raw_packet_[36] = {0x21, 0x00, 0x21,  // type, length
                             0x02,              // two parameters
                             0x02, 0x32,        // max_subscribe_id = 50
                             // moqt_implementation:
                             0x05, 0x1c, 0x4d, 0x6f, 0x71, 0x20, 0x54, 0x65,
                             0x73, 0x74, 0x20, 0x49, 0x6d, 0x70, 0x6c, 0x65,
                             0x6d, 0x65, 0x6e, 0x74, 0x61, 0x74, 0x69, 0x6f,
                             0x6e, 0x20, 0x54, 0x79, 0x70, 0x65};
  MoqtServerSetup server_setup_ = {
      SetupParameters(50),
  };
};

class QUICHE_NO_EXPORT SubscribeMessage : public TestMessageBase {
 public:
  SubscribeMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribe>(values);
    if (cast.request_id != subscribe_.request_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE subscribe ID mismatch";
      return false;
    }
    if (cast.full_track_name != subscribe_.full_track_name) {
      QUIC_LOG(INFO) << "SUBSCRIBE track name mismatch";
      return false;
    }
    if (cast.parameters != subscribe_.parameters) {
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvv---v----vv--vv-----vvvvvv---vv");
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_);
  }

 protected:
  MoqtSubscribe subscribe_ = {
      /*request_id=*/1,
      FullTrackName("foo", "abcd"),
      SubscribeForTest(),
  };

 private:
  uint8_t raw_packet_[36] = {
      0x03, 0x00, 0x21, 0x01,                    // request_id = 1
      0x01, 0x03, 0x66, 0x6f, 0x6f,              // track_namespace = "foo"
      0x04, 0x61, 0x62, 0x63, 0x64,              // track_name = "abcd"
      0x06,                                      // 6 parameters
      0x02, 0x67, 0x10,                          // delivery_timeout = 10000 ms
      0x01, 0x05, 0x03, 0x00, 0x62, 0x61, 0x72,  // authorization_tag = "bar"
      0x0d, 0x01,                                // forward = true
      0x10, 0x20,                                // subscriber_priority = 0x20
      0x01, 0x03, 0x03, 0x04, 0x01,  // filter_type = kAbsoluteStart (4, 1)
      0x01, 0x02,                    // group_order = kDescending
  };
};

class QUICHE_NO_EXPORT SubscribeOkMessage : public TestMessageBase {
 public:
  SubscribeOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    subscribe_ok_.parameters.expires = quic::QuicTimeDelta::FromMilliseconds(3);
    subscribe_ok_.parameters.largest_object = Location(12, 20);
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeOk>(values);
    if (cast.request_id != subscribe_ok_.request_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK subscribe ID mismatch";
      return false;
    }
    if (cast.track_alias != subscribe_ok_.track_alias) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK track alias mismatch";
      return false;
    }
    if (cast.parameters != subscribe_ok_.parameters) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK parameter mismatch";
      return false;
    }
    if (cast.extensions != subscribe_ok_.extensions) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK extensions mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvvvvv--v--v--vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_ok_);
  }

  void SetInvalidDeliveryOrder() {
    raw_packet_[19] = 0x10;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 protected:
  // This is protected so that TrackStatusOk can edit the fields.
  MoqtSubscribeOk subscribe_ok_ = {
      /*request_id=*/1,
      /*track_alias=*/2,
      MessageParameters(),  // Set in the constructor.
      TrackExtensions(
          /*delivery_timeout=*/quic::QuicTimeDelta::FromMilliseconds(10000),
          /*max_cache_duration=*/quic::QuicTimeDelta::FromMilliseconds(10000),
          /*publisher_priority=*/std::nullopt,
          /*group_order=*/MoqtDeliveryOrder::kDescending,
          /*dynamic_groups=*/std::nullopt,
          /*immutable_extensions=*/std::nullopt),
  };

 private:
  uint8_t raw_packet_[20] = {
      0x04, 0x00, 0x11, 0x01, 0x02, 0x02,  // request_id, alias, 2 params
      0x08, 0x03,                          // expires = 3
      0x01, 0x02, 0x0c, 0x14,              // largest_location = (12, 20)
      // Extensions
      0x02, 0x67, 0x10,  // delivery_timeout = 10000
      0x02, 0x67, 0x10,  // max_cache_duration = 10000
      0x1e, 0x02         // default_publisher_group_order = 2
  };
};

class QUICHE_NO_EXPORT RequestErrorMessage : public TestMessageBase {
 public:
  RequestErrorMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtRequestError>(values);
    if (cast.request_id != request_error_.request_id) {
      QUIC_LOG(INFO) << "REQUEST_ERROR request_id mismatch";
      return false;
    }
    if (cast.error_code != request_error_.error_code) {
      QUIC_LOG(INFO) << "REQUEST_ERROR error code mismatch";
      return false;
    }
    if (cast.retry_interval != request_error_.retry_interval) {
      QUIC_LOG(INFO) << "REQUEST_ERROR retry interval mismatch";
      return false;
    }
    if (cast.reason_phrase != request_error_.reason_phrase) {
      QUIC_LOG(INFO) << "REQUEST_ERROR reason phrase mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(request_error_);
  }

 protected:
  MoqtRequestError request_error_ = {
      /*request_id=*/2,
      /*error_code=*/RequestErrorCode::kInvalidRange,
      /*retry_interval=*/quic::QuicTimeDelta::FromSeconds(10),
      /*reason_phrase=*/"bar",
  };

 private:
  uint8_t raw_packet_[11] = {
      0x05, 0x00, 0x08,
      0x02,                    // request_id = 2
      0x11,                    // error_code = 17
      0x67, 0x11,              // retry_interval = 10000 ms
      0x03, 0x62, 0x61, 0x72,  // reason_phrase = "bar"
  };
};

class QUICHE_NO_EXPORT UnsubscribeMessage : public TestMessageBase {
 public:
  UnsubscribeMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtUnsubscribe>(values);
    if (cast.request_id != unsubscribe_.request_id) {
      QUIC_LOG(INFO) << "UNSUBSCRIBE request ID mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unsubscribe_);
  }

 private:
  uint8_t raw_packet_[4] = {
      0x0a, 0x00, 0x01, 0x03,  // request_id = 3
  };

  MoqtUnsubscribe unsubscribe_ = {
      /*request_id=*/3,
  };
};

class QUICHE_NO_EXPORT PublishDoneMessage : public TestMessageBase {
 public:
  PublishDoneMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishDone>(values);
    if (cast.request_id != publish_done_.request_id) {
      QUIC_LOG(INFO) << "PUBLISH_DONE request ID mismatch";
      return false;
    }
    if (cast.status_code != publish_done_.status_code) {
      QUIC_LOG(INFO) << "PUBLISH_DONE status code mismatch";
      return false;
    }
    if (cast.stream_count != publish_done_.stream_count) {
      QUIC_LOG(INFO) << "PUBLISH_DONE stream count mismatch";
      return false;
    }
    if (cast.error_reason != publish_done_.error_reason) {
      QUIC_LOG(INFO) << "PUBLISH_DONE error reason mismatch";
      return false;
    }

    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv--"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_done_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x0b, 0x00, 0x06, 0x02, 0x02,  // request_id = 2, error_code = 2,
      0x05,                          // stream_count = 5
      0x02, 0x68, 0x69,              // error_reason = "hi"
  };

  MoqtPublishDone publish_done_ = {
      /*request_id=*/2,
      /*error_code=*/PublishDoneCode::kTrackEnded,
      /*stream_count=*/5,
      /*error_reason=*/"hi",
  };
};

class QUICHE_NO_EXPORT RequestUpdateMessage : public TestMessageBase {
 public:
  RequestUpdateMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    request_update_.parameters.delivery_timeout =
        quic::QuicTimeDelta::FromMilliseconds(10000);
    request_update_.parameters.set_forward(true);
    request_update_.parameters.subscriber_priority = 0xaa;
    request_update_.parameters.subscription_filter.emplace(Location(3, 1), 5);
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtRequestUpdate>(values);
    if (cast.request_id != request_update_.request_id) {
      QUIC_LOG(INFO) << "REQUEST_UPDATE request ID mismatch";
      return false;
    }
    if (cast.existing_request_id != request_update_.existing_request_id) {
      QUIC_LOG(INFO) << "REQUEST_UPDATE existing request ID mismatch";
      return false;
    }
    if (cast.parameters != request_update_.parameters) {
      QUIC_LOG(INFO) << "REQUEST_UPDATE parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv--vvv--vv----"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(request_update_);
  }

 private:
  uint8_t raw_packet_[20] = {
      0x02, 0x00, 0x11, 0x02, 0x00,        // request IDs 2 and 0
      0x04,                                // Four parameters
      0x02, 0x67, 0x10,                    // delivery_timeout = 10000
      0x0e, 0x01,                          // forward = true
      0x10, 0x40, 0xaa,                    // subscriber_priority = 0xaa
      0x01, 0x04, 0x04, 0x03, 0x01, 0x05,  // Absolute Range: (3, 1) to 5.
  };

  MoqtRequestUpdate request_update_ = {
      /*request_id=*/2,
      /*existing_request_id=*/0,
      MessageParameters(),  // Set in the constructor.
  };
};

class QUICHE_NO_EXPORT PublishNamespaceMessage : public TestMessageBase {
 public:
  PublishNamespaceMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    publish_namespace_.parameters.authorization_tokens.push_back(
        AuthToken(AuthTokenType::kOutOfBand, "bar"));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishNamespace>(values);
    if (cast.request_id != publish_namespace_.request_id) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE request ID mismatch";
      return false;
    }
    if (cast.track_namespace != publish_namespace_.track_namespace) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE track namespace mismatch";
      return false;
    }
    if (cast.parameters != publish_namespace_.parameters) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---vvv-----"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_namespace_);
  }

 private:
  uint8_t raw_packet_[17] = {
      0x06, 0x00, 0x0e, 0x02,                    // request_id = 2
      0x01, 0x03, 0x66, 0x6f, 0x6f,              // track_namespace = "foo"
      0x01,                                      // 1 parameter
      0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x72,  // authorization_tag = "bar"
  };

  MoqtPublishNamespace publish_namespace_ = {
      /*request_id=*/2,
      TrackNamespace{"foo"},
      MessageParameters(),
  };
};

class QUICHE_NO_EXPORT NamespaceMessage : public TestMessageBase {
 public:
  NamespaceMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtNamespace>(values);
    if (cast.track_namespace_suffix != namespace_.track_namespace_suffix) {
      QUIC_LOG(INFO) << "NAMESPACE suffix mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(namespace_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x08, 0x00, 0x05, 0x01,
      0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
  };

  MoqtNamespace namespace_ = {
      TrackNamespace{"foo"},
  };
};

class QUICHE_NO_EXPORT NamespaceDoneMessage : public TestMessageBase {
 public:
  NamespaceDoneMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtNamespaceDone>(values);
    if (cast.track_namespace_suffix != namespace_done_.track_namespace_suffix) {
      QUIC_LOG(INFO) << "NAMESPACE_DONE suffix mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(namespace_done_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x0e, 0x00, 0x05, 0x01,
      0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
  };

  MoqtNamespaceDone namespace_done_ = {
      TrackNamespace{"foo"},
  };
};

class QUICHE_NO_EXPORT RequestOkMessage : public TestMessageBase {
 public:
  RequestOkMessage() : TestMessageBase() {
    request_ok_.parameters.largest_object = Location(5, 1);
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtRequestOk>(values);
    if (cast.request_id != request_ok_.request_id) {
      QUIC_LOG(INFO) << "REQUEST_OK request ID mismatch";
      return false;
    }
    if (cast.parameters != request_ok_.parameters) {
      QUIC_LOG(INFO) << "REQUEST_OK parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv--"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(request_ok_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x07, 0x00, 0x06, 0x01,  // request_id = 1
      0x01,                    // 1 parameter
      0x09, 0x02, 0x05, 0x01,  // Largest Object = (5, 1)
  };

  MoqtRequestOk request_ok_ = {
      /*request_id=*/1,
      MessageParameters(),  // Set in the constructor.
  };
};

class QUICHE_NO_EXPORT PublishNamespaceDoneMessage : public TestMessageBase {
 public:
  PublishNamespaceDoneMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishNamespaceDone>(values);
    if (cast.request_id != publish_namespace_done_.request_id) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE_DONE request ID mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_namespace_done_);
  }

 private:
  uint8_t raw_packet_[4] = {
      0x09,
      0x00,
      0x01,
      0x01,  // request_id = 1
  };

  MoqtPublishNamespaceDone publish_namespace_done_ = {
      /*request_id=*/1,
  };
};

class QUICHE_NO_EXPORT PublishNamespaceCancelMessage : public TestMessageBase {
 public:
  PublishNamespaceCancelMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishNamespaceCancel>(values);
    if (cast.request_id != publish_namespace_cancel_.request_id) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE CANCEL request ID mismatch";
      return false;
    }
    if (cast.error_code != publish_namespace_cancel_.error_code) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE CANCEL error code mismatch";
      return false;
    }
    if (cast.error_reason != publish_namespace_cancel_.error_reason) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE CANCEL reason phrase mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_namespace_cancel_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x0c, 0x00, 0x06, 0x02,  // request_id = 2
      0x03,                    // error_code = 3
      0x03, 0x62, 0x61, 0x72,  // error_reason = "bar"
  };

  MoqtPublishNamespaceCancel publish_namespace_cancel_ = {
      /*request_id=*/2,
      RequestErrorCode::kNotSupported,
      /*error_reason=*/"bar",
  };
};

class QUICHE_NO_EXPORT TrackStatusMessage : public SubscribeMessage {
 public:
  TrackStatusMessage() : SubscribeMessage() {
    SetByte(0, static_cast<uint8_t>(MoqtMessageType::kTrackStatus));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto value = std::get<MoqtTrackStatus>(values);
    auto* subscribe = reinterpret_cast<MoqtSubscribe*>(&value);
    MessageStructuredData structured_data =
        TestMessageBase::MessageStructuredData(*subscribe);
    return SubscribeMessage::EqualFieldValues(structured_data);
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(MoqtTrackStatus(subscribe_));
  }
};

class QUICHE_NO_EXPORT GoAwayMessage : public TestMessageBase {
 public:
  GoAwayMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtGoAway>(values);
    if (cast.new_session_uri != goaway_.new_session_uri) {
      QUIC_LOG(INFO) << "GOAWAY full track name mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(goaway_);
  }

 private:
  uint8_t raw_packet_[7] = {
      0x10, 0x00, 0x04, 0x03, 0x66, 0x6f, 0x6f,
  };

  MoqtGoAway goaway_ = {
      /*new_session_uri=*/"foo",
  };
};

class QUICHE_NO_EXPORT SubscribeNamespaceMessage : public TestMessageBase {
 public:
  SubscribeNamespaceMessage() : TestMessageBase() {
    subscribe_namespace_.parameters.authorization_tokens.push_back(
        AuthToken(AuthTokenType::kOutOfBand, "bar"));
    subscribe_namespace_.parameters.set_forward(true);
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeNamespace>(values);
    if (cast.request_id != subscribe_namespace_.request_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE request_id mismatch";
      return false;
    }
    if (cast.track_namespace_prefix !=
        subscribe_namespace_.track_namespace_prefix) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE track namespace mismatch";
      return false;
    }
    if (cast.subscribe_options != subscribe_namespace_.subscribe_options) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE subscribe options mismatch";
      return false;
    }
    if (cast.parameters != subscribe_namespace_.parameters) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE parameters mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---vvv-----"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_namespace_);
  }

 private:
  uint8_t raw_packet_[20] = {
      0x11, 0x00, 0x11, 0x01,                    // request_id = 1
      0x01, 0x03, 0x66, 0x6f, 0x6f,              // namespace = "foo"
      0x02,                                      // subscribe_options = kBoth
      0x02,                                      // 2 parameters
      0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x72,  // authorization_tag = "bar"
      0x0d, 0x01,                                // forward = true
  };

  MoqtSubscribeNamespace subscribe_namespace_ = {
      /*request_id=*/1,
      TrackNamespace({"foo"}),
      SubscribeNamespaceOption::kBoth,
      MessageParameters(),  // set in constructor.
  };
};

class QUICHE_NO_EXPORT MaxRequestIdMessage : public TestMessageBase {
 public:
  MaxRequestIdMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtMaxRequestId>(values);
    if (cast.max_request_id != max_request_id_.max_request_id) {
      QUIC_LOG(INFO) << "MAX_REQUEST_ID mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(max_request_id_);
  }

 private:
  uint8_t raw_packet_[4] = {
      0x15,
      0x00,
      0x01,
      0x0b,
  };

  MoqtMaxRequestId max_request_id_ = {
      /*max_request_id =*/11,
  };
};

class QUICHE_NO_EXPORT FetchMessage : public TestMessageBase {
 public:
  FetchMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    fetch_.parameters.authorization_tokens.push_back(
        AuthToken(AuthTokenType::kOutOfBand, "baz"));
    fetch_.parameters.group_order = MoqtDeliveryOrder::kAscending;
    fetch_.parameters.subscriber_priority = 2;
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetch>(values);
    if (cast.request_id != fetch_.request_id) {
      QUIC_LOG(INFO) << "FETCH request_id mismatch";
      return false;
    }
    if (cast.fetch != fetch_.fetch) {
      QUIC_LOG(INFO) << "FETCH mismatch";
      return false;
    }
    if (cast.parameters != fetch_.parameters) {
      QUIC_LOG(INFO) << "FETCH parameters mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvv---v---vvvvvvv-----vvvv");
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_);
  }

  void SetEndObject(uint64_t group, std::optional<uint64_t> object) {
    // Avoid varint nonsense.
    QUICHE_CHECK(group < 64);
    QUICHE_CHECK(!object.has_value() || *object < 64);
    std::get<StandaloneFetch>(fetch_.fetch).end_location.group = group;
    std::get<StandaloneFetch>(fetch_.fetch).end_location.object =
        object.has_value() ? *object : kMaxObjectId;
    raw_packet_[16] = group;
    raw_packet_[17] = object.has_value() ? (*object + 1) : 0;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  void SetGroupOrder(uint8_t group_order) {
    raw_packet_[29] = static_cast<uint8_t>(group_order);
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[30] = {
      0x16, 0x00, 0x1b,
      0x01,                                      // request_id = 1
      0x01,                                      // type = kStandalone
      0x01, 0x03, 0x66, 0x6f, 0x6f,              // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,                    // track_name = "bar"
      0x01, 0x02,                                // start_location = 1, 2
      0x05, 0x07,                                // end_location = 5, 6
      0x03,                                      // 3 parameters
      0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x7a,  // token = "baz"
      0x1d, 0x02,                                // priority = kHigh
      0x02, 0x01,                                // group_order = kAscending
  };

  MoqtFetch fetch_ = {
      /*request_id=*/1,
      StandaloneFetch{
          FullTrackName("foo", "bar"),
          /*start_location=*/Location{1, 2},
          /*end_location=*/Location{5, 6},
      },
      MessageParameters(),  // set in constructor.
  };
};

// This is not used in the parameterized Parser and Framer tests, because it
// does not have its own MoqtMessageType.
class QUICHE_NO_EXPORT RelativeJoiningFetchMessage : public TestMessageBase {
 public:
  RelativeJoiningFetchMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    fetch_.parameters.authorization_tokens.push_back(
        AuthToken(AuthTokenType::kOutOfBand, "baz"));
    fetch_.parameters.group_order = MoqtDeliveryOrder::kAscending;
    fetch_.parameters.subscriber_priority = 2;
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetch>(values);
    if (cast.request_id != fetch_.request_id) {
      QUIC_LOG(INFO) << "FETCH request_id mismatch";
      return false;
    }
    if (cast.fetch != fetch_.fetch) {
      QUIC_LOG(INFO) << "FETCH mismatch";
      return false;
    }
    if (cast.parameters != fetch_.parameters) {
      QUIC_LOG(INFO) << "FETCH parameters mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvvvvv-----vvvv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_);
  }

  void SetGroupOrder(uint8_t group_order) {
    raw_packet_[18] = static_cast<uint8_t>(group_order);
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[19] = {
      0x16, 0x00, 0x10,
      0x01,        // request_id = 1
      0x02,        // type = kRelativeJoining
      0x02, 0x02,  // joining_request_id = 2, 2 groups
      0x03,        // 3 parameters
      0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x7a,  // token = "baz"
      0x1d, 0x02,                                // priority = kHigh
      0x02, 0x01,                                // group_order = kAscending
  };

  MoqtFetch fetch_ = {
      /*request_id =*/1,
      JoiningFetchRelative{2, 2},
      MessageParameters(),  // set in constructor.
  };
};

// This is not used in the parameterized Parser and Framer tests, because it
// does not have its own MoqtMessageType.
class QUICHE_NO_EXPORT AbsoluteJoiningFetchMessage : public TestMessageBase {
 public:
  AbsoluteJoiningFetchMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    fetch_.parameters.authorization_tokens.push_back(
        AuthToken(AuthTokenType::kOutOfBand, "baz"));
    fetch_.parameters.group_order = MoqtDeliveryOrder::kAscending;
    fetch_.parameters.subscriber_priority = 2;
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetch>(values);
    if (cast.request_id != fetch_.request_id) {
      QUIC_LOG(INFO) << "FETCH request_id mismatch";
      return false;
    }
    if (cast.fetch != fetch_.fetch) {
      QUIC_LOG(INFO) << "FETCH mismatch";
      return false;
    }
    if (cast.parameters != fetch_.parameters) {
      QUIC_LOG(INFO) << "FETCH parameters mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvvvvv-----vvvv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_);
  }

  void SetGroupOrder(uint8_t group_order) {
    raw_packet_[5] = static_cast<uint8_t>(group_order);
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[19] = {
      0x16, 0x00, 0x10,
      0x01,        // request_id = 1
      0x03,        // type = kAbsoluteJoining
      0x02, 0x02,  // joining_request_id = 2, group_id = 2
      0x03,        // 3 parameters
      0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x7a,  // token = "baz"
      0x1d, 0x02,                                // priority = kHigh
      0x02, 0x01,                                // group_order = kAscending
  };

  MoqtFetch fetch_ = {
      /*request_id=*/1,
      JoiningFetchAbsolute{2, 2},
      MessageParameters(),  // set in constructor.
  };
};

class QUICHE_NO_EXPORT FetchOkMessage : public TestMessageBase {
 public:
  FetchOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetchOk>(values);
    if (cast.request_id != fetch_ok_.request_id) {
      QUIC_LOG(INFO) << "FETCH_OK request_id mismatch";
      return false;
    }
    if (cast.end_of_track != fetch_ok_.end_of_track) {
      QUIC_LOG(INFO) << "FETCH_OK end_of_track mismatch";
      return false;
    }
    if (cast.end_location != fetch_ok_.end_location) {
      QUIC_LOG(INFO) << "FETCH_OK end_location mismatch";
      return false;
    }
    if (cast.parameters != fetch_ok_.parameters) {
      QUIC_LOG(INFO) << "FETCH_OK parameters mismatch";
      return false;
    }
    if (cast.extensions != fetch_ok_.extensions) {
      QUIC_LOG(INFO) << "FETCH_OK extensions mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v-vvvv--vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_ok_);
  }

 private:
  uint8_t raw_packet_[13] = {
      0x18, 0x00, 0x0a,
      0x01,              // request_id = 1
      0x00,              // end_of_track = false
      0x05, 0x04,        // end_location = 5, 3
      0x00,              // no parameters
      0x04, 0x67, 0x10,  // MaxCacheDuration = 10000
      0x1e, 0x02,        // group_order = kDescending
  };

  MoqtFetchOk fetch_ok_ = {
      /*request_id =*/1,
      /*end_of_track=*/false,
      /*end_location=*/Location{5, 3},
      MessageParameters(),
      TrackExtensions(std::nullopt,
                      quic::QuicTimeDelta::FromMilliseconds(10000),
                      std::nullopt, MoqtDeliveryOrder::kDescending,
                      std::nullopt, std::nullopt),
  };
};

class QUICHE_NO_EXPORT FetchCancelMessage : public TestMessageBase {
 public:
  FetchCancelMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetchCancel>(values);
    if (cast.request_id != fetch_cancel_.request_id) {
      QUIC_LOG(INFO) << "FETCH_CANCEL subscribe_id mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_cancel_);
  }

 private:
  uint8_t raw_packet_[4] = {
      0x17, 0x00, 0x01,
      0x01,  // request_id = 1
  };

  MoqtFetchCancel fetch_cancel_ = {
      /*request_id =*/1,
  };
};

class QUICHE_NO_EXPORT RequestsBlockedMessage : public TestMessageBase {
 public:
  RequestsBlockedMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtRequestsBlocked>(values);
    if (cast.max_request_id != requests_blocked_.max_request_id) {
      QUIC_LOG(INFO) << "SUBSCRIBES_BLOCKED max_subscribe_id mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(requests_blocked_);
  }

 private:
  uint8_t raw_packet_[4] = {
      0x1a, 0x00, 0x01,
      0x0b,  // max_request_id = 11
  };

  MoqtRequestsBlocked requests_blocked_ = {
      /*max_request_id=*/11,
  };
};

class QUICHE_NO_EXPORT PublishMessage : public TestMessageBase {
 public:
  PublishMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    publish_.parameters.authorization_tokens.push_back(
        AuthToken(AuthTokenType::kOutOfBand, "baz"));
    publish_.parameters.largest_object = Location(10, 1);
    publish_.parameters.set_forward(true);
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublish>(values);
    if (cast.request_id != publish_.request_id) {
      QUIC_LOG(INFO) << "PUBLISH request_id mismatch";
      return false;
    }
    if (cast.full_track_name != publish_.full_track_name) {
      QUIC_LOG(INFO) << "PUBLISH full_track_name mismatch";
      return false;
    }
    if (cast.track_alias != publish_.track_alias) {
      QUIC_LOG(INFO) << "PUBLISH track_alias mismatch";
      return false;
    }
    if (cast.parameters != publish_.parameters) {
      QUIC_LOG(INFO) << "PUBLISH parameters mismatch";
      return false;
    }
    if (cast.extensions != publish_.extensions) {
      QUIC_LOG(INFO) << "PUBLISH extensions mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvv---v---vvvv-----vv--vvvv");
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_);
  }

 private:
  uint8_t raw_packet_[30] = {
      0x1d, 0x00, 0x1b,
      0x01,                                      // request_id = 1
      0x01, 0x03, 0x66, 0x6f, 0x6f,              // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,                    // track_name = "bar"
      0x04,                                      // track_alias = 4
      0x03,                                      // 3 parameters
      0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x7a,  // token = "baz"
      0x06, 0x02, 0x0a, 0x01,                    // largest_object = 10, 1
      0x07, 0x01,                                // forward = 1
      0x22, 0x02,                                // group_order = kAscending
  };

  MoqtPublish publish_ = {
      /*request_id=*/1,
      FullTrackName("foo", "bar"),
      /*track_alias=*/4,
      MessageParameters(),
      TrackExtensions(std::nullopt, std::nullopt, std::nullopt,
                      MoqtDeliveryOrder::kDescending, std::nullopt,
                      std::nullopt),
  };
};

class QUICHE_NO_EXPORT PublishOkMessage : public TestMessageBase {
 public:
  PublishOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    publish_ok_.parameters.delivery_timeout =
        quic::QuicTimeDelta::FromMilliseconds(10000);
    publish_ok_.parameters.set_forward(true);
    publish_ok_.parameters.subscriber_priority = 2;
    publish_ok_.parameters.group_order = MoqtDeliveryOrder::kAscending;
    publish_ok_.parameters.subscription_filter =
        SubscriptionFilter(Location(5, 4), 6);
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishOk>(values);
    if (cast.request_id != publish_ok_.request_id) {
      QUIC_LOG(INFO) << "PUBLISH_OK request_id mismatch";
      return false;
    }
    if (cast.parameters != publish_ok_.parameters) {
      QUIC_LOG(INFO) << "PUBLISH_OK parameters mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv--vvvvvv----vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_ok_);
  }

 private:
  uint8_t raw_packet_[20] = {
      0x1e, 0x00, 0x11,
      0x01,                                // request_id = 1
      0x05,                                // 5 parameters
      0x02, 0x67, 0x10,                    // delivery_timeout = 10000 ms
      0x0e, 0x01,                          // forward = true
      0x10, 0x02,                          // subscriber_priority = 2
      0x01, 0x04, 0x04, 0x05, 0x04, 0x06,  // subscription filter: (5, 4) to 6
      0x01, 0x01,                          // group_order = kAscending
  };
  MoqtPublishOk publish_ok_ = {
      /*request_id=*/1,
      MessageParameters(),  // set in constructor.
  };
};

class QUICHE_NO_EXPORT ObjectAckMessage : public TestMessageBase {
 public:
  ObjectAckMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtObjectAck>(values);
    if (cast.subscribe_id != object_ack_.subscribe_id) {
      QUIC_LOG(INFO) << "OBJECT_ACK subscribe ID mismatch";
      return false;
    }
    if (cast.group_id != object_ack_.group_id) {
      QUIC_LOG(INFO) << "OBJECT_ACK group ID mismatch";
      return false;
    }
    if (cast.object_id != object_ack_.object_id) {
      QUIC_LOG(INFO) << "OBJECT_ACK object ID mismatch";
      return false;
    }
    if (cast.delta_from_deadline != object_ack_.delta_from_deadline) {
      QUIC_LOG(INFO) << "OBJECT_ACK delta from deadline mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(object_ack_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x71, 0x84, 0x00, 0x04,  // type
      0x01, 0x10, 0x20,        // subscribe ID, group, object
      0x20,                    // 0x10 time delta
  };

  MoqtObjectAck object_ack_ = {
      /*subscribe_id=*/0x01,
      /*group_id=*/0x10,
      /*object_id=*/0x20,
      /*delta_from_deadline=*/quic::QuicTimeDelta::FromMicroseconds(0x10),
  };
};

// Factory function for test messages.
static inline std::unique_ptr<TestMessageBase> CreateTestMessage(
    MoqtMessageType message_type, bool is_webtrans) {
  switch (message_type) {
    case MoqtMessageType::kRequestOk:
      return std::make_unique<RequestOkMessage>();
    case MoqtMessageType::kRequestError:
      return std::make_unique<RequestErrorMessage>();
    case MoqtMessageType::kSubscribe:
      return std::make_unique<SubscribeMessage>();
    case MoqtMessageType::kSubscribeOk:
      return std::make_unique<SubscribeOkMessage>();
    case MoqtMessageType::kUnsubscribe:
      return std::make_unique<UnsubscribeMessage>();
    case MoqtMessageType::kPublishDone:
      return std::make_unique<PublishDoneMessage>();
    case MoqtMessageType::kRequestUpdate:
      return std::make_unique<RequestUpdateMessage>();
    case MoqtMessageType::kPublishNamespace:
      return std::make_unique<PublishNamespaceMessage>();
    case MoqtMessageType::kPublishNamespaceDone:
      return std::make_unique<PublishNamespaceDoneMessage>();
    case MoqtMessageType::kNamespace:
      return std::make_unique<NamespaceMessage>();
    case MoqtMessageType::kNamespaceDone:
      return std::make_unique<NamespaceDoneMessage>();
    case MoqtMessageType::kPublishNamespaceCancel:
      return std::make_unique<PublishNamespaceCancelMessage>();
    case MoqtMessageType::kTrackStatus:
      return std::make_unique<TrackStatusMessage>();
    case MoqtMessageType::kGoAway:
      return std::make_unique<GoAwayMessage>();
    case MoqtMessageType::kSubscribeNamespace:
      return std::make_unique<SubscribeNamespaceMessage>();
    case MoqtMessageType::kMaxRequestId:
      return std::make_unique<MaxRequestIdMessage>();
    case MoqtMessageType::kFetch:
      return std::make_unique<FetchMessage>();
    case MoqtMessageType::kFetchCancel:
      return std::make_unique<FetchCancelMessage>();
    case MoqtMessageType::kFetchOk:
      return std::make_unique<FetchOkMessage>();
    case MoqtMessageType::kRequestsBlocked:
      return std::make_unique<RequestsBlockedMessage>();
    case MoqtMessageType::kPublish:
      return std::make_unique<PublishMessage>();
    case MoqtMessageType::kPublishOk:
      return std::make_unique<PublishOkMessage>();
    case MoqtMessageType::kObjectAck:
      return std::make_unique<ObjectAckMessage>();
    case MoqtMessageType::kClientSetup:
      return std::make_unique<ClientSetupMessage>(is_webtrans);
    case MoqtMessageType::kServerSetup:
      return std::make_unique<ServerSetupMessage>();
    default:
      return nullptr;
  }
}

static inline std::unique_ptr<TestMessageBase> CreateTestDataStream(
    MoqtDataStreamType type) {
  if (type.IsPadding()) {
    return nullptr;
  }
  if (type.IsFetch()) {
    return std::make_unique<StreamHeaderFetchMessage>();
  }
  return std::make_unique<StreamHeaderSubgroupMessage>(type);
}

static inline std::unique_ptr<TestMessageBase> CreateTestFetch(
    MoqtFetchSerialization type) {
  return std::make_unique<StreamMiddlerFetchMessage>(type);
}

static inline std::unique_ptr<TestMessageBase> CreateTestDatagram(
    MoqtDatagramType type) {
  return std::make_unique<ObjectDatagramMessage>(type);
}

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_
