// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_BIDI_STREAM_H
#define QUICHE_QUIC_MOQT_MOQT_BIDI_STREAM_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/web_transport/stream_helpers.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

enum class MoqtBidiStreamType : uint8_t {
  kUnknown,
  kControl,
  kSubscribeNamespace,  // TODO(martinduke): Support this case.
};

using SessionErrorCallback =
    quiche::SingleUseCallback<void(MoqtError, absl::string_view)>;
// The provider of this callback owns nothing in MoqtBidiStreamBase. This merely
// deletes the record.
using BidiStreamDeletedCallback = quiche::SingleUseCallback<void()>;

// A generic parser visitor that assumes all messages are invalid. Serves a base
// class for visitors that accept a subset of messages and maintains state based
// on those messages.
class MoqtBidiStreamBase : public MoqtControlParserVisitor,
                           public webtransport::StreamVisitor {
 public:
  MoqtBidiStreamBase(MoqtFramer* absl_nonnull framer,
                     BidiStreamDeletedCallback stream_deleted_callback,
                     SessionErrorCallback session_error_callback)
      : framer_(framer),
        stream_deleted_callback_(std::move(stream_deleted_callback)),
        session_error_callback_(std::move(session_error_callback)) {}
  ~MoqtBidiStreamBase() override { std::move(stream_deleted_callback_)(); }
  virtual void set_stream(webtransport::Stream* absl_nonnull stream) {
    set_stream(stream, std::nullopt);
  }

  // MoqtControlParserVisitor implementation. All control messages are protocol
  // violations by default.
  virtual void OnClientSetupMessage(const MoqtClientSetup& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnServerSetupMessage(const MoqtServerSetup& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnRequestOkMessage(const MoqtRequestOk& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnRequestErrorMessage(const MoqtRequestError& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnSubscribeMessage(const MoqtSubscribe& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnSubscribeOkMessage(const MoqtSubscribeOk& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnUnsubscribeMessage(const MoqtUnsubscribe& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnPublishDoneMessage(const MoqtPublishDone& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnRequestUpdateMessage(
      const MoqtRequestUpdate& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnPublishNamespaceMessage(
      const MoqtPublishNamespace& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnPublishNamespaceDoneMessage(
      const MoqtPublishNamespaceDone& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnNamespaceMessage(const MoqtNamespace& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnNamespaceDoneMessage(
      const MoqtNamespaceDone& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnPublishNamespaceCancelMessage(
      const MoqtPublishNamespaceCancel& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnTrackStatusMessage(const MoqtTrackStatus& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnGoAwayMessage(const MoqtGoAway& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnSubscribeNamespaceMessage(
      const MoqtSubscribeNamespace& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnMaxRequestIdMessage(const MoqtMaxRequestId& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnFetchMessage(const MoqtFetch& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnFetchCancelMessage(const MoqtFetchCancel& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnFetchOkMessage(const MoqtFetchOk& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnRequestsBlockedMessage(
      const MoqtRequestsBlocked& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnPublishMessage(const MoqtPublish& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnPublishOkMessage(const MoqtPublishOk& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnObjectAckMessage(const MoqtObjectAck& message) override {
    OnParsingError(wrong_message_error_, wrong_message_reason_);
  }
  virtual void OnParsingError(MoqtError code,
                              absl::string_view reason) override {
    std::move(session_error_callback_)(code, reason);
  }

  // webtransport::StreamVisitor implementation.
  void OnResetStreamReceived(webtransport::StreamErrorCode error) override {}
  void OnStopSendingReceived(webtransport::StreamErrorCode error) override {}
  void OnWriteSideInDataRecvdState() override {}
  void OnCanRead() override {
    if (parser_ == nullptr) {
      QUICHE_BUG(quiche_bug_moqt_parser_is_null) << "Parser is null";
      return;
    }
    parser_->ReadAndDispatchMessages();
  }
  void OnCanWrite() override {
    if (pending_messages_.empty() && fin_queued_) {
      if (!stream_->SendFin()) {
        std::move(session_error_callback_)(MoqtError::kInternalError,
                                           "Failed to send FIN");
      }
      return;
    }
    while (!pending_messages_.empty() && stream_->CanWrite()) {
      SendMessage(std::move(pending_messages_.front()),
                  fin_queued_ && pending_messages_.size() == 1);
      pending_messages_.pop();
    }
  }

  bool QueueIsFull() const {
    return pending_messages_.size() == kMaxPendingMessages;
  }

  void SendOrBufferMessage(quiche::QuicheBuffer message, bool fin = false) {
    if (fin_queued_) {
      return;
    }
    if (stream_ == nullptr || !stream_->CanWrite()) {
      AddToQueue(std::move(message));
      return;
    }
    SendMessage(std::move(message), fin);
  }
  void SendRequestOk(uint64_t request_id, const MessageParameters& parameters,
                     bool fin = false) {
    SendOrBufferMessage(
        framer_->SerializeRequestOk(MoqtRequestOk{request_id, parameters}),
        fin);
  }
  void SendRequestError(uint64_t request_id, RequestErrorCode error_code,
                        std::optional<quic::QuicTimeDelta> retry_interval,
                        absl::string_view reason_phrase, bool fin = false) {
    MoqtRequestError request_error;
    request_error.request_id = request_id;
    request_error.error_code = error_code;
    request_error.retry_interval = retry_interval;
    request_error.reason_phrase = reason_phrase;
    SendOrBufferMessage(framer_->SerializeRequestError(request_error), fin);
  }
  void SendRequestError(uint64_t request_id, MoqtRequestErrorInfo info,
                        bool fin = false) {
    SendRequestError(request_id, info.error_code, info.retry_interval,
                     info.reason_phrase, fin);
  }
  void Fin() {
    fin_queued_ = true;
    if (pending_messages_.empty()) {
      if (stream_ != nullptr && !webtransport::SendFinOnStream(*stream_).ok()) {
        std::move(session_error_callback_)(MoqtError::kInternalError,
                                           "Failed to send FIN");
      }
      return;
    }
  }
  void Reset(webtransport::StreamErrorCode error) {
    if (stream_ != nullptr) {
      stream_->ResetWithUserCode(error);
    }
  }

 protected:
  // The caller is responsible for calling stream->SetVisitor(). Derived
  // classes will wrap this with a call to stream->SetPriority().
  void set_stream(webtransport::Stream* absl_nonnull stream,
                  std::optional<MoqtMessageType> first_message_type) {
    stream_ = stream;
    parser_ = std::make_unique<MoqtControlParser>(framer_->using_webtrans(),
                                                  stream_, *this);
    if (first_message_type.has_value()) {
      parser_->set_message_type(static_cast<uint64_t>(*first_message_type));
    }
  }
  const size_t kMaxPendingMessages = 100;
  void AddToQueue(quiche::QuicheBuffer message) {
    if (pending_messages_.size() == kMaxPendingMessages) {
      std::move(session_error_callback_)(
          MoqtError::kInternalError,
          "Not enough flow credit on the control stream");
      return;
    }
    pending_messages_.push(std::move(message));
  }
  MoqtFramer* absl_nonnull framer_;
  MoqtControlParser* parser() { return parser_.get(); }
  void OnBidiStreamDeleted() {
    if (stream_deleted_callback_ != nullptr) {
      std::move(stream_deleted_callback_)();
    }
  }
  webtransport::Stream* stream() { return stream_; }

 private:
  void SendMessage(quiche::QuicheBuffer message, bool fin) {
    webtransport::StreamWriteOptions options;
    options.set_send_fin(fin);
    // TODO: while we buffer unconditionally, we should still at some point tear
    // down the connection if we've buffered too many control messages;
    // otherwise, there is potential for memory exhaustion attacks.
    options.set_buffer_unconditionally(true);
    std::array write_vector = {quiche::QuicheMemSlice(std::move(message))};
    absl::Status success =
        stream_->Writev(absl::MakeSpan(write_vector), options);
    if (!success.ok()) {
      std::move(session_error_callback_)(MoqtError::kInternalError,
                                         "Failed to write a control message");
    }
  }

  webtransport::Stream* stream_;
  std::unique_ptr<MoqtControlParser> parser_;
  std::queue<quiche::QuicheBuffer> pending_messages_;
  bool fin_queued_ = false;
  BidiStreamDeletedCallback stream_deleted_callback_;
  SessionErrorCallback session_error_callback_;
  const MoqtError wrong_message_error_ = MoqtError::kProtocolViolation;
  const absl::string_view wrong_message_reason_ =
      "Message not allowed for this stream type";
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_BIDI_STREAM_H
