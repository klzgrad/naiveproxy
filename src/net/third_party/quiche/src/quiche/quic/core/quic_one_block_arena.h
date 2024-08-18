// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// An arena that consists of a single inlined block of |ArenaSize|. Useful to
// avoid repeated calls to malloc/new and to improve memory locality.
// QUICHE_DCHECK's if an allocation out of the arena ever fails in debug builds;
// falls back to heap allocation in release builds.

#ifndef QUICHE_QUIC_CORE_QUIC_ONE_BLOCK_ARENA_H_
#define QUICHE_QUIC_CORE_QUIC_ONE_BLOCK_ARENA_H_

#include <cstdint>

#include "absl/base/optimization.h"
#include "quiche/quic/core/quic_arena_scoped_ptr.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

template <uint32_t ArenaSize>
class QUICHE_EXPORT QuicOneBlockArena {
  static const uint32_t kMaxAlign = 8;

 public:
  QuicOneBlockArena() : offset_(0) {}
  QuicOneBlockArena(const QuicOneBlockArena&) = delete;
  QuicOneBlockArena& operator=(const QuicOneBlockArena&) = delete;

  // Instantiates an object of type |T| with |args|. |args| are perfectly
  // forwarded to |T|'s constructor. The returned pointer's lifetime is
  // controlled by QuicArenaScopedPtr.
  template <typename T, typename... Args>
  QuicArenaScopedPtr<T> New(Args&&... args) {
    QUICHE_DCHECK_LT(AlignedSize<T>(), ArenaSize)
        << "Object is too large for the arena.";
    static_assert(alignof(T) > 1,
                  "Objects added to the arena must be at least 2B aligned.");
    if (ABSL_PREDICT_FALSE(offset_ > ArenaSize - AlignedSize<T>())) {
      QUIC_BUG(quic_bug_10593_1)
          << "Ran out of space in QuicOneBlockArena at " << this
          << ", max size was " << ArenaSize << ", failing request was "
          << AlignedSize<T>() << ", end of arena was " << offset_;
      return QuicArenaScopedPtr<T>(new T(std::forward<Args>(args)...));
    }

    void* buf = &storage_[offset_];
    new (buf) T(std::forward<Args>(args)...);
    offset_ += AlignedSize<T>();
    return QuicArenaScopedPtr<T>(buf,
                                 QuicArenaScopedPtr<T>::ConstructFrom::kArena);
  }

 private:
  // Returns the size of |T| aligned up to |kMaxAlign|.
  template <typename T>
  static inline uint32_t AlignedSize() {
    return ((sizeof(T) + (kMaxAlign - 1)) / kMaxAlign) * kMaxAlign;
  }

  // Actual storage.
  // Subtle/annoying: the value '8' must be coded explicitly into the alignment
  // declaration for MSVC.
  alignas(8) char storage_[ArenaSize];
  // Current offset into the storage.
  uint32_t offset_;
};

// QuicConnections currently use around 1KB of polymorphic types which would
// ordinarily be on the heap. Instead, store them inline in an arena.
using QuicConnectionArena = QuicOneBlockArena<1380>;

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_ONE_BLOCK_ARENA_H_
