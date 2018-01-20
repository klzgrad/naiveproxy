// Copyright 2023 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_TOOLS_NAIVE_NAIVE_PADDING_SOCKET_H_
#define NET_TOOLS_NAIVE_NAIVE_PADDING_SOCKET_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "net/base/address_list.h"
#include "net/base/completion_once_callback.h"
#include "net/base/completion_repeating_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/socket/stream_socket.h"
#include "net/tools/naive/naive_padding_framer.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/gurl.h"

namespace net {

class NaivePaddingSocket {
 public:
  NaivePaddingSocket(StreamSocket* transport_socket,
                     PaddingType padding_type,
                     Direction direction);

  NaivePaddingSocket(const NaivePaddingSocket&) = delete;
  NaivePaddingSocket& operator=(const NaivePaddingSocket&) = delete;

  // On destruction Disconnect() is called.
  ~NaivePaddingSocket();

  void Disconnect();

  int Read(IOBuffer* buf, int buf_len, CompletionOnceCallback callback);

  int Write(IOBuffer* buf,
            int buf_len,
            CompletionOnceCallback callback,
            const NetworkTrafficAnnotationTag& traffic_annotation);

 private:
  int ReadNoPadding(IOBuffer* buf,
                    int buf_len,
                    CompletionOnceCallback callback);
  int WriteNoPadding(IOBuffer* buf,
                     int buf_len,
                     CompletionOnceCallback callback,
                     const NetworkTrafficAnnotationTag& traffic_annotation);
  void OnReadNoPaddingComplete(CompletionOnceCallback callback, int rv);
  void OnWriteNoPaddingComplete(
      CompletionOnceCallback callback,
      const NetworkTrafficAnnotationTag& traffic_annotation,
      int rv);

  int ReadPaddingV1(IOBuffer* buf,
                    int buf_len,
                    CompletionOnceCallback callback);
  int WritePaddingV1(IOBuffer* buf,
                     int buf_len,
                     CompletionOnceCallback callback,
                     const NetworkTrafficAnnotationTag& traffic_annotation);
  void OnReadPaddingV1Complete(int rv);
  void OnWritePaddingV1Complete(
      const NetworkTrafficAnnotationTag& traffic_annotation,
      int rv);

  // Exhausts synchronous reads if it is a pure padding
  // so this does not return zero for non-EOF condition.
  int ReadPaddingV1Payload();

  int WritePaddingV1Drain(
      const NetworkTrafficAnnotationTag& traffic_annotation);

  // Stores the underlying socket.
  // Non-owning because this socket does not take part in the client socket pool
  // handling and making it owning the transport socket may interfere badly
  // with the client socket pool.
  StreamSocket* transport_socket_;

  PaddingType padding_type_;
  Direction direction_;

  IOBuffer* read_user_buf_ = nullptr;
  int read_user_buf_len_ = 0;
  CompletionOnceCallback read_callback_;
  scoped_refptr<IOBuffer> read_buf_;

  int write_user_payload_len_ = 0;
  CompletionOnceCallback write_callback_;
  scoped_refptr<DrainableIOBuffer> write_buf_;

  NaivePaddingFramer framer_;
};

}  // namespace net

#endif  // NET_TOOLS_NAIVE_NAIVE_PADDING_SOCKET_H_
