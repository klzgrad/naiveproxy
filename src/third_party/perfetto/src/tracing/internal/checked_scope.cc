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

#include "perfetto/tracing/internal/checked_scope.h"

#include <utility>

namespace perfetto {
namespace internal {

#if PERFETTO_DCHECK_IS_ON()
CheckedScope::CheckedScope(CheckedScope* parent_scope)
    : parent_scope_(parent_scope) {
  if (parent_scope_) {
    PERFETTO_DCHECK(parent_scope_->is_active());
    parent_scope_->set_is_active(false);
  }
}

CheckedScope::~CheckedScope() {
  Reset();
}

void CheckedScope::Reset() {
  if (!is_active_) {
    // The only case when inactive scope could be destroyed is when Reset() was
    // called explicitly or the contents of the object were moved away.
    PERFETTO_DCHECK(deleted_);
    return;
  }
  is_active_ = false;
  deleted_ = true;
  if (parent_scope_)
    parent_scope_->set_is_active(true);
}

CheckedScope::CheckedScope(CheckedScope&& other) {
  *this = std::move(other);
}

CheckedScope& CheckedScope::operator=(CheckedScope&& other) {
  is_active_ = other.is_active_;
  parent_scope_ = other.parent_scope_;
  deleted_ = other.deleted_;

  other.is_active_ = false;
  other.parent_scope_ = nullptr;
  other.deleted_ = true;

  return *this;
}
#endif

}  // namespace internal
}  // namespace perfetto
