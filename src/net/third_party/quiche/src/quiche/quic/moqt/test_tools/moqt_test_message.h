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
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_endian.h"

namespace moqt::test {

// Base class containing a wire image and the corresponding structured
// representation of an example of each message. It allows parser and framer
// tests to iterate through all message types without much specialized code.
class QUICHE_NO_EXPORT TestMessageBase {
 public:
  virtual ~TestMessageBase() = default;

  using MessageStructuredData = absl::variant<
      MoqtClientSetup, MoqtServerSetup, MoqtObject, MoqtSubscribe,
      MoqtSubscribeOk, MoqtSubscribeError, MoqtUnsubscribe, MoqtSubscribeDone,
      MoqtSubscribeUpdate, MoqtAnnounce, MoqtAnnounceOk, MoqtAnnounceError,
      MoqtAnnounceCancel, MoqtTrackStatusRequest, MoqtUnannounce,
      MoqtTrackStatus, MoqtGoAway, MoqtSubscribeAnnounces,
      MoqtSubscribeAnnouncesOk, MoqtSubscribeAnnouncesError,
      MoqtUnsubscribeAnnounces, MoqtMaxSubscribeId, MoqtFetch, MoqtFetchCancel,
      MoqtFetchOk, MoqtFetchError, MoqtObjectAck>;

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
    wire_image_[length_offset]--;
  }
  void IncreasePayloadLengthByOne() {
    size_t length_offset =
        0x1 << ((static_cast<uint8_t>(wire_image_[0]) & 0xc0) >> 6);
    wire_image_[length_offset]++;
    set_wire_image_size(wire_image_size_ + 1);
  }

 protected:
  void SetWireImage(uint8_t* wire_image, size_t wire_image_size) {
    memcpy(wire_image_, wire_image, wire_image_size);
    wire_image_size_ = wire_image_size;
  }

