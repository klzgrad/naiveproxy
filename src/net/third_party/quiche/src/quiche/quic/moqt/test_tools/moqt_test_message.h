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
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
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
        for (bool zero_object_id : {false, true}) {
          types.push_back(MoqtDatagramType(payload, extension, end_of_group,
                                           zero_object_id));
        }
      }
    }
  }
  return types;
}

inline std::vector<MoqtDataStreamType> AllMoqtDataStreamTypes() {
  std::vector<MoqtDataStreamType> types;
  types.push_back(MoqtDataStreamType::Fetch());
  uint64_t first_object_id = 1;
  for (uint64_t subgroup_id : {0, 1, 2}) {
    for (bool no_extension_headers : {true, false}) {
      for (bool end_of_group : {false, true}) {
        types.push_back(MoqtDataStreamType::Subgroup(
            subgroup_id, first_object_id, no_extension_headers, end_of_group));
      }
    }
  }
  return types;
}

// Base class containing a wire image and the corresponding structured
// representation of an example of each message. It allows parser and framer
// tests to iterate through all message types without much specialized code.
class QUICHE_NO_EXPORT TestMessageBase {
 public:
  virtual ~TestMessageBase() = default;

  using MessageStructuredData =
      std::variant<MoqtClientSetup, MoqtServerSetup, MoqtObject, MoqtSubscribe,
                   MoqtSubscribeOk, MoqtSubscribeError, MoqtUnsubscribe,
                   MoqtPublishDone, MoqtSubscribeUpdate, MoqtPublishNamespace,
                   MoqtPublishNamespaceOk, MoqtPublishNamespaceError,
                   MoqtPublishNamespaceDone, MoqtPublishNamespaceCancel,
                   MoqtTrackStatus, MoqtTrackStatusOk, MoqtTrackStatusError,
                   MoqtGoAway, MoqtSubscribeNamespace, MoqtSubscribeNamespaceOk,
                   MoqtSubscribeNamespaceError, MoqtUnsubscribeNamespace,
                   MoqtMaxRequestId, MoqtFetch, MoqtFetchCancel, MoqtFetchOk,
                   MoqtFetchError, MoqtRequestsBlocked, MoqtPublish,
                   MoqtPublishOk, MoqtPublishError, MoqtObjectAck>;

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
  virtual void MakeObjectEndOfStream() {}

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
    object_.subgroup_id = object_.object_id;
    quic::QuicDataWriter writer(sizeof(raw_packet_),
                                reinterpret_cast<char*>(raw_packet_));
    EXPECT_TRUE(writer.WriteVarInt62(datagram_type.value()));
    EXPECT_TRUE(writer.WriteStringPiece(kRawAliasGroup));
    if (datagram_type.has_object_id()) {
      EXPECT_TRUE(writer.WriteStringPiece(kRawObject));
    }
    EXPECT_TRUE(writer.WriteStringPiece(kRawPriority));
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
    varints += "-";  // priority
    if (datagram_type_.has_extension()) {
      varints += "v-------";
    }
    if (datagram_type_.has_status()) {
      varints += "v";
    }
    ExpandVarintsImpl(varints, false);
  }

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
      EXPECT_TRUE(writer.WriteUInt8(object_.subgroup_id));
    }
    EXPECT_TRUE(writer.WriteBytes(kRawMiddle.data(), kRawMiddle.length()));
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
    if (!type_.IsSubgroupPresent()) {
      if (!type_.AreExtensionHeadersPresent()) {
        ExpandVarintsImpl("vvv-vv---", false);
      } else {
        ExpandVarintsImpl("vvv-vv-------v---", false);
      }
    } else {
      if (!type_.AreExtensionHeadersPresent()) {
        ExpandVarintsImpl("vvvv-vv---", false);
      } else {
        ExpandVarintsImpl("vvvv-vv-------v---", false);
      }
    }
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
  static constexpr absl::string_view kRawMiddle = "\x07\x06";
  // publisher priority, object ID
  static constexpr absl::string_view kRawExtensions{
      "\x07\x00\x0c\x01\x03\x66\x6f\x6f", 8};  // see kDefaultExtensionBlob
  static constexpr absl::string_view kRawPayload = "\x03\x66\x6f\x6f";
  char raw_packet_[18];
  size_t payload_length_offset_;
};

// Used only for tests that process multiple objects on one stream.
class QUICHE_NO_EXPORT StreamMiddlerSubgroupMessage : public ObjectMessage {
 public:
  StreamMiddlerSubgroupMessage(MoqtDataStreamType type)
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
    ExpandVarintsImpl("vvvvv-v-------v---", false);
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
  uint8_t raw_packet_[18] = {
      0x05,              // type field
      0x04,              // subscribe ID
                         // object middler:
      0x05, 0x08, 0x06,  // sequence
      0x07, 0x07,        // publisher priority, 7B extensions
      0x00, 0x0c, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // extensions
      0x03, 0x66, 0x6f, 0x6f,                    // payload = "foo"
  };
};

