/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_PROFILING_MEMORY_SCOPED_SPINLOCK_H_
#define SRC_PROFILING_MEMORY_SCOPED_SPINLOCK_H_

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/utils.h"

#include <atomic>
#include <new>
#include <utility>

namespace perfetto {
namespace profiling {

struct Spinlock {
  std::atomic<uint8_t> locked;
  std::atomic<uint8_t> poisoned;
};

static_assert(sizeof(Spinlock) == 2, "spinlock size must be ABI independent");

void PoisonSpinlock(Spinlock* lock);

class ScopedSpinlock {
 public:
  enum class Mode {
    // Try for a fixed number of attempts, then return an unlocked handle.
    Try,
    // Keep spinning until successful.
    Blocking
  };

  ScopedSpinlock(Spinlock* lock, Mode mode) : lock_(lock) {
    if (PERFETTO_UNLIKELY(lock_->poisoned.load(std::memory_order_relaxed))) {
      return;
    }
    if (PERFETTO_LIKELY(
            !lock_->locked.exchange(true, std::memory_order_acquire))) {
      locked_ = true;
      return;
    }
    LockSlow(mode);
  }

  ScopedSpinlock(const ScopedSpinlock&) = delete;
  ScopedSpinlock& operator=(const ScopedSpinlock&) = delete;

  ScopedSpinlock(ScopedSpinlock&& other) noexcept
      : lock_(other.lock_), locked_(other.locked_) {
    other.locked_ = false;
  }

  ScopedSpinlock& operator=(ScopedSpinlock&& other) {
    if (this != &other) {
      this->~ScopedSpinlock();
      new (this) ScopedSpinlock(std::move(other));
    }
    return *this;
  }

  ~ScopedSpinlock() { Unlock(); }

  void Unlock() {
    if (locked_) {
      PERFETTO_DCHECK(lock_->locked.load());
      lock_->locked.store(false, std::memory_order_release);
    }
    locked_ = false;
  }

  bool locked() const { return locked_; }
  size_t blocked_us() const { return blocked_us_; }

 private:
  void LockSlow(Mode mode);
  Spinlock* lock_;
  size_t blocked_us_ = 0;
  bool locked_ = false;
};

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_PROFILING_MEMORY_SCOPED_SPINLOCK_H_
