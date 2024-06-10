// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/devious_baton.h"

#include <cstdint>
#include <functional>
#include <memory>

#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/wire_serialization.h"
#include "quiche/web_transport/complete_buffer_visitor.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

namespace {

constexpr QuicByteCount kMaxPaddingSize = 64;
constexpr char kPaddingData[kMaxPaddingSize] = {0};

absl::StatusOr<DeviousBatonValue> Parse(absl::string_view message) {
  quiche::QuicheDataReader reader(message);
  uint64_t padding_size;
  if (!reader.ReadVarInt62(&padding_size)) {
    return absl::InvalidArgumentError("Failed to read the padding size");
  }
  if (!reader.Seek(padding_size)) {
    return absl::InvalidArgumentError("Failed to skip padding");
  }
  DeviousBatonValue value;
  if (!reader.ReadUInt8(&value)) {
    return absl::InvalidArgumentError("Failed to read the baton");
  }
  if (!reader.IsDoneReading()) {
    return absl::InvalidArgumentError("Trailing data after the baton");
  }
  return value;
}

std::string Serialize(DeviousBatonValue value) {
  // Randomize padding size for extra deviousness.
  QuicByteCount padding_size =
      QuicRandom::GetInstance()->InsecureRandUint64() % kMaxPaddingSize;
  absl::string_view padding(kPaddingData, padding_size);

  absl::StatusOr<std::string> result = quiche::SerializeIntoString(
      quiche::WireStringWithLengthPrefix<quiche::WireVarInt62>(padding),
      quiche::WireUint8(value));
  QUICHE_DCHECK(result.ok());
  return *std::move(result);
}

class IncomingBidiBatonVisitor : public webtransport::CompleteBufferVisitor {
 public:
  IncomingBidiBatonVisitor(webtransport::Session& session,
                           webtransport::Stream& stream)
      : CompleteBufferVisitor(
            &stream, absl::bind_front(
                         &IncomingBidiBatonVisitor::OnAllDataReceived, this)),
        session_(&session) {}

 private:
  void OnAllDataReceived(std::string data) {
    absl::StatusOr<DeviousBatonValue> value = Parse(data);
    if (!value.ok()) {
      session_->CloseSession(kDeviousBatonErrorBruh,
                             absl::StrCat("Failed to parse incoming baton: ",
                                          value.status().message()));
      return;
    }
    DeviousBatonValue next_value = 1 + *value;
    if (next_value != 0) {
      SetOutgoingData(Serialize(*value + 1));
    }
  }

  webtransport::Session* session_;
};

}  // namespace

void DeviousBatonSessionVisitor::OnSessionReady() {
  if (!is_server_) {
    return;
  }
  for (int i = 0; i < count_; ++i) {
    webtransport::Stream* stream = session_->OpenOutgoingUnidirectionalStream();
    if (stream == nullptr) {
      session_->CloseSession(
          kDeviousBatonErrorDaYamn,
          "Insufficient flow control when opening initial baton streams");
      return;
    }
    stream->SetVisitor(std::make_unique<webtransport::CompleteBufferVisitor>(
        stream, Serialize(initial_value_)));
    stream->visitor()->OnCanWrite();
  }
}

void DeviousBatonSessionVisitor::OnSessionClosed(
    webtransport::SessionErrorCode error_code,
    const std::string& error_message) {
  QUICHE_LOG(INFO) << "Devious Baton session closed with error " << error_code
                   << " (message: " << error_message << ")";
}

void DeviousBatonSessionVisitor::OnIncomingBidirectionalStreamAvailable() {
  while (true) {
    webtransport::Stream* stream =
        session_->AcceptIncomingBidirectionalStream();
    if (stream == nullptr) {
      return;
    }
    stream->SetVisitor(
        std::make_unique<IncomingBidiBatonVisitor>(*session_, *stream));
    stream->visitor()->OnCanRead();
  }
}

void DeviousBatonSessionVisitor::OnIncomingUnidirectionalStreamAvailable() {
  while (true) {
    webtransport::Stream* stream =
        session_->AcceptIncomingUnidirectionalStream();
    if (stream == nullptr) {
      return;
    }
    stream->SetVisitor(std::make_unique<webtransport::CompleteBufferVisitor>(
        stream, CreateResponseCallback(
                    &DeviousBatonSessionVisitor::SendBidirectionalBaton)));
    stream->visitor()->OnCanRead();
  }
}

void DeviousBatonSessionVisitor::OnDatagramReceived(
    absl::string_view datagram) {
  // TODO(vasilvv): implement datagram behavior.
}

void DeviousBatonSessionVisitor::OnCanCreateNewOutgoingBidirectionalStream() {
  while (!outgoing_bidi_batons_.empty()) {
    webtransport::Stream* stream = session_->OpenOutgoingBidirectionalStream();
    if (stream == nullptr) {
      return;
    }
    stream->SetVisitor(std::make_unique<webtransport::CompleteBufferVisitor>(
        stream, Serialize(outgoing_bidi_batons_.front()),
        CreateResponseCallback(
            &DeviousBatonSessionVisitor::SendUnidirectionalBaton)));
    outgoing_bidi_batons_.pop_front();
    stream->visitor()->OnCanWrite();
  }
}

void DeviousBatonSessionVisitor::OnCanCreateNewOutgoingUnidirectionalStream() {
  while (!outgoing_unidi_batons_.empty()) {
    webtransport::Stream* stream = session_->OpenOutgoingUnidirectionalStream();
    if (stream == nullptr) {
      return;
    }
    stream->SetVisitor(std::make_unique<webtransport::CompleteBufferVisitor>(
        stream, Serialize(outgoing_unidi_batons_.front())));
    outgoing_unidi_batons_.pop_front();
    stream->visitor()->OnCanWrite();
  }
}

quiche::SingleUseCallback<void(std::string)>
DeviousBatonSessionVisitor::CreateResponseCallback(SendFunction send_function) {
  return [this, send_function](std::string data) {
    absl::StatusOr<DeviousBatonValue> value = Parse(data);
    if (!value.ok()) {
      session_->CloseSession(kDeviousBatonErrorBruh,
                             absl::StrCat("Failed to parse incoming baton: ",
                                          value.status().message()));
      return;
    }
    DeviousBatonValue new_value = 1 + *value;
    if (new_value != 0) {
      std::invoke(send_function, this, *value);
    }
  };
}

}  // namespace quic
