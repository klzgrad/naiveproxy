// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CHROMIUM_SPDY_STREAM_TEST_UTIL_H_
#define NET_SPDY_CHROMIUM_SPDY_STREAM_TEST_UTIL_H_

#include <memory>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "net/base/io_buffer.h"
#include "net/base/test_completion_callback.h"
#include "net/log/net_log_source.h"
#include "net/spdy/chromium/spdy_read_queue.h"
#include "net/spdy/chromium/spdy_stream.h"
#include "net/spdy/platform/api/spdy_string_piece.h"

namespace net {

namespace test {

// Delegate that calls Close() on |stream_| on OnClose. Used by tests
// to make sure that such an action is harmless.
class ClosingDelegate : public SpdyStream::Delegate {
 public:
  explicit ClosingDelegate(const base::WeakPtr<SpdyStream>& stream);
  ~ClosingDelegate() override;

  // SpdyStream::Delegate implementation.
  void OnHeadersSent() override;
  void OnHeadersReceived(const SpdyHeaderBlock& response_headers) override;
  void OnDataReceived(std::unique_ptr<SpdyBuffer> buffer) override;
  void OnDataSent() override;
  void OnTrailers(const SpdyHeaderBlock& trailers) override;
  void OnClose(int status) override;
  NetLogSource source_dependency() const override;

  // Returns whether or not the stream is closed.
  bool StreamIsClosed() const { return !stream_.get(); }

 private:
  base::WeakPtr<SpdyStream> stream_;
};

// Base class with shared functionality for test delegate
// implementations below.
class StreamDelegateBase : public SpdyStream::Delegate {
 public:
  explicit StreamDelegateBase(const base::WeakPtr<SpdyStream>& stream);
  ~StreamDelegateBase() override;

  void OnHeadersSent() override;
  void OnHeadersReceived(const SpdyHeaderBlock& response_headers) override;
  void OnDataReceived(std::unique_ptr<SpdyBuffer> buffer) override;
  void OnDataSent() override;
  void OnTrailers(const SpdyHeaderBlock& trailers) override;
  void OnClose(int status) override;
  NetLogSource source_dependency() const override;

  // Waits for the stream to be closed and returns the status passed
  // to OnClose().
  int WaitForClose();

  // Drains all data from the underlying read queue and returns it as
  // a string.
  SpdyString TakeReceivedData();

  // Returns whether or not the stream is closed.
  bool StreamIsClosed() const { return !stream_.get(); }

  // Returns the stream's ID. If called when the stream is closed,
  // returns the stream's ID when it was open.
  SpdyStreamId stream_id() const { return stream_id_; }

  SpdyString GetResponseHeaderValue(const SpdyString& name) const;
  bool send_headers_completed() const { return send_headers_completed_; }

 protected:
  const base::WeakPtr<SpdyStream>& stream() { return stream_; }

 private:
  base::WeakPtr<SpdyStream> stream_;
  SpdyStreamId stream_id_;
  TestCompletionCallback callback_;
  bool send_headers_completed_;
  SpdyHeaderBlock response_headers_;
  SpdyReadQueue received_data_queue_;
};

// Test delegate that does nothing. Used to capture data about the
// stream, e.g. its id when it was open.
class StreamDelegateDoNothing : public StreamDelegateBase {
 public:
  explicit StreamDelegateDoNothing(const base::WeakPtr<SpdyStream>& stream);
  ~StreamDelegateDoNothing() override;
};

// Test delegate that sends data immediately in OnHeadersReceived().
class StreamDelegateSendImmediate : public StreamDelegateBase {
 public:
  // |data| can be NULL.
  StreamDelegateSendImmediate(const base::WeakPtr<SpdyStream>& stream,
                              SpdyStringPiece data);
  ~StreamDelegateSendImmediate() override;

  void OnHeadersReceived(const SpdyHeaderBlock& response_headers) override;

 private:
  SpdyStringPiece data_;
};

// Test delegate that sends body data.
class StreamDelegateWithBody : public StreamDelegateBase {
 public:
  StreamDelegateWithBody(const base::WeakPtr<SpdyStream>& stream,
                         SpdyStringPiece data);
  ~StreamDelegateWithBody() override;

  void OnHeadersSent() override;

 private:
  scoped_refptr<StringIOBuffer> buf_;
};

// Test delegate that closes stream in OnHeadersReceived().
class StreamDelegateCloseOnHeaders : public StreamDelegateBase {
 public:
  explicit StreamDelegateCloseOnHeaders(
      const base::WeakPtr<SpdyStream>& stream);
  ~StreamDelegateCloseOnHeaders() override;

  void OnHeadersReceived(const SpdyHeaderBlock& response_headers) override;
};

}  // namespace test

}  // namespace net

#endif  // NET_SPDY_CHROMIUM_SPDY_STREAM_TEST_UTIL_H_
