// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/quic_chromium_packet_writer.h"

#include <string>

#include "base/location.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/sparse_histogram.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/quic/chromium/quic_chromium_client_session.h"

namespace net {

namespace {

enum NotReusableReason {
  NOT_REUSABLE_NULLPTR = 0,
  NOT_REUSABLE_TOO_SMALL = 1,
  NOT_REUSABLE_REF_COUNT = 2,
  NUM_NOT_REUSABLE_REASONS = 3,
};

const int kMaxRetries = 12;  // 2^12 = 4 seconds, which should be a LOT.

void RecordNotReusableReason(NotReusableReason reason) {
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.WritePacketNotReusable", reason,
                            NUM_NOT_REUSABLE_REASONS);
}

void RecordRetryCount(int count) {
  UMA_HISTOGRAM_EXACT_LINEAR("Net.QuicSession.RetryAfterWriteErrorCount2",
                             count, kMaxRetries + 1);
}

}  // namespace

QuicChromiumPacketWriter::ReusableIOBuffer::ReusableIOBuffer(size_t capacity)
    : IOBuffer(capacity), capacity_(capacity), size_(0) {}

QuicChromiumPacketWriter::ReusableIOBuffer::~ReusableIOBuffer() {}

void QuicChromiumPacketWriter::ReusableIOBuffer::Set(const char* buffer,
                                                     size_t buf_len) {
  CHECK_LE(buf_len, capacity_);
  CHECK(HasOneRef());
  size_ = buf_len;
  std::memcpy(data(), buffer, buf_len);
}

QuicChromiumPacketWriter::QuicChromiumPacketWriter() : weak_factory_(this) {}

QuicChromiumPacketWriter::QuicChromiumPacketWriter(
    DatagramClientSocket* socket,
    base::SequencedTaskRunner* task_runner)
    : socket_(socket),
      delegate_(nullptr),
      packet_(new ReusableIOBuffer(kMaxPacketSize)),
      write_blocked_(false),
      retry_count_(0),
      weak_factory_(this) {
  retry_timer_.SetTaskRunner(task_runner);
  write_callback_ = base::Bind(&QuicChromiumPacketWriter::OnWriteComplete,
                               weak_factory_.GetWeakPtr());
}

QuicChromiumPacketWriter::~QuicChromiumPacketWriter() {}

void QuicChromiumPacketWriter::SetPacket(const char* buffer, size_t buf_len) {
  if (UNLIKELY(!packet_)) {
    packet_ = new ReusableIOBuffer(
        std::max(buf_len, static_cast<size_t>(kMaxPacketSize)));
    RecordNotReusableReason(NOT_REUSABLE_NULLPTR);
  }
  if (UNLIKELY(packet_->capacity() < buf_len)) {
    packet_ = new ReusableIOBuffer(buf_len);
    RecordNotReusableReason(NOT_REUSABLE_TOO_SMALL);
  }
  if (UNLIKELY(!packet_->HasOneRef())) {
    packet_ = new ReusableIOBuffer(
        std::max(buf_len, static_cast<size_t>(kMaxPacketSize)));
    RecordNotReusableReason(NOT_REUSABLE_REF_COUNT);
  }
  packet_->Set(buffer, buf_len);
}

WriteResult QuicChromiumPacketWriter::WritePacket(
    const char* buffer,
    size_t buf_len,
    const QuicIpAddress& self_address,
    const QuicSocketAddress& peer_address,
    PerPacketOptions* /*options*/) {
  DCHECK(!IsWriteBlocked());
  SetPacket(buffer, buf_len);
  return WritePacketToSocketImpl();
}

WriteResult QuicChromiumPacketWriter::WritePacketToSocket(
    scoped_refptr<ReusableIOBuffer> packet) {
  packet_ = std::move(packet);
  return QuicChromiumPacketWriter::WritePacketToSocketImpl();
}

