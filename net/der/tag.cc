// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/der/tag.h"

#include "base/logging.h"

namespace net {

namespace der {

Tag ContextSpecificConstructed(uint8_t class_number) {
  DCHECK_EQ(class_number, class_number & kTagNumberMask);
  return (class_number & kTagNumberMask) | kTagConstructed |
         kTagContextSpecific;
}

Tag ContextSpecificPrimitive(uint8_t base) {
  DCHECK_EQ(base, base & kTagNumberMask);
  return (base & kTagNumberMask) | kTagPrimitive | kTagContextSpecific;
}

bool IsConstructed(Tag tag) {
  return (tag & kTagConstructionMask) == kTagConstructed;
}

}  // namespace der

}  // namespace net
