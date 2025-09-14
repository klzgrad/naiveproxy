// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_PARSER_TEST_VISITOR_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_PARSER_TEST_VISITOR_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/test_tools/moqt_test_message.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace moqt::test {

class MoqtParserTestVisitor : public MoqtControlParserVisitor,
                              public MoqtDataParserVisitor {
 public:
  explicit MoqtParserTestVisitor(bool enable_logging = true)
      : enable_logging_(enable_logging) {}
  ~MoqtParserTestVisitor() = default;

  void OnObjectMessage(const MoqtObject& message, absl::string_view payload,
                       bool end_of_message) override {
    MoqtObject object = message;
    object_payloads_.push_back(std::string(payload));
    end_of_message_ = end_of_message;
    if (end_of_message) {
      ++messages_received_;
    }
    last_message_ = TestMessageBase::MessageStructuredData(object);
  }
  template <typename Message>
  void OnControlMessage(const Message& message) {
    end_of_message_ = true;
    ++messages_received_;
    last_message_ = TestMessageBase::MessageStructuredData(message);
  }
  void OnClientSetupMessage(const MoqtClientSetup& message) override {
    OnControlMessage(message);
  }
  void OnServerSetupMessage(const MoqtServerSetup& message) override {
    OnControlMessage(message);
  }
  void OnSubscribeMessage(const MoqtSubscribe& message) override {
    OnControlMessage(message);
  }
  void OnSubscribeOkMessage(const MoqtSubscribeOk& message) override {
    OnControlMessage(message);
  }
  void OnSubscribeErrorMessage(const MoqtSubscribeError& message) override {
    OnControlMessage(message);
  }
  void OnSubscribeUpdateMessage(const MoqtSubscribeUpdate& message) override {
    OnControlMessage(message);
  }
  void OnUnsubscribeMessage(const MoqtUnsubscribe& message) override {
    OnControlMessage(message);
  }
  void OnSubscribeDoneMessage(const MoqtSubscribeDone& message) override {
    OnControlMessage(message);
  }
  void OnAnnounceMessage(const MoqtAnnounce& message) override {
    OnControlMessage(message);
  }
  void OnAnnounceOkMessage(const MoqtAnnounceOk& message) override {
    OnControlMessage(message);
  }
  void OnAnnounceErrorMessage(const MoqtAnnounceError& message) override {
    OnControlMessage(message);
  }
  void OnAnnounceCancelMessage(const MoqtAnnounceCancel& message) override {
    OnControlMessage(message);
  }
  void OnTrackStatusRequestMessage(
      const MoqtTrackStatusRequest& message) override {
    OnControlMessage(message);
  }
  void OnUnannounceMessage(const MoqtUnannounce& message) override {
    OnControlMessage(message);
  }
  void OnTrackStatusMessage(const MoqtTrackStatus& message) override {
    OnControlMessage(message);
  }
  void OnGoAwayMessage(const MoqtGoAway& message) override {
    OnControlMessage(message);
  }
  void OnSubscribeAnnouncesMessage(
      const MoqtSubscribeAnnounces& message) override {
    OnControlMessage(message);
  }
  void OnSubscribeAnnouncesOkMessage(
      const MoqtSubscribeAnnouncesOk& message) override {
    OnControlMessage(message);
  }
  void OnSubscribeAnnouncesErrorMessage(
      const MoqtSubscribeAnnouncesError& message) override {
    OnControlMessage(message);
  }
  void OnUnsubscribeAnnouncesMessage(
      const MoqtUnsubscribeAnnounces& message) override {
    OnControlMessage(message);
  }
  void OnMaxRequestIdMessage(const MoqtMaxRequestId& message) override {
    OnControlMessage(message);
  }
  void OnFetchMessage(const MoqtFetch& message) override {
    OnControlMessage(message);
  }
  void OnFetchCancelMessage(const MoqtFetchCancel& message) override {
    OnControlMessage(message);
  }
  void OnFetchOkMessage(const MoqtFetchOk& message) override {
    OnControlMessage(message);
  }
  void OnFetchErrorMessage(const MoqtFetchError& message) override {
    OnControlMessage(message);
  }
  void OnRequestsBlockedMessage(const MoqtRequestsBlocked& message) override {
    OnControlMessage(message);
  }
  void OnPublishMessage(const MoqtPublish& message) override {
    OnControlMessage(message);
  }
  void OnPublishOkMessage(const MoqtPublishOk& message) override {
    OnControlMessage(message);
  }
  void OnPublishErrorMessage(const MoqtPublishError& message) override {
    OnControlMessage(message);
  }
  void OnObjectAckMessage(const MoqtObjectAck& message) override {
    OnControlMessage(message);
  }
  void OnParsingError(MoqtError code, absl::string_view reason) override {
    QUICHE_LOG_IF(INFO, enable_logging_) << "Parsing error: " << reason;
    parsing_error_ = reason;
    parsing_error_code_ = code;
  }

  std::string object_payload() { return absl::StrJoin(object_payloads_, ""); }

  bool enable_logging_ = true;
  std::vector<std::string> object_payloads_;
  bool end_of_message_ = false;
  std::optional<std::string> parsing_error_;
  MoqtError parsing_error_code_;
  uint64_t messages_received_ = 0;
  std::optional<TestMessageBase::MessageStructuredData> last_message_;
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_PARSER_TEST_VISITOR_H_
