// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/promise/then_and_catch_executor.h"

namespace base {
namespace internal {

ThenAndCatchExecutorCommon::ThenAndCatchExecutorCommon(
    internal::CallbackBase&& resolve_executor,
    internal::CallbackBase&& reject_executor)
    : resolve_callback_(std::move(resolve_executor)),
      reject_callback_(std::move(reject_executor)) {}

ThenAndCatchExecutorCommon::~ThenAndCatchExecutorCommon() = default;

bool ThenAndCatchExecutorCommon::IsCancelled() const {
  if (!resolve_callback_.is_null()) {
    // If there is both a resolve and a reject executor they must be canceled
    // at the same time.
    DCHECK(reject_callback_.is_null() ||
           reject_callback_.IsCancelled() == resolve_callback_.IsCancelled());
    return resolve_callback_.IsCancelled();
  }
  return reject_callback_.IsCancelled();
}

AbstractPromise::Executor::PrerequisitePolicy
ThenAndCatchExecutorCommon::GetPrerequisitePolicy() const {
  return AbstractPromise::Executor::PrerequisitePolicy::kAll;
}

void ThenAndCatchExecutorCommon::Execute(AbstractPromise* promise,
                                         ExecuteCallback execute_then,
                                         ExecuteCallback execute_catch) {
  AbstractPromise* prerequisite =
      promise->GetOnlyPrerequisite()->FindNonCurriedAncestor();
  if (prerequisite->IsResolved()) {
    if (ProcessNullCallback(resolve_callback_, prerequisite, promise)) {
      promise->OnResolved();
      return;
    }

    execute_then(prerequisite, promise, this);
  } else {
    DCHECK(prerequisite->IsRejected());
    if (ProcessNullCallback(reject_callback_, prerequisite, promise)) {
      promise->OnResolved();
      return;
    }

    execute_catch(prerequisite, promise, this);
  }
}

// static
bool ThenAndCatchExecutorCommon::ProcessNullCallback(
    const CallbackBase& callback,
    AbstractPromise* arg,
    AbstractPromise* result) {
  if (callback.is_null()) {
    // A curried promise is used to forward the result through null callbacks.
    result->emplace(scoped_refptr<AbstractPromise>(arg));
    DCHECK(result->IsResolvedWithPromise());
    return true;
  }

  return false;
}

}  // namespace internal
}  // namespace base
