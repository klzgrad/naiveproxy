// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_PROMISE_DEPENDENT_LIST_H_
#define BASE_TASK_PROMISE_DEPENDENT_LIST_H_

#include <atomic>

#include "base/base_export.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/scoped_refptr.h"

namespace base {
namespace internal {

class AbstractPromise;

// AbstractPromise needs to know which promises depend upon it. This lock free
// class stores the list of dependents. This is not a general purpose list
// because the data can only be consumed once. This class' methods have implicit
// acquire/release semantics (i.e., callers can assume the result they get
// happens-after memory changes which lead to it).
class BASE_EXPORT DependentList {
 public:
  struct ConstructUnresolved {};
  struct ConstructResolved {};
  struct ConstructRejected {};

  explicit DependentList(ConstructUnresolved);
  explicit DependentList(ConstructResolved);
  explicit DependentList(ConstructRejected);
  ~DependentList();

  enum class InsertResult {
    SUCCESS,
    FAIL_PROMISE_RESOLVED,
    FAIL_PROMISE_REJECTED,
    FAIL_PROMISE_CANCELED,
  };

  struct BASE_EXPORT Node {
    Node();
    explicit Node(Node&& other) noexcept;
    ~Node();

    scoped_refptr<AbstractPromise> dependent;
    std::atomic<Node*> next{nullptr};
  };

  // Insert will only succeed if one of the Consume operations hasn't been
  // called yet. |node| must outlive DependentList, and it can't be altered
  // after Insert or the release barrier will be ineffective.
  InsertResult Insert(Node* node);

  // A ConsumeXXX function may only be called once.
  Node* ConsumeOnceForResolve();

  // A ConsumeXXX function may only be called once.
  Node* ConsumeOnceForReject();

  // A ConsumeXXX function may only be called once.
  Node* ConsumeOnceForCancel();

  bool IsSettled() const;
  bool IsResolved() const;
  bool IsRejected() const;
  bool IsCanceled() const;

 private:
  std::atomic<uintptr_t> head_;

  // Special values for |head_| which correspond to various states. If |head_|
  // contains one of these then Insert() will fail.
  static constexpr uintptr_t kResolvedSentinel = 1;
  static constexpr uintptr_t kRejectedSentinel = 2;
  static constexpr uintptr_t kCanceledSentinel = 3;

  DISALLOW_COPY_AND_ASSIGN(DependentList);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_PROMISE_DEPENDENT_LIST_H_
