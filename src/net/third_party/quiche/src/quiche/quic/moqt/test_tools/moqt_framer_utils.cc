// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/test_tools/moqt_framer_utils.h"

#include <cstdint>
#include <string>
#include <variant>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_data_reader.h"

namespace moqt::test {

namespace {

struct FramingVisitor {
  quiche::QuicheBuffer operator()(const MoqtSetup& message) {
    return framer.SerializeSetup(message);
  }
  quiche::QuicheBuffer operator()(const MoqtRequestOk& message) {
    return framer.SerializeRequestOk(message);
  }
  quiche::QuicheBuffer operator()(const MoqtRequestError& message) {
    return framer.SerializeRequestError(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribe& message) {
    return framer.SerializeSubscribe(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeOk& message) {
    return framer.SerializeSubscribeOk(message);
  }
  quiche::QuicheBuffer operator()(const MoqtPublishDone& message) {
    return framer.SerializePublishDone(message);
  }
  quiche::QuicheBuffer operator()(const MoqtRequestUpdate& message) {
    return framer.SerializeRequestUpdate(message);
  }
  quiche::QuicheBuffer operator()(const MoqtPublishNamespace& message) {
    return framer.SerializePublishNamespace(message);
  }
  quiche::QuicheBuffer operator()(const MoqtNamespace& message) {
    return framer.SerializeNamespace(message);
  }
  quiche::QuicheBuffer operator()(const MoqtNamespaceDone& message) {
    return framer.SerializeNamespaceDone(message);
  }
  quiche::QuicheBuffer operator()(const MoqtTrackStatus& message) {
    return framer.SerializeTrackStatus(message);
  }
  quiche::QuicheBuffer operator()(const MoqtGoAway& message) {
    return framer.SerializeGoAway(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeNamespace& message) {
    return framer.SerializeSubscribeNamespace(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeTracks& message) {
    return framer.SerializeSubscribeTracks(message);
  }
  quiche::QuicheBuffer operator()(const MoqtMaxRequestId& message) {
    return framer.SerializeMaxRequestId(message);
  }
  quiche::QuicheBuffer operator()(const MoqtFetch& message) {
    return framer.SerializeFetch(message);
  }
  quiche::QuicheBuffer operator()(const MoqtFetchCancel& message) {
    return framer.SerializeFetchCancel(message);
  }
  quiche::QuicheBuffer operator()(const MoqtFetchOk& message) {
    return framer.SerializeFetchOk(message);
  }
  quiche::QuicheBuffer operator()(const MoqtRequestsBlocked& message) {
    return framer.SerializeRequestsBlocked(message);
  }
  quiche::QuicheBuffer operator()(const MoqtPublish& message) {
    return framer.SerializePublish(message);
  }
  quiche::QuicheBuffer operator()(const MoqtObjectAck& message) {
    return framer.SerializeObjectAck(message);
  }

  MoqtFramer& framer;
  bool is_track_status;
};

}  // namespace

std::string SerializeGenericMessage(const AnyMoqtControlMessage& frame,
                                    bool use_webtrans) {
  quic::Perspective perspective = quic::Perspective::IS_CLIENT;
  if (std::holds_alternative<MoqtSetup>(frame)) {
    const MoqtSetup& setup = std::get<MoqtSetup>(frame);
    if (!use_webtrans && !setup.parameters.path.has_value()) {
      perspective = quic::Perspective::IS_SERVER;
    }
  }
  MoqtFramer framer(use_webtrans, perspective);
  return std::string(std::visit(FramingVisitor{framer}, frame).AsStringView());
}

MoqtRawControlMessage UnframeRawControlMessage(absl::string_view message) {
  quiche::QuicheDataReader reader(message);
  uint64_t raw_type;
  uint16_t message_size;
  bool parse_success = reader.ReadMoqVarInt(&raw_type) &&
                       reader.ReadUInt16(&message_size) &&
                       reader.BytesRemaining() == message_size;
  if (!parse_success) {
    ADD_FAILURE() << "Failed to unframe the control message";
    return MoqtRawControlMessage();
  }
  return MoqtRawControlMessage{
      .type = static_cast<MoqtMessageType>(raw_type),
      .payload = std::string(reader.ReadRemainingPayload())};
}

}  // namespace moqt::test
