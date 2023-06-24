// Copyright 2023 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/tools/naive/naive_padding_socket.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <tuple>
#include <utility>

#include "base/rand_util.h"
#include "base/task/single_thread_task_runner.h"
#include "net/base/io_buffer.h"

namespace net {

namespace {
constexpr int kMaxBufferSize = 64 * 1024;
constexpr int kFirstPaddings = 8;
}  // namespace

NaivePaddingSocket::NaivePaddingSocket(StreamSocket* transport_socket,
                                       PaddingType padding_type,
                                       Direction direction)
    : transport_socket_(transport_socket),
      padding_type_(padding_type),
      direction_(direction),
      read_buf_(base::MakeRefCounted<IOBuffer>(kMaxBufferSize)),
      framer_(kFirstPaddings) {}

NaivePaddingSocket::~NaivePaddingSocket() {
  Disconnect();
}

void NaivePaddingSocket::Disconnect() {
  transport_socket_->Disconnect();
}

int NaivePaddingSocket::Read(IOBuffer* buf,
                             int buf_len,
                             CompletionOnceCallback callback) {
  DCHECK(!callback.is_null());

  switch (padding_type_) {
    case PaddingType::kNone:
      return ReadNoPadding(buf, buf_len, std::move(callback));
    case PaddingType::kVariant1:
      if (framer_.num_read_frames() < kFirstPaddings) {
        return ReadPaddingV1(buf, buf_len, std::move(callback));
      } else {
        return ReadNoPadding(buf, buf_len, std::move(callback));
      }
    default:
      NOTREACHED();
  }
}

int NaivePaddingSocket::ReadNoPadding(IOBuffer* buf,
                                      int buf_len,
                                      CompletionOnceCallback callback) {
  int rv = transport_socket_->Read(
      buf, buf_len,
      base::BindOnce(&NaivePaddingSocket::OnReadNoPaddingComplete,
                     base::Unretained(this), std::move(callback)));
  return rv;
}

void NaivePaddingSocket::OnReadNoPaddingComplete(
    CompletionOnceCallback callback,
    int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  DCHECK(callback);

  std::move(callback).Run(rv);
}

int NaivePaddingSocket::ReadPaddingV1(IOBuffer* buf,
                                      int buf_len,
                                      CompletionOnceCallback callback) {
  DCHECK(!callback.is_null());
  DCHECK(read_user_buf_ == nullptr);

  // Truncates user requested buf len if it is too large for the padding
  // buffer.
  buf_len = std::min(buf_len, kMaxBufferSize);
  read_user_buf_ = buf;
  read_user_buf_len_ = buf_len;

  int rv = ReadPaddingV1Payload();

  if (rv == ERR_IO_PENDING) {
    read_callback_ = std::move(callback);
    return rv;
  }

  read_user_buf_ = nullptr;

  return rv;
}

void NaivePaddingSocket::OnReadPaddingV1Complete(int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  DCHECK(read_callback_);
  DCHECK(read_user_buf_ != nullptr);

  if (rv > 0) {
    rv = framer_.Read(read_buf_->data(), rv, read_user_buf_->data(),
                      read_user_buf_len_);
    if (rv == 0) {
      rv = ReadPaddingV1Payload();
      if (rv == ERR_IO_PENDING)
        return;
    }
  }

  // Must reset read_user_buf_ before invoking read_callback_, which may reenter
  // Read().
  read_user_buf_ = nullptr;

  std::move(read_callback_).Run(rv);
}

int NaivePaddingSocket::ReadPaddingV1Payload() {
  for (;;) {
    int rv = transport_socket_->Read(
        read_buf_.get(), read_user_buf_len_,
        base::BindOnce(&NaivePaddingSocket::OnReadPaddingV1Complete,
                       base::Unretained(this)));
    if (rv <= 0) {
      return rv;
    }
    rv = framer_.Read(read_buf_->data(), rv, read_user_buf_->data(),
                      read_user_buf_len_);
    if (rv > 0) {
      return rv;
    }
  }
}

int NaivePaddingSocket::Write(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& traffic_annotation) {
  switch (padding_type_) {
    case PaddingType::kNone:
      return WriteNoPadding(buf, buf_len, std::move(callback),
                            traffic_annotation);
    case PaddingType::kVariant1:
      if (framer_.num_written_frames() < kFirstPaddings) {
        return WritePaddingV1(buf, buf_len, std::move(callback),
                              traffic_annotation);
      } else {
        return WriteNoPadding(buf, buf_len, std::move(callback),
                              traffic_annotation);
      }
    default:
      NOTREACHED();
  }
}

int NaivePaddingSocket::WriteNoPadding(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& traffic_annotation) {
  return transport_socket_->Write(
      buf, buf_len,
      base::BindOnce(&NaivePaddingSocket::OnWriteNoPaddingComplete,
                     base::Unretained(this), std::move(callback),
                     traffic_annotation),
      traffic_annotation);
}

void NaivePaddingSocket::OnWriteNoPaddingComplete(
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& traffic_annotation,
    int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  DCHECK(callback);

  std::move(callback).Run(rv);
}

int NaivePaddingSocket::WritePaddingV1(
    IOBuffer* buf,
    int buf_len,
    CompletionOnceCallback callback,
    const NetworkTrafficAnnotationTag& traffic_annotation) {
  DCHECK(write_buf_ == nullptr);

  auto padded = base::MakeRefCounted<IOBuffer>(kMaxBufferSize);
  int padding_size;
  if (direction_ == kServer) {
    if (buf_len < 100) {
      padding_size = base::RandInt(framer_.max_padding_size() - buf_len,
                                   framer_.max_padding_size());
    } else {
      padding_size = base::RandInt(0, framer_.max_padding_size());
    }
  } else {
    padding_size = base::RandInt(0, framer_.max_padding_size());
  }
  int write_buf_len =
      framer_.Write(buf->data(), buf_len, padding_size, padded->data(),
                    kMaxBufferSize, write_user_payload_len_);
  // Using DrainableIOBuffer here because we do not want to
  // repeatedly encode the padding frames when short writes happen.
  write_buf_ =
      base::MakeRefCounted<DrainableIOBuffer>(std::move(padded), write_buf_len);

  int rv = WritePaddingV1Drain(traffic_annotation);
  if (rv == ERR_IO_PENDING) {
    write_callback_ = std::move(callback);
    return rv;
  }

  write_buf_ = nullptr;
  write_user_payload_len_ = 0;

  return rv;
}

void NaivePaddingSocket::OnWritePaddingV1Complete(
    const NetworkTrafficAnnotationTag& traffic_annotation,
    int rv) {
  DCHECK_NE(ERR_IO_PENDING, rv);
  DCHECK(write_callback_);
  DCHECK(write_buf_ != nullptr);

  if (rv > 0) {
    write_buf_->DidConsume(rv);
    rv = WritePaddingV1Drain(traffic_annotation);
    if (rv == ERR_IO_PENDING)
      return;
  }

  // Must reset these before invoking write_callback_, which may reenter
  // Write().
  write_buf_ = nullptr;
  write_user_payload_len_ = 0;

  std::move(write_callback_).Run(rv);
}

int NaivePaddingSocket::WritePaddingV1Drain(
    const NetworkTrafficAnnotationTag& traffic_annotation) {
  DCHECK(write_buf_ != nullptr);

  while (write_buf_->BytesRemaining() > 0) {
    int remaining = write_buf_->BytesRemaining();
    if (direction_ == kServer && write_user_payload_len_ > 400 &&
        write_user_payload_len_ < 1024) {
      remaining = std::min(remaining, base::RandInt(200, 300));
    }
    int rv = transport_socket_->Write(
        write_buf_.get(), remaining,
        base::BindOnce(&NaivePaddingSocket::OnWritePaddingV1Complete,
                       base::Unretained(this), traffic_annotation),
        traffic_annotation);
    if (rv <= 0) {
      return rv;
    }
    write_buf_->DidConsume(rv);
  }
  // Synchronously drained the buffer.
  return write_user_payload_len_;
}

}  // namespace net
