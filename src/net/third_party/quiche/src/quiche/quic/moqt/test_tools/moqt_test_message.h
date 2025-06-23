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
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_endian.h"

namespace moqt::test {

inline constexpr absl::string_view kDefaultExtensionBlob(
    "\x00\x0c\x01\x03\x66\x6f\x6f", 7);

// Base class containing a wire image and the corresponding structured
// representation of an example of each message. It allows parser and framer
// tests to iterate through all message types without much specialized code.
class QUICHE_NO_EXPORT TestMessageBase {
 public:
  virtual ~TestMessageBase() = default;

  using MessageStructuredData =
      std::variant<MoqtClientSetup, MoqtServerSetup, MoqtObject, MoqtSubscribe,
                   MoqtSubscribeOk, MoqtSubscribeError, MoqtUnsubscribe,
                   MoqtSubscribeDone, MoqtSubscribeUpdate, MoqtAnnounce,
                   MoqtAnnounceOk, MoqtAnnounceError, MoqtAnnounceCancel,
                   MoqtTrackStatusRequest, MoqtUnannounce, MoqtTrackStatus,
                   MoqtGoAway, MoqtSubscribeAnnounces, MoqtSubscribeAnnouncesOk,
                   MoqtSubscribeAnnouncesError, MoqtUnsubscribeAnnounces,
                   MoqtMaxRequestId, MoqtFetch, MoqtFetchCancel, MoqtFetchOk,
                   MoqtFetchError, MoqtRequestsBlocked, MoqtObjectAck>;

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

 protected:
  void SetWireImage(uint8_t* wire_image, size_t wire_image_size) {
    memcpy(wire_image_, wire_image, wire_image_size);
    wire_image_size_ = wire_image_size;
  }

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
      /*subgroup_id=*/std::nullopt,
      /*payload_length=*/3,
  };
};

class QUICHE_NO_EXPORT ObjectDatagramMessage : public ObjectMessage {
 public:
  ObjectDatagramMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvv-v-------v---", false);
  }

 private:
  uint8_t raw_packet_[17] = {
      0x01, 0x04, 0x05, 0x06,  // varints
      0x07, 0x07,              // publisher priority, 7B extensions
      0x00, 0x0c, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // extensions
      0x03, 0x66, 0x6f, 0x6f,                    // payload = "foo"
  };
};

class QUICHE_NO_EXPORT ObjectStatusDatagramMessage : public ObjectMessage {
 public:
  ObjectStatusDatagramMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.object_status = MoqtObjectStatus::kEndOfGroup;
    object_.payload_length = 0;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv-v-------v", false); }

 private:
  uint8_t raw_packet_[14] = {
      0x02, 0x04, 0x05, 0x06,                    // varints
      0x07,                                      // publisher priority
      0x07,                                      // 7B extensions
      0x00, 0x0c, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // extensions
      0x03,                                      // kEndOfGroup
  };
};

// Concatenation of the base header and the object-specific header. Follow-on
// object headers are handled in a different class.
class QUICHE_NO_EXPORT StreamHeaderSubgroupMessage : public ObjectMessage {
 public:
  StreamHeaderSubgroupMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.subgroup_id = 8;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvv-vv-------v---", false);
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
      0x04,                                      // type field
      0x04, 0x05, 0x08,                          // varints
      0x07,                                      // publisher priority
      0x06, 0x07,                                // object ID, 7B extensions
      0x00, 0x0c, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // extensions
      0x03, 0x66, 0x6f, 0x6f,                    // payload = "foo"
  };
};

// Used only for tests that process multiple objects on one stream.
class QUICHE_NO_EXPORT StreamMiddlerSubgroupMessage : public ObjectMessage {
 public:
  StreamMiddlerSubgroupMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.subgroup_id = 8;
    object_.object_id = 9;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv-------v---", false); }

