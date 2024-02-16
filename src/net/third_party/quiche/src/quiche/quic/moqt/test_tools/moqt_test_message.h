// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_endian.h"

namespace moqt::test {

// Base class containing a wire image and the corresponding structured
// representation of an example of each message. It allows parser and framer
// tests to iterate through all message types without much specialized code.
class QUICHE_NO_EXPORT TestMessageBase {
 public:
  TestMessageBase(MoqtMessageType message_type) : message_type_(message_type) {}
  virtual ~TestMessageBase() = default;
  MoqtMessageType message_type() const { return message_type_; }

  typedef absl::variant<MoqtClientSetup, MoqtServerSetup, MoqtObject,
                        MoqtSubscribeRequest, MoqtSubscribeOk,
                        MoqtSubscribeError, MoqtUnsubscribe, MoqtSubscribeFin,
                        MoqtSubscribeRst, MoqtAnnounce, MoqtAnnounceOk,
                        MoqtAnnounceError, MoqtUnannounce, MoqtGoAway>
      MessageStructuredData;

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
  void ExpandVarintsImpl(absl::string_view varints) {
    int next_varint_len = 2;
    char new_wire_image[kMaxMessageHeaderSize + 1];
    quic::QuicDataReader reader(
        absl::string_view(wire_image_, wire_image_size_));
    quic::QuicDataWriter writer(sizeof(new_wire_image), new_wire_image);
    size_t i = 0;
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
  }

 protected:
  MoqtMessageType message_type_;

 private:
  char wire_image_[kMaxMessageHeaderSize + 20];
  size_t wire_image_size_;
};

// Base class for the two subtypes of Object Message.
class QUICHE_NO_EXPORT ObjectMessage : public TestMessageBase {
 public:
  ObjectMessage(MoqtMessageType type) : TestMessageBase(type) {}

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtObject>(values);
    if (cast.track_id != object_.track_id) {
      QUIC_LOG(INFO) << "OBJECT Track ID mismatch";
      return false;
    }
    if (cast.group_sequence != object_.group_sequence) {
      QUIC_LOG(INFO) << "OBJECT Group Sequence mismatch";
      return false;
    }
    if (cast.object_sequence != object_.object_sequence) {
      QUIC_LOG(INFO) << "OBJECT Object Sequence mismatch";
      return false;
    }
    if (cast.object_send_order != object_.object_send_order) {
      QUIC_LOG(INFO) << "OBJECT Object Send Order mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvvvv");  // first six fields are varints
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(object_);
  }

 protected:
  MoqtObject object_ = {
      /*track_id=*/4,
      /*group_sequence=*/5,
      /*object_sequence=*/6,
      /*object_send_order=*/7,
      /*payload_length=*/std::nullopt,
  };
};

class QUICHE_NO_EXPORT ObjectMessageWithLength : public ObjectMessage {
 public:
  ObjectMessageWithLength()
      : ObjectMessage(MoqtMessageType::kObjectWithPayloadLength) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.payload_length = payload_length_;
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtObject>(values);
    if (cast.payload_length != payload_length_) {
      QUIC_LOG(INFO) << "OBJECT Payload Length mismatch";
      return false;
    }
    return ObjectMessage::EqualFieldValues(values);
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvvvv");  // first six fields are varints
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(object_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x00, 0x04, 0x05, 0x06, 0x07,  // varints
      0x03, 0x66, 0x6f, 0x6f,        // payload = "foo"
  };
  std::optional<uint64_t> payload_length_ = 3;
};

class QUICHE_NO_EXPORT ObjectMessageWithoutLength : public ObjectMessage {
 public:
  ObjectMessageWithoutLength()
      : ObjectMessage(MoqtMessageType::kObjectWithoutPayloadLength) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtObject>(values);
    if (cast.payload_length != std::nullopt) {
      QUIC_LOG(INFO) << "OBJECT Payload Length mismatch";
      return false;
    }
    return ObjectMessage::EqualFieldValues(values);
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvvv");  // first six fields are varints
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(object_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x02, 0x04, 0x05, 0x06, 0x07,  // varints
      0x66, 0x6f, 0x6f,              // payload = "foo"
  };
};

