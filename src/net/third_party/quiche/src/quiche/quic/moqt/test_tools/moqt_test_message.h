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
                        MoqtSubscribe, MoqtSubscribeOk, MoqtSubscribeError,
                        MoqtUnsubscribe, MoqtSubscribeDone, MoqtAnnounce,
                        MoqtAnnounceOk, MoqtAnnounceError, MoqtUnannounce,
                        MoqtGoAway>
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
  ObjectMessage(MoqtMessageType type) : TestMessageBase(type) {
    object_.forwarding_preference = GetForwardingPreference(type);
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtObject>(values);
    if (cast.subscribe_id != object_.subscribe_id) {
      QUIC_LOG(INFO) << "OBJECT Track ID mismatch";
      return false;
    }
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
    if (cast.object_send_order != object_.object_send_order) {
      QUIC_LOG(INFO) << "OBJECT Object Send Order mismatch";
      return false;
    }
    if (cast.forwarding_preference != object_.forwarding_preference) {
      QUIC_LOG(INFO) << "OBJECT Object Send Order mismatch";
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
      /*subscribe_id=*/3,
      /*track_alias=*/4,
      /*group_id*/ 5,
      /*object_id=*/6,
      /*object_send_order=*/7,
      /*forwarding_preference=*/MoqtForwardingPreference::kTrack,
      /*payload_length=*/std::nullopt,
  };
};

class QUICHE_NO_EXPORT ObjectStreamMessage : public ObjectMessage {
 public:
  ObjectStreamMessage() : ObjectMessage(MoqtMessageType::kObjectStream) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kObject;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvvvv");  // first six fields are varints
  }

 private:
  uint8_t raw_packet_[9] = {
      0x00, 0x03, 0x04, 0x05, 0x06, 0x07,  // varints
      0x66, 0x6f, 0x6f,                    // payload = "foo"
  };
};

class QUICHE_NO_EXPORT ObjectDatagramMessage : public ObjectMessage {
 public:
  ObjectDatagramMessage() : ObjectMessage(MoqtMessageType::kObjectDatagram) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kDatagram;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvvvv");  // first six fields are varints
  }

 private:
  uint8_t raw_packet_[9] = {
      0x01, 0x03, 0x04, 0x05, 0x06, 0x07,  // varints
      0x66, 0x6f, 0x6f,                    // payload = "foo"
  };
};

// Concatentation of the base header and the object-specific header. Follow-on
// object headers are handled in a different class.
class QUICHE_NO_EXPORT StreamHeaderTrackMessage : public ObjectMessage {
 public:
  StreamHeaderTrackMessage()
      : ObjectMessage(MoqtMessageType::kStreamHeaderTrack) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kTrack;
    object_.payload_length = 3;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("--vvvvvv");  // six one-byte varints
  }

 private:
  // Some tests check that a FIN sent at the halfway point of a message results
  // in an error. Without the unnecessary expanded varint 0x0405, the halfway
  // point falls at the end of the Stream Header, which is legal. Expand the
  // varint so that the FIN would be illegal.
  uint8_t raw_packet_[11] = {
      0x40, 0x50,              // two byte type field
      0x03, 0x04, 0x07,        // varints
      0x05, 0x06,              // object middler
      0x03, 0x66, 0x6f, 0x6f,  // payload = "foo"
  };
};

// Used only for tests that process multiple objects on one stream.
class QUICHE_NO_EXPORT StreamMiddlerTrackMessage : public ObjectMessage {
 public:
  StreamMiddlerTrackMessage()
      : ObjectMessage(MoqtMessageType::kStreamHeaderTrack) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kTrack;
    object_.payload_length = 3;
    object_.group_id = 9;
    object_.object_id = 10;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv"); }

 private:
  uint8_t raw_packet_[6] = {
      0x09, 0x0a, 0x03, 0x62, 0x61, 0x72,  // object middler; payload = "bar"
  };
};

