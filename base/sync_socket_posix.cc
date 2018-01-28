// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sync_socket.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#if defined(OS_SOLARIS)
#include <sys/filio.h>
#endif

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"

namespace base {

namespace {
// To avoid users sending negative message lengths to Send/Receive
// we clamp message lengths, which are size_t, to no more than INT_MAX.
const size_t kMaxMessageLength = static_cast<size_t>(INT_MAX);

// Writes |length| of |buffer| into |handle|.  Returns the number of bytes
// written or zero on error.  |length| must be greater than 0.
size_t SendHelper(SyncSocket::Handle handle,
                  const void* buffer,
                  size_t length) {
  DCHECK_GT(length, 0u);
  DCHECK_LE(length, kMaxMessageLength);
  DCHECK_NE(handle, SyncSocket::kInvalidHandle);
  const char* charbuffer = static_cast<const char*>(buffer);
  return WriteFileDescriptor(handle, charbuffer, length)
             ? static_cast<size_t>(length)
             : 0;
}

bool CloseHandle(SyncSocket::Handle handle) {
  if (handle != SyncSocket::kInvalidHandle && close(handle) < 0) {
    DPLOG(ERROR) << "close";
    return false;
  }

  return true;
}

}  // namespace

const SyncSocket::Handle SyncSocket::kInvalidHandle = -1;

SyncSocket::SyncSocket() : handle_(kInvalidHandle) {}

SyncSocket::~SyncSocket() {
  Close();
}

// static
bool SyncSocket::CreatePair(SyncSocket* socket_a, SyncSocket* socket_b) {
  DCHECK_NE(socket_a, socket_b);
  DCHECK_EQ(socket_a->handle_, kInvalidHandle);
  DCHECK_EQ(socket_b->handle_, kInvalidHandle);

#if defined(OS_MACOSX)
  int nosigpipe = 1;
#endif  // defined(OS_MACOSX)

  Handle handles[2] = { kInvalidHandle, kInvalidHandle };
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, handles) != 0) {
    CloseHandle(handles[0]);
    CloseHandle(handles[1]);
    return false;
  }

#if defined(OS_MACOSX)
  // On OSX an attempt to read or write to a closed socket may generate a
  // SIGPIPE rather than returning -1.  setsockopt will shut this off.
  if (0 != setsockopt(handles[0], SOL_SOCKET, SO_NOSIGPIPE,
                      &nosigpipe, sizeof nosigpipe) ||
      0 != setsockopt(handles[1], SOL_SOCKET, SO_NOSIGPIPE,
                      &nosigpipe, sizeof nosigpipe)) {
    CloseHandle(handles[0]);
    CloseHandle(handles[1]);
    return false;
  }
#endif

  // Copy the handles out for successful return.
  socket_a->handle_ = handles[0];
  socket_b->handle_ = handles[1];

  return true;
}

// static
SyncSocket::Handle SyncSocket::UnwrapHandle(
    const TransitDescriptor& descriptor) {
  return descriptor.fd;
}

bool SyncSocket::PrepareTransitDescriptor(ProcessHandle peer_process_handle,
                                          TransitDescriptor* descriptor) {
  descriptor->fd = handle();
  descriptor->auto_close = false;
  return descriptor->fd != kInvalidHandle;
}

bool SyncSocket::Close() {
  const bool retval = CloseHandle(handle_);
  handle_ = kInvalidHandle;
  return retval;
}

size_t SyncSocket::Send(const void* buffer, size_t length) {
  AssertBlockingAllowed();
  return SendHelper(handle_, buffer, length);
}

size_t SyncSocket::Receive(void* buffer, size_t length) {
  AssertBlockingAllowed();
  DCHECK_GT(length, 0u);
  DCHECK_LE(length, kMaxMessageLength);
  DCHECK_NE(handle_, kInvalidHandle);
  char* charbuffer = static_cast<char*>(buffer);
  if (ReadFromFD(handle_, charbuffer, length))
    return length;
  return 0;
}

