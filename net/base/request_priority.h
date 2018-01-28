// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_REQUEST_PRIORITY_H_
#define NET_BASE_REQUEST_PRIORITY_H_

#include "net/base/net_export.h"

namespace net {

// Prioritization used in various parts of the networking code such
// as connection prioritization and resource loading prioritization.
// A Java counterpart will be generated for this enum.
// GENERATED_JAVA_ENUM_PACKAGE: org.chromium.net
// GENERATED_JAVA_CLASS_NAME_OVERRIDE: RequestPriority
enum RequestPriority {
  THROTTLED = 0,  // Used to signal that resources
                  // should be reserved for following
                  // requests (i.e. that higher priority
                  // following requests are expected).
  MINIMUM_PRIORITY = THROTTLED,
  IDLE,  // Default "as resources available" level.
  LOWEST,
  DEFAULT_PRIORITY = LOWEST,
  LOW,
  MEDIUM,
  HIGHEST,
  MAXIMUM_PRIORITY = HIGHEST,
};

// For simplicity, one can assume that one can index into array of
// NUM_PRIORITIES elements with a RequestPriority (i.e.,
// MINIMUM_PRIORITY == 0).
enum RequestPrioritySize {
  NUM_PRIORITIES = MAXIMUM_PRIORITY + 1,
};

NET_EXPORT const char* RequestPriorityToString(RequestPriority priority);

}  // namespace net

#endif  // NET_BASE_REQUEST_PRIORITY_H_
