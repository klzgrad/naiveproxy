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
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace moqt::test {

class MoqtParserTestVisitor : public MoqtDataParserVisitor {
 public:
  explicit MoqtParserTestVisitor(bool enable_logging = true)
      : enable_logging_(enable_logging) {}

  void OnObjectMessage(const MoqtObject& message, absl::string_view payload,
                       bool end_of_message) override {
    MoqtObject object = message;
    object_payloads_.push_back(std::string(payload));
    end_of_message_ = end_of_message;
    if (end_of_message) {
      ++messages_received_;
    }
    last_message_.emplace(object);
  }
  void OnFin() override { fin_received_ = true; }
  void OnParsingError(MoqtError code, absl::string_view reason) override {
    QUICHE_LOG_IF(INFO, enable_logging_) << "Parsing error: " << reason;
    parsing_error_ = reason;
    parsing_error_code_ = code;
  }

  std::string object_payload() const {
    return absl::StrJoin(object_payloads_, "");
  }
  std::vector<std::string>& object_payloads() { return object_payloads_; }
  uint64_t messages_received() const { return messages_received_; }
  bool end_of_message() const { return end_of_message_; }
  bool fin_received() const { return fin_received_; }
  std::optional<MoqtObject> last_message() const { return last_message_; }
  std::optional<std::string> parsing_error() const { return parsing_error_; }

 private:
  bool enable_logging_ = true;
  std::vector<std::string> object_payloads_;
  bool end_of_message_ = false;
  bool fin_received_ = false;
  std::optional<std::string> parsing_error_;
  MoqtError parsing_error_code_;
  uint64_t messages_received_ = 0;
  std::optional<MoqtObject> last_message_;
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_PARSER_TEST_VISITOR_H_