 private:
  uint8_t raw_packet_[13] = {
      0x09, 0x07,                                // object ID; 7B extensions
      0x00, 0x0c, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // extensions
      0x03, 0x62, 0x61, 0x72,                    // payload = "bar"
  };
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
      0x05, 0x08, 0x09, 0x07, 0x07,              // Object metadata
      0x00, 0x0c, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // extensions
      0x03, 0x62, 0x61, 0x72,                    // Payload = "bar"
  };
};

class QUICHE_NO_EXPORT ClientSetupMessage : public TestMessageBase {
 public:
  explicit ClientSetupMessage(bool webtrans) : TestMessageBase() {
    client_setup_.parameters.using_webtrans = webtrans;
    if (webtrans) {
      // Should not send PATH.
      client_setup_.parameters.path = "";
      raw_packet_[2] = 0x06;  // adjust payload length (-5)
      raw_packet_[6] = 0x01;  // only one parameter
      SetWireImage(raw_packet_, sizeof(raw_packet_) - 5);
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
      ExpandVarintsImpl("vvvvvvvv---");
    } else {
      ExpandVarintsImpl("vvvvvv");
    }
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(client_setup_);
  }

 private:
  uint8_t raw_packet_[14] = {
      0x20, 0x00, 0x0b,              // type
      0x02, 0x01, 0x02,              // versions
      0x02,                          // 2 parameters
      0x02, 0x32,                    // max_request_id = 50
      0x01, 0x03, 0x66, 0x6f, 0x6f,  // path = "foo"
  };
  MoqtClientSetup client_setup_ = {
      /*supported_versions=*/std::vector<MoqtVersion>(
          {static_cast<MoqtVersion>(1), static_cast<MoqtVersion>(2)}),
      MoqtSessionParameters(quic::Perspective::IS_CLIENT, "foo", 50),
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
  uint8_t raw_packet_[7] = {
      0x21, 0x00, 0x04,  // type
      0x01, 0x01,        // version, one parameter
      0x02, 0x32,        // max_subscribe_id = 50
  };
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
    if (cast.track_alias != subscribe_.track_alias) {
      QUIC_LOG(INFO) << "SUBSCRIBE track alias mismatch";
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
    ExpandVarintsImpl("vvvv---v-------vvvvv--vv-----");
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_);
  }

 private:
  uint8_t raw_packet_[32] = {
      0x03, 0x00, 0x1d, 0x01, 0x02,  // id and alias
      0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x04, 0x61, 0x62, 0x63, 0x64,  // track_name = "abcd"
      0x20,                          // subscriber priority = 0x20
      0x02,                          // group order = descending
      0x01,                          // forward = true
      0x03,                          // Filter type: Absolute Start
      0x04,                          // start_group = 4
      0x01,                          // start_object = 1
      // No EndGroup or EndObject
      0x02,                                      // 2 parameters
      0x02, 0x67, 0x10,                          // delivery_timeout = 10000 ms
      0x01, 0x05, 0x03, 0x00, 0x62, 0x61, 0x72,  // authorization_tag = "bar"
  };

