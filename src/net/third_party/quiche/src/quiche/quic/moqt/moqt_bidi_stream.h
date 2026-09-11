// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_BIDI_STREAM_H
#define QUICHE_QUIC_MOQT_MOQT_BIDI_STREAM_H

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_control_message_queue.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_request_update_queue.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace test {
class MoqtBidiStreamTestWrapper;
}

using SessionErrorCallback =
    quiche::SingleUseCallback<void(MoqtError, absl::string_view)>;

// MoqtBidiStreamBase is the base class for bidirectional streams in MoQT.  It
// contains basic methods for handling and dispatching messages.  An instance of
// MoqtBidiStreamBase can be created before the underlying stream is available,
// as it might not yet exist due to flow control limits.
class MoqtBidiStreamBase : public webtransport::StreamVisitor {
 public:
  MoqtBidiStreamBase(MoqtFramer* absl_nonnull framer,
                     const MoqtControlMessageParser& message_parser,
                     SessionErrorCallback session_error_callback)
      : framer_(framer),
        message_parser_(message_parser),
        session_error_callback_(std::move(session_error_callback)) {}
  ~MoqtBidiStreamBase() = default;

  // Binds a WebTransport stream associated with `parser` to this object.
  void BindStream(MoqtStreamTypeParser parser) {
    QUICHE_DCHECK(!stream_parser_.has_value());
    stream_parser_.emplace(std::move(parser));
    outgoing_message_queue_.SetStream(stream_parser_->stream());
    OnStreamBound();
  }
  // Binds a WebTransport stream `stream` to this object.
  void BindStream(webtransport::Stream* absl_nonnull stream) {
    QUICHE_DCHECK(!stream_parser_.has_value());
    stream_parser_.emplace(stream);
    outgoing_message_queue_.SetStream(stream);
    OnStreamBound();
  }

  // webtransport::StreamVisitor implementation.
  void OnResetStreamReceived(webtransport::StreamErrorCode error) override {
    Reset(error);
  }
  void OnStopSendingReceived(webtransport::StreamErrorCode error) override {
    Reset(error);
  }
  void OnWriteSideInDataRecvdState() override {}
  void OnCanRead() override;
  void OnCanWrite() override;

  bool QueueIsFull() const { return outgoing_message_queue_.QueueIsFull(); }

  absl::Status SendOrBufferMessage(quiche::QuicheBuffer message,
                                   bool fin = false) {
    absl::Status status =
        outgoing_message_queue_.SendOrBufferMessage(std::move(message), fin);
    if (fin) {
      Detach();
    }
    return status;
  }
  void SendOrBufferMessageOrFatal(quiche::QuicheBuffer message,
                                  bool fin = false) {
    CheckStatus(SendOrBufferMessage(std::move(message), fin));
  }

  absl::Status SendRequestOk(uint64_t request_id,
                             const MessageParameters& parameters,
                             bool fin = false);
  absl::Status SendRequestError(
      uint64_t request_id, RequestErrorCode error_code,
      std::optional<quic::QuicTimeDelta> retry_interval,
      absl::string_view reason_phrase, bool fin = false);
  absl::Status SendRequestError(uint64_t request_id, MoqtRequestErrorInfo info,
                                bool fin = false);
  // Can be overridden for message-specific constraints.
  virtual absl::Status SendRequestUpdate(uint64_t request_id,
                                         uint64_t existing_request_id,
                                         const MessageParameters& parameters,
                                         MoqtResponseCallback callback);
  void Fin() {
    CheckStatus(outgoing_message_queue_.Fin());
    Detach();
  }
  void Reset(webtransport::StreamErrorCode error) {
    if (stream() != nullptr) {
      stream()->ResetWithUserCode(error);
    }
    Detach();
  }

  // If `status` is not OK, terminates the connection with a fatal error.
  void CheckStatus(absl::Status status) {
    if (!status.ok()) {
      OnFatalError(status);
    }
  }

  MoqtFramer* framer() const { return framer_; }

  // Removes any state in MoqtSession related to the stream. Overrides of this
  // method must be robust to multiple invocations. Only called on sending a FIN
  // or RESET. If otherwise destroyed, it's due to a larger cleanup where the
  // state no longer matters.
  virtual void Detach() = 0;

 protected:
  // Called when a WebTransport stream has been associated with the object.
  // Should be used to set the priority for the stream.
  virtual void OnStreamBound() = 0;

  // Called when a control message has been received.  The subclass should use
  // DispatchControlMessage to process it.
  virtual absl::Status OnRawControlMessage(
      const MoqtRawControlMessage& message) = 0;

  MoqtRequestUpdateQueue& request_update_queue() {
    return request_update_queue_;
  }

  // Terminates the MoQT session due to a fatal error encountered.
  void OnFatalError(absl::Status status);

  MoqtControlStreamParser* stream_parser() {
    return stream_parser_.has_value() ? &*stream_parser_ : nullptr;
  }
  const MoqtControlMessageParser& message_parser() const {
    return message_parser_;
  }
  webtransport::Stream* stream() const {
    return stream_parser_.has_value() ? stream_parser_->stream() : nullptr;
  }

 private:
  friend class test::MoqtBidiStreamTestWrapper;

  MoqtFramer* absl_nonnull framer_;
  std::optional<MoqtControlStreamParser> stream_parser_;
  MoqtControlMessageParser message_parser_;
  MoqtControlMessageQueue outgoing_message_queue_;
  MoqtRequestUpdateQueue request_update_queue_;
  SessionErrorCallback session_error_callback_;
};

// DispatchControlMessage is wrapped into a class so that the caller class can
// provide it access to private OnControlMessage methods.
class ControlMessageDispatcher {
 public:
  // Parses the supplied control message. If the message is well-formed, and the
  // class defines an `OnControlMessage` method that accepts it, it is passed to
  // that method. Otherwise, an appropriate error message is returned;
  // `stream_type` is used to format that message.
  template <typename StreamClass>
  static absl::Status DispatchControlMessage(
      StreamClass& stream, const MoqtControlMessageParser& parser,
      const MoqtRawControlMessage& message, absl::string_view stream_type) {
    static_assert(!std::is_same_v<StreamClass, MoqtBidiStreamBase>);
    return parser.ParseMessage(message, [&](const auto& parsed_message) {
      if constexpr (CanDispatch<StreamClass, decltype(parsed_message)>::value) {
        return stream.OnControlMessage(parsed_message);
      } else {
        return absl::InvalidArgumentError(
            absl::StrCat("Received an unexpected message of type ",
                         MoqtMessageTypeToString(message.type), " on a ",
                         stream_type, " stream"));
      }
    });
  }

 private:
  // CanDispatch<S, M> indicates whether `S` has a method with signature
  //     absl::Status OnControlMessage(const M&);
  template <typename Subclass, typename Message, typename = void>
  struct CanDispatch : std::false_type {};
  template <typename Subclass, typename Message>
  struct CanDispatch<Subclass, Message,
                     std::enable_if_t<std::is_same_v<
                         decltype(std::declval<Subclass>().OnControlMessage(
                             std::declval<Message>())),
                         absl::Status>>> : std::true_type {};
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_BIDI_STREAM_H
