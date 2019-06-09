// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_PROMISE_ALL_TUPLE_EXECUTOR_H_
#define BASE_TASK_PROMISE_ALL_TUPLE_EXECUTOR_H_

#include <tuple>

#include "base/task/promise/abstract_promise.h"
#include "base/task/promise/helpers.h"

namespace base {
namespace internal {

template <typename Tuple,
          typename Indices =
              std::make_index_sequence<std::tuple_size<Tuple>::value>>
struct TupleConstructor;

template <typename Tuple, size_t... Indices>
struct TupleConstructor<Tuple, std::index_sequence<Indices...>> {
  template <typename ArgType>
  static auto GetResolvedValueFromPromise(AbstractPromise* arg) {
    using ResolvedType = base::Resolved<ArgType>;
    return ArgMoveSemanticsHelper<ArgType, ResolvedType>::Get(arg);
  }

  // Resolves |result| with a std::tuple of the promise results of the dependent
  // promises.
  static void ConstructTuple(
      const std::vector<AbstractPromise::AdjacencyListNode>* prerequisite_list,
      AbstractPromise* result) {
    DCHECK_EQ(sizeof...(Indices), prerequisite_list->size());
    result->emplace(
        in_place_type_t<Resolved<Tuple>>(),
        GetResolvedValueFromPromise<std::tuple_element_t<Indices, Tuple>>(
            (*prerequisite_list)[Indices].prerequisite.get())...);
  }
};

template <typename T>
struct TupleCanResolveHelper;

template <typename... Ts>
struct TupleCanResolveHelper<std::tuple<Ts...>> {
  static constexpr bool value =
      any_of({!std::is_same<Ts, NoResolve>::value...});
};

// For Promises::All(Promise<Ts>... promises)
template <typename ResolveTuple, typename RejectType>
class AllTuplePromiseExecutor {
 public:
  using RejectT = Rejected<RejectType>;

  bool IsCancelled() const { return false; }

  AbstractPromise::Executor::PrerequisitePolicy GetPrerequisitePolicy() const {
    return AbstractPromise::Executor::PrerequisitePolicy::kAll;
  }

  void Execute(AbstractPromise* promise) {
    // All is rejected if any prerequisites are rejected.
    if (AbstractPromise* rejected = promise->GetFirstRejectedPrerequisite()) {
      AllPromiseRejectHelper<RejectT>::Reject(promise, rejected);
      promise->OnRejected();
      return;
    }

    const std::vector<AbstractPromise::AdjacencyListNode>* prerequisite_list =
        promise->prerequisite_list();
    DCHECK(prerequisite_list);
    TupleConstructor<ResolveTuple>::ConstructTuple(prerequisite_list, promise);
    promise->OnResolved();
  }

#if DCHECK_IS_ON()
  AbstractPromise::Executor::ArgumentPassingType ResolveArgumentPassingType()
      const {
    return UseMoveSemantics<ResolveTuple>::argument_passing_type;
  }

  AbstractPromise::Executor::ArgumentPassingType RejectArgumentPassingType()
      const {
    return UseMoveSemantics<RejectType>::argument_passing_type;
  }

  bool CanResolve() const { return TupleCanResolveHelper<ResolveTuple>::value; }

  bool CanReject() const { return !std::is_same<RejectType, NoReject>::value; }
#endif
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_PROMISE_ALL_TUPLE_EXECUTOR_H_
