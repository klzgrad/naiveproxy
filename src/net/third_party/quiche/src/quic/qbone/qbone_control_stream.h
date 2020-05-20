// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_CONTROL_STREAM_H_
#define QUICHE_QUIC_QBONE_QBONE_CONTROL_STREAM_H_

#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_control.pb.h"

namespace quic {

class QboneSessionBase;

class QUIC_EXPORT_PRIVATE QboneControlStreamBase : public QuicStream {
 public:
  explicit QboneControlStreamBase(QuicSession* session);

  void OnDataAvailable() override;

  void OnStreamReset(const QuicRstStreamFrame& frame) override;

 protected:
  virtual void OnMessage(const std::string& data) = 0;
  bool SendMessage(const proto2::Message& proto);

 private:
  uint16_t pending_message_size_;
  std::string buffer_;
};

template <class T>
class QUIC_EXPORT_PRIVATE QboneControlHandler {
 public:
  virtual ~QboneControlHandler() { }

  virtual void OnControlRequest(const T& request) = 0;
  virtual void OnControlError() = 0;
};

template <class Incoming, class Outgoing>
class QUIC_EXPORT_PRIVATE QboneControlStream : public QboneControlStreamBase {
 public:
  using Handler = QboneControlHandler<Incoming>;

  QboneControlStream(QuicSession* session, Handler* handler)
      : QboneControlStreamBase(session), handler_(handler) {}

  bool SendRequest(const Outgoing& request) { return SendMessage(request); }

 protected:
  void OnMessage(const std::string& data) override {
    Incoming request;
    if (!request.ParseFromString(data)) {
      QUIC_LOG(ERROR) << "Failed to parse incoming request";
      if (handler_ != nullptr) {
        handler_->OnControlError();
      }
      return;
    }
    if (handler_ != nullptr) {
      handler_->OnControlRequest(request);
    }
  }

 private:
  Handler* handler_;
};

using QboneServerControlStream =
    QboneControlStream<QboneServerRequest, QboneClientRequest>;
using QboneClientControlStream =
    QboneControlStream<QboneClientRequest, QboneServerRequest>;

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_CONTROL_STREAM_H_
