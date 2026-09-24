// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_FRAMER_UTILS_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_FRAMER_UTILS_H_

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_mem_slice.h"

namespace moqt::test {

using AnyMoqtControlMessage =
    std::variant<MoqtSetup, MoqtRequestOk, MoqtRequestError, MoqtSubscribe,
                 MoqtSubscribeOk, MoqtPublishDone, MoqtRequestUpdate,
                 MoqtPublishNamespace, MoqtTrackStatus, MoqtGoAway,
                 MoqtSubscribeNamespace, MoqtSubscribeTracks, MoqtMaxRequestId,
                 MoqtFetch, MoqtFetchCancel, MoqtFetchOk, MoqtRequestsBlocked,
                 MoqtPublish, MoqtNamespace, MoqtNamespaceDone, MoqtObjectAck>;

std::string SerializeGenericMessage(const AnyMoqtControlMessage& frame,
                                    bool use_webtrans = false);

MATCHER_P(SerializedControlMessage, message,
          "Matches against a specific expected MoQT message") {
  std::vector<absl::string_view> data_written;
  data_written.reserve(arg.size());
  for (const quiche::QuicheMemSlice& slice : arg) {
    data_written.push_back(slice.AsStringView());
  }
  std::string merged_message = absl::StrJoin(data_written, "");
  return merged_message == SerializeGenericMessage(message);
}

MATCHER_P(ControlMessageOfType, expected_type,
          "Matches against an MoQT message of a specific type") {
  std::vector<absl::string_view> data_written;
  data_written.reserve(arg.size());
  for (const quiche::QuicheMemSlice& slice : arg) {
    data_written.push_back(slice.AsStringView());
  }
  std::string merged_message = absl::StrJoin(data_written, "");
  quiche::QuicheDataReader reader(merged_message);
  uint64_t type_raw;
  if (!reader.ReadMoqVarInt(&type_raw)) {
    *result_listener << "Failed to extract type from the message";
    return false;
  }
  MoqtMessageType type = static_cast<MoqtMessageType>(type_raw);
  if (type != expected_type) {
    *result_listener << "Expected message of type "
                     << MoqtMessageTypeToString(expected_type) << ", got "
                     << MoqtMessageTypeToString(type);
    return false;
  }
  return true;
}

// Parses `message` into a MoqtRawControlMessage.  Assumes `message` is exactly
// one message; reports a test failure if not.
MoqtRawControlMessage UnframeRawControlMessage(absl::string_view message);

inline MoqtRawControlMessage GenericMessageToRawControlMessage(
    const AnyMoqtControlMessage& message) {
  return UnframeRawControlMessage(SerializeGenericMessage(message));
}

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_FRAMER_UTILS_H_
