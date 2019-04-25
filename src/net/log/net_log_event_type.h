// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_LOG_NET_LOG_EVENT_TYPE_H_
#define NET_LOG_NET_LOG_EVENT_TYPE_H_

namespace net {

enum class NetLogEventType {
#define EVENT_TYPE(label) label,
#include "net/log/net_log_event_type_list.h"
#undef EVENT_TYPE
  COUNT
};

// The 'phase' of an event trace (whether it marks the beginning or end
// of an event.).
enum class NetLogEventPhase {
  NONE,
  BEGIN,
  END,
};

}  // namespace net

#endif  // NET_LOG_NET_LOG_EVENT_TYPE_H_
