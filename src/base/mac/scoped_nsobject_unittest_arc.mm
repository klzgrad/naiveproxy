// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#import <CoreFoundation/CoreFoundation.h>

#include "base/logging.h"
#import "base/mac/scoped_nsobject.h"
#include "testing/gtest/include/gtest/gtest.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

template <typename NST>
CFIndex GetRetainCount(const base::scoped_nsobject<NST>& nst) {
  @autoreleasepool {
    return CFGetRetainCount((__bridge CFTypeRef)nst.get()) - 1;
  }
}

#if __has_feature(objc_arc_weak)
TEST(ScopedNSObjectTestARC, DefaultPolicyIsRetain) {
  __weak id o;
  @autoreleasepool {
    base::scoped_nsprotocol<id> p([[NSObject alloc] init]);
    o = p.get();
    DCHECK_EQ(o, p.get());
  }
  DCHECK_EQ(o, nil);
}
#endif

TEST(ScopedNSObjectTestARC, ScopedNSObject) {
  base::scoped_nsobject<NSObject> p1([[NSObject alloc] init]);
  @autoreleasepool {
    EXPECT_TRUE(p1.get());
    EXPECT_TRUE(p1.get());
  }
  EXPECT_EQ(1, GetRetainCount(p1));
  EXPECT_EQ(1, GetRetainCount(p1));
  base::scoped_nsobject<NSObject> p2(p1);
  @autoreleasepool {
    EXPECT_EQ(p1.get(), p2.get());
  }
  EXPECT_EQ(2, GetRetainCount(p1));
  p2.reset();
  EXPECT_EQ(nil, p2.get());
  EXPECT_EQ(1, GetRetainCount(p1));
  {
    base::scoped_nsobject<NSObject> p3 = p1;
    @autoreleasepool {
      EXPECT_EQ(p1.get(), p3.get());
    }
    EXPECT_EQ(2, GetRetainCount(p1));
    @autoreleasepool {
      p3 = p1;
      EXPECT_EQ(p1.get(), p3.get());
    }
    EXPECT_EQ(2, GetRetainCount(p1));
  }
  EXPECT_EQ(1, GetRetainCount(p1));
  base::scoped_nsobject<NSObject> p4;
  @autoreleasepool {
    p4 = base::scoped_nsobject<NSObject>(p1.get());
  }
  EXPECT_EQ(2, GetRetainCount(p1));
  @autoreleasepool {
    EXPECT_TRUE(p1 == p1.get());
    EXPECT_TRUE(p1 == p1);
    EXPECT_FALSE(p1 != p1);
    EXPECT_FALSE(p1 != p1.get());
  }
  base::scoped_nsobject<NSObject> p5([[NSObject alloc] init]);
  @autoreleasepool {
    EXPECT_TRUE(p1 != p5);
    EXPECT_TRUE(p1 != p5.get());
    EXPECT_FALSE(p1 == p5);
    EXPECT_FALSE(p1 == p5.get());
  }

  base::scoped_nsobject<NSObject> p6 = p1;
  EXPECT_EQ(3, GetRetainCount(p6));
  @autoreleasepool {
    p6.autorelease();
    EXPECT_EQ(nil, p6.get());
  }
  EXPECT_EQ(2, GetRetainCount(p1));
}

TEST(ScopedNSObjectTestARC, ScopedNSObjectInContainer) {
  base::scoped_nsobject<id> p([[NSObject alloc] init]);
  @autoreleasepool {
    EXPECT_TRUE(p.get());
  }
  EXPECT_EQ(1, GetRetainCount(p));
  @autoreleasepool {
    std::vector<base::scoped_nsobject<id>> objects;
    objects.push_back(p);
    EXPECT_EQ(2, GetRetainCount(p));
    @autoreleasepool {
      EXPECT_EQ(p.get(), objects[0].get());
    }
    objects.push_back(base::scoped_nsobject<id>([[NSObject alloc] init]));
    @autoreleasepool {
      EXPECT_TRUE(objects[1].get());
    }
    EXPECT_EQ(1, GetRetainCount(objects[1]));
  }
  EXPECT_EQ(1, GetRetainCount(p));
}

TEST(ScopedNSObjectTestARC, ScopedNSObjectFreeFunctions) {
  base::scoped_nsobject<id> p1([[NSObject alloc] init]);
  id o1 = p1.get();
  EXPECT_TRUE(o1 == p1);
  EXPECT_FALSE(o1 != p1);
  base::scoped_nsobject<id> p2([[NSObject alloc] init]);
  EXPECT_TRUE(o1 != p2);
  EXPECT_FALSE(o1 == p2);
  id o2 = p2.get();
  swap(p1, p2);
  EXPECT_EQ(o2, p1.get());
  EXPECT_EQ(o1, p2.get());
}

}  // namespace