// Used only for tests that process multiple objects on one stream.
class QUICHE_NO_EXPORT StreamMiddlerFetchMessage : public ObjectMessage {
 public:
  StreamMiddlerFetchMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.subgroup_id = 8;
    object_.object_id = 9;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvv-v-------v---", false);
  }

 private:
  uint8_t raw_packet_[16] = {
      0x05, 0x08, 0x09, 0x07,                          // Object metadata
      0x07, 0x00, 0x0c, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // extensions
      0x03, 0x62, 0x61, 0x72,                          // Payload = "bar"
  };
};

class QUICHE_NO_EXPORT ClientSetupMessage : public TestMessageBase {
 public:
  explicit ClientSetupMessage(bool webtrans) : TestMessageBase() {
    client_setup_.parameters.using_webtrans = webtrans;
    if (webtrans) {
      // Should not send PATH or AUTHORITY.
      client_setup_.parameters.path = "";
      client_setup_.parameters.authority = "";
      raw_packet_[2] = 0x23;  // adjust payload length (-17)
      raw_packet_[6] = 0x02;  // only two parameters
      // Move MoqtImplementation up in the packet.
      memmove(raw_packet_ + 9, raw_packet_ + 26, 29);
      SetWireImage(raw_packet_, sizeof(raw_packet_) - 17);
    } else {
      SetWireImage(raw_packet_, sizeof(raw_packet_));
    }
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtClientSetup>(values);
    if (cast.supported_versions.size() !=
        client_setup_.supported_versions.size()) {
      QUIC_LOG(INFO) << "CLIENT_SETUP number of supported versions mismatch";
      return false;
    }
    for (uint64_t i = 0; i < cast.supported_versions.size(); ++i) {
      // Listed versions are 1 and 2, in that order.
      if (cast.supported_versions[i] != client_setup_.supported_versions[i]) {
        QUIC_LOG(INFO) << "CLIENT_SETUP supported version mismatch";
        return false;
      }
    }
    if (cast.parameters != client_setup_.parameters) {
      QUIC_LOG(INFO) << "CLIENT_SETUP parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    if (!client_setup_.parameters.path.empty()) {
      ExpandVarintsImpl("vvvvvvvv----vv---------vv---------------------------");
    } else {
      ExpandVarintsImpl("vvvvvvvv---------------------------");
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
  uint8_t raw_packet_[55] = {
      0x20, 0x00, 0x34,                    // type, length
      0x02, 0x01, 0x02,                    // versions
      0x04,                                // 4 parameters
      0x02, 0x32,                          // max_request_id = 50
      0x01, 0x04, 0x70, 0x61, 0x74, 0x68,  // path = "path"
      0x05, 0x09, 0x61, 0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x74,
      0x79,  // authority = "authority"
      // moqt_implementation:
      0x07, 0x1b, 0x47, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x20, 0x51, 0x55, 0x49,
      0x43, 0x48, 0x45, 0x20, 0x4d, 0x4f, 0x51, 0x54, 0x20, 0x64, 0x72, 0x61,
      0x66, 0x74, 0x20, 0x31, 0x34};
  MoqtClientSetup client_setup_ = {
      /*supported_versions=*/std::vector<MoqtVersion>(
          {static_cast<MoqtVersion>(1), static_cast<MoqtVersion>(2)}),
      MoqtSessionParameters(quic::Perspective::IS_CLIENT, "path", "authority",
                            50),
  };
};

class QUICHE_NO_EXPORT ServerSetupMessage : public TestMessageBase {
 public:
  explicit ServerSetupMessage(bool webtrans) : TestMessageBase() {
    server_setup_.parameters.using_webtrans = webtrans;
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(server_setup_);
  }

 private:
  uint8_t raw_packet_[36] = {0x21, 0x00,
                             0x21,  // type
                             0x01,
                             0x02,  // version, two parameters
                             0x02,
                             0x32,  // max_subscribe_id = 50
                             // moqt_implementation:
                             0x07, 0x1b, 0x47, 0x6f, 0x6f, 0x67, 0x6c, 0x65,
                             0x20, 0x51, 0x55, 0x49, 0x43, 0x48, 0x45, 0x20,
                             0x4d, 0x4f, 0x51, 0x54, 0x20, 0x64, 0x72, 0x61,
                             0x66, 0x74, 0x20, 0x31, 0x34};
  MoqtServerSetup server_setup_ = {
      /*selected_version=*/static_cast<MoqtVersion>(1),
      MoqtSessionParameters(quic::Perspective::IS_SERVER, 50),
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
    if (cast.subscriber_priority != subscribe_.subscriber_priority) {
      QUIC_LOG(INFO) << "SUBSCRIBE subscriber priority mismatch";
      return false;
    }
    if (cast.group_order != subscribe_.group_order) {
      QUIC_LOG(INFO) << "SUBSCRIBE group order mismatch";
      return false;
    }
    if (cast.forward != subscribe_.forward) {
      QUIC_LOG(INFO) << "SUBSCRIBE forward mismatch";
      return false;
    }
    if (cast.filter_type != subscribe_.filter_type) {
      QUIC_LOG(INFO) << "SUBSCRIBE filter type mismatch";
      return false;
    }
    if (cast.start != subscribe_.start) {
      QUIC_LOG(INFO) << "SUBSCRIBE start mismatch";
      return false;
    }
    if (cast.end_group != subscribe_.end_group) {
      QUIC_LOG(INFO) << "SUBSCRIBE end group mismatch";
      return false;
    }
    if (cast.parameters != subscribe_.parameters) {
      QUIC_LOG(INFO) << "SUBSCRIBE parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvv---v-------vvvvv--vv-----");
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_);
  }

 protected:
  MoqtSubscribe subscribe_ = {
      /*request_id=*/1,
      FullTrackName("foo", "abcd"),
      /*subscriber_priority=*/0x20,
      /*group_order=*/MoqtDeliveryOrder::kDescending,
      /*forward=*/true,
      /*filter_type=*/MoqtFilterType::kAbsoluteStart,
      /*start=*/Location(4, 1),
      /*end_group=*/std::nullopt,
      VersionSpecificParameters(quic::QuicTimeDelta::FromMilliseconds(10000),
                                AuthTokenType::kOutOfBand, "bar"),
  };

 private:
  uint8_t raw_packet_[31] = {
      0x03,
      0x00,
      0x1c,
      0x01,  // request_id = 1
      0x01,
      0x03,
      0x66,
      0x6f,
      0x6f,  // track_namespace = "foo"
      0x04,
      0x61,
      0x62,
      0x63,
      0x64,  // track_name = "abcd"
      0x20,  // subscriber priority = 0x20
      0x02,  // group order = descending
      0x01,  // forward = true
      0x03,  // Filter type: Absolute Start
      0x04,  // start_group = 4
      0x01,  // start_object = 1
      // No EndGroup or EndObject
      0x02,  // 2 parameters
      0x02,
      0x67,
      0x10,  // delivery_timeout = 10000 ms
      0x03,
      0x05,
      0x03,
      0x00,
      0x62,
      0x61,
      0x72,  // authorization_tag = "bar"
  };
};

class QUICHE_NO_EXPORT SubscribeOkMessage : public TestMessageBase {
 public:
  SubscribeOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
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
    if (cast.expires != subscribe_ok_.expires) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK expiration mismatch";
      return false;
    }
    if (cast.group_order != subscribe_ok_.group_order) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK group order mismatch";
      return false;
    }
    if (cast.largest_location != subscribe_ok_.largest_location) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK largest ID mismatch";
      return false;
    }
    if (cast.parameters != subscribe_ok_.parameters) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv--vvvv--v--"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_ok_);
  }

  void SetInvalidContentExists() {
    raw_packet_[7] = 0x02;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  void SetInvalidDeliveryOrder() {
    raw_packet_[6] = 0x10;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 protected:
  // This is protected so that TrackStatusOk can edit the fields.
  MoqtSubscribeOk subscribe_ok_ = {
      /*request_id=*/1,
      /*track_alias=*/2,
      /*expires=*/quic::QuicTimeDelta::FromMilliseconds(3),
      /*group_order=*/MoqtDeliveryOrder::kDescending,
      /*largest_location=*/Location(12, 20),
      VersionSpecificParameters(quic::QuicTimeDelta::FromMilliseconds(10000),
                                quic::QuicTimeDelta::FromMilliseconds(10000)),
  };

 private:
  uint8_t raw_packet_[17] = {
      0x04, 0x00, 0x0e, 0x01, 0x02, 0x03,  // request_id, alias, expires
      0x02, 0x01,                          // group_order = 2, content exists
      0x0c, 0x14,                          // largest_location = (12, 20)
      0x02,                                // 2 parameters
      0x02, 0x67, 0x10,                    // delivery_timeout = 10000
      0x04, 0x67, 0x10,                    // max_cache_duration = 10000
  };
};

class QUICHE_NO_EXPORT SubscribeErrorMessage : public TestMessageBase {
 public:
  SubscribeErrorMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeError>(values);
    if (cast.request_id != subscribe_error_.request_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE ERROR request_id mismatch";
      return false;
    }
    if (cast.error_code != subscribe_error_.error_code) {
      QUIC_LOG(INFO) << "SUBSCRIBE ERROR error code mismatch";
      return false;
    }
    if (cast.reason_phrase != subscribe_error_.reason_phrase) {
      QUIC_LOG(INFO) << "SUBSCRIBE ERROR reason phrase mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_error_);
  }

 protected:
  MoqtSubscribeError subscribe_error_ = {
      /*request_id=*/2,
      /*error_code=*/RequestErrorCode::kInvalidRange,
      /*reason_phrase=*/"bar",
  };

 private:
  uint8_t raw_packet_[9] = {
      0x05, 0x00, 0x06,
      0x02,                    // request_id = 2
      0x05,                    // error_code = 5
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
    if (cast.request_id != subscribe_done_.request_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE_DONE request ID mismatch";
      return false;
    }
    if (cast.status_code != subscribe_done_.status_code) {
      QUIC_LOG(INFO) << "SUBSCRIBE_DONE status code mismatch";
      return false;
    }
    if (cast.stream_count != subscribe_done_.stream_count) {
      QUIC_LOG(INFO) << "SUBSCRIBE_DONE stream count mismatch";
      return false;
    }
    if (cast.error_reason != subscribe_done_.error_reason) {
      QUIC_LOG(INFO) << "SUBSCRIBE_DONE error reason mismatch";
      return false;
    }

    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv--"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_done_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x0b, 0x00, 0x06, 0x02, 0x02,  // request_id = 2, error_code = 2,
      0x05,                          // stream_count = 5
      0x02, 0x68, 0x69,              // error_reason = "hi"
  };

  MoqtPublishDone subscribe_done_ = {
      /*request_id=*/2,
      /*error_code=*/PublishDoneCode::kTrackEnded,
      /*stream_count=*/5,
      /*error_reason=*/"hi",
  };
};

class QUICHE_NO_EXPORT SubscribeUpdateMessage : public TestMessageBase {
 public:
  SubscribeUpdateMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeUpdate>(values);
    if (cast.request_id != subscribe_update_.request_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE subscribe ID mismatch";
      return false;
    }
    if (cast.start != subscribe_update_.start) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE start group mismatch";
      return false;
    }
    if (cast.end_group != subscribe_update_.end_group) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE end group mismatch";
      return false;
    }
    if (cast.subscriber_priority != subscribe_update_.subscriber_priority) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE subscriber priority mismatch";
      return false;
    }
    if (cast.forward != subscribe_update_.forward) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE forward mismatch";
      return false;
    }
    if (cast.parameters != subscribe_update_.parameters) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv--vv--"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_update_);
  }

 private:
  uint8_t raw_packet_[13] = {
      0x02, 0x00, 0x0a, 0x02, 0x03, 0x01, 0x05,  // start and end sequences
      0xaa, 0x01,                                // subscriber_priority, forward
      0x01,                                      // 1 parameter
      0x02, 0x67, 0x10,                          // delivery_timeout = 10000
  };

  MoqtSubscribeUpdate subscribe_update_ = {
      /*request_id=*/2,
      /*start=*/Location(3, 1),
      /*end_group=*/4,
      /*subscriber_priority=*/0xaa,
      /*forward=*/true,
      VersionSpecificParameters(quic::QuicTimeDelta::FromMilliseconds(10000),
                                quic::QuicTimeDelta::Infinite()),
  };
};

