// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_WEB_TRANSPORT_TEST_VISITORS_H_
#define QUICHE_QUIC_TOOLS_WEB_TRANSPORT_TEST_VISITORS_H_

#include <string>

#include "quic/core/web_transport_interface.h"
#include "quic/platform/api/quic_logging.h"

namespace quic {

// Discards any incoming data.
class WebTransportDiscardVisitor : public WebTransportStreamVisitor {
 public:
  WebTransportDiscardVisitor(WebTransportStream* stream) : stream_(stream) {}

  void OnCanRead() override {
    std::string buffer;
    WebTransportStream::ReadResult result = stream_->Read(&buffer);
    QUIC_DVLOG(2) << "Read " << result.bytes_read
                  << " bytes from WebTransport stream "
                  << stream_->GetStreamId() << ", fin: " << result.fin;
  }

  void OnCanWrite() override {}

 private:
  WebTransportStream* stream_;
};

// Echoes any incoming data back on the same stream.
class WebTransportBidirectionalEchoVisitor : public WebTransportStreamVisitor {
 public:
  WebTransportBidirectionalEchoVisitor(WebTransportStream* stream)
      : stream_(stream) {}

  void OnCanRead() override {
    WebTransportStream::ReadResult result = stream_->Read(&buffer_);
    QUIC_DVLOG(1) << "Attempted reading on WebTransport bidirectional stream "
                  << stream_->GetStreamId()
                  << ", bytes read: " << result.bytes_read;
    if (result.fin) {
      send_fin_ = true;
    }
    OnCanWrite();
  }

  void OnCanWrite() override {
    if (!buffer_.empty()) {
      bool success = stream_->Write(buffer_);
      QUIC_DVLOG(1) << "Attempted writing on WebTransport bidirectional stream "
                    << stream_->GetStreamId()
                    << ", success: " << (success ? "yes" : "no");
      if (!success) {
        return;
      }

      buffer_ = "";
    }

    if (send_fin_) {
      bool success = stream_->SendFin();
      QUICHE_DCHECK(success);
    }
  }

 private:
  WebTransportStream* stream_;
  std::string buffer_;
  bool send_fin_ = false;
};

// Buffers all of the data and calls |callback| with the entirety of the stream
// data.
class WebTransportUnidirectionalEchoReadVisitor
    : public WebTransportStreamVisitor {
 public:
  using Callback = std::function<void(const std::string&)>;

  WebTransportUnidirectionalEchoReadVisitor(WebTransportStream* stream,
                                            Callback callback)
      : stream_(stream), callback_(std::move(callback)) {}

  void OnCanRead() override {
    WebTransportStream::ReadResult result = stream_->Read(&buffer_);
    QUIC_DVLOG(1) << "Attempted reading on WebTransport unidirectional stream "
                  << stream_->GetStreamId()
                  << ", bytes read: " << result.bytes_read;
    if (result.fin) {
      QUIC_DVLOG(1) << "Finished receiving data on a WebTransport stream "
                    << stream_->GetStreamId() << ", queueing up the echo";
      callback_(buffer_);
    }
  }

  void OnCanWrite() override { QUIC_NOTREACHED(); }

 private:
  WebTransportStream* stream_;
  std::string buffer_;
  Callback callback_;
};

// Sends supplied data.
class WebTransportUnidirectionalEchoWriteVisitor
    : public WebTransportStreamVisitor {
 public:
  WebTransportUnidirectionalEchoWriteVisitor(WebTransportStream* stream,
                                             const std::string& data)
      : stream_(stream), data_(data) {}

  void OnCanRead() override { QUIC_NOTREACHED(); }
  void OnCanWrite() override {
    if (data_.empty()) {
      return;
    }
    if (!stream_->Write(data_)) {
      return;
    }
    data_ = "";
    bool fin_sent = stream_->SendFin();
    QUICHE_DVLOG(1)
        << "WebTransportUnidirectionalEchoWriteVisitor finished sending data.";
    QUICHE_DCHECK(fin_sent);
  }

 private:
  WebTransportStream* stream_;
  std::string data_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_WEB_TRANSPORT_TEST_VISITORS_H_