size_t SyncSocket::ReceiveWithTimeout(void* buffer,
                                      size_t length,
                                      TimeDelta timeout) {
  AssertBlockingAllowed();
  DCHECK_GT(length, 0u);
  DCHECK_LE(length, kMaxMessageLength);
  DCHECK_NE(handle_, kInvalidHandle);

  // Only timeouts greater than zero and less than one second are allowed.
  DCHECK_GT(timeout.InMicroseconds(), 0);
  DCHECK_LT(timeout.InMicroseconds(),
            TimeDelta::FromSeconds(1).InMicroseconds());

  // Track the start time so we can reduce the timeout as data is read.
  TimeTicks start_time = TimeTicks::Now();
  const TimeTicks finish_time = start_time + timeout;

  struct pollfd pollfd;
  pollfd.fd = handle_;
  pollfd.events = POLLIN;
  pollfd.revents = 0;

  size_t bytes_read_total = 0;
  while (bytes_read_total < length) {
    const TimeDelta this_timeout = finish_time - TimeTicks::Now();
    const int timeout_ms =
        static_cast<int>(this_timeout.InMillisecondsRoundedUp());
    if (timeout_ms <= 0)
      break;
    const int poll_result = poll(&pollfd, 1, timeout_ms);
    // Handle EINTR manually since we need to update the timeout value.
    if (poll_result == -1 && errno == EINTR)
      continue;
    // Return if other type of error or a timeout.
    if (poll_result <= 0)
      return bytes_read_total;

    // poll() only tells us that data is ready for reading, not how much.  We
    // must Peek() for the amount ready for reading to avoid blocking.
    // At hang up (POLLHUP), the write end has been closed and there might still
    // be data to be read.
    // No special handling is needed for error (POLLERR); we can let any of the
    // following operations fail and handle it there.
    DCHECK(pollfd.revents & (POLLIN | POLLHUP | POLLERR)) << pollfd.revents;
    const size_t bytes_to_read = std::min(Peek(), length - bytes_read_total);

    // There may be zero bytes to read if the socket at the other end closed.
    if (!bytes_to_read)
      return bytes_read_total;

    const size_t bytes_received =
        Receive(static_cast<char*>(buffer) + bytes_read_total, bytes_to_read);
    bytes_read_total += bytes_received;
    if (bytes_received != bytes_to_read)
      return bytes_read_total;
  }

  return bytes_read_total;
}

size_t SyncSocket::Peek() {
  DCHECK_NE(handle_, kInvalidHandle);
  int number_chars = 0;
  if (ioctl(handle_, FIONREAD, &number_chars) == -1) {
    // If there is an error in ioctl, signal that the channel would block.
    return 0;
  }
  DCHECK_GE(number_chars, 0);
  return number_chars;
}

SyncSocket::Handle SyncSocket::Release() {
  Handle r = handle_;
  handle_ = kInvalidHandle;
  return r;
}

CancelableSyncSocket::CancelableSyncSocket() = default;
CancelableSyncSocket::CancelableSyncSocket(Handle handle)
    : SyncSocket(handle) {
}

bool CancelableSyncSocket::Shutdown() {
  DCHECK_NE(handle_, kInvalidHandle);
  return HANDLE_EINTR(shutdown(handle_, SHUT_RDWR)) >= 0;
}

size_t CancelableSyncSocket::Send(const void* buffer, size_t length) {
  DCHECK_GT(length, 0u);
  DCHECK_LE(length, kMaxMessageLength);
  DCHECK_NE(handle_, kInvalidHandle);

  const int flags = fcntl(handle_, F_GETFL);
  if (flags != -1 && (flags & O_NONBLOCK) == 0) {
    // Set the socket to non-blocking mode for sending if its original mode
    // is blocking.
    fcntl(handle_, F_SETFL, flags | O_NONBLOCK);
  }

  const size_t len = SendHelper(handle_, buffer, length);

  if (flags != -1 && (flags & O_NONBLOCK) == 0) {
    // Restore the original flags.
    fcntl(handle_, F_SETFL, flags);
  }

  return len;
}

// static
bool CancelableSyncSocket::CreatePair(CancelableSyncSocket* socket_a,
                                      CancelableSyncSocket* socket_b) {
  return SyncSocket::CreatePair(socket_a, socket_b);
}

}  // namespace base