class QUICHE_NO_EXPORT PublishNamespaceMessage : public TestMessageBase {
 public:
  PublishNamespaceMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
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
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "bar"),
  };
};

class QUICHE_NO_EXPORT PublishNamespaceOkMessage : public TestMessageBase {
 public:
  PublishNamespaceOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishNamespaceOk>(values);
    if (cast.request_id != publish_namespace_ok_.request_id) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE OK MESSAGE request ID mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_namespace_ok_);
  }

 private:
  uint8_t raw_packet_[4] = {
      0x07, 0x00, 0x01, 0x01,  // request_id = 1
  };

  MoqtPublishNamespaceOk publish_namespace_ok_ = {
      /*request_id=*/1,
  };
};

class QUICHE_NO_EXPORT PublishNamespaceErrorMessage : public TestMessageBase {
 public:
  PublishNamespaceErrorMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishNamespaceError>(values);
    if (cast.request_id != publish_namespace_error_.request_id) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE_ERROR request ID mismatch";
      return false;
    }
    if (cast.error_code != publish_namespace_error_.error_code) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE_ERROR error code mismatch";
      return false;
    }
    if (cast.error_reason != publish_namespace_error_.error_reason) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE_ERROR error reason mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_namespace_error_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x08, 0x00, 0x06, 0x01,  // request_id = 1
      0x03,                    // error_code = 3
      0x03, 0x62, 0x61, 0x72,  // reason_phrase = "bar"
  };

  MoqtPublishNamespaceError publish_namespace_error_ = {
      /*request_id=*/1,
      RequestErrorCode::kNotSupported,
      /*reason_phrase=*/"bar",
  };
};