  MoqtSubscribe subscribe_ = {
      /*subscribe_id=*/1,
      /*track_alias=*/2,
      /*full_track_name=*/FullTrackName({"foo", "abcd"}),
      /*subscriber_priority=*/0x20,
      /*group_order=*/MoqtDeliveryOrder::kDescending,
      /*forward=*/true,
      /*filter_type=*/MoqtFilterType::kAbsoluteStart,
      /*start=*/Location(4, 1),
      /*end_group=*/std::nullopt,
      VersionSpecificParameters(quic::QuicTimeDelta::FromMilliseconds(10000),
                                AuthTokenType::kOutOfBand, "bar"),
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

  void ExpandVarints() override { ExpandVarintsImpl("vv--vvvv--v--"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_ok_);
  }

  void SetInvalidContentExists() {
    raw_packet_[6] = 0x02;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  void SetInvalidDeliveryOrder() {
    raw_packet_[5] = 0x10;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[16] = {
      0x04, 0x00, 0x0d, 0x01, 0x03,  // request_id = 1, expires = 3
      0x02, 0x01,                    // group_order = 2, content exists
      0x0c, 0x14,                    // largest_location = (12, 20)
      0x02,                          // 2 parameters
      0x02, 0x67, 0x10,              // delivery_timeout = 10000
      0x04, 0x67, 0x10,              // max_cache_duration = 10000
  };

  MoqtSubscribeOk subscribe_ok_ = {
      /*request_id=*/1,
      /*expires=*/quic::QuicTimeDelta::FromMilliseconds(3),
      /*group_order=*/MoqtDeliveryOrder::kDescending,
      /*largest_location=*/Location(12, 20),
      VersionSpecificParameters(quic::QuicTimeDelta::FromMilliseconds(10000),
                                quic::QuicTimeDelta::FromMilliseconds(10000)),
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
    if (cast.track_alias != subscribe_error_.track_alias) {
      QUIC_LOG(INFO) << "SUBSCRIBE ERROR track alias mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---v"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_error_);
  }

 private:
  uint8_t raw_packet_[10] = {
      0x05, 0x00, 0x07,
      0x02,                    // request_id = 2
      0x05,                    // error_code = 5
      0x03, 0x62, 0x61, 0x72,  // reason_phrase = "bar"
      0x04,                    // track_alias = 4
  };

  MoqtSubscribeError subscribe_error_ = {
      /*request_id=*/2,
      /*error_code=*/RequestErrorCode::kInvalidRange,
      /*reason_phrase=*/"bar",
      /*track_alias=*/4,
  };
};

class QUICHE_NO_EXPORT UnsubscribeMessage : public TestMessageBase {
 public:
  UnsubscribeMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtUnsubscribe>(values);
    if (cast.subscribe_id != unsubscribe_.subscribe_id) {
      QUIC_LOG(INFO) << "UNSUBSCRIBE subscribe ID mismatch";
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
      0x0a, 0x00, 0x01, 0x03,  // subscribe_id = 3
  };

  MoqtUnsubscribe unsubscribe_ = {
      /*subscribe_id=*/3,
  };
};

class QUICHE_NO_EXPORT SubscribeDoneMessage : public TestMessageBase {
 public:
  SubscribeDoneMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeDone>(values);
    if (cast.subscribe_id != subscribe_done_.subscribe_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE_DONE subscribe ID mismatch";
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
    if (cast.reason_phrase != subscribe_done_.reason_phrase) {
      QUIC_LOG(INFO) << "SUBSCRIBE_DONE reason phrase mismatch";
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
      0x0b, 0x00, 0x06, 0x02, 0x02,  // subscribe_id = 2, error_code = 2,
      0x05,                          // stream_count = 5
      0x02, 0x68, 0x69,              // reason_phrase = "hi"
  };

  MoqtSubscribeDone subscribe_done_ = {
      /*subscribe_id=*/2,
      /*error_code=*/SubscribeDoneCode::kTrackEnded,
      /*stream_count=*/5,
      /*reason_phrase=*/"hi",
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

class QUICHE_NO_EXPORT AnnounceMessage : public TestMessageBase {
 public:
  AnnounceMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtAnnounce>(values);
    if (cast.track_namespace != announce_.track_namespace) {
      QUIC_LOG(INFO) << "ANNOUNCE MESSAGE track namespace mismatch";
      return false;
    }
    if (cast.parameters != announce_.parameters) {
      QUIC_LOG(INFO) << "ANNOUNCE MESSAGE parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---vvv-----"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_);
  }

 private:
  uint8_t raw_packet_[16] = {
      0x06, 0x00, 0x0d, 0x01, 0x03, 0x66, 0x6f,
      0x6f,                                      // track_namespace = "foo"
      0x01,                                      // 1 parameter
      0x01, 0x05, 0x03, 0x00, 0x62, 0x61, 0x72,  // authorization_tag = "bar"
  };

  MoqtAnnounce announce_ = {
      /*track_namespace=*/FullTrackName{"foo"},
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "bar"),
  };
};

class QUICHE_NO_EXPORT AnnounceOkMessage : public TestMessageBase {
 public:
  AnnounceOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtAnnounceOk>(values);
    if (cast.track_namespace != announce_ok_.track_namespace) {
      QUIC_LOG(INFO) << "ANNOUNCE OK MESSAGE track namespace mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_ok_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x07, 0x00, 0x05, 0x01,
      0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
  };

  MoqtAnnounceOk announce_ok_ = {
      /*track_namespace=*/FullTrackName{"foo"},
  };
};

class QUICHE_NO_EXPORT AnnounceErrorMessage : public TestMessageBase {
 public:
  AnnounceErrorMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtAnnounceError>(values);
    if (cast.track_namespace != announce_error_.track_namespace) {
      QUIC_LOG(INFO) << "ANNOUNCE ERROR track namespace mismatch";
      return false;
    }
    if (cast.error_code != announce_error_.error_code) {
      QUIC_LOG(INFO) << "ANNOUNCE ERROR error code mismatch";
      return false;
    }
    if (cast.reason_phrase != announce_error_.reason_phrase) {
      QUIC_LOG(INFO) << "ANNOUNCE ERROR reason phrase mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_error_);
  }

 private:
  uint8_t raw_packet_[13] = {
      0x08, 0x00, 0x0a, 0x01,
      0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03,                    // error_code = 3
      0x03, 0x62, 0x61, 0x72,  // reason_phrase = "bar"
  };

  MoqtAnnounceError announce_error_ = {
      /*track_namespace=*/FullTrackName{"foo"},
      /*error_code=*/RequestErrorCode::kNotSupported,
      /*reason_phrase=*/"bar",
  };
};

class QUICHE_NO_EXPORT AnnounceCancelMessage : public TestMessageBase {
 public:
  AnnounceCancelMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtAnnounceCancel>(values);
    if (cast.track_namespace != announce_cancel_.track_namespace) {
      QUIC_LOG(INFO) << "ANNOUNCE CANCEL track namespace mismatch";
      return false;
    }
    if (cast.error_code != announce_cancel_.error_code) {
      QUIC_LOG(INFO) << "ANNOUNCE CANCEL error code mismatch";
      return false;
    }
    if (cast.reason_phrase != announce_cancel_.reason_phrase) {
      QUIC_LOG(INFO) << "ANNOUNCE CANCEL reason phrase mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_cancel_);
  }

 private:
  uint8_t raw_packet_[13] = {
      0x0c, 0x00, 0x0a, 0x01,
      0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03,                    // error_code = 3
      0x03, 0x62, 0x61, 0x72,  // reason_phrase = "bar"
  };

  MoqtAnnounceCancel announce_cancel_ = {
      /*track_namespace=*/FullTrackName{"foo"},
      /*error_code=*/RequestErrorCode::kNotSupported,
      /*reason_phrase=*/"bar",
  };
};

class QUICHE_NO_EXPORT TrackStatusRequestMessage : public TestMessageBase {
 public:
  TrackStatusRequestMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtTrackStatusRequest>(values);
    if (cast.full_track_name != track_status_request_.full_track_name) {
      QUIC_LOG(INFO) << "TRACK STATUS REQUEST track name mismatch";
      return false;
    }
    if (cast.parameters != track_status_request_.parameters) {
      QUIC_LOG(INFO) << "TRACK STATUS REQUEST parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---v----vvv-----"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(track_status_request_);
  }

 private:
  uint8_t raw_packet_[21] = {
      0x0d, 0x00, 0x12, 0x01, 0x03, 0x66, 0x6f,
      0x6f,                                      // track_namespace = "foo"
      0x04, 0x61, 0x62, 0x63, 0x64,              // track_name = "abcd"
      0x01,                                      // 1 parameter
      0x01, 0x05, 0x03, 0x00, 0x62, 0x61, 0x72,  // authorization_tag = "bar"
  };

  MoqtTrackStatusRequest track_status_request_ = {
      /*full_track_name=*/FullTrackName({"foo", "abcd"}),
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "bar"),
  };
};

class QUICHE_NO_EXPORT UnannounceMessage : public TestMessageBase {
 public:
  UnannounceMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtUnannounce>(values);
    if (cast.track_namespace != unannounce_.track_namespace) {
      QUIC_LOG(INFO) << "UNANNOUNCE track namespace mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unannounce_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x09, 0x00, 0x05, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace
  };

  MoqtUnannounce unannounce_ = {
      /*track_namespace=*/FullTrackName{"foo"},
  };
};

class QUICHE_NO_EXPORT TrackStatusMessage : public TestMessageBase {
 public:
  TrackStatusMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtTrackStatus>(values);
    if (cast.full_track_name != track_status_.full_track_name) {
      QUIC_LOG(INFO) << "TRACK STATUS track name mismatch";
      return false;
    }
    if (cast.status_code != track_status_.status_code) {
      QUIC_LOG(INFO) << "TRACK STATUS code mismatch";
      return false;
    }
    if (cast.last_group != track_status_.last_group) {
      QUIC_LOG(INFO) << "TRACK STATUS last group mismatch";
      return false;
    }
    if (cast.last_object != track_status_.last_object) {
      QUIC_LOG(INFO) << "TRACK STATUS last object mismatch";
      return false;
    }
    if (cast.parameters != track_status_.parameters) {
      QUIC_LOG(INFO) << "TRACK STATUS parameters mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---v----vvvvv--v--"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(track_status_);
  }

 private:
  uint8_t raw_packet_[23] = {
      0x0e, 0x00, 0x14, 0x01, 0x03,
      0x66, 0x6f, 0x6f,              // track_namespace = "foo"
      0x04, 0x61, 0x62, 0x63, 0x64,  // track_name = "abcd"
      0x00, 0x0c, 0x14,              // status, last_group, last_object
      0x02,                          // 2 parameters
      0x02, 0x67, 0x10,              // Delivery Timeout = 10000
      0x04, 0x67, 0x10,              // Max Cache Duration = 10000
  };

  MoqtTrackStatus track_status_ = {
      /*full_track_name=*/FullTrackName({"foo", "abcd"}),
      /*status_code=*/MoqtTrackStatusCode::kInProgress,
      /*last_group=*/12,
      /*last_object=*/20,
      VersionSpecificParameters(quic::QuicTimeDelta::FromMilliseconds(10000),
                                quic::QuicTimeDelta::FromMilliseconds(10000)),
  };
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

class QUICHE_NO_EXPORT SubscribeAnnouncesMessage : public TestMessageBase {
 public:
  SubscribeAnnouncesMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeAnnounces>(values);
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

  void ExpandVarints() override { ExpandVarintsImpl("vv---vvv-----"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_namespace_);
  }

 private:
  uint8_t raw_packet_[16] = {
      0x11, 0x00, 0x0d, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // namespace = "foo"
      0x01,                                            // 1 parameter
      0x01, 0x05, 0x03, 0x00, 0x62, 0x61, 0x72,  // authorization_tag = "bar"
  };

  MoqtSubscribeAnnounces subscribe_namespace_ = {
      /*track_namespace=*/FullTrackName{"foo"},
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "bar"),
  };
};

class QUICHE_NO_EXPORT SubscribeAnnouncesOkMessage : public TestMessageBase {
 public:
  SubscribeAnnouncesOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeAnnouncesOk>(values);
    if (cast.track_namespace != subscribe_namespace_ok_.track_namespace) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE_OK track namespace mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_namespace_ok_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x12, 0x00, 0x05, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // namespace = "foo"
  };

  MoqtSubscribeAnnouncesOk subscribe_namespace_ok_ = {
      /*track_namespace=*/FullTrackName{"foo"},
  };
};

class QUICHE_NO_EXPORT SubscribeAnnouncesErrorMessage : public TestMessageBase {
 public:
  SubscribeAnnouncesErrorMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeAnnouncesError>(values);
    if (cast.track_namespace != subscribe_namespace_error_.track_namespace) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE_ERROR track namespace mismatch";
      return false;
    }
    if (cast.error_code != subscribe_namespace_error_.error_code) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE_ERROR error code mismatch";
      return false;
    }
    if (cast.reason_phrase != subscribe_namespace_error_.reason_phrase) {
      QUIC_LOG(INFO) << "SUBSCRIBE_NAMESPACE_ERROR reason phrase mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_namespace_error_);
  }

