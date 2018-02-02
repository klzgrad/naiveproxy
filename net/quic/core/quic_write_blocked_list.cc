// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_write_blocked_list.h"

namespace net {

QuicWriteBlockedList::QuicWriteBlockedList()
    : last_priority_popped_(0),
      crypto_stream_blocked_(false),
      headers_stream_blocked_(false) {
  memset(batch_write_stream_id_, 0, sizeof(batch_write_stream_id_));
  memset(bytes_left_for_batch_write_, 0, sizeof(bytes_left_for_batch_write_));
}

QuicWriteBlockedList::~QuicWriteBlockedList() {}

}  // namespace net
