// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_SYSCALL_WRAPPER_H_
#define QUICHE_QUIC_CORE_QUIC_SYSCALL_WRAPPER_H_

#include <sys/socket.h>
#include <sys/types.h>

#include "quiche/quic/platform/api/quic_export.h"

struct mmsghdr;
namespace quic {

// QuicSyscallWrapper is a pass-through proxy to the real syscalls.
class QUICHE_EXPORT QuicSyscallWrapper {
 public:
  virtual ~QuicSyscallWrapper() = default;

  virtual ssize_t Sendmsg(int sockfd, const msghdr* msg, int flags);

  virtual int Sendmmsg(int sockfd, mmsghdr* msgvec, unsigned int vlen,
                       int flags);
};

// A global instance of QuicSyscallWrapper, used by some socket util functions.
QuicSyscallWrapper* GetGlobalSyscallWrapper();

// Change the global QuicSyscallWrapper to |wrapper|, for testing.
void SetGlobalSyscallWrapper(QuicSyscallWrapper* wrapper);

// ScopedGlobalSyscallWrapperOverride changes the global QuicSyscallWrapper
// during its lifetime, for testing.
class QUICHE_EXPORT ScopedGlobalSyscallWrapperOverride {
 public:
  explicit ScopedGlobalSyscallWrapperOverride(
      QuicSyscallWrapper* wrapper_in_scope);
  ~ScopedGlobalSyscallWrapperOverride();

 private:
  QuicSyscallWrapper* original_wrapper_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_SYSCALL_WRAPPER_H_
