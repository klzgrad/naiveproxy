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
  quiche::QuicheBuffer operator()(const MoqtPublishDone& message) {
    return framer.SerializePublishDone(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeUpdate& message) {
    return framer.SerializeSubscribeUpdate(message);
  }
  quiche::QuicheBuffer operator()(const MoqtPublishNamespace& message) {
    return framer.SerializePublishNamespace(message);
  }
  quiche::QuicheBuffer operator()(const MoqtPublishNamespaceOk& message) {
    return framer.SerializePublishNamespaceOk(message);
  }
  quiche::QuicheBuffer operator()(const MoqtPublishNamespaceError& message) {
    return framer.SerializePublishNamespaceError(message);
  }
  quiche::QuicheBuffer operator()(const MoqtPublishNamespaceDone& message) {
    return framer.SerializePublishNamespaceDone(message);
  }
  quiche::QuicheBuffer operator()(const MoqtPublishNamespaceCancel& message) {
    return framer.SerializePublishNamespaceCancel(message);
  }
  quiche::QuicheBuffer operator()(const MoqtTrackStatus& message) {
    return framer.SerializeTrackStatus(message);
  }
  quiche::QuicheBuffer operator()(const MoqtTrackStatusOk& message) {
    return framer.SerializeTrackStatusOk(message);
  }
  quiche::QuicheBuffer operator()(const MoqtTrackStatusError& message) {
    return framer.SerializeTrackStatusError(message);
  }
  quiche::QuicheBuffer operator()(const MoqtGoAway& message) {
    return framer.SerializeGoAway(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeNamespace& message) {
    return framer.SerializeSubscribeNamespace(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeNamespaceOk& message) {
    return framer.SerializeSubscribeNamespaceOk(message);
  }
  quiche::QuicheBuffer operator()(const MoqtSubscribeNamespaceError& message) {
    return framer.SerializeSubscribeNamespaceError(message);
  }
  quiche::QuicheBuffer operator()(const MoqtUnsubscribeNamespace& message) {
    return framer.SerializeUnsubscribeNamespace(message);
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
  quiche::QuicheBuffer operator()(const MoqtPublish& message) {
    return framer.SerializePublish(message);
  }
  quiche::QuicheBuffer operator()(const MoqtPublishOk& message) {
    return framer.SerializePublishOk(message);
  }
  quiche::QuicheBuffer operator()(const MoqtPublishError& message) {
    return framer.SerializePublishError(message);
  }
  quiche::QuicheBuffer operator()(const MoqtObjectAck& message) {
    return framer.SerializeObjectAck(message);
  }

  MoqtFramer& framer;
  bool is_track_status;
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
  void OnPublishDoneMessage(const MoqtPublishDone& message) {
    frames_.push_back(message);
  }
  void OnSubscribeUpdateMessage(const MoqtSubscribeUpdate& message) {
    frames_.push_back(message);
  }
  void OnPublishNamespaceMessage(const MoqtPublishNamespace& message) {
    frames_.push_back(message);
  }
  void OnPublishNamespaceOkMessage(const MoqtPublishNamespaceOk& message) {
    frames_.push_back(message);
  }
  void OnPublishNamespaceErrorMessage(
      const MoqtPublishNamespaceError& message) {
    frames_.push_back(message);
  }
  void OnPublishNamespaceDoneMessage(const MoqtPublishNamespaceDone& message) {
    frames_.push_back(message);
  }
  void OnPublishNamespaceCancelMessage(
      const MoqtPublishNamespaceCancel& message) {
    frames_.push_back(message);
  }
  void OnTrackStatusMessage(const MoqtTrackStatus& message) {
    frames_.push_back(message);
  }
  void OnTrackStatusOkMessage(const MoqtTrackStatusOk& message) {
    frames_.push_back(message);
  }
  void OnTrackStatusErrorMessage(const MoqtTrackStatusError& message) {
    frames_.push_back(message);
  }
  void OnGoAwayMessage(const MoqtGoAway& message) {
    frames_.push_back(message);
  }
  void OnSubscribeNamespaceMessage(const MoqtSubscribeNamespace& message) {
    frames_.push_back(message);
  }
  void OnSubscribeNamespaceOkMessage(const MoqtSubscribeNamespaceOk& message) {
    frames_.push_back(message);
  }
  void OnSubscribeNamespaceErrorMessage(
      const MoqtSubscribeNamespaceError& message) {
    frames_.push_back(message);
  }
  void OnUnsubscribeNamespaceMessage(const MoqtUnsubscribeNamespace& message) {
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
  void OnPublishMessage(const MoqtPublish& message) {
    frames_.push_back(message);
  }
  void OnPublishOkMessage(const MoqtPublishOk& message) {
    frames_.push_back(message);
  }
  void OnPublishErrorMessage(const MoqtPublishError& message) {
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