  // Expands all the varints in the message, alternating between making them 2,
  // 4, and 8 bytes long. Updates length fields accordingly.
  // Each character in |varints| corresponds to a byte in the original message.
  // If there is a 'v', it is a varint that should be expanded. If '-', skip
  // to the next byte.
  // Always expand the message length field (if a control message) to 2 bytes,
  // so it's a known length that is large enough to be safe. The second byte
  // of |varints| does not matter.
  void ExpandVarintsImpl(absl::string_view varints,
                         bool is_control_message = true) {
    int next_varint_len = 2;
    char new_wire_image[kMaxMessageHeaderSize + 1];
    quic::QuicDataReader reader(
        absl::string_view(wire_image_, wire_image_size_));
    quic::QuicDataWriter writer(sizeof(new_wire_image), new_wire_image);
    size_t i = 0;
    size_t length_field = 0;
    if (is_control_message) {
      // the length will be a 16-bit varint.
      bool nonvarint_type = false;
      while (varints[i] == '-') {
        ++i;
        nonvarint_type = true;
        uint8_t byte;
        reader.ReadUInt8(&byte);
        writer.WriteUInt8(byte);
      }
      uint64_t value;
      if (!nonvarint_type) {
        ++i;
        reader.ReadVarInt62(&value);
        writer.WriteVarInt62WithForcedLength(
            value, static_cast<quiche::QuicheVariableLengthIntegerLength>(
                       next_varint_len));
        next_varint_len *= 2;
        if (next_varint_len == 16) {
          next_varint_len = 2;
        }
      }
      reader.ReadVarInt62(&value);
      ++i;
      length_field = writer.length();
      // Write in current length as a 2B placeholder.
      writer.WriteVarInt62WithForcedLength(
          value, static_cast<quiche::QuicheVariableLengthIntegerLength>(2));
    }
    while (!reader.IsDoneReading()) {
      if (i >= varints.length() || varints[i++] == '-') {
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
    if (is_control_message) {
      wire_image_[length_field + 1] =
          static_cast<uint8_t>(writer.length() - length_field - 2);
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
    auto cast = std::get<MoqtObject>(values);
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
    if (cast.object_status != object_.object_status) {
      QUIC_LOG(INFO) << "OBJECT Object Status mismatch";
      return false;
    }
    if (cast.forwarding_preference != object_.forwarding_preference) {
      QUIC_LOG(INFO) << "OBJECT Object Send Order mismatch";
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
      /*object_status=*/MoqtObjectStatus::kNormal,
      /*forwarding_preference=*/MoqtForwardingPreference::kTrack,
      /*subgroup_id=*/std::nullopt,
      /*payload_length=*/3,
  };
};

class QUICHE_NO_EXPORT ObjectDatagramMessage : public ObjectMessage {
 public:
  ObjectDatagramMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kDatagram;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv-v---", false); }

 private:
  uint8_t raw_packet_[9] = {
      0x01, 0x04, 0x05, 0x06,  // varints
      0x07,                    // publisher priority
      0x03, 0x66, 0x6f, 0x6f,  // payload = "foo"
  };
};

// Concatentation of the base header and the object-specific header. Follow-on
// object headers are handled in a different class.
class QUICHE_NO_EXPORT StreamHeaderTrackMessage : public ObjectMessage {
 public:
  StreamHeaderTrackMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kTrack;
    object_.payload_length = 3;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv-vvv", false); }

 private:
  // Some tests check that a FIN sent at the halfway point of a message results
  // in an error. Without the unnecessary expanded varint 0x0405, the halfway
  // point falls at the end of the Stream Header, which is legal. Expand the
  // varint so that the FIN would be illegal.
  uint8_t raw_packet_[9] = {
      0x02,                    // type field
      0x04,                    // varints
      0x07,                    // publisher priority
      0x05, 0x06,              // object middler
      0x03, 0x66, 0x6f, 0x6f,  // payload = "foo"
  };
};

// Used only for tests that process multiple objects on one stream.
class QUICHE_NO_EXPORT StreamMiddlerTrackMessage : public ObjectMessage {
 public:
  StreamMiddlerTrackMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kTrack;
    object_.group_id = 9;
    object_.object_id = 10;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv", false); }

 private:
  uint8_t raw_packet_[6] = {
      0x09, 0x0a,              // object middler
      0x03, 0x62, 0x61, 0x72,  // payload = "bar"
  };
};

class QUICHE_NO_EXPORT StreamHeaderSubgroupMessage : public ObjectMessage {
 public:
  StreamHeaderSubgroupMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kSubgroup;
    object_.subgroup_id = 8;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv-vv", false); }

  bool SetPayloadLength(uint8_t payload_length) {
    if (payload_length > 63) {
      // This only supports one-byte varints.
      return false;
    }
    object_.payload_length = payload_length;
    raw_packet_[6] = payload_length;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    return true;
  }

 private:
  uint8_t raw_packet_[10] = {
      0x04,                          // type field
      0x04, 0x05, 0x08,              // varints
      0x07,                          // publisher priority
      0x06, 0x03, 0x66, 0x6f, 0x6f,  // object middler; payload = "foo"
  };
};

// Used only for tests that process multiple objects on one stream.
class QUICHE_NO_EXPORT StreamMiddlerSubgroupMessage : public ObjectMessage {
 public:
  StreamMiddlerSubgroupMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kSubgroup;
    object_.subgroup_id = 8;
    object_.object_id = 9;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv", false); }

 private:
  uint8_t raw_packet_[5] = {
      0x09, 0x03, 0x62, 0x61, 0x72,  // object middler; payload = "bar"
  };
};

class QUICHE_NO_EXPORT StreamHeaderFetchMessage : public ObjectMessage {
 public:
  StreamHeaderFetchMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.subgroup_id = 8;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvvv-v---", false); }

  bool SetPayloadLength(uint8_t payload_length) {
    if (payload_length > 63) {
      // This only supports one-byte varints.
      return false;
    }
    object_.payload_length = payload_length;
    raw_packet_[6] = payload_length;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    return true;
  }

 private:
  uint8_t raw_packet_[10] = {
      0x05,                    // type field
      0x04,                    // subscribe ID
                               // object middler:
      0x05, 0x08, 0x06,        // sequence
      0x07,                    // publisher priority
      0x03, 0x66, 0x6f, 0x6f,  // payload = "foo"
  };
};

// Used only for tests that process multiple objects on one stream.
class QUICHE_NO_EXPORT StreamMiddlerFetchMessage : public ObjectMessage {
 public:
  StreamMiddlerFetchMessage() : ObjectMessage() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kTrack;
    object_.subgroup_id = 8;
    object_.object_id = 9;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv-v---", false); }

 private:
  uint8_t raw_packet_[8] = {
      0x05, 0x08, 0x09, 0x07,  // Object metadata
      0x03, 0x62, 0x61, 0x72,  // Payload = "bar"
  };
};

class QUICHE_NO_EXPORT ClientSetupMessage : public TestMessageBase {
 public:
  explicit ClientSetupMessage(bool webtrans) : TestMessageBase() {
    if (webtrans) {
      // Should not send PATH.
      client_setup_.path = std::nullopt;
      raw_packet_[2] = 0x0a;  // adjust payload length (-5)
      raw_packet_[6] = 0x02;  // only two parameters
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
    if (cast.role != client_setup_.role) {
      QUIC_LOG(INFO) << "CLIENT_SETUP role mismatch";
      return false;
    }
    if (cast.path != client_setup_.path) {
      QUIC_LOG(INFO) << "CLIENT_SETUP path mismatch";
      return false;
    }
    if (cast.max_subscribe_id != client_setup_.max_subscribe_id) {
      QUIC_LOG(INFO) << "CLIENT_SETUP max_subscribe_id mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    if (client_setup_.path.has_value()) {
      ExpandVarintsImpl("--vvvvvvv-vv-vv---");
      // first two bytes are already a 2B varint. Also, don't expand parameter
      // varints because that messes up the parameter length field.
    } else {
      ExpandVarintsImpl("--vvvvvvv-vv-");
    }
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(client_setup_);
  }

 private:
  uint8_t raw_packet_[18] = {
      0x40, 0x40, 0x0f,              // type
      0x02, 0x01, 0x02,              // versions
      0x03,                          // 3 parameters
      0x00, 0x01, 0x03,              // role = PubSub
      0x02, 0x01, 0x32,              // max_subscribe_id = 50
      0x01, 0x03, 0x66, 0x6f, 0x6f,  // path = "foo"
  };
  MoqtClientSetup client_setup_ = {
      /*supported_versions=*/std::vector<MoqtVersion>(
          {static_cast<MoqtVersion>(1), static_cast<MoqtVersion>(2)}),
      /*role=*/MoqtRole::kPubSub,
      /*path=*/"foo",
      /*max_subscribe_id=*/50,
  };
};

class QUICHE_NO_EXPORT ServerSetupMessage : public TestMessageBase {
 public:
  explicit ServerSetupMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtServerSetup>(values);
    if (cast.selected_version != server_setup_.selected_version) {
      QUIC_LOG(INFO) << "SERVER_SETUP selected version mismatch";
      return false;
    }
    if (cast.role != server_setup_.role) {
      QUIC_LOG(INFO) << "SERVER_SETUP role mismatch";
      return false;
    }
    if (cast.max_subscribe_id != server_setup_.max_subscribe_id) {
      QUIC_LOG(INFO) << "SERVER_SETUP max_subscribe_id mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("--vvvvv-vv-");  // first two bytes are already a 2b
                                       // varint
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(server_setup_);
  }

 private:
  uint8_t raw_packet_[11] = {
      0x40, 0x41, 0x08,  // type
      0x01, 0x02,        // version, two parameters
      0x00, 0x01, 0x03,  // role = PubSub
      0x02, 0x01, 0x32,  // max_subscribe_id = 50
  };
  MoqtServerSetup server_setup_ = {
      /*selected_version=*/static_cast<MoqtVersion>(1),
      /*role=*/MoqtRole::kPubSub,
      /*max_subscribe_id=*/50,
  };
};

class QUICHE_NO_EXPORT SubscribeMessage : public TestMessageBase {
 public:
  SubscribeMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribe>(values);
    if (cast.subscribe_id != subscribe_.subscribe_id) {
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
    if (cast.start_group != subscribe_.start_group) {
      QUIC_LOG(INFO) << "SUBSCRIBE start group mismatch";
      return false;
    }
    if (cast.start_object != subscribe_.start_object) {
      QUIC_LOG(INFO) << "SUBSCRIBE start object mismatch";
      return false;
    }
    if (cast.end_group != subscribe_.end_group) {
      QUIC_LOG(INFO) << "SUBSCRIBE end group mismatch";
      return false;
    }
    if (cast.end_object != subscribe_.end_object) {
      QUIC_LOG(INFO) << "SUBSCRIBE end object mismatch";
      return false;
    }
    if (cast.parameters != subscribe_.parameters) {
      QUIC_LOG(INFO) << "SUBSCRIBE parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvvvv---v------vvvvvv---vv--vv--");
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_);
  }

 private:
  uint8_t raw_packet_[33] = {
      0x03, 0x1f, 0x01, 0x02,        // id and alias
      0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x04, 0x61, 0x62, 0x63, 0x64,  // track_name = "abcd"
      0x20,                          // subscriber priority = 0x20
      0x02,                          // group order = descending
      0x03,                          // Filter type: Absolute Start
      0x04,                          // start_group = 4 (relative previous)
      0x01,                          // start_object = 1 (absolute)
      // No EndGroup or EndObject
      0x03,                          // 3 parameters
      0x02, 0x03, 0x62, 0x61, 0x72,  // authorization_info = "bar"
      0x03, 0x02, 0x67, 0x10,        // delivery_timeout = 10000 ms
      0x04, 0x02, 0x67, 0x10,        // max_cache_duration = 10000 ms
  };

  MoqtSubscribe subscribe_ = {
      /*subscribe_id=*/1,
      /*track_alias=*/2,
      /*full_track_name=*/FullTrackName({"foo", "abcd"}),
      /*subscriber_priority=*/0x20,
      /*group_order=*/MoqtDeliveryOrder::kDescending,
      /*start_group=*/4,
      /*start_object=*/1,
      /*end_group=*/std::nullopt,
      /*end_object=*/std::nullopt,
      /*parameters=*/
      MoqtSubscribeParameters{
          "bar", quic::QuicTimeDelta::FromMilliseconds(10000),
          quic::QuicTimeDelta::FromMilliseconds(10000), std::nullopt},
  };
};

class QUICHE_NO_EXPORT SubscribeOkMessage : public TestMessageBase {
 public:
  SubscribeOkMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeOk>(values);
    if (cast.subscribe_id != subscribe_ok_.subscribe_id) {
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
    if (cast.largest_id != subscribe_ok_.largest_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK largest ID mismatch";
      return false;
    }
    if (cast.parameters != subscribe_ok_.parameters) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv--vvvvv--vv--"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_ok_);
  }

  void SetInvalidContentExists() {
    raw_packet_[5] = 0x02;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  void SetInvalidDeliveryOrder() {
    raw_packet_[4] = 0x10;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[17] = {
      0x04, 0x0f, 0x01, 0x03,  // subscribe_id = 1, expires = 3
      0x02, 0x01,              // group_order = 2, content exists
      0x0c, 0x14,              // largest_group_id = 12, largest_object_id = 20,
      0x02,                    // 2 parameters
      0x03, 0x02, 0x67, 0x10,  // delivery_timeout = 10000
      0x04, 0x02, 0x67, 0x10,  // max_cache_duration = 10000
  };

  MoqtSubscribeOk subscribe_ok_ = {
      /*subscribe_id=*/1,
      /*expires=*/quic::QuicTimeDelta::FromMilliseconds(3),
      /*group_order=*/MoqtDeliveryOrder::kDescending,
      /*largest_id=*/FullSequence(12, 20),
      /*parameters=*/
      MoqtSubscribeParameters{
          std::nullopt, quic::QuicTimeDelta::FromMilliseconds(10000),
          quic::QuicTimeDelta::FromMilliseconds(10000), std::nullopt},
  };
};

class QUICHE_NO_EXPORT SubscribeErrorMessage : public TestMessageBase {
 public:
  SubscribeErrorMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeError>(values);
    if (cast.subscribe_id != subscribe_error_.subscribe_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE ERROR subscribe_id mismatch";
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvvv---v"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_error_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x05, 0x07,
      0x02,                    // subscribe_id = 2
      0x01,                    // error_code = 2
      0x03, 0x62, 0x61, 0x72,  // reason_phrase = "bar"
      0x04,                    // track_alias = 4
  };

  MoqtSubscribeError subscribe_error_ = {
      /*subscribe_id=*/2,
      /*subscribe=*/SubscribeErrorCode::kInvalidRange,
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

  void ExpandVarints() override { ExpandVarintsImpl("vvv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unsubscribe_);
  }

 private:
  uint8_t raw_packet_[3] = {
      0x0a, 0x01, 0x03,  // subscribe_id = 3
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
    if (cast.reason_phrase != subscribe_done_.reason_phrase) {
      QUIC_LOG(INFO) << "SUBSCRIBE_DONE reason phrase mismatch";
      return false;
    }
    if (cast.final_id != subscribe_done_.final_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE_DONE final ID mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvvv---vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_done_);
  }

  void SetInvalidContentExists() {
    raw_packet_[7] = 0x02;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[10] = {
      0x0b, 0x08, 0x02, 0x03,  // subscribe_id = 2, error_code = 3,
      0x02, 0x68, 0x69,        // reason_phrase = "hi"
      0x01, 0x08, 0x0c,        // final_id = (8,12)
  };

  MoqtSubscribeDone subscribe_done_ = {
      /*subscribe_id=*/2,
      /*error_code=*/SubscribeDoneCode::kTrackEnded,
      /*reason_phrase=*/"hi",
      /*final_id=*/FullSequence(8, 12),
  };
};

class QUICHE_NO_EXPORT SubscribeUpdateMessage : public TestMessageBase {
 public:
  SubscribeUpdateMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeUpdate>(values);
    if (cast.subscribe_id != subscribe_update_.subscribe_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE subscribe ID mismatch";
      return false;
    }
    if (cast.start_group != subscribe_update_.start_group) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE start group mismatch";
      return false;
    }
    if (cast.start_object != subscribe_update_.start_object) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE start group mismatch";
      return false;
    }
    if (cast.end_group != subscribe_update_.end_group) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE end group mismatch";
      return false;
    }
    if (cast.end_object != subscribe_update_.end_object) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE end group mismatch";
      return false;
    }
    if (cast.subscriber_priority != subscribe_update_.subscriber_priority) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE subscriber priority mismatch";
      return false;
    }
    if (cast.parameters != subscribe_update_.parameters) {
      QUIC_LOG(INFO) << "SUBSCRIBE_UPDATE parameter mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvvvvv-vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_update_);
  }

 private:
  uint8_t raw_packet_[17] = {
      0x02, 0x0f, 0x02, 0x03, 0x01, 0x05, 0x06,  // start and end sequences
      0xaa,                                      // subscriber_priority
      0x02,                                      // 1 parameter
      0x03, 0x02, 0x67, 0x10,                    // delivery_timeout = 10000
      0x04, 0x02, 0x67, 0x10,                    // max_cache_duration = 10000
  };

  MoqtSubscribeUpdate subscribe_update_ = {
      /*subscribe_id=*/2,
      /*start_group=*/3,
      /*start_object=*/1,
      /*end_group=*/4,
      /*end_object=*/5,
      /*subscriber_priority=*/0xaa,
      /*parameters=*/
      MoqtSubscribeParameters{
          std::nullopt, quic::QuicTimeDelta::FromMilliseconds(10000),
          quic::QuicTimeDelta::FromMilliseconds(10000), std::nullopt},
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
      QUIC_LOG(INFO) << "ANNOUNCE MESSAGE authorization info mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---vvv---vv--"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_);
  }

 private:
  uint8_t raw_packet_[17] = {
      0x06, 0x0f, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x02,                                      // 2 parameters
      0x02, 0x03, 0x62, 0x61, 0x72,              // authorization_info = "bar"
      0x04, 0x02, 0x67, 0x10,                    // max_cache_duration = 10000ms
  };

  MoqtAnnounce announce_ = {
      /*track_namespace=*/FullTrackName{"foo"},
      /*parameters=*/
      MoqtSubscribeParameters{"bar", std::nullopt,
                              quic::QuicTimeDelta::FromMilliseconds(10000),
                              std::nullopt},
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_ok_);
  }

 private:
  uint8_t raw_packet_[7] = {
      0x07, 0x05, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_error_);
  }

 private:
  uint8_t raw_packet_[12] = {
      0x08, 0x0a, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x01,                                      // error_code = 1
      0x03, 0x62, 0x61, 0x72,                    // reason_phrase = "bar"
  };

  MoqtAnnounceError announce_error_ = {
      /*track_namespace=*/FullTrackName{"foo"},
      /*error_code=*/MoqtAnnounceErrorCode::kAnnounceNotSupported,
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_cancel_);
  }

