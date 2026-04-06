// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_WEB_TRANSPORT_COMPLETE_BUFFER_VISITOR_H_
#define QUICHE_WEB_TRANSPORT_COMPLETE_BUFFER_VISITOR_H_

#include <optional>
#include <utility>

#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace webtransport {

// A visitor that will buffer the entirety of the incoming stream into a string,
// and will send a pre-specified string all at once.
class QUICHE_EXPORT CompleteBufferVisitor : public StreamVisitor {
 public:
  using AllDataReadCallback = quiche::SingleUseCallback<void(std::string)>;

  CompleteBufferVisitor(webtransport::Stream* stream, std::string outgoing_data)
      : stream_(stream),
        outgoing_data_(std::in_place, std::move(outgoing_data)) {}
  CompleteBufferVisitor(webtransport::Stream* stream,
                        AllDataReadCallback incoming_data_callback)
      : stream_(stream),
        incoming_data_callback_(std::in_place,
                                std::move(incoming_data_callback)) {}
  CompleteBufferVisitor(webtransport::Stream* stream, std::string outgoing_data,
                        AllDataReadCallback incoming_data_callback)
      : stream_(stream),
        outgoing_data_(std::in_place, std::move(outgoing_data)),
        incoming_data_callback_(std::in_place,
                                std::move(incoming_data_callback)) {}

  void OnCanRead() override;
  void OnCanWrite() override;

  void OnResetStreamReceived(StreamErrorCode) override {}
  void OnStopSendingReceived(StreamErrorCode) override {}
  void OnWriteSideInDataRecvdState() override {}

 protected:
  void SetOutgoingData(std::string data);

 private:
  webtransport::Stream* stream_;
  std::optional<std::string> outgoing_data_;
  std::optional<AllDataReadCallback> incoming_data_callback_;
  std::string incoming_data_buffer_;
};

}  // namespace webtransport

#endif  // QUICHE_WEB_TRANSPORT_COMPLETE_BUFFER_VISITOR_H_
