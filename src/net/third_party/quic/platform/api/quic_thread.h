// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_THREAD_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_THREAD_H_

#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/impl/quic_thread_impl.h"

namespace quic {

// A class representing a thread of execution in QUIC.
class QuicThread : public QuicThreadImpl {
 public:
  QuicThread(const QuicString& string) : QuicThreadImpl(string) {}
  QuicThread(const QuicThread&) = delete;
  QuicThread& operator=(const QuicThread&) = delete;

  // Impl defines a virtual void Run() method which subclasses
  // must implement.
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_THREAD_H_
