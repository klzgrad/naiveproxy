// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
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

  typedef absl::variant<MoqtSetup, MoqtObject, MoqtSubscribeRequest,
                        MoqtSubscribeOk, MoqtSubscribeError, MoqtUnsubscribe,
                        MoqtAnnounce, MoqtAnnounceOk, MoqtAnnounceError,
                        MoqtUnannounce, MoqtGoAway>
      MessageStructuredData;

  // The total actual size of the message.
  size_t total_message_size() const { return wire_image_size_; }

  // The message size indicated in the second varint in every message.
  size_t message_size() const {
    quic::QuicDataReader reader(PacketSample());
    uint64_t value;
    if (!reader.ReadVarInt62(&value)) {
      return 0;
    }
    if (!reader.ReadVarInt62(&value)) {
      return 0;
    }
    return value;
  }

  absl::string_view PacketSample() const {
    return absl::string_view(wire_image_, wire_image_size_);
  }

  void set_wire_image_size(size_t wire_image_size) {
    wire_image_size_ = wire_image_size;
  }

  // Returns a copy of the structured data for the message.
  virtual MessageStructuredData structured_data() const = 0;

  // Sets the message length field. If |message_size| == 0, just change the
  // field in the wire image. If another value, this will either truncate the
  // message or increase its length (which adds uninitialized bytes). This can
  // be useful for playing with different Object Payload lengths, for example.
  void set_message_size(uint64_t message_size) {
    char new_wire_image[sizeof(wire_image_)];
    quic::QuicDataReader reader(PacketSample());
    quic::QuicDataWriter writer(sizeof(new_wire_image), new_wire_image);
    uint64_t type;
    auto field_size = reader.PeekVarInt62Length();
    reader.ReadVarInt62(&type);
    writer.WriteVarInt62WithForcedLength(
        type, std::max(field_size, writer.GetVarInt62Len(type)));
    uint64_t original_length;
    field_size = reader.PeekVarInt62Length();
    reader.ReadVarInt62(&original_length);
    // Try to preserve the original field length, unless it's too small.
    writer.WriteVarInt62WithForcedLength(
        message_size,
        std::max(field_size, writer.GetVarInt62Len(message_size)));
    writer.WriteStringPiece(reader.PeekRemainingPayload());
    memcpy(wire_image_, new_wire_image, writer.length());
    wire_image_size_ = writer.length();
    if (message_size > original_length) {
      wire_image_size_ += (message_size - original_length);
    }
    if (message_size > 0 && message_size < original_length) {
      wire_image_size_ -= (original_length - message_size);
    }
  }

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
    size_t message_length = 0;
    int item = 0;
    size_t i = 0;
    while (!reader.IsDoneReading()) {
      if (i >= varints.length() || varints[i++] == '-') {
        uint8_t byte;
        reader.ReadUInt8(&byte);
        writer.WriteUInt8(byte);
        continue;
      }
      uint64_t value;
      item++;
      reader.ReadVarInt62(&value);
      writer.WriteVarInt62WithForcedLength(
          value, static_cast<quiche::QuicheVariableLengthIntegerLength>(
                     next_varint_len));
      if (item == 2) {
        // this is the message length field.
        message_length = value;
      }
      next_varint_len *= 2;
      if (next_varint_len == 16) {
        next_varint_len = 2;
      }
    }
    if (message_length > 0) {
      // Update message length. Based on the progression of next_varint_len,
      // the message_type is 2 bytes and message_length is 4 bytes.
      message_length = writer.length() - 6;
      auto new_writer = quic::QuicDataWriter(4, (char*)&new_wire_image[2]);
      new_writer.WriteVarInt62WithForcedLength(
          message_length,
          static_cast<quiche::QuicheVariableLengthIntegerLength>(4));
    }
    memcpy(wire_image_, new_wire_image, writer.length());
    wire_image_size_ = writer.length();
  }

 private:
  MoqtMessageType message_type_;
  char wire_image_[kMaxMessageHeaderSize + 20];
  size_t wire_image_size_;
};

