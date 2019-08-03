// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_SYSTEM_EVENT_LOOP_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_SYSTEM_EVENT_LOOP_IMPL_H_

#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/task/thread_pool/thread_pool.h"

inline void QuicRunSystemEventLoopIterationImpl() {
  base::RunLoop().RunUntilIdle();
}

class QuicSystemEventLoopImpl {
 public:
  QuicSystemEventLoopImpl(std::string context_name) {
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams(context_name);
  }

 private:
  base::MessageLoopForIO message_loop_;
};

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_SYSTEM_EVENT_LOOP_IMPL_H_