class QUICHE_NO_EXPORT PublishNamespaceDoneMessage : public TestMessageBase {
 public:
  PublishNamespaceDoneMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishNamespaceDone>(values);
    if (cast.track_namespace != publish_namespace_done_.track_namespace) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE_DONE track namespace mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_namespace_done_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x09, 0x00, 0x05, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace
  };

  MoqtPublishNamespaceDone publish_namespace_done_ = {
      TrackNamespace("foo"),
  };
};

class QUICHE_NO_EXPORT PublishNamespaceCancelMessage : public TestMessageBase {
 public:
  PublishNamespaceCancelMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishNamespaceCancel>(values);
    if (cast.track_namespace != publish_namespace_cancel_.track_namespace) {
      QUIC_LOG(INFO) << "PUBLISH_NAMESPACE CANCEL track namespace mismatch";
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

  void ExpandVarints() override { ExpandVarintsImpl("vv---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_namespace_cancel_);
  }

 private:
  uint8_t raw_packet_[13] = {
      0x0c, 0x00, 0x0a, 0x01,
      0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03,                    // error_code = 3
      0x03, 0x62, 0x61, 0x72,  // error_reason = "bar"
  };

  MoqtPublishNamespaceCancel publish_namespace_cancel_ = {
      TrackNamespace("foo"),
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

class QUICHE_NO_EXPORT TrackStatusOkMessage : public SubscribeOkMessage {
 public:
  TrackStatusOkMessage() : SubscribeOkMessage() {
    SetByte(0, static_cast<uint8_t>(MoqtMessageType::kTrackStatusOk));
    // Track alias is zero.
    SetByte(4, 0x00);
    subscribe_ok_.track_alias = 0;
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto value = std::get<MoqtTrackStatusOk>(values);
    auto* subscribe = reinterpret_cast<MoqtSubscribeOk*>(&value);
    MessageStructuredData structured_data =
        TestMessageBase::MessageStructuredData(*subscribe);
    return SubscribeOkMessage::EqualFieldValues(structured_data);
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(
        MoqtTrackStatusOk(subscribe_ok_));
  }
};

class QUICHE_NO_EXPORT TrackStatusErrorMessage : public SubscribeErrorMessage {
 public:
  TrackStatusErrorMessage() : SubscribeErrorMessage() {
    SetByte(0, static_cast<uint8_t>(MoqtMessageType::kTrackStatusError));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto value = std::get<MoqtTrackStatusError>(values);
    auto* subscribe = reinterpret_cast<MoqtSubscribeError*>(&value);
    MessageStructuredData structured_data =
        TestMessageBase::MessageStructuredData(*subscribe);
    return SubscribeErrorMessage::EqualFieldValues(structured_data);
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(
        MoqtTrackStatusError(subscribe_error_));
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
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeNamespace>(values);
    if (cast.request_id != subscribe_namespace_.request_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE request_id mismatch";
      return false;
    }
    if (cast.track_namespace != subscribe_namespace_.track_namespace) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE track namespace mismatch";
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
  uint8_t raw_packet_[17] = {
      0x11, 0x00, 0x0e, 0x01,                    // request_id = 1
      0x01, 0x03, 0x66, 0x6f, 0x6f,              // namespace = "foo"
      0x01,                                      // 1 parameter
      0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x72,  // authorization_tag = "bar"
  };

  MoqtSubscribeNamespace subscribe_namespace_ = {
      /*request_id=*/1,
      TrackNamespace("foo"),
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "bar"),
  };
};

class QUICHE_NO_EXPORT SubscribeNamespaceOkMessage : public TestMessageBase {
 public:
  SubscribeNamespaceOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeNamespaceOk>(values);
    if (cast.request_id != subscribe_namespace_ok_.request_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE_OK request_id mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_namespace_ok_);
  }

 private:
  uint8_t raw_packet_[4] = {
      0x12, 0x00, 0x01, 0x01,  // request_id = 1
  };

  MoqtSubscribeNamespaceOk subscribe_namespace_ok_ = {
      /*request_id=*/1,
  };
};

class QUICHE_NO_EXPORT SubscribeNamespaceErrorMessage : public TestMessageBase {
 public:
  SubscribeNamespaceErrorMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeNamespaceError>(values);
    if (cast.request_id != subscribe_namespace_error_.request_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE_ERROR request_id mismatch";
      return false;
    }
    if (cast.error_code != subscribe_namespace_error_.error_code) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE_ERROR error code mismatch";
      return false;
    }
    if (cast.error_reason != subscribe_namespace_error_.error_reason) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE_ERROR error reason mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_namespace_error_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x13, 0x00, 0x06, 0x01,  // request_id = 1
      0x01,                    // error_code = 1
      0x03, 0x62, 0x61, 0x72,  // error_reason = "bar"
  };

  MoqtSubscribeNamespaceError subscribe_namespace_error_ = {
      /*request_id=*/1,
      /*error_code=*/RequestErrorCode::kUnauthorized,
      /*error_reason=*/"bar",
  };
};

class QUICHE_NO_EXPORT UnsubscribeNamespaceMessage : public TestMessageBase {
 public:
  UnsubscribeNamespaceMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtUnsubscribeNamespace>(values);
    if (cast.track_namespace != unsubscribe_namespace_.track_namespace) {
      QUIC_LOG(INFO) << "UNSUBSCRIBE_NAMESPACE track namespace mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unsubscribe_namespace_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x14, 0x00, 0x05, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace
  };

