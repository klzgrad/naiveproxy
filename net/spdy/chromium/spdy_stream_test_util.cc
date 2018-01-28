// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/spdy_stream_test_util.h"

#include <cstddef>
#include <utility>

#include "base/stl_util.h"
#include "net/base/completion_callback.h"
#include "net/spdy/chromium/spdy_stream.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace test {

ClosingDelegate::ClosingDelegate(
    const base::WeakPtr<SpdyStream>& stream) : stream_(stream) {
  DCHECK(stream_);
}

ClosingDelegate::~ClosingDelegate() {}

void ClosingDelegate::OnHeadersSent() {}

void ClosingDelegate::OnHeadersReceived(
    const SpdyHeaderBlock& response_headers) {}

void ClosingDelegate::OnDataReceived(std::unique_ptr<SpdyBuffer> buffer) {}

void ClosingDelegate::OnDataSent() {}

void ClosingDelegate::OnTrailers(const SpdyHeaderBlock& trailers) {}

void ClosingDelegate::OnClose(int status) {
  DCHECK(stream_);
  stream_->Close();
  // The |stream_| may still be alive (if it is our delegate).
}

NetLogSource ClosingDelegate::source_dependency() const {
  return NetLogSource();
}

StreamDelegateBase::StreamDelegateBase(
    const base::WeakPtr<SpdyStream>& stream)
    : stream_(stream),
      stream_id_(0),
      send_headers_completed_(false) {
}

StreamDelegateBase::~StreamDelegateBase() {
}

void StreamDelegateBase::OnHeadersSent() {
  stream_id_ = stream_->stream_id();
  EXPECT_NE(stream_id_, 0u);
  send_headers_completed_ = true;
}

void StreamDelegateBase::OnHeadersReceived(
    const SpdyHeaderBlock& response_headers) {
  EXPECT_EQ(stream_->type() != SPDY_PUSH_STREAM, send_headers_completed_);
  response_headers_ = response_headers.Clone();
}

void StreamDelegateBase::OnDataReceived(std::unique_ptr<SpdyBuffer> buffer) {
  if (buffer)
    received_data_queue_.Enqueue(std::move(buffer));
}

void StreamDelegateBase::OnDataSent() {}

void StreamDelegateBase::OnTrailers(const SpdyHeaderBlock& trailers) {}

void StreamDelegateBase::OnClose(int status) {
  if (!stream_.get())
    return;
  stream_id_ = stream_->stream_id();
  stream_.reset();
  callback_.callback().Run(status);
}

NetLogSource StreamDelegateBase::source_dependency() const {
  return NetLogSource();
}

int StreamDelegateBase::WaitForClose() {
  int result = callback_.WaitForResult();
  EXPECT_TRUE(!stream_.get());
  return result;
}

SpdyString StreamDelegateBase::TakeReceivedData() {
  size_t len = received_data_queue_.GetTotalSize();
  SpdyString received_data(len, '\0');
  if (len > 0) {
    EXPECT_EQ(len, received_data_queue_.Dequeue(
                       base::string_as_array(&received_data), len));
  }
  return received_data;
}

SpdyString StreamDelegateBase::GetResponseHeaderValue(
    const SpdyString& name) const {
  SpdyHeaderBlock::const_iterator it = response_headers_.find(name);
  return (it == response_headers_.end()) ? SpdyString()
                                         : it->second.as_string();
}

StreamDelegateDoNothing::StreamDelegateDoNothing(
    const base::WeakPtr<SpdyStream>& stream)
    : StreamDelegateBase(stream) {}

StreamDelegateDoNothing::~StreamDelegateDoNothing() {
}

StreamDelegateSendImmediate::StreamDelegateSendImmediate(
    const base::WeakPtr<SpdyStream>& stream,
    SpdyStringPiece data)
    : StreamDelegateBase(stream), data_(data) {}

StreamDelegateSendImmediate::~StreamDelegateSendImmediate() {
}

void StreamDelegateSendImmediate::OnHeadersReceived(
    const SpdyHeaderBlock& response_headers) {
  StreamDelegateBase::OnHeadersReceived(response_headers);
  if (data_.data()) {
    scoped_refptr<StringIOBuffer> buf(new StringIOBuffer(data_.as_string()));
    stream()->SendData(buf.get(), buf->size(), MORE_DATA_TO_SEND);
  }
}

StreamDelegateWithBody::StreamDelegateWithBody(
    const base::WeakPtr<SpdyStream>& stream,
    SpdyStringPiece data)
    : StreamDelegateBase(stream), buf_(new StringIOBuffer(data.as_string())) {}

StreamDelegateWithBody::~StreamDelegateWithBody() {
}

void StreamDelegateWithBody::OnHeadersSent() {
  StreamDelegateBase::OnHeadersSent();
  stream()->SendData(buf_.get(), buf_->size(), NO_MORE_DATA_TO_SEND);
}

StreamDelegateCloseOnHeaders::StreamDelegateCloseOnHeaders(
    const base::WeakPtr<SpdyStream>& stream)
    : StreamDelegateBase(stream) {
}

StreamDelegateCloseOnHeaders::~StreamDelegateCloseOnHeaders() {
}

void StreamDelegateCloseOnHeaders::OnHeadersReceived(
    const SpdyHeaderBlock& response_headers) {
  stream()->Cancel();
}

}  // namespace test

}  // namespace net