 private:
  uint8_t raw_packet_[12] = {
      0x0c, 0x0a, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x01,                                      // error_code = 1
      0x03, 0x62, 0x61, 0x72,                    // reason_phrase = "bar"
  };

  MoqtAnnounceCancel announce_cancel_ = {
      /*track_namespace=*/FullTrackName{"foo"},
      /*error_code=*/1,
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
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---v----"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(track_status_request_);
  }

 private:
  uint8_t raw_packet_[12] = {
      0x0d, 0x0a, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x04, 0x61, 0x62, 0x63, 0x64,              // track_name = "abcd"
  };

  MoqtTrackStatusRequest track_status_request_ = {
      /*full_track_name=*/FullTrackName({"foo", "abcd"}),
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unannounce_);
  }

 private:
  uint8_t raw_packet_[7] = {
      0x09, 0x05, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace
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
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---v----vvv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(track_status_);
  }

 private:
  uint8_t raw_packet_[15] = {
      0x0e, 0x0d, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x04, 0x61, 0x62, 0x63, 0x64,              // track_name = "abcd"
      0x00, 0x0c, 0x14,  // status, last_group, last_object
  };

  MoqtTrackStatus track_status_ = {
      /*full_track_name=*/FullTrackName({"foo", "abcd"}),
      /*status_code=*/MoqtTrackStatusCode::kInProgress,
      /*last_group=*/12,
      /*last_object=*/20,
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

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(goaway_);
  }

 private:
  uint8_t raw_packet_[6] = {
      0x10, 0x04, 0x03, 0x66, 0x6f, 0x6f,
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_namespace_);
  }

 private:
  uint8_t raw_packet_[13] = {
      0x11, 0x0b, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // namespace = "foo"
      0x01,                                      // 1 parameter
      0x02, 0x03, 0x62, 0x61, 0x72,              // authorization_info = "bar"
  };

  MoqtSubscribeAnnounces subscribe_namespace_ = {
      /*track_namespace=*/FullTrackName{"foo"},
      /*parameters=*/
      MoqtSubscribeParameters{"bar", std::nullopt, std::nullopt, std::nullopt},
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_namespace_ok_);
  }

 private:
  uint8_t raw_packet_[7] = {
      0x12, 0x05, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // namespace = "foo"
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_namespace_error_);
  }

 private:
  uint8_t raw_packet_[12] = {
      0x13, 0x0a, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x01,                                      // error_code = 1
      0x03, 0x62, 0x61, 0x72,                    // reason_phrase = "bar"
  };

  MoqtSubscribeAnnouncesError subscribe_namespace_error_ = {
      /*track_namespace=*/FullTrackName{"foo"},
      /*error_code=*/MoqtAnnounceErrorCode::kAnnounceNotSupported,
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unsubscribe_namespace_);
  }

 private:
  uint8_t raw_packet_[7] = {
      0x14, 0x05, 0x01, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace
  };

  MoqtUnsubscribeAnnounces unsubscribe_namespace_ = {
      /*track_namespace=*/FullTrackName{"foo"},
  };
};