  MoqtUnsubscribeNamespace unsubscribe_namespace_ = {
      TrackNamespace("foo"),
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
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetch>(values);
    if (cast.request_id != fetch_.request_id) {
      QUIC_LOG(INFO) << "FETCH request_id mismatch";
      return false;
    }
    if (cast.subscriber_priority != fetch_.subscriber_priority) {
      QUIC_LOG(INFO) << "FETCH subscriber_priority mismatch";
      return false;
    }
    if (cast.group_order != fetch_.group_order) {
      QUIC_LOG(INFO) << "FETCH group_order mismatch";
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
    ExpandVarintsImpl("v--vvv---v---vvvvvv-----");
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
    raw_packet_[18] = group;
    raw_packet_[19] = object.has_value() ? (*object + 1) : 0;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  void SetGroupOrder(uint8_t group_order) {
    raw_packet_[5] = static_cast<uint8_t>(group_order);
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[28] = {
      0x16, 0x00, 0x19,
      0x01,                          // request_id = 1
      0x02,                          // priority = kHigh
      0x01,                          // group_order = kAscending
      0x01,                          // type = kStandalone
      0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,        // track_name = "bar"
      0x01, 0x02,                    // start_location = 1, 2
      0x05, 0x07,                    // end_location = 5, 6
      0x01, 0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x7a,  // parameters = "baz"
  };

  MoqtFetch fetch_ = {
      /*request_id=*/1,
      /*subscriber_priority=*/2,
      /*group_order=*/MoqtDeliveryOrder::kAscending,
      /*fetch =*/
      StandaloneFetch{
          FullTrackName("foo", "bar"),
          /*start_location=*/Location{1, 2},
          /*end_location=*/Location{5, 6},
      },
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "baz"),
  };
};

// This is not used in the parameterized Parser and Framer tests, because it
// does not have its own MoqtMessageType.
class QUICHE_NO_EXPORT RelativeJoiningFetchMessage : public TestMessageBase {
 public:
  RelativeJoiningFetchMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetch>(values);
    if (cast.request_id != fetch_.request_id) {
      QUIC_LOG(INFO) << "FETCH request_id mismatch";
      return false;
    }
    if (cast.subscriber_priority != fetch_.subscriber_priority) {
      QUIC_LOG(INFO) << "FETCH subscriber_priority mismatch";
      return false;
    }
    if (cast.group_order != fetch_.group_order) {
      QUIC_LOG(INFO) << "FETCH group_order mismatch";
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
    ExpandVarintsImpl("v--vvv---v---vvvvvv-----");
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_);
  }

  void SetGroupOrder(uint8_t group_order) {
    raw_packet_[5] = static_cast<uint8_t>(group_order);
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[17] = {
      0x16, 0x00, 0x0e,
      0x01,        // request_id = 1
      0x02,        // priority = kHigh
      0x01,        // group_order = kAscending
      0x02,        // type = kRelativeJoining
      0x02, 0x02,  // joining_request_id = 2, 2 groups
      0x01, 0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x7a,  // parameters = "baz"
  };

  MoqtFetch fetch_ = {
      /*request_id =*/1,
      /*subscriber_priority=*/2,
      /*group_order=*/MoqtDeliveryOrder::kAscending,
      /*fetch=*/JoiningFetchRelative{2, 2},
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "baz"),
  };
};

// This is not used in the parameterized Parser and Framer tests, because it
// does not have its own MoqtMessageType.
class QUICHE_NO_EXPORT AbsoluteJoiningFetchMessage : public TestMessageBase {
 public:
  AbsoluteJoiningFetchMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetch>(values);
    if (cast.request_id != fetch_.request_id) {
      QUIC_LOG(INFO) << "FETCH request_id mismatch";
      return false;
    }
    if (cast.subscriber_priority != fetch_.subscriber_priority) {
      QUIC_LOG(INFO) << "FETCH subscriber_priority mismatch";
      return false;
    }
    if (cast.group_order != fetch_.group_order) {
      QUIC_LOG(INFO) << "FETCH group_order mismatch";
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
    ExpandVarintsImpl("v--vvv---v---vvvvvv-----");
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_);
  }