 private:
  uint8_t raw_packet_[13] = {
      0x13, 0x00, 0x0a, 0x01,
      0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x01,                    // error_code = 1
      0x03, 0x62, 0x61, 0x72,  // reason_phrase = "bar"
  };

  MoqtSubscribeAnnouncesError subscribe_namespace_error_ = {
      /*track_namespace=*/FullTrackName{"foo"},
      /*error_code=*/RequestErrorCode::kUnauthorized,
      /*reason_phrase=*/"bar",
  };
};

class QUICHE_NO_EXPORT UnsubscribeAnnouncesMessage : public TestMessageBase {
 public:
  UnsubscribeAnnouncesMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtUnsubscribeAnnounces>(values);
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

  MoqtUnsubscribeAnnounces unsubscribe_namespace_ = {
      /*track_namespace=*/FullTrackName{"foo"},
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
    if (cast.fetch_id != fetch_.fetch_id) {
      QUIC_LOG(INFO) << "FETCH fetch_id mismatch";
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
    if (cast.joining_fetch.has_value() != fetch_.joining_fetch.has_value()) {
      QUIC_LOG(INFO) << "FETCH type mismatch";
      return false;
    }
    if (cast.joining_fetch.has_value()) {
      if (cast.joining_fetch->joining_subscribe_id !=
          fetch_.joining_fetch->joining_subscribe_id) {
        QUIC_LOG(INFO) << "FETCH joining_subscribe_id mismatch";
        return false;
      }
      if (cast.joining_fetch->preceding_group_offset !=
          fetch_.joining_fetch->preceding_group_offset) {
        QUIC_LOG(INFO) << "FETCH preceding_group_offset mismatch";
        return false;
      }
    } else {
      if (cast.full_track_name != fetch_.full_track_name) {
        QUIC_LOG(INFO) << "FETCH full_track_name mismatch";
        return false;
      }
      if (cast.start_object != fetch_.start_object) {
        QUIC_LOG(INFO) << "FETCH start_object mismatch";
        return false;
      }
      if (cast.end_group != fetch_.end_group) {
        QUIC_LOG(INFO) << "FETCH end_group mismatch";
        return false;
      }
      if (cast.end_object != fetch_.end_object) {
        QUIC_LOG(INFO) << "FETCH end_object mismatch";
        return false;
      }
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
    fetch_.end_group = group;
    fetch_.end_object = object;
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
      0x01,                          // fetch_id = 1
      0x02,                          // priority = kHigh
      0x01,                          // group_order = kAscending
      0x01,                          // type = kStandalone
      0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,        // track_name = "bar"
      0x01, 0x02,                    // start_object = 1, 2
      0x05, 0x07,                    // end_object = 5, 6
      0x01, 0x01, 0x05, 0x03, 0x00, 0x62, 0x61, 0x7a,  // parameters = "baz"
  };

  MoqtFetch fetch_ = {
      /*fetch_id =*/1,
      /*subscriber_priority=*/2,
      /*group_order=*/MoqtDeliveryOrder::kAscending,
      /*joining_fetch=*/std::optional<JoiningFetch>(),
      /*full_track_name=*/FullTrackName{"foo", "bar"},
      /*start_object=*/Location{1, 2},
      /*end_group=*/5,
      /*end_object=*/6,
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "baz"),
  };
};

// This is not used in the parameterized Parser and Framer tests, because it
// does not have its own MoqtMessageType.
class QUICHE_NO_EXPORT JoiningFetchMessage : public TestMessageBase {
 public:
  JoiningFetchMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetch>(values);
    if (cast.fetch_id != fetch_.fetch_id) {
      QUIC_LOG(INFO) << "FETCH fetch_id mismatch";
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
    if (cast.joining_fetch.has_value() != fetch_.joining_fetch.has_value()) {
      QUIC_LOG(INFO) << "FETCH type mismatch";
      return false;
    }
    if (cast.joining_fetch.has_value()) {
      if (cast.joining_fetch->joining_subscribe_id !=
          fetch_.joining_fetch->joining_subscribe_id) {
        QUIC_LOG(INFO) << "FETCH joining_subscribe_id mismatch";
        return false;
      }
      if (cast.joining_fetch->preceding_group_offset !=
          fetch_.joining_fetch->preceding_group_offset) {
        QUIC_LOG(INFO) << "FETCH preceding_group_offset mismatch";
        return false;
      }
    } else {
      if (cast.full_track_name != fetch_.full_track_name) {
        QUIC_LOG(INFO) << "FETCH full_track_name mismatch";
        return false;
      }
      if (cast.start_object != fetch_.start_object) {
        QUIC_LOG(INFO) << "FETCH start_object mismatch";
        return false;
      }
      if (cast.end_group != fetch_.end_group) {
        QUIC_LOG(INFO) << "FETCH end_group mismatch";
        return false;
      }
      if (cast.end_object != fetch_.end_object) {
        QUIC_LOG(INFO) << "FETCH end_object mismatch";
        return false;
      }
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
      0x01,        // fetch_id = 1
      0x02,        // priority = kHigh
      0x01,        // group_order = kAscending
      0x02,        // type = kJoining
      0x02, 0x02,  // joining_subscribe_id = 2, 2 groups
      0x01, 0x01, 0x05, 0x03, 0x00, 0x62, 0x61, 0x7a,  // parameters = "baz"
  };

  MoqtFetch fetch_ = {
      /*fetch_id =*/1,
      /*subscriber_priority=*/2,
      /*group_order=*/MoqtDeliveryOrder::kAscending,
      /*joining_fetch=*/JoiningFetch{2, 2},
      /* the next four are ignored for joining fetches*/
      /*full_track_name=*/FullTrackName{"foo", "bar"},
      /*start_object=*/Location{1, 2},
      /*end_group=*/5,
      /*end_object=*/6,
      VersionSpecificParameters(AuthTokenType::kOutOfBand, "baz"),
  };
};

class QUICHE_NO_EXPORT FetchCancelMessage : public TestMessageBase {
 public:
  FetchCancelMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetchCancel>(values);
    if (cast.subscribe_id != fetch_cancel_.subscribe_id) {
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
      0x01,  // subscribe_id = 1
  };

