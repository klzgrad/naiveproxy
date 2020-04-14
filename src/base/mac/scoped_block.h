// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MAC_SCOPED_BLOCK_H_
#define BASE_MAC_SCOPED_BLOCK_H_

#include <Block.h>

#include "base/mac/scoped_typeref.h"

#if defined(__has_feature) && __has_feature(objc_arc)
#define BASE_MAC_BRIDGE_CAST(TYPE, VALUE) (__bridge TYPE)(VALUE)
#else
#define BASE_MAC_BRIDGE_CAST(TYPE, VALUE) VALUE
#endif

namespace base {
namespace mac {

namespace internal {

template <typename B>
struct ScopedBlockTraits {
  static B InvalidValue() { return nullptr; }
  static B Retain(B block) {
    return BASE_MAC_BRIDGE_CAST(
        B, Block_copy(BASE_MAC_BRIDGE_CAST(const void*, block)));
  }
  static void Release(B block) {
    Block_release(BASE_MAC_BRIDGE_CAST(const void*, block));
  }
};

}  // namespace internal

// ScopedBlock<> is patterned after ScopedCFTypeRef<>, but uses Block_copy() and
// Block_release() instead of CFRetain() and CFRelease().
template <typename B>
class ScopedBlock : public ScopedTypeRef<B, internal::ScopedBlockTraits<B>> {
 public:
  using Traits = internal::ScopedBlockTraits<B>;

#if !defined(__has_feature) || !__has_feature(objc_arc)
  explicit ScopedBlock(
      B block = Traits::InvalidValue(),
      base::scoped_policy::OwnershipPolicy policy = base::scoped_policy::ASSUME)
      : ScopedTypeRef<B, Traits>(block, policy) {}
#else
  explicit ScopedBlock(B block = Traits::InvalidValue())
      : ScopedTypeRef<B, Traits>(block, base::scoped_policy::RETAIN) {}
#endif

#if !defined(__has_feature) || !__has_feature(objc_arc)
  void reset(B block = Traits::InvalidValue(),
             base::scoped_policy::OwnershipPolicy policy =
                 base::scoped_policy::ASSUME) {
    ScopedTypeRef<B, Traits>::reset(block, policy);
  }
#else
  void reset(B block = Traits::InvalidValue()) {
    ScopedTypeRef<B, Traits>::reset(block, base::scoped_policy::RETAIN);
  }
#endif
};

}  // namespace mac
}  // namespace base

#undef BASE_MAC_BRIDGE_CAST

#endif  // BASE_MAC_SCOPED_BLOCK_H_