  void SetGroupOrder(uint8_t group_order) {
    raw_packet_[5] = static_cast<uint8_t>(group_order);
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[17] = {
      0x16, 0x00, 0x0e,
      0x01,        // request_id = 1
      0x02,        // priority = kHigh
      0x01,        // group_order = kAscending
      0x03,        // type = kAbsoluteJoining
      0x02, 0x02,  // joining_request_id = 2, group_id = 2
      0x01, 0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x7a,  // parameters = "baz"
  };

  MoqtFetch fetch_ = {
      /*request_id=*/1,
      /*subscriber_priority=*/2,
      /*group_order=*/MoqtDeliveryOrder::kAscending,
      /*fetch=*/JoiningFetchAbsolute{2, 2},
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "baz"),
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
    if (cast.group_order != fetch_ok_.group_order) {
      QUIC_LOG(INFO) << "FETCH_OK group_order mismatch";
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
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v--vvvvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_ok_);
  }

 private:
  uint8_t raw_packet_[12] = {
      0x18, 0x00, 0x09,
      0x01,                    // request_id = 1
      0x01,                    // group_order = kAscending
      0x00,                    // end_of_track = false
      0x05, 0x04,              // end_location = 5, 3
      0x01, 0x04, 0x67, 0x10,  // MaxCacheDuration = 10000
  };

