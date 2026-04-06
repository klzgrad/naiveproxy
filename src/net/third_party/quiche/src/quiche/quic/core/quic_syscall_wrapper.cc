// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_syscall_wrapper.h"

#include <atomic>
#include <cerrno>

namespace quic {
namespace {
std::atomic<QuicSyscallWrapper*> global_syscall_wrapper(new QuicSyscallWrapper);
}  // namespace

ssize_t QuicSyscallWrapper::Sendmsg(int sockfd, const msghdr* msg, int flags) {
  return ::sendmsg(sockfd, msg, flags);
}

int QuicSyscallWrapper::Sendmmsg(int sockfd, mmsghdr* msgvec, unsigned int vlen,
                                 int flags) {
#if defined(__linux__) && !defined(__ANDROID__)
  return ::sendmmsg(sockfd, msgvec, vlen, flags);
#else
  errno = ENOSYS;
  return -1;
#endif
}

QuicSyscallWrapper* GetGlobalSyscallWrapper() {
  return global_syscall_wrapper.load();
}

void SetGlobalSyscallWrapper(QuicSyscallWrapper* wrapper) {
  global_syscall_wrapper.store(wrapper);
}

ScopedGlobalSyscallWrapperOverride::ScopedGlobalSyscallWrapperOverride(
    QuicSyscallWrapper* wrapper_in_scope)
    : original_wrapper_(GetGlobalSyscallWrapper()) {
  SetGlobalSyscallWrapper(wrapper_in_scope);
}

ScopedGlobalSyscallWrapperOverride::~ScopedGlobalSyscallWrapperOverride() {
  SetGlobalSyscallWrapper(original_wrapper_);
}

}  // namespace quic
