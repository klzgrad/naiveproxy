// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_write_blocked_list.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

QuicWriteBlockedList::QuicWriteBlockedList(QuicTransportVersion version)
    : priority_write_scheduler_(
          std::make_unique<spdy::PriorityWriteScheduler<QuicStreamId>>(
              QuicVersionUsesCryptoFrames(version)
                  ? std::numeric_limits<QuicStreamId>::max()
                  : 0)),
      last_priority_popped_(0),
      scheduler_type_(spdy::WriteSchedulerType::SPDY) {
  memset(batch_write_stream_id_, 0, sizeof(batch_write_stream_id_));
  memset(bytes_left_for_batch_write_, 0, sizeof(bytes_left_for_batch_write_));
}

QuicWriteBlockedList::~QuicWriteBlockedList() {}

}  // namespace quic
