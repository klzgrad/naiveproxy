/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "perfetto/ext/base/crash_keys.h"

#include <string.h>

#include <atomic>
#include <cinttypes>

#include "perfetto/ext/base/string_utils.h"

namespace perfetto {
namespace base {

namespace {

constexpr size_t kMaxKeys = 32;

std::atomic<CrashKey*> g_keys[kMaxKeys]{};
std::atomic<uint32_t> g_num_keys{};
}  // namespace

void CrashKey::Register() {
  // If doesn't matter if we fail below. If there are no slots left, don't
  // keep trying re-registering on every Set(), the outcome won't change.

  // If two threads raced on the Register(), avoid registering the key twice.
  if (registered_.exchange(true))
    return;

  uint32_t slot = g_num_keys.fetch_add(1);
  if (slot >= kMaxKeys) {
    PERFETTO_LOG("Too many crash keys registered");
    return;
  }
  g_keys[slot].store(this);
}

// Returns the number of chars written, without counting the \0.
size_t CrashKey::ToString(char* dst, size_t len) {
  if (len > 0)
    *dst = '\0';
  switch (type_.load(std::memory_order_relaxed)) {
    case Type::kUnset:
      break;
    case Type::kInt:
      return SprintfTrunc(dst, len, "%s: %" PRId64 "\n", name_,
                          int_value_.load(std::memory_order_relaxed));
    case Type::kStr:
      char buf[sizeof(str_value_)];
      for (size_t i = 0; i < sizeof(str_value_); i++)
        buf[i] = str_value_[i].load(std::memory_order_relaxed);

      // Don't assume |str_value_| is properly null-terminated.
      return SprintfTrunc(dst, len, "%s: %.*s\n", name_, int(sizeof(buf)), buf);
  }
  return 0;
}

void UnregisterAllCrashKeysForTesting() {
  g_num_keys.store(0);
  for (auto& key : g_keys)
    key.store(nullptr);
}

size_t SerializeCrashKeys(char* dst, size_t len) {
  size_t written = 0;
  uint32_t num_keys = g_num_keys.load();
  if (len > 0)
    *dst = '\0';
  for (uint32_t i = 0; i < num_keys && written < len; i++) {
    CrashKey* key = g_keys[i].load();
    if (!key)
      continue;  // Can happen if we hit this between the add and the store.
    written += key->ToString(dst + written, len - written);
  }
  PERFETTO_DCHECK(written <= len);
  PERFETTO_DCHECK(len == 0 || dst[written] == '\0');
  return written;
}

}  // namespace base
}  // namespace perfetto
