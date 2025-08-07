// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/test_tools/moqt_framer_utils.h"

#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/web_transport/test_tools/in_memory_stream.h"

namespace moqt::test {

namespace {

struct TypeVisitor {
  MoqtMessageType operator()(const MoqtClientSetup&) {
    return MoqtMessageType::kClientSetup;
  }
  MoqtMessageType operator()(const MoqtServerSetup&) {
    return MoqtMessageType::kServerSetup;
  }
  MoqtMessageType operator()(const MoqtSubscribe&) {
    return MoqtMessageType::kSubscribe;
  }
  MoqtMessageType operator()(const MoqtSubscribeOk&) {
    return MoqtMessageType::kSubscribeOk;
  }
  MoqtMessageType operator()(const MoqtSubscribeError&) {
    return MoqtMessageType::kSubscribeError;
  }
  MoqtMessageType operator()(const MoqtUnsubscribe&) {
    return MoqtMessageType::kUnsubscribe;
  }
  MoqtMessageType operator()(const MoqtSubscribeDone&) {
    return MoqtMessageType::kSubscribeDone;
  }
  MoqtMessageType operator()(const MoqtSubscribeUpdate&) {
    return MoqtMessageType::kSubscribeUpdate;
  }
  MoqtMessageType operator()(const MoqtAnnounce&) {
    return MoqtMessageType::kAnnounce;
  }
  MoqtMessageType operator()(const MoqtAnnounceOk&) {
    return MoqtMessageType::kAnnounceOk;
  }
  MoqtMessageType operator()(const MoqtAnnounceError&) {
    return MoqtMessageType::kAnnounceError;
  }
  MoqtMessageType operator()(const MoqtAnnounceCancel&) {
    return MoqtMessageType::kAnnounceCancel;
  }
  MoqtMessageType operator()(const MoqtTrackStatusRequest&) {
    return MoqtMessageType::kTrackStatusRequest;
  }
  MoqtMessageType operator()(const MoqtUnannounce&) {
    return MoqtMessageType::kUnannounce;
  }
  MoqtMessageType operator()(const MoqtTrackStatus&) {
    return MoqtMessageType::kTrackStatus;
  }
  MoqtMessageType operator()(const MoqtGoAway&) {
    return MoqtMessageType::kGoAway;
  }
  MoqtMessageType operator()(const MoqtSubscribeAnnounces&) {
    return MoqtMessageType::kSubscribeAnnounces;
  }
  MoqtMessageType operator()(const MoqtSubscribeAnnouncesOk&) {
    return MoqtMessageType::kSubscribeAnnouncesOk;
  }
  MoqtMessageType operator()(const MoqtSubscribeAnnouncesError&) {
    return MoqtMessageType::kSubscribeAnnouncesError;
  }
  MoqtMessageType operator()(const MoqtUnsubscribeAnnounces&) {
    return MoqtMessageType::kUnsubscribeAnnounces;
  }
  MoqtMessageType operator()(const MoqtMaxRequestId&) {
    return MoqtMessageType::kMaxRequestId;
  }
  MoqtMessageType operator()(const MoqtFetch&) {
    return MoqtMessageType::kFetch;
  }
  MoqtMessageType operator()(const MoqtFetchCancel&) {
    return MoqtMessageType::kFetchCancel;
  }
  MoqtMessageType operator()(const MoqtFetchOk&) {
    return MoqtMessageType::kFetchOk;
  }
  MoqtMessageType operator()(const MoqtFetchError&) {
    return MoqtMessageType::kFetchError;
  }
  MoqtMessageType operator()(const MoqtRequestsBlocked&) {
    return MoqtMessageType::kRequestsBlocked;
  }
  MoqtMessageType operator()(const MoqtObjectAck&) {
    return MoqtMessageType::kObjectAck;
  }
};

struct FramingVisitor {
  quiche::QuicheBuffer operator()(const MoqtClientSetup& message) {
    return framer.SerializeClientSetup(message);
  }
  quiche::QuicheBuffer operator()(const MoqtServerSetup& message) {
    return framer.SerializeServerSetup(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribe& message) {
    return framer.SerializeSubscribe(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeOk& message) {
    return framer.SerializeSubscribeOk(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeError& message) {
    return framer.SerializeSubscribeError(message);
  }
  quiche::QuicheBuffer operator()(const MoqtUnsubscribe& message) {
    return framer.SerializeUnsubscribe(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeDone& message) {
    return framer.SerializeSubscribeDone(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeUpdate& message) {
    return framer.SerializeSubscribeUpdate(message);
  }
  quiche::QuicheBuffer operator()(const MoqtAnnounce& message) {
    return framer.SerializeAnnounce(message);
  }
  quiche::QuicheBuffer operator()(const MoqtAnnounceOk& message) {
    return framer.SerializeAnnounceOk(message);
  }
  quiche::QuicheBuffer operator()(const MoqtAnnounceError& message) {
    return framer.SerializeAnnounceError(message);
  }
  quiche::QuicheBuffer operator()(const MoqtAnnounceCancel& message) {
    return framer.SerializeAnnounceCancel(message);
  }
  quiche::QuicheBuffer operator()(const MoqtTrackStatusRequest& message) {
    return framer.SerializeTrackStatusRequest(message);
  }
  quiche::QuicheBuffer operator()(const MoqtUnannounce& message) {
    return framer.SerializeUnannounce(message);
  }
  quiche::QuicheBuffer operator()(const MoqtTrackStatus& message) {
    return framer.SerializeTrackStatus(message);
  }
  quiche::QuicheBuffer operator()(const MoqtGoAway& message) {
    return framer.SerializeGoAway(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeAnnounces& message) {
    return framer.SerializeSubscribeAnnounces(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeAnnouncesOk& message) {
    return framer.SerializeSubscribeAnnouncesOk(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeAnnouncesError& message) {
    return framer.SerializeSubscribeAnnouncesError(message);
  }
  quiche::QuicheBuffer operator()(const MoqtUnsubscribeAnnounces& message) {
    return framer.SerializeUnsubscribeAnnounces(message);
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
  quiche::QuicheBuffer operator()(const MoqtFetchError& message) {
    return framer.SerializeFetchError(message);
  }
  quiche::QuicheBuffer operator()(const MoqtRequestsBlocked& message) {
    return framer.SerializeRequestsBlocked(message);
  }
  quiche::QuicheBuffer operator()(const MoqtObjectAck& message) {
    return framer.SerializeObjectAck(message);
  }

  MoqtFramer& framer;
};

class GenericMessageParseVisitor : public MoqtControlParserVisitor {
 public:
  explicit GenericMessageParseVisitor(std::vector<MoqtGenericFrame>* frames)
      : frames_(*frames) {}

  void OnClientSetupMessage(const MoqtClientSetup& message) {
    frames_.push_back(message);
  }
  void OnServerSetupMessage(const MoqtServerSetup& message) {
    frames_.push_back(message);
  }
  void OnSubscribeMessage(const MoqtSubscribe& message) {
    frames_.push_back(message);
  }
  void OnSubscribeOkMessage(const MoqtSubscribeOk& message) {
    frames_.push_back(message);
  }
  void OnSubscribeErrorMessage(const MoqtSubscribeError& message) {
    frames_.push_back(message);
  }
  void OnUnsubscribeMessage(const MoqtUnsubscribe& message) {
    frames_.push_back(message);
  }
  void OnSubscribeDoneMessage(const MoqtSubscribeDone& message) {
    frames_.push_back(message);
  }
  void OnSubscribeUpdateMessage(const MoqtSubscribeUpdate& message) {
    frames_.push_back(message);
  }
  void OnAnnounceMessage(const MoqtAnnounce& message) {
    frames_.push_back(message);
  }
  void OnAnnounceOkMessage(const MoqtAnnounceOk& message) {
    frames_.push_back(message);
  }
  void OnAnnounceErrorMessage(const MoqtAnnounceError& message) {
    frames_.push_back(message);
  }
  void OnAnnounceCancelMessage(const MoqtAnnounceCancel& message) {
    frames_.push_back(message);
  }
  void OnTrackStatusRequestMessage(const MoqtTrackStatusRequest& message) {
    frames_.push_back(message);
  }
  void OnUnannounceMessage(const MoqtUnannounce& message) {
    frames_.push_back(message);
  }
  void OnTrackStatusMessage(const MoqtTrackStatus& message) {
    frames_.push_back(message);
  }
  void OnGoAwayMessage(const MoqtGoAway& message) {
    frames_.push_back(message);
  }
  void OnSubscribeAnnouncesMessage(const MoqtSubscribeAnnounces& message) {
    frames_.push_back(message);
  }
  void OnSubscribeAnnouncesOkMessage(const MoqtSubscribeAnnouncesOk& message) {
    frames_.push_back(message);
  }
  void OnSubscribeAnnouncesErrorMessage(
      const MoqtSubscribeAnnouncesError& message) {
    frames_.push_back(message);
  }
  void OnUnsubscribeAnnouncesMessage(const MoqtUnsubscribeAnnounces& message) {
    frames_.push_back(message);
  }
  void OnMaxRequestIdMessage(const MoqtMaxRequestId& message) {
    frames_.push_back(message);
  }
  void OnFetchMessage(const MoqtFetch& message) { frames_.push_back(message); }
  void OnFetchCancelMessage(const MoqtFetchCancel& message) {
    frames_.push_back(message);
  }
  void OnFetchOkMessage(const MoqtFetchOk& message) {
    frames_.push_back(message);
  }
  void OnFetchErrorMessage(const MoqtFetchError& message) {
    frames_.push_back(message);
  }
  void OnRequestsBlockedMessage(const MoqtRequestsBlocked& message) {
    frames_.push_back(message);
  }
  void OnObjectAckMessage(const MoqtObjectAck& message) {
    frames_.push_back(message);
  }

  void OnParsingError(MoqtError code, absl::string_view reason) {
    ADD_FAILURE() << "Parsing failed: " << reason;
  }

 private:
  std::vector<MoqtGenericFrame>& frames_;
};

}  // namespace

std::string SerializeGenericMessage(const MoqtGenericFrame& frame,
                                    bool use_webtrans) {
  MoqtFramer framer(quiche::SimpleBufferAllocator::Get(), use_webtrans);
  return std::string(std::visit(FramingVisitor{framer}, frame).AsStringView());
}

MoqtMessageType MessageTypeForGenericMessage(const MoqtGenericFrame& frame) {
  return std::visit(TypeVisitor(), frame);
}

std::vector<MoqtGenericFrame> ParseGenericMessage(absl::string_view body) {
  std::vector<MoqtGenericFrame> result;
  GenericMessageParseVisitor visitor(&result);
  webtransport::test::InMemoryStream stream(/*id=*/0);
  MoqtControlParser parser(/*uses_web_transport=*/true, &stream, visitor);
  stream.Receive(body, /*fin=*/false);
  parser.ReadAndDispatchMessages();
  return result;
}

absl::Status StoreSubscribe::operator()(
    absl::Span<const absl::string_view> data,
    const quiche::StreamWriteOptions& options) const {
  std::string merged_message = absl::StrJoin(data, "");
  std::vector<MoqtGenericFrame> frames = ParseGenericMessage(merged_message);
  if (frames.size() != 1 || !std::holds_alternative<MoqtSubscribe>(frames[0])) {
    ADD_FAILURE() << "Expected one SUBSCRIBE frame in a write";
    return absl::InternalError("Expected one SUBSCRIBE frame in a write");
  }
  *subscribe_ = std::get<MoqtSubscribe>(frames[0]);
  return absl::OkStatus();
}

}  // namespace moqt::test