class QUICHE_NO_EXPORT StreamHeaderGroupMessage : public ObjectMessage {
 public:
  StreamHeaderGroupMessage()
      : ObjectMessage(MoqtMessageType::kStreamHeaderGroup) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kGroup;
    object_.payload_length = 3;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("--vvvvvv");  // six one-byte varints
  }

 private:
  uint8_t raw_packet_[11] = {
      0x40, 0x51,                    // two-byte type field
      0x03, 0x04, 0x05, 0x07,        // varints
      0x06, 0x03, 0x66, 0x6f, 0x6f,  // object middler; payload = "foo"
  };
};

// Used only for tests that process multiple objects on one stream.
class QUICHE_NO_EXPORT StreamMiddlerGroupMessage : public ObjectMessage {
 public:
  StreamMiddlerGroupMessage()
      : ObjectMessage(MoqtMessageType::kStreamHeaderGroup) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
    object_.forwarding_preference = MoqtForwardingPreference::kGroup;
    object_.payload_length = 3;
    object_.object_id = 9;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv"); }

 private:
  uint8_t raw_packet_[5] = {
      0x09, 0x03, 0x62, 0x61, 0x72,  // object middler; payload = "bar"
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
      0x00, 0x01, 0x03,             // role = PubSub
      0x01, 0x03, 0x66, 0x6f, 0x6f  // path = "foo"
  };
  MoqtClientSetup client_setup_ = {
      /*supported_versions=*/std::vector<MoqtVersion>(
          {static_cast<MoqtVersion>(1), static_cast<MoqtVersion>(2)}),
      /*role=*/MoqtRole::kPubSub,
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
    ExpandVarintsImpl("--vvvv-");  // first two bytes are already a 2b varint
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(server_setup_);
  }

 private:
  uint8_t raw_packet_[7] = {
      0x40, 0x41,        // type
      0x01, 0x01,        // version, one param
      0x00, 0x01, 0x03,  // role = PubSub
  };
  MoqtServerSetup server_setup_ = {
      /*selected_version=*/static_cast<MoqtVersion>(1),
      /*role=*/MoqtRole::kPubSub,
  };
};

class QUICHE_NO_EXPORT SubscribeMessage : public TestMessageBase {
 public:
  SubscribeMessage() : TestMessageBase(MoqtMessageType::kSubscribe) {
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
    if (cast.track_namespace != subscribe_.track_namespace) {
      QUIC_LOG(INFO) << "SUBSCRIBE track namespace mismatch";
      return false;
    }
    if (cast.track_name != subscribe_.track_name) {
      QUIC_LOG(INFO) << "SUBSCRIBE track name mismatch";
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
    if (cast.authorization_info != subscribe_.authorization_info) {
      QUIC_LOG(INFO) << "SUBSCRIBE authorization info mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    ExpandVarintsImpl("vvvv---v----vvvvvvvvv");
  }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_);
  }

 private:
  uint8_t raw_packet_[24] = {
    0x03,
    0x01,
    0x02,  // id and alias
    0x03,
    0x66,
    0x6f,
    0x6f,  // track_namespace = "foo"
    0x04,
    0x61,
    0x62,
    0x63,
    0x64,  // track_name = "abcd"
    0x02,
    0x04,  // start_group = 4 (relative previous)
    0x01,
    0x01,  // start_object = 1 (absolute)
    0x00,  // end_group = none
    0x00,  // end_object = none
           // TODO(martinduke): figure out what to do about the missing num
           // parameters field.
    0x01,  // 1 parameter
    0x02,
    0x03,
    0x62,
    0x61,
    0x72,  // authorization_info = "bar"
  };

  MoqtSubscribe subscribe_ = {
      /*subscribe_id=*/1,
      /*track_alias=*/2,
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
    if (cast.subscribe_id != subscribe_ok_.subscribe_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK subscribe ID mismatch";
      return false;
    }
    if (cast.expires != subscribe_ok_.expires) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK expiration mismatch";
      return false;
    }
    if (cast.largest_id != subscribe_ok_.largest_id) {
      QUIC_LOG(INFO) << "SUBSCRIBE OK largest ID mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv-vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_ok_);
  }

