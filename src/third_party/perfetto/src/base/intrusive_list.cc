/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/base/intrusive_list.h"

#include "perfetto/base/logging.h"

namespace perfetto::base::internal {

void ListOps::PushFront(IntrusiveListNode* node) {
  node->prev = nullptr;
  node->next = front_;

  if (front_) {
    front_->prev = node;
  }

  front_ = node;
  ++size_;
}

void ListOps::PopFront() {
  PERFETTO_DCHECK(front_);
  front_ = front_->next;

  if (front_) {
    front_->prev = nullptr;
  }

  PERFETTO_DCHECK(size_ > 0);
  --size_;
}

void ListOps::Erase(IntrusiveListNode* node) {
  auto* prev = node->prev;
  auto* next = node->next;

  if (node == front_) {
    front_ = next;
  }

  if (prev) {
    prev->next = next;
  }

  if (next) {
    next->prev = prev;
  }

  PERFETTO_DCHECK(size_ > 0);
  --size_;
}

}  // namespace perfetto::base::internal