  MoqtFetchCancel fetch_cancel_ = {
      /*subscribe_id =*/1,
  };
};

class QUICHE_NO_EXPORT FetchOkMessage : public TestMessageBase {
 public:
  FetchOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetchOk>(values);
    if (cast.subscribe_id != fetch_ok_.subscribe_id) {
      QUIC_LOG(INFO) << "FETCH_OK subscribe_id mismatch";
      return false;
    }
    if (cast.group_order != fetch_ok_.group_order) {
      QUIC_LOG(INFO) << "FETCH_OK group_order mismatch";
      return false;
    }
    if (cast.largest_id != fetch_ok_.largest_id) {
      QUIC_LOG(INFO) << "FETCH_OK start_object mismatch";
      return false;
    }
    if (cast.parameters != fetch_ok_.parameters) {
      QUIC_LOG(INFO) << "FETCH_OK parameters mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("v-vvvvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_ok_);
  }

 private:
  uint8_t raw_packet_[11] = {
      0x18, 0x00, 0x08,
      0x01,                    // subscribe_id = 1
      0x01,                    // group_order = kAscending
      0x05, 0x04,              // largest_object = 5, 4
      0x01, 0x04, 0x67, 0x10,  // MaxCacheDuration = 10000
  };

  MoqtFetchOk fetch_ok_ = {
      /*subscribe_id =*/1,
      /*group_order=*/MoqtDeliveryOrder::kAscending,
      /*start_object=*/Location{5, 4},
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
    if (cast.subscribe_id != fetch_error_.subscribe_id) {
      QUIC_LOG(INFO) << "FETCH_ERROR subscribe_id mismatch";
      return false;
    }
    if (cast.error_code != fetch_error_.error_code) {
      QUIC_LOG(INFO) << "FETCH_ERROR group_order mismatch";
      return false;
    }
    if (cast.reason_phrase != fetch_error_.reason_phrase) {
      QUIC_LOG(INFO) << "FETCH_ERROR reason_phrase mismatch";
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
      0x01,                    // subscribe_id = 1
      0x01,                    // error_code = kUnauthorized
      0x03, 0x62, 0x61, 0x72,  // reason_phrase = "bar"
  };

  MoqtFetchError fetch_error_ = {
      /*subscribe_id =*/1,
      /*error_code=*/RequestErrorCode::kUnauthorized,
      /*reason_phrase=*/"bar",
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
    case MoqtMessageType::kSubscribeDone:
      return std::make_unique<SubscribeDoneMessage>();
    case MoqtMessageType::kSubscribeUpdate:
      return std::make_unique<SubscribeUpdateMessage>();
    case MoqtMessageType::kAnnounce:
      return std::make_unique<AnnounceMessage>();
    case MoqtMessageType::kAnnounceOk:
      return std::make_unique<AnnounceOkMessage>();
    case MoqtMessageType::kAnnounceError:
      return std::make_unique<AnnounceErrorMessage>();
    case MoqtMessageType::kAnnounceCancel:
      return std::make_unique<AnnounceCancelMessage>();
    case MoqtMessageType::kTrackStatusRequest:
      return std::make_unique<TrackStatusRequestMessage>();
    case MoqtMessageType::kUnannounce:
      return std::make_unique<UnannounceMessage>();
    case MoqtMessageType::kTrackStatus:
      return std::make_unique<TrackStatusMessage>();
    case MoqtMessageType::kGoAway:
      return std::make_unique<GoAwayMessage>();
    case MoqtMessageType::kSubscribeAnnounces:
      return std::make_unique<SubscribeAnnouncesMessage>();
    case MoqtMessageType::kSubscribeAnnouncesOk:
      return std::make_unique<SubscribeAnnouncesOkMessage>();
    case MoqtMessageType::kSubscribeAnnouncesError:
      return std::make_unique<SubscribeAnnouncesErrorMessage>();
    case MoqtMessageType::kUnsubscribeAnnounces:
      return std::make_unique<UnsubscribeAnnouncesMessage>();
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
  switch (type) {
    case MoqtDataStreamType::kStreamHeaderSubgroup:
      return std::make_unique<StreamHeaderSubgroupMessage>();
    case MoqtDataStreamType::kStreamHeaderFetch:
      return std::make_unique<StreamHeaderFetchMessage>();
    case MoqtDataStreamType::kPadding:
      return nullptr;
  }
  return nullptr;
}

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_
