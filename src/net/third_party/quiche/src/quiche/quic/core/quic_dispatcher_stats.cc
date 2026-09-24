// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_dispatcher_stats.h"

#include <ostream>

namespace quic {

std::ostream& operator<<(std::ostream& os, const QuicDispatcherStats& s) {
  os << "{ packets_processed: " << s.packets_processed;
  os << ", packets_processed_with_unknown_cid: "
     << s.packets_processed_with_unknown_cid;
  os << ", packets_processed_with_replaced_cid_in_store: "
     << s.packets_processed_with_replaced_cid_in_store;
  os << ", packets_enqueued_early: " << s.packets_enqueued_early;
  os << ", packets_enqueued_chlo: " << s.packets_enqueued_chlo;
  os << ", packets_sent: " << s.packets_sent;
  os << ", sessions_created: " << s.sessions_created;
  os << " }";

  return os;
}

}  // namespace quic