  void SetInvalidContentExists() {
    raw_packet_[3] = 0x02;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[6] = {
      0x04, 0x01, 0x03,  // subscribe_id = 1, expires = 3
      0x01, 0x0c, 0x14,  // largest_group_id = 12, largest_object_id = 20,
  };

  MoqtSubscribeOk subscribe_ok_ = {
      /*subscribe_id=*/1,
      /*expires=*/quic::QuicTimeDelta::FromMilliseconds(3),
      /*largest_id=*/FullSequence(12, 20),
  };
};

class QUICHE_NO_EXPORT SubscribeErrorMessage : public TestMessageBase {
 public:
  SubscribeErrorMessage() : TestMessageBase(MoqtMessageType::kSubscribeError) {
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---v"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_error_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x05, 0x02,              // subscribe_id = 2
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
  UnsubscribeMessage() : TestMessageBase(MoqtMessageType::kUnsubscribe) {
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

  void ExpandVarints() override { ExpandVarintsImpl("vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unsubscribe_);
  }

 private:
  uint8_t raw_packet_[2] = {
      0x0a, 0x03,  // subscribe_id = 3
  };

  MoqtUnsubscribe unsubscribe_ = {
      /*subscribe_id=*/3,
  };
};

class QUICHE_NO_EXPORT SubscribeDoneMessage : public TestMessageBase {
 public:
  SubscribeDoneMessage() : TestMessageBase(MoqtMessageType::kSubscribeDone) {
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

  void ExpandVarints() override { ExpandVarintsImpl("vvvv---vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_done_);
  }

  void SetInvalidContentExists() {
    raw_packet_[6] = 0x02;
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

 private:
  uint8_t raw_packet_[9] = {
      0x0b, 0x02, 0x03,  // subscribe_id = 2, error_code = 3,
      0x02, 0x68, 0x69,  // reason_phrase = "hi"
      0x01, 0x08, 0x0c,  // final_id = (8,12)
  };

  MoqtSubscribeDone subscribe_done_ = {
      /*subscribe_id=*/2,
      /*error_code=*/3,
      /*reason_phrase=*/"hi",
      /*final_id=*/FullSequence(8, 12),
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
      /*error_code=*/MoqtAnnounceErrorCode::kAnnounceNotSupported,
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

// Factory function for test messages.
static inline std::unique_ptr<TestMessageBase> CreateTestMessage(
    MoqtMessageType message_type, bool is_webtrans) {
  switch (message_type) {
    case MoqtMessageType::kObjectStream:
      return std::make_unique<ObjectStreamMessage>();
    case MoqtMessageType::kObjectDatagram:
      return std::make_unique<ObjectDatagramMessage>();
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
    case MoqtMessageType::kAnnounce:
      return std::make_unique<AnnounceMessage>();
    case MoqtMessageType::kAnnounceOk:
      return std::make_unique<AnnounceOkMessage>();
    case MoqtMessageType::kAnnounceError:
      return std::make_unique<AnnounceErrorMessage>();
    case MoqtMessageType::kUnannounce:
      return std::make_unique<UnannounceMessage>();
    case MoqtMessageType::kGoAway:
      return std::make_unique<GoAwayMessage>();
    case MoqtMessageType::kClientSetup:
      return std::make_unique<ClientSetupMessage>(is_webtrans);
    case MoqtMessageType::kServerSetup:
      return std::make_unique<ServerSetupMessage>();
    case MoqtMessageType::kStreamHeaderTrack:
      return std::make_unique<StreamHeaderTrackMessage>();
    case MoqtMessageType::kStreamHeaderGroup:
      return std::make_unique<StreamHeaderGroupMessage>();
    default:
      return nullptr;
  }
}

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_