class QUICHE_NO_EXPORT ObjectMessage : public TestMessageBase {
 public:
  ObjectMessage() : TestMessageBase(MoqtMessageType::kObject) {
    SetWireImage(raw_packet_, sizeof(raw_packet_));
  }

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

 private:
  uint8_t raw_packet_[9] = {
      0x00, 0x07, 0x04, 0x05, 0x06, 0x07,  // varints
      0x66, 0x6f, 0x6f,                    // payload = "foo"
  };
  MoqtObject object_ = {
      /*track_id=*/4,
      /*group_sequence=*/5,
      /*object_sequence=*/6,
      /*object_send_order=*/7,
  };
};

class QUICHE_NO_EXPORT SetupMessage : public TestMessageBase {
 public:
  explicit SetupMessage(bool client_parser, bool webtrans)
      : TestMessageBase(MoqtMessageType::kSetup), client_(client_parser) {
    if (client_parser) {
      SetWireImage(server_raw_packet_, sizeof(server_raw_packet_));
    } else {
      SetWireImage(client_raw_packet_, sizeof(client_raw_packet_));
      if (webtrans) {
        // Should not send PATH.
        set_message_size(message_size() - 5);
        client_setup_.path = absl::nullopt;
      }
    }
  }

  bool EqualFieldValues(MessageStructuredData& values) const override {
    auto cast = std::get<MoqtSetup>(values);
    const MoqtSetup* compare = client_ ? &server_setup_ : &client_setup_;
    if (cast.supported_versions.size() != compare->supported_versions.size()) {
      QUIC_LOG(INFO) << "SETUP number of supported versions mismatch";
      return false;
    }
    for (uint64_t i = 0; i < cast.supported_versions.size(); ++i) {
      // Listed versions are 1 and 2, in that order.
      if (cast.supported_versions[i] != compare->supported_versions[i]) {
        QUIC_LOG(INFO) << "SETUP supported version mismatch";
        return false;
      }
    }
    if (cast.role != compare->role) {
      QUIC_LOG(INFO) << "SETUP role mismatch";
      return false;
    }
    if (cast.path != compare->path) {
      QUIC_LOG(INFO) << "SETUP path mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override {
    if (client_) {
      ExpandVarintsImpl("vvvvvvv-vv---");  // skip one byte for Role value
    } else {
      ExpandVarintsImpl("vvv");  // all three are varints
    }
  }

  MessageStructuredData structured_data() const override {
    if (client_) {
      return TestMessageBase::MessageStructuredData(server_setup_);
    }
    return TestMessageBase::MessageStructuredData(client_setup_);
  }

 private:
  bool client_;
  uint8_t client_raw_packet_[13] = {
      0x01, 0x0b, 0x02, 0x01, 0x02,  // versions
      0x00, 0x01, 0x03,              // role = both
      0x01, 0x03, 0x66, 0x6f, 0x6f   //  path = "foo"
  };
  uint8_t server_raw_packet_[3] = {
      0x01, 0x01,
      0x01,  // version
  };
  MoqtSetup client_setup_ = {
      /*supported_versions=*/std::vector<MoqtVersion>(
          {static_cast<MoqtVersion>(1), static_cast<MoqtVersion>(2)}),
      /*role=*/MoqtRole::kBoth,
      /*path=*/"foo",
  };
  MoqtSetup server_setup_ = {
      /*supported_versions=*/std::vector<MoqtVersion>(
          {static_cast<MoqtVersion>(1)}),
      /*role=*/absl::nullopt,
      /*path=*/absl::nullopt,
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
    if (cast.full_track_name != subscribe_request_.full_track_name) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST full track name mismatch";
      return false;
    }
    if (cast.group_sequence != subscribe_request_.group_sequence) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST group sequence mismatch";
      return false;
    }
    if (cast.object_sequence != subscribe_request_.object_sequence) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST object sequence mismatch";
      return false;
    }
    if (cast.authorization_info != subscribe_request_.authorization_info) {
      QUIC_LOG(INFO) << "SUBSCRIBE REQUEST authorization info mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---vv-vv-vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_request_);
  }

 private:
  uint8_t raw_packet_[17] = {
      0x03, 0x0f, 0x03, 0x66, 0x6f, 0x6f,  // track_name = "foo"
      0x00, 0x01, 0x01,                    // group_sequence = 1
      0x01, 0x01, 0x02,                    // object_sequence = 2
      0x02, 0x03, 0x62, 0x61, 0x72,        // authorization_info = "bar"
  };

  MoqtSubscribeRequest subscribe_request_ = {
      /*full_track_name=*/"foo",
      /*group_sequence=*/1,
      /*object_sequence=*/2,
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
    if (cast.full_track_name != subscribe_ok_.full_track_name) {
      return false;
    }
    if (cast.track_id != subscribe_ok_.track_id) {
      return false;
    }
    if (cast.expires != subscribe_ok_.expires) {
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_ok_);
  }

 private:
  uint8_t raw_packet_[8] = {
      0x04, 0x06, 0x03, 0x66, 0x6f, 0x6f,  // track_name = "foo"
      0x01,                                // track_id = 1
      0x02,                                // expires = 2
  };

  MoqtSubscribeOk subscribe_ok_ = {
      /*full_track_name=*/"foo",
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
    if (cast.full_track_name != subscribe_error_.full_track_name) {
      QUIC_LOG(INFO) << "SUBSCRIBE ERROR full track name mismatch";
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

  void ExpandVarints() override { ExpandVarintsImpl("vvv---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(subscribe_error_);
  }

 private:
  uint8_t raw_packet_[11] = {
      0x05, 0x09, 0x03, 0x66, 0x6f, 0x6f,  // track_name = "foo"
      0x01,                                // error_code = 1
      0x03, 0x62, 0x61, 0x72,              // reason_phrase = "bar"
  };

  MoqtSubscribeError subscribe_error_ = {
      /*full_track_name=*/"foo",
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
    if (cast.full_track_name != unsubscribe_.full_track_name) {
      QUIC_LOG(INFO) << "UNSUBSCRIBE full track name mismatch";
      return false;
    }
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unsubscribe_);
  }

 private:
  uint8_t raw_packet_[6] = {
      0x0a, 0x04, 0x03, 0x66, 0x6f, 0x6f,  // track_name = "foo"
  };

  MoqtUnsubscribe unsubscribe_ = {
      /*full_track_name=*/"foo",
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

  void ExpandVarints() override { ExpandVarintsImpl("vvv---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_);
  }

 private:
  uint8_t raw_packet_[11] = {
      0x06, 0x09, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x02, 0x03, 0x62, 0x61, 0x72,        // authorization_info = "bar"
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

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_ok_);
  }

 private:
  uint8_t raw_packet_[6] = {
      0x07, 0x04, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
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

  void ExpandVarints() override { ExpandVarintsImpl("vvv---vv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(announce_error_);
  }

 private:
  uint8_t raw_packet_[11] = {
      0x08, 0x09, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace = "foo"
      0x01,                                // error__code = 1
      0x03, 0x62, 0x61, 0x72,              // reason_phrase = "bar"
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

  void ExpandVarints() override { ExpandVarintsImpl("vvv---"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(unannounce_);
  }

 private:
  uint8_t raw_packet_[6] = {
      0x09, 0x04, 0x03, 0x66, 0x6f, 0x6f,  // track_namespace
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

  bool EqualFieldValues(MessageStructuredData& /*values*/) const override {
    return true;
  }

  void ExpandVarints() override { ExpandVarintsImpl("vv"); }

  MessageStructuredData structured_data() const override {
    return TestMessageBase::MessageStructuredData(goaway_);
  }

 private:
  uint8_t raw_packet_[2] = {
      0x10,
      0x00,
  };

  MoqtGoAway goaway_ = {};
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_TEST_MESSAGE_H_
