// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_THREAD_IMPL_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_THREAD_IMPL_H_

#include "base/threading/simple_thread.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

// A class representing a thread of execution in QUIC.
class QuicThreadImpl : public base::SimpleThread {
 public:
  QuicThreadImpl(const QuicString& string) : base::SimpleThread(string) {}
  QuicThreadImpl(const QuicThreadImpl&) = delete;
  QuicThreadImpl& operator=(const QuicThreadImpl&) = delete;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_THREAD_IMPL_H_