WriteResult QuicChromiumPacketWriter::WritePacketToSocketImpl() {
  base::TimeTicks now = base::TimeTicks::Now();
  int rv = socket_->Write(packet_.get(), packet_->size(), write_callback_);

  if (MaybeRetryAfterWriteError(rv))
    return WriteResult(WRITE_STATUS_BLOCKED, ERR_IO_PENDING);

  if (rv < 0 && rv != ERR_IO_PENDING && delegate_ != nullptr) {
    // If write error, then call delegate's HandleWriteError, which
    // may be able to migrate and rewrite packet on a new socket.
    // HandleWriteError returns the outcome of that rewrite attempt.
    rv = delegate_->HandleWriteError(rv, std::move(packet_));
    DCHECK(packet_ == nullptr);
  }

  WriteStatus status = WRITE_STATUS_OK;
  if (rv < 0) {
    if (rv != ERR_IO_PENDING) {
      status = WRITE_STATUS_ERROR;
    } else {
      status = WRITE_STATUS_BLOCKED;
      write_blocked_ = true;
    }
  }

  base::TimeDelta delta = base::TimeTicks::Now() - now;
  if (status == WRITE_STATUS_OK) {
    UMA_HISTOGRAM_TIMES("Net.QuicSession.PacketWriteTime.Synchronous", delta);
  } else if (status == WRITE_STATUS_BLOCKED) {
    UMA_HISTOGRAM_TIMES("Net.QuicSession.PacketWriteTime.Asynchronous", delta);
  }

  return WriteResult(status, rv);
}

void QuicChromiumPacketWriter::RetryPacketAfterNoBuffers() {
  DCHECK_GT(retry_count_, 0);
  WriteResult result = WritePacketToSocketImpl();
  if (result.error_code != ERR_IO_PENDING)
    OnWriteComplete(result.error_code);
}

bool QuicChromiumPacketWriter::IsWriteBlockedDataBuffered() const {
  // Chrome sockets' Write() methods buffer the data until the Write is
  // permitted.
  return true;
}

bool QuicChromiumPacketWriter::IsWriteBlocked() const {
  return write_blocked_;
}

void QuicChromiumPacketWriter::SetWritable() {
  write_blocked_ = false;
}

void QuicChromiumPacketWriter::OnWriteComplete(int rv) {
  DCHECK_NE(rv, ERR_IO_PENDING);
  DCHECK(delegate_) << "Uninitialized delegate.";
  write_blocked_ = false;
  if (rv < 0) {
    if (MaybeRetryAfterWriteError(rv))
      return;

    // If write error, then call delegate's HandleWriteError, which
    // may be able to migrate and rewrite packet on a new socket.
    // HandleWriteError returns the outcome of that rewrite attempt.
    rv = delegate_->HandleWriteError(rv, std::move(packet_));
    DCHECK(packet_ == nullptr);
    if (rv == ERR_IO_PENDING)
      return;
  }
  if (retry_count_ != 0) {
    RecordRetryCount(retry_count_);
    retry_count_ = 0;
  }

  if (rv < 0)
    delegate_->OnWriteError(rv);
  else
    delegate_->OnWriteUnblocked();
}

bool QuicChromiumPacketWriter::MaybeRetryAfterWriteError(int rv) {
  if (rv != ERR_NO_BUFFER_SPACE)
    return false;

  if (retry_count_ >= kMaxRetries) {
    RecordRetryCount(retry_count_);
    return false;
  }

  retry_timer_.Start(
      FROM_HERE, base::TimeDelta::FromMilliseconds(UINT64_C(1) << retry_count_),
      base::Bind(&QuicChromiumPacketWriter::RetryPacketAfterNoBuffers,
                 weak_factory_.GetWeakPtr()));
  retry_count_++;
  write_blocked_ = true;
  return true;
}

QuicByteCount QuicChromiumPacketWriter::GetMaxPacketSize(
    const QuicSocketAddress& peer_address) const {
  return kMaxPacketSize;
}

}  // namespace net
