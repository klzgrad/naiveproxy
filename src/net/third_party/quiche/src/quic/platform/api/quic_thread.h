// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_THREAD_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_THREAD_H_

#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/quic/platform/impl/quic_thread_impl.h"

namespace quic {

// A class representing a thread of execution in QUIC.
class QUIC_EXPORT_PRIVATE QuicThread : public QuicThreadImpl {
 public:
  QuicThread(const std::string& string) : QuicThreadImpl(string) {}
  QuicThread(const QuicThread&) = delete;
  QuicThread& operator=(const QuicThread&) = delete;

  // Impl defines a virtual void Run() method which subclasses
  // must implement.
};

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_THREAD_H_