  MoqtFetchOk fetch_ok_ = {
      /*request_id =*/1,
      /*group_order=*/MoqtDeliveryOrder::kAscending,
      /*end_of_track=*/false,
      /*end_location=*/Location{5, 3},
      VersionSpecificParameters(quic::QuicTimeDelta::Infinite(),
                                quic::QuicTimeDelta::FromMilliseconds(10000)),
  };
};

class QUICHE_NO_EXPORT FetchErrorMessage : public TestMessageBase {
 public:
  FetchErrorMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetchError>(values);
    if (cast.request_id != fetch_error_.request_id) {
      QUIC_LOG(INFO) << "FETCH_ERROR request_id mismatch";
      return false;
    }
    if (cast.error_code != fetch_error_.error_code) {
      QUIC_LOG(INFO) << "FETCH_ERROR group_order mismatch";
      return false;
    }
    if (cast.error_reason != fetch_error_.error_reason) {
      QUIC_LOG(INFO) << "FETCH_ERROR error_reason mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_error_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x19, 0x00, 0x06,
      0x01,                    // request_id = 1
      0x01,                    // error_code = kUnauthorized
      0x03, 0x62, 0x61, 0x72,  // error_reason = "bar"
  };

  MoqtFetchError fetch_error_ = {
      /*request_id =*/1,
      /*error_code=*/RequestErrorCode::kUnauthorized,
      /*error_reason=*/"bar",
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
    if (cast.group_order != publish_.group_order) {
      QUIC_LOG(INFO) << "PUBLISH group_order mismatch";
      return false;
    }
    if (cast.largest_location != publish_.largest_location) {
      QUIC_LOG(INFO) << "PUBLISH largest_location mismatch";
      return false;
    }
    if (cast.forward != publish_.forward) {
      QUIC_LOG(INFO) << "PUBLISH forward mismatch";
      return false;
    }
    if (cast.parameters != publish_.parameters) {
      QUIC_LOG(INFO) << "PUBLISH parameters mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvv---v---v--vv-vvv-----");
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_);
  }

 private:
  uint8_t raw_packet_[27] = {
      0x1d, 0x00, 0x18,
      0x01,                          // request_id = 1
      0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,        // track_name = "bar"
      0x04,                          // track_alias = 4
      0x01,                          // group_order = kAscending
      0x01, 0x0a, 0x01,              // content exists, largest_location = 10, 1
      0x01,                          // forward = true
      0x01, 0x03, 0x05, 0x03, 0x00, 0x62, 0x61, 0x7a,  // parameters = "baz"
  };

  MoqtPublish publish_ = {
      /*request_id=*/1,
      FullTrackName("foo", "bar"),
      /*track_alias=*/4,
      MoqtDeliveryOrder::kAscending,
      /*largest_location=*/Location(10, 1),
      /*forward=*/true,
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "baz"),
  };
};

class QUICHE_NO_EXPORT PublishOkMessage : public TestMessageBase {
 public:
  PublishOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishOk>(values);
    if (cast.request_id != publish_ok_.request_id) {
      QUIC_LOG(INFO) << "PUBLISH_OK request_id mismatch";
      return false;
    }
    if (cast.forward != publish_ok_.forward) {
      QUIC_LOG(INFO) << "PUBLISH_OK forward mismatch";
      return false;
    }
    if (cast.subscriber_priority != publish_ok_.subscriber_priority) {
      QUIC_LOG(INFO) << "PUBLISH_OK subscriber_priority mismatch";
      return false;
    }
    if (cast.group_order != publish_ok_.group_order) {
      QUIC_LOG(INFO) << "PUBLISH_OK group_order mismatch";
      return false;
    }
    if (cast.filter_type != publish_ok_.filter_type) {
      QUIC_LOG(INFO) << "PUBLISH_OK filter_type mismatch";
      return false;
    }
    if (cast.start != publish_ok_.start) {
      QUIC_LOG(INFO) << "PUBLISH_OK start mismatch";
      return false;
    }
    if (cast.end_group != publish_ok_.end_group) {
      QUIC_LOG(INFO) << "PUBLISH_OK end_group mismatch";
      return false;
    }
    if (cast.parameters != publish_ok_.parameters) {
      QUIC_LOG(INFO) << "PUBLISH_OK parameters mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v---vvvvvv--"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_ok_);
  }

 private:
  uint8_t raw_packet_[15] = {
      0x1e, 0x00, 0x0c,
      0x01,                    // request_id = 1
      0x01,                    // forward = true
      0x02,                    // subscriber_priority = 2
      0x01,                    // group_order = kAscending
      0x04,                    // filter_type = kAbsoluteRange
      0x05, 0x04,              // start = 5, 4
      0x06,                    // end_group = 6
      0x01, 0x02, 0x67, 0x10,  // delivery_timeout = 10000 ms
  };
  MoqtPublishOk publish_ok_ = {
      /*request_id=*/1,
      /*forward=*/true,
      /*subscriber_priority=*/2,
      MoqtDeliveryOrder::kAscending,
      MoqtFilterType::kAbsoluteRange,
      /*start=*/Location(5, 4),
      /*end_group=*/6,
      VersionSpecificParameters(quic::QuicTimeDelta::FromMilliseconds(10000),
                                quic::QuicTimeDelta::Infinite()),
  };
};