class QUICHE_NO_EXPORT MaxSubscribeIdMessage : public TestMessageBase {
 public:
  MaxSubscribeIdMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtMaxSubscribeId>(values);
    if (cast.max_subscribe_id != max_subscribe_id_.max_subscribe_id) {
      QUIC_LOG(INFO) << "MAX_SUBSCRIBE_ID mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(max_subscribe_id_);
  }

 private:
  uint8_t raw_packet_[3] = {
      0x15,
      0x01,
      0x0b,
  };

  MoqtMaxSubscribeId max_subscribe_id_ = {
      /*max_subscribe_id =*/11,
  };
};

class QUICHE_NO_EXPORT FetchMessage : public TestMessageBase {
 public:
  FetchMessage() : TestMessageBase() {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }
  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtFetch>(values);
    if (cast.subscribe_id != fetch_.subscribe_id) {
      QUIC_LOG(INFO) << "FETCH subscribe_id mismatch";
      return false;
    }
    if (cast.full_track_name != fetch_.full_track_name) {
      QUIC_LOG(INFO) << "FETCH full_track_name mismatch";
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
    if (cast.parameters != fetch_.parameters) {
      QUIC_LOG(INFO) << "FETCH parameters mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvv---v-----vvvvvvv---");
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
    raw_packet_[16] = group;
    raw_packet_[17] = object.has_value() ? (*object + 1) : 0;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  void SetGroupOrder(uint8_t group_order) {
    raw_packet_[13] = static_cast<uint8_t>(group_order);
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[24] = {
      0x16, 0x16,
      0x01,                                // subscribe_id = 1
      0x01, 0x03, 0x66, 0x6f, 0x6f,        // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,              // track_name = "bar"
      0x02,                                // priority = kHigh
      0x01,                                // group_order = kAscending
      0x01, 0x02,                          // start_object = 1, 2
      0x05, 0x07,                          // end_object = 5, 6
      0x01, 0x02, 0x03, 0x62, 0x61, 0x7a,  // parameters = "baz"
  };

  MoqtFetch fetch_ = {
      /*subscribe_id =*/1,
      /*full_track_name=*/FullTrackName{"foo", "bar"},
      /*subscriber_priority=*/2,
      /*group_order=*/MoqtDeliveryOrder::kAscending,
      /*start_object=*/FullSequence{1, 2},
      /*end_group=*/5,
      /*end_object=*/6,
      /*parameters=*/
      MoqtSubscribeParameters{"baz", std::nullopt, std::nullopt, std::nullopt},
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

  void ExpandVarints() override { ExpandVarintsImpl("vvv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_cancel_);
  }

 private:
  uint8_t raw_packet_[3] = {
      0x17, 0x01,
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

  void ExpandVarints() override { ExpandVarintsImpl("vvv-vvvvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_ok_);
  }

 private:
  uint8_t raw_packet_[12] = {
      0x18, 0x0a,
      0x01,                                // subscribe_id = 1
      0x01,                                // group_order = kAscending
      0x05, 0x04,                          // largest_object = 5, 4
      0x01, 0x02, 0x03, 0x62, 0x61, 0x7a,  // parameters = "baz"
  };

  MoqtFetchOk fetch_ok_ = {
      /*subscribe_id =*/1,
      /*group_order=*/MoqtDeliveryOrder::kAscending,
      /*start_object=*/FullSequence{5, 4},
      /*parameters=*/
      MoqtSubscribeParameters{"baz", std::nullopt, std::nullopt, std::nullopt},
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(fetch_error_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x19, 0x06,
      0x01,                    // subscribe_id = 1
      0x04,                    // error_code = kUnauthorized
      0x03, 0x62, 0x61, 0x72,  // reason_phrase = "bar"
  };

  MoqtFetchError fetch_error_ = {
      /*subscribe_id =*/1,
      /*error_code=*/SubscribeErrorCode::kUnauthorized,
      /*reason_phrase=*/"bar",
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvvvv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(object_ack_);
  }

 private:
  uint8_t raw_packet_[7] = {
      0x71, 0x84, 0x04,  // type
      0x01, 0x10, 0x20,  // subscribe ID, group, object
      0x20,              // 0x10 time delta
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
    case MoqtMessageType::kMaxSubscribeId:
      return std::make_unique<MaxSubscribeIdMessage>();
    case MoqtMessageType::kFetch:
      return std::make_unique<FetchMessage>();
    case MoqtMessageType::kFetchCancel:
      return std::make_unique<FetchCancelMessage>();
    case MoqtMessageType::kFetchOk:
      return std::make_unique<FetchOkMessage>();
    case MoqtMessageType::kFetchError:
      return std::make_unique<FetchErrorMessage>();
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
  switch (type) {
    case MoqtDataStreamType::kObjectDatagram:
      return std::make_unique<ObjectDatagramMessage>();
    case MoqtDataStreamType::kStreamHeaderTrack:
      return std::make_unique<StreamHeaderTrackMessage>();
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
