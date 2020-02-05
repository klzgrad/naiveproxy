// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "base/mac/scoped_nsobject.h"

namespace base {
namespace internal {

id ScopedNSProtocolTraitsRetain(id obj) {
  return [obj retain];
}

id ScopedNSProtocolTraitsAutoRelease(id obj) {
  return [obj autorelease];
}

void ScopedNSProtocolTraitsRelease(id obj) {
  return [obj release];
}

}  // namespace internal
}  // namespace base
