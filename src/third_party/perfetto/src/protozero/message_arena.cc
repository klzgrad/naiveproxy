/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "perfetto/protozero/message_arena.h"

#include <atomic>
#include <type_traits>

#include "perfetto/base/logging.h"
#include "perfetto/protozero/message_handle.h"

namespace protozero {

MessageArena::MessageArena() {
  // The code below assumes that there is always at least one block.
  blocks_.emplace_front();
}

MessageArena::~MessageArena() = default;

Message* MessageArena::NewMessage() {
  PERFETTO_DCHECK(!blocks_.empty());  // Should never become empty.

  Block* block = &blocks_.front();
  if (PERFETTO_UNLIKELY(block->entries >= Block::kCapacity)) {
    blocks_.emplace_front();
    block = &blocks_.front();
  }
  const auto idx = block->entries++;
  void* storage = block->storage[idx];
  PERFETTO_ASAN_UNPOISON(storage, sizeof(Message));
  return new (storage) Message();
}

void MessageArena::DeleteLastMessageInternal() {
  PERFETTO_DCHECK(!blocks_.empty());  // Should never be empty, see below.
  Block* block = &blocks_.front();
  PERFETTO_DCHECK(block->entries > 0);

  // This is the reason why there is no ~Message() call here.
  // MessageArea::Reset() (see header) also relies on dtor being trivial.
  static_assert(std::is_trivially_destructible<Message>::value,
                "Message must be trivially destructible");

  --block->entries;
  PERFETTO_ASAN_POISON(&block->storage[block->entries], sizeof(Message));

  // Don't remove the first block to avoid malloc/free calls when the root
  // message is reset. Hitting the allocator all the times is a waste of time.
  if (block->entries == 0 && std::next(blocks_.cbegin()) != blocks_.cend()) {
    blocks_.pop_front();
  }
}

}  // namespace protozero
