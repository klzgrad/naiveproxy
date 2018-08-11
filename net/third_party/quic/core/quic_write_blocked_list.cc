// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_write_blocked_list.h"

#include "net/third_party/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quic/platform/api/quic_flags.h"

namespace net {

QuicWriteBlockedList::QuicWriteBlockedList(bool register_static_streams)
    : last_priority_popped_(0),
      crypto_stream_blocked_(false),
      headers_stream_blocked_(false),
      register_static_streams_(register_static_streams) {
  if (register_static_streams_) {
    QUIC_FLAG_COUNT(quic_reloadable_flag_quic_register_static_streams);
  }
  memset(batch_write_stream_id_, 0, sizeof(batch_write_stream_id_));
  memset(bytes_left_for_batch_write_, 0, sizeof(bytes_left_for_batch_write_));
}

QuicWriteBlockedList::~QuicWriteBlockedList() {}

}  // namespace net
