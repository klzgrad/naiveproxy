// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/callback_internal.h"

#include "base/logging.h"

namespace base {
namespace internal {

namespace {

bool ReturnFalse(const BindStateBase*) {
  return false;
}

}  // namespace

void BindStateBaseRefCountTraits::Destruct(const BindStateBase* bind_state) {
  bind_state->destructor_(bind_state);
}

BindStateBase::BindStateBase(InvokeFuncStorage polymorphic_invoke,
                             void (*destructor)(const BindStateBase*))
    : BindStateBase(polymorphic_invoke, destructor, &ReturnFalse) {
}

BindStateBase::BindStateBase(InvokeFuncStorage polymorphic_invoke,
                             void (*destructor)(const BindStateBase*),
                             bool (*is_cancelled)(const BindStateBase*))
    : polymorphic_invoke_(polymorphic_invoke),
      destructor_(destructor),
      is_cancelled_(is_cancelled) {}

CallbackBase::CallbackBase(CallbackBase&& c) = default;
CallbackBase& CallbackBase::operator=(CallbackBase&& c) = default;
CallbackBase::CallbackBase(const CallbackBaseCopyable& c)
    : bind_state_(c.bind_state_) {}

CallbackBase& CallbackBase::operator=(const CallbackBaseCopyable& c) {
  bind_state_ = c.bind_state_;
  return *this;
}

CallbackBase::CallbackBase(CallbackBaseCopyable&& c)
    : bind_state_(std::move(c.bind_state_)) {}

CallbackBase& CallbackBase::operator=(CallbackBaseCopyable&& c) {
  bind_state_ = std::move(c.bind_state_);
  return *this;
}

void CallbackBase::Reset() {
  // NULL the bind_state_ last, since it may be holding the last ref to whatever
  // object owns us, and we may be deleted after that.
  bind_state_ = nullptr;
}

bool CallbackBase::IsCancelled() const {
  DCHECK(bind_state_);
  return bind_state_->IsCancelled();
}

bool CallbackBase::EqualsInternal(const CallbackBase& other) const {
  return bind_state_ == other.bind_state_;
}

CallbackBase::CallbackBase(BindStateBase* bind_state)
    : bind_state_(bind_state ? AdoptRef(bind_state) : nullptr) {
  DCHECK(!bind_state_.get() || bind_state_->HasOneRef());
}

CallbackBase::~CallbackBase() = default;

CallbackBaseCopyable::CallbackBaseCopyable(const CallbackBaseCopyable& c)
    : CallbackBase(nullptr) {
  bind_state_ = c.bind_state_;
}

CallbackBaseCopyable::CallbackBaseCopyable(CallbackBaseCopyable&& c) = default;

CallbackBaseCopyable& CallbackBaseCopyable::operator=(
    const CallbackBaseCopyable& c) {
  bind_state_ = c.bind_state_;
  return *this;
}

CallbackBaseCopyable& CallbackBaseCopyable::operator=(
    CallbackBaseCopyable&& c) = default;

}  // namespace internal
}  // namespace base
