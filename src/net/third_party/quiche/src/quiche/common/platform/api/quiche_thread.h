// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_THREAD_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_THREAD_H_

#include <string>

#include "quiche_platform_impl/quiche_thread_impl.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// A class representing a thread of execution in QUIC.
class QUICHE_EXPORT QuicheThread : public QuicheThreadImpl {
 public:
  QuicheThread(const std::string& string) : QuicheThreadImpl(string) {}
  QuicheThread(const QuicheThread&) = delete;
  QuicheThread& operator=(const QuicheThread&) = delete;

  // Impl defines a virtual void Run() method which subclasses
  // must implement.
};

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_THREAD_H_