class QUICHE_NO_EXPORT ClientSetupMessage : public TestMessageBase {
 public:
  explicit ClientSetupMessage(bool webtrans)
      : TestMessageBase(MoqtMessageType::kClientSetup) {
    if (webtrans) {
      // Should not send PATH.
      client_setup_.path = std::nullopt;
      raw_packet_[5] = 0x01;  // only one parameter
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
    return true;
  }

  void ExpandVarints() override {
    if (client_setup_.path.has_value()) {
      ExpandVarintsImpl("--vvvvvv-vv---");
      // first two bytes are already a 2B varint. Also, don't expand parameter
      // varints because that messes up the parameter length field.
    } else {
      ExpandVarintsImpl("--vvvvvv-");
    }
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(client_setup_);
  }

 private:
  uint8_t raw_packet_[14] = {
      0x40, 0x40,                   // type
      0x02, 0x01, 0x02,             // versions
      0x02,                         // 2 parameters
      0x00, 0x01, 0x03,             // role = both
      0x01, 0x03, 0x66, 0x6f, 0x6f  // path = "foo"
  };
  MoqtClientSetup client_setup_ = {
      /*supported_versions=*/std::vector<MoqtVersion>(
          {static_cast<MoqtVersion>(1), static_cast<MoqtVersion>(2)}),
      /*role=*/MoqtRole::kBoth,
      /*path=*/"foo",
  };
};

class QUICHE_NO_EXPORT ServerSetupMessage : public TestMessageBase {
 public:
  explicit ServerSetupMessage()
      : TestMessageBase(MoqtMessageType::kServerSetup) {
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
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("--v");  // first two bytes are already a 2b varint
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(server_setup_);
  }

 private:
  uint8_t raw_packet_[4] = {
      0x40, 0x41,  // type
      0x01, 0x00,  // version, zero params
  };
  MoqtServerSetup server_setup_ = {
      /*selected_version=*/static_cast<MoqtVersion>(1),
      /*role=*/std::nullopt,
  };
};

class QUICHE_NO_EXPORT SubscribeRequestMessage : public TestMessageBase {
 public:
  SubscribeRequestMessage()
      : TestMessageBase(MoqtMessageType::kSubscribeRequest) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeRequest>(values);
    if (cast.track_namespace != subscribe_request_.track_namespace) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST track namespace mismatch";
      return false;
    }
    if (cast.track_name != subscribe_request_.track_name) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST track name mismatch";
      return false;
    }
    if (cast.start_group != subscribe_request_.start_group) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST start group mismatch";
      return false;
    }
    if (cast.start_object != subscribe_request_.start_object) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST start object mismatch";
      return false;
    }
    if (cast.end_group != subscribe_request_.end_group) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST end group mismatch";
      return false;
    }
    if (cast.end_object != subscribe_request_.end_object) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST end object mismatch";
      return false;
    }
    if (cast.authorization_info != subscribe_request_.authorization_info) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST authorization info mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---v----vvvvvvvvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_request_);
  }

 private:
  uint8_t raw_packet_[22] = {
      0x03, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x04, 0x61, 0x62, 0x63, 0x64,  // track_name = "abcd"
      0x02, 0x04,                    // start_group = 4 (relative previous)
      0x01, 0x01,                    // start_object = 1 (absolute)
      0x00,                          // end_group = none
      0x00,                          // end_object = none
      0x01,                          // 1 parameter
      0x02, 0x03, 0x62, 0x61, 0x72,  // authorization_info = "bar"
  };

  MoqtSubscribeRequest subscribe_request_ = {
      /*track_namespace=*/"foo",
      /*track_name=*/"abcd",
      /*start_group=*/MoqtSubscribeLocation(false, (int64_t)(-4)),
      /*start_object=*/MoqtSubscribeLocation(true, (uint64_t)1),
      /*end_group=*/std::nullopt,
      /*end_object=*/std::nullopt,
      /*authorization_info=*/"bar",
  };
};

class QUICHE_NO_EXPORT SubscribeOkMessage : public TestMessageBase {
 public:
  SubscribeOkMessage() : TestMessageBase(MoqtMessageType::kSubscribeOk) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeOk>(values);
    if (cast.track_namespace != subscribe_ok_.track_namespace) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK track namespace mismatch";
      return false;
    }
    if (cast.track_name != subscribe_ok_.track_name) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK track name mismatch";
      return false;
    }
    if (cast.track_id != subscribe_ok_.track_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK track ID mismatch";
      return false;
    }
    if (cast.expires != subscribe_ok_.expires) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK expiration mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---v---vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_ok_);
  }

 private:
  uint8_t raw_packet_[11] = {
      0x04, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,        // track_namespace = "bar"
      0x01,                          // track_id = 1
      0x02,                          // expires = 2
  };

  MoqtSubscribeOk subscribe_ok_ = {
      /*track_namespace=*/"foo",
      /*track_name=*/"bar",
      /*track_id=*/1,
      /*expires=*/quic::QuicTimeDelta::FromMilliseconds(2),
  };
};