class QUICHE_NO_EXPORT PublishErrorMessage : public TestMessageBase {
 public:
  PublishErrorMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtPublishError>(values);
    if (cast.request_id != publish_error_.request_id) {
      QUIC_LOG(INFO) << "PUBLISH_ERROR request_id mismatch";
      return false;
    }
    if (cast.error_code != publish_error_.error_code) {
      QUIC_LOG(INFO) << "PUBLISH_ERROR error_code mismatch";
      return false;
    }
    if (cast.error_reason != publish_error_.error_reason) {
      QUIC_LOG(INFO) << "PUBLISH_ERROR error_reason mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(publish_error_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x1f, 0x00, 0x06,
      0x01,                    // request_id = 1
      0x01,                    // error_code = kUnauthorized
      0x03, 0x62, 0x61, 0x72,  // error_reason = "bar"
  };
  MoqtPublishError publish_error_ = {
      /*request_id=*/1,
      /*error_code=*/RequestErrorCode::kUnauthorized,
      /*error_reason=*/"bar",
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
    case MoqtMessageType::kSubscribe:
      return std::make_unique<SubscribeMessage>();
    case MoqtMessageType::kSubscribeOk:
      return std::make_unique<SubscribeOkMessage>();
    case MoqtMessageType::kSubscribeError:
      return std::make_unique<SubscribeErrorMessage>();
    case MoqtMessageType::kUnsubscribe:
      return std::make_unique<UnsubscribeMessage>();
    case MoqtMessageType::kPublishDone:
      return std::make_unique<PublishDoneMessage>();
    case MoqtMessageType::kSubscribeUpdate:
      return std::make_unique<SubscribeUpdateMessage>();
    case MoqtMessageType::kPublishNamespace:
      return std::make_unique<PublishNamespaceMessage>();
    case MoqtMessageType::kPublishNamespaceOk:
      return std::make_unique<PublishNamespaceOkMessage>();
    case MoqtMessageType::kPublishNamespaceError:
      return std::make_unique<PublishNamespaceErrorMessage>();
    case MoqtMessageType::kPublishNamespaceDone:
      return std::make_unique<PublishNamespaceDoneMessage>();
    case MoqtMessageType::kPublishNamespaceCancel:
      return std::make_unique<PublishNamespaceCancelMessage>();
    case MoqtMessageType::kTrackStatus:
      return std::make_unique<TrackStatusMessage>();
    case MoqtMessageType::kTrackStatusOk:
      return std::make_unique<TrackStatusOkMessage>();
    case MoqtMessageType::kTrackStatusError:
      return std::make_unique<TrackStatusErrorMessage>();
    case MoqtMessageType::kGoAway:
      return std::make_unique<GoAwayMessage>();
    case MoqtMessageType::kSubscribeNamespace:
      return std::make_unique<SubscribeNamespaceMessage>();
    case MoqtMessageType::kSubscribeNamespaceOk:
      return std::make_unique<SubscribeNamespaceOkMessage>();
    case MoqtMessageType::kSubscribeNamespaceError:
      return std::make_unique<SubscribeNamespaceErrorMessage>();
    case MoqtMessageType::kUnsubscribeNamespace:
      return std::make_unique<UnsubscribeNamespaceMessage>();
    case MoqtMessageType::kMaxRequestId:
      return std::make_unique<MaxRequestIdMessage>();
    case MoqtMessageType::kFetch:
      return std::make_unique<FetchMessage>();
    case MoqtMessageType::kFetchCancel:
      return std::make_unique<FetchCancelMessage>();
    case MoqtMessageType::kFetchOk:
      return std::make_unique<FetchOkMessage>();
    case MoqtMessageType::kFetchError:
      return std::make_unique<FetchErrorMessage>();
    case MoqtMessageType::kRequestsBlocked:
      return std::make_unique<RequestsBlockedMessage>();
    case MoqtMessageType::kPublish:
      return std::make_unique<PublishMessage>();
    case MoqtMessageType::kPublishOk:
      return std::make_unique<PublishOkMessage>();
    case MoqtMessageType::kPublishError:
      return std::make_unique<PublishErrorMessage>();
    case MoqtMessageType::kObjectAck:
      return std::make_unique<ObjectAckMessage>();
    case MoqtMessageType::kClientSetup:
      return std::make_unique<ClientSetupMessage>(is_webtrans);
    case MoqtMessageType::kServerSetup:
      return std::make_unique<ServerSetupMessage>(is_webtrans);
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

static inline std::unique_ptr<TestMessageBase> CreateTestDatagram(
    MoqtDatagramType type) {
  return std::make_unique<ObjectDatagramMessage>(type);
}

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_
