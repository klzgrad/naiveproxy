// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_FRAMER_UTILS_H_
#define QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_FRAMER_UTILS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_stream.h"

namespace moqt::test {

// TODO: remove MoqtObject from TestMessageBase::MessageStructuredData and merge
// those two types.
using MoqtGenericFrame = std::variant<
    MoqtClientSetup, MoqtServerSetup, MoqtSubscribe, MoqtSubscribeOk,
    MoqtSubscribeError, MoqtUnsubscribe, MoqtPublishDone, MoqtSubscribeUpdate,
    MoqtPublishNamespace, MoqtPublishNamespaceOk, MoqtPublishNamespaceError,
    MoqtPublishNamespaceDone, MoqtPublishNamespaceCancel, MoqtTrackStatus,
    MoqtTrackStatusOk, MoqtTrackStatusError, MoqtGoAway, MoqtSubscribeNamespace,
    MoqtSubscribeNamespaceOk, MoqtSubscribeNamespaceError,
    MoqtUnsubscribeNamespace, MoqtMaxRequestId, MoqtFetch, MoqtFetchCancel,
    MoqtFetchOk, MoqtFetchError, MoqtRequestsBlocked, MoqtPublish,
    MoqtPublishOk, MoqtPublishError, MoqtObjectAck>;

std::string SerializeGenericMessage(const MoqtGenericFrame& frame,
                                    bool use_webtrans = false);

// Parses a concatenation of one or more MoQT control messages.
std::vector<MoqtGenericFrame> ParseGenericMessage(absl::string_view body);

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
  if (!reader.ReadVarInt62(&type_raw)) {
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

// gmock action for extracting an SUBSCRIBE message written onto a stream.
class StoreSubscribe {
 public:
  explicit StoreSubscribe(std::optional<MoqtSubscribe>* subscribe)
      : subscribe_(subscribe) {}

  // quiche::WriteStream::Writev() implementation.
  absl::Status operator()(absl::Span<const absl::string_view> data,
                          const quiche::StreamWriteOptions& options) const;

 private:
  std::optional<MoqtSubscribe>* subscribe_;
};

}  // namespace moqt::test

#endif  // QUICHE_QUIC_MOQT_TEST_TOOLS_MOQT_FRAMER_UTILS_H_