class QUICHE_NO_EXPORT SubscribeErrorMessage : public TestMessageBase {
 public:
  SubscribeErrorMessage() : TestMessageBase(MoqtMessageType::kSubscribeError) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeError>(values);
    if (cast.track_namespace != subscribe_error_.track_namespace) {
      QUIC_LOG(INFO) << "SUBSCRIBE ERROR track namespace mismatch";
      return false;
    }
    if (cast.track_name != subscribe_error_.track_name) {
      QUIC_LOG(INFO) << "SUBSCRIBE ERROR track name mismatch";
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

  void ExpandVarints() override { ExpandVarintsImpl("vv---v---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_error_);
  }

 private:
  uint8_t raw_packet_[14] = {
      0x05, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,        // track_namespace = "bar"
      0x01,                          // error_code = 1
      0x03, 0x62, 0x61, 0x72,        // reason_phrase = "bar"
  };

  MoqtSubscribeError subscribe_error_ = {
      /*track_namespace=*/"foo",
      /*track_name=*/"bar",
      /*subscribe=*/1,
      /*reason_phrase=*/"bar",
  };
};

class QUICHE_NO_EXPORT UnsubscribeMessage : public TestMessageBase {
 public:
  UnsubscribeMessage() : TestMessageBase(MoqtMessageType::kUnsubscribe) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtUnsubscribe>(values);
    if (cast.track_namespace != unsubscribe_.track_namespace) {
      QUIC_LOG(INFO) << "UNSUBSCRIBE track name mismatch";
      return false;
    }
    if (cast.track_name != unsubscribe_.track_name) {
      QUIC_LOG(INFO) << "UNSUBSCRIBE track name mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---v---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unsubscribe_);
  }

 private:
  uint8_t raw_packet_[9] = {
      0x0a, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,        // track_namespace = "bar"
  };

  MoqtUnsubscribe unsubscribe_ = {
      /*track_namespace=*/"foo",
      /*track_name=*/"bar",
  };
};

class QUICHE_NO_EXPORT SubscribeFinMessage : public TestMessageBase {
 public:
  SubscribeFinMessage() : TestMessageBase(MoqtMessageType::kSubscribeFin) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeFin>(values);
    if (cast.track_namespace != subscribe_fin_.track_namespace) {
      QUIC_LOG(INFO) << "SUBSCRIBE_FIN track name mismatch";
      return false;
    }
    if (cast.track_name != subscribe_fin_.track_name) {
      QUIC_LOG(INFO) << "SUBSCRIBE_FIN track name mismatch";
      return false;
    }
    if (cast.final_group != subscribe_fin_.final_group) {
      QUIC_LOG(INFO) << "SUBSCRIBE_FIN final group mismatch";
      return false;
    }
    if (cast.final_object != subscribe_fin_.final_object) {
      QUIC_LOG(INFO) << "SUBSCRIBE_FIN final object mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---v---vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_fin_);
  }

 private:
  uint8_t raw_packet_[11] = {
      0x0b, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,        // track_namespace = "bar"
      0x08,                          // final_group = 8
      0x0c,                          // final_object = 12
  };

  MoqtSubscribeFin subscribe_fin_ = {
      /*track_namespace=*/"foo",
      /*track_name=*/"bar",
      /*final_group=*/8,
      /*final_object=*/12,
  };
};

class QUICHE_NO_EXPORT SubscribeRstMessage : public TestMessageBase {
 public:
  SubscribeRstMessage() : TestMessageBase(MoqtMessageType::kSubscribeRst) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSubscribeRst>(values);
    if (cast.track_namespace != subscribe_rst_.track_namespace) {
      QUIC_LOG(INFO) << "SUBSCRIBE_RST track name mismatch";
      return false;
    }
    if (cast.track_name != subscribe_rst_.track_name) {
      QUIC_LOG(INFO) << "SUBSCRIBE_RST track name mismatch";
      return false;
    }
    if (cast.error_code != subscribe_rst_.error_code) {
      QUIC_LOG(INFO) << "SUBSCRIBE_RST error code mismatch";
      return false;
    }
    if (cast.reason_phrase != subscribe_rst_.reason_phrase) {
      QUIC_LOG(INFO) << "SUBSCRIBE_RST reason phrase mismatch";
      return false;
    }
    if (cast.final_group != subscribe_rst_.final_group) {
      QUIC_LOG(INFO) << "SUBSCRIBE_RST final group mismatch";
      return false;
    }
    if (cast.final_object != subscribe_rst_.final_object) {
      QUIC_LOG(INFO) << "SUBSCRIBE_RST final object mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---v---vv--vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_rst_);
  }

 private:
  uint8_t raw_packet_[15] = {
      0x0c, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x03, 0x62, 0x61, 0x72,        // track_namespace = "bar"
      0x03,                          // error_code = 3
      0x02, 0x68, 0x69,              // reason_phrase = "hi"
      0x08,                          // final_group = 8
      0x0c,                          // final_object = 12
  };

  MoqtSubscribeRst subscribe_rst_ = {
      /*track_namespace=*/"foo",
      /*track_name=*/"bar",
      /*error_code=*/3,
      /*reason_phrase=*/"hi",
      /*final_group=*/8,
      /*final_object=*/12,
  };
};

class QUICHE_NO_EXPORT AnnounceMessage : public TestMessageBase {
 public:
  AnnounceMessage() : TestMessageBase(MoqtMessageType::kAnnounce) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtAnnounce>(values);
    if (cast.track_namespace != announce_.track_namespace) {
      QUIC_LOG(INFO) << "ANNOUNCE MESSAGE track namespace mismatch";
      return false;
    }
    if (cast.authorization_info != announce_.authorization_info) {
      QUIC_LOG(INFO) << "ANNOUNCE MESSAGE authorization info mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_);
  }

 private:
  uint8_t raw_packet_[11] = {
      0x06, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x01,                          // 1 parameter
      0x02, 0x03, 0x62, 0x61, 0x72,  // authorization_info = "bar"
  };

  MoqtAnnounce announce_ = {
      /*track_namespace=*/"foo",
      /*authorization_info=*/"bar",
  };
};

class QUICHE_NO_EXPORT AnnounceOkMessage : public TestMessageBase {
 public:
  AnnounceOkMessage() : TestMessageBase(MoqtMessageType::kAnnounceOk) {
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
  uint8_t raw_packet_[5] = {
      0x07, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
  };

  MoqtAnnounceOk announce_ok_ = {
      /*track_namespace=*/"foo",
  };
};

class QUICHE_NO_EXPORT AnnounceErrorMessage : public TestMessageBase {
 public:
  AnnounceErrorMessage() : TestMessageBase(MoqtMessageType::kAnnounceError) {
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
  uint8_t raw_packet_[10] = {
      0x08, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x01,                          // error_code = 1
      0x03, 0x62, 0x61, 0x72,        // reason_phrase = "bar"
  };

  MoqtAnnounceError announce_error_ = {
      /*track_namespace=*/"foo",
      /*error_code=*/1,
      /*reason_phrase=*/"bar",
  };
};

class QUICHE_NO_EXPORT UnannounceMessage : public TestMessageBase {
 public:
  UnannounceMessage() : TestMessageBase(MoqtMessageType::kUnannounce) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtUnannounce>(values);
    if (cast.track_namespace != unannounce_.track_namespace) {
      QUIC_LOG(INFO) << "UNSUBSCRIBE full track name mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unannounce_);
  }

 private:
  uint8_t raw_packet_[5] = {
      0x09, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace
  };

  MoqtUnannounce unannounce_ = {
      /*track_namespace=*/"foo",
  };
};

class QUICHE_NO_EXPORT GoAwayMessage : public TestMessageBase {
 public:
  GoAwayMessage() : TestMessageBase(MoqtMessageType::kGoAway) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtGoAway>(values);
    if (cast.new_session_uri != goaway_.new_session_uri) {
      QUIC_LOG(INFO) << "UNSUBSCRIBE full track name mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(goaway_);
  }

 private:
  uint8_t raw_packet_[5] = {
      0x10, 0x03, 0x66, 0x6f, 0x6f,
  };

  MoqtGoAway goaway_ = {
      /*new_session_uri=*/"foo",
  };
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_
