// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_PROMISE_ABSTRACT_PROMISE_H_
#define BASE_TASK_PROMISE_ABSTRACT_PROMISE_H_

#include <utility>

#include "base/containers/unique_any.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/no_destructor.h"
#include "base/task/common/checked_lock.h"
#include "base/task/promise/dependent_list.h"
#include "base/thread_annotations.h"

namespace base {
class TaskRunner;

// std::variant, std::tuple and other templates can't contain void but they can
// contain the empty type Void. This is the same idea as std::monospace.
struct Void {};

// Signals that a promise doesn't resolve.  E.g. Promise<NoResolve, int>
struct NoResolve {};

// Signals that a promise doesn't reject.  E.g. Promise<int, NoReject>
struct NoReject {};

// A promise for either |ResolveType| if successful or |RejectType| on error.
template <typename ResolveType, typename RejectType>
class Promise;

// This enum is used to configure AbstractPromise's uncaught reject detection.
// Usually not catching a reject reason is a coding error, but at times that can
// become onerous. When that happens kCatchNotRequired should be used.
enum class RejectPolicy {
  kMustCatchRejection,
  kCatchNotRequired,
};

// Internally Resolved<> is used to store the result of a promise callback that
// resolved. This lets us disambiguate promises with the same resolve and reject
// type.
template <typename T>
struct Resolved {
  using Type = T;

  static_assert(!std::is_same<T, NoReject>::value,
                "Can't have Resolved<NoReject>");

  Resolved() {
    static_assert(!std::is_same<T, NoResolve>::value,
                  "Can't have Resolved<NoResolve>");
  }

  template <typename... Args>
  Resolved(Args&&... args) noexcept : value(std::forward<Args>(args)...) {}

  T value;
};

template <>
struct Resolved<void> {
  using Type = void;
  Void value;
};

// Internally Rejected<> is used to store the result of a promise callback that
// rejected. This lets us disambiguate promises with the same resolve and reject
// type.
template <typename T>
struct Rejected {
  using Type = T;
  T value;

  static_assert(!std::is_same<T, NoResolve>::value,
                "Can't have Rejected<NoResolve>");

  Rejected() {
    static_assert(!std::is_same<T, NoReject>::value,
                  "Can't have Rejected<NoReject>");
  }

  template <typename... Args>
  Rejected(Args&&... args) noexcept : value(std::forward<Args>(args)...) {
    static_assert(!std::is_same<T, NoReject>::value,
                  "Can't have Rejected<NoReject>");
  }
};

template <>
struct Rejected<void> {
  using Type = void;
  Void value;
};

namespace internal {

// Internal promise representation, maintains a graph of dependencies and posts
// promises as they become ready. In debug builds various sanity checks are
// performed to catch common errors such as double move or forgetting to catch a
// potential reject (NB this last check can be turned off with
// RejectPolicy::kCatchNotRequired).
class BASE_EXPORT AbstractPromise
    : public RefCountedThreadSafe<AbstractPromise> {
 public:
  struct AdjacencyList;

  template <typename ConstructType, typename DerivedExecutorType>
  struct ConstructWith {};

  template <typename ConstructType,
            typename DerivedExecutorType,
            typename... ExecutorArgs>
  static scoped_refptr<AbstractPromise> Create(
      scoped_refptr<TaskRunner>&& task_runner,
      const Location& from_here,
      std::unique_ptr<AdjacencyList> prerequisites,
      RejectPolicy reject_policy,
      ConstructWith<ConstructType, DerivedExecutorType> tag,
      ExecutorArgs&&... executor_args) {
    scoped_refptr<AbstractPromise> promise = subtle::AdoptRefIfNeeded(
        new internal::AbstractPromise(
            std::move(task_runner), from_here, std::move(prerequisites),
            reject_policy, tag, std::forward<ExecutorArgs>(executor_args)...),
        AbstractPromise::kRefCountPreference);
    // It's important this is called after |promise| has been initialized
    // because otherwise it could trigger a scoped_refptr destructor on another
    // thread before this thread has had a chance to increment the refcount.
    promise->AddAsDependentForAllPrerequisites();
    return promise;
  }

  AbstractPromise(const AbstractPromise&) = delete;
  AbstractPromise& operator=(const AbstractPromise&) = delete;

  const Location& from_here() const { return from_here_; }

  bool IsCanceled() const;
  bool IsRejected() const { return dependents_.IsRejected(); }
  bool IsResolved() const { return dependents_.IsResolved(); }
  bool IsSettled() const { return dependents_.IsSettled(); }

  bool IsResolvedWithPromise() const {
    return value_.type() ==
           TypeId::From<scoped_refptr<internal::AbstractPromise>>();
  }

  const unique_any& value() const { return FindNonCurriedAncestor()->value_; }

  class ValueHandle {
   public:
    unique_any& value() { return value_; }

    ~ValueHandle() { value_.reset(); }

   private:
    friend class AbstractPromise;

    explicit ValueHandle(unique_any& value) : value_(value) {}

    unique_any& value_;
  };

  ValueHandle TakeValue() {
    AbstractPromise* non_curried_ancestor = FindNonCurriedAncestor();
    DCHECK(non_curried_ancestor->value_.has_value());
    return ValueHandle(non_curried_ancestor->value_);
  }

  // If this promise isn't curried, returns this. Otherwise follows the chain of
  // currying until a non-curried promise is found.
  const AbstractPromise* FindNonCurriedAncestor() const;

  AbstractPromise* FindNonCurriedAncestor() {
    return const_cast<AbstractPromise*>(
        const_cast<const AbstractPromise*>(this)->FindNonCurriedAncestor());
  }

  // Sets the |value_| to |t|. The caller should call OnResolved() or
  // OnRejected() afterwards.
  template <typename T>
  void emplace(T&& t) {
    DCHECK(GetExecutor() != nullptr) << "Only valid to emplace once";
    value_ = std::forward<T>(t);
  }

  template <typename T, typename... Args>
  void emplace(in_place_type_t<T>, Args&&... args) {
    DCHECK(GetExecutor() != nullptr) << "Only valid to emplace once";
    value_.emplace<T>(std::forward<Args>(args)...);
  }

  // Unresolved promises have an executor which invokes one of the callbacks
  // associated with the promise. Once the callback has been invoked the
  // Executor is destroyed.
  //
  // Ideally Executor would be a pure virtual class, but we want to store these
  // inline to reduce the number of memory allocations (small object
  // optimization). The problem is even though placement new returns the same
  // address it was allocated at, you have to use the returned pointer.  Casting
  // the buffer to the derived class is undefined behavior. STL implementations
  // usually store an extra pointer, but there we have opted for implementing
  // our own VTable to save a little bit of memory.
  class BASE_EXPORT Executor {
   public:
    // Constructs |Derived| in place.
    template <typename Derived, typename... Args>
    explicit Executor(in_place_type_t<Derived>, Args&&... args) {
      static_assert(sizeof(Derived) <= MaxSize, "Derived is too big");
      static_assert(sizeof(Executor) <= sizeof(AnyInternal::InlineAlloc),
                    "Executor is too big");
      vtable_ = &VTableHelper<Derived>::vtable_;
      new (storage_) Derived(std::forward<Args>(args)...);
    }

    ~Executor();

    // Controls whether or not a promise should wait for its prerequisites
    // before becoming eligible for execution.
    enum class PrerequisitePolicy : uint8_t {
      // Wait for all prerequisites to resolve (or any to reject) before
      // becoming eligible for execution. If any prerequisites are canceled it
      // will be canceled too.
      kAll,

      // Wait for any prerequisite to resolve or reject before becoming eligible
      // for execution. If all prerequisites are canceled it will be canceled
      // too.
      kAny,

      // Never become eligible for execution. Cancellation is ignored.
      kNever,
    };

    // Returns the associated PrerequisitePolicy.
    PrerequisitePolicy GetPrerequisitePolicy() const;

    // NB if there is both a resolve and a reject executor we require them to
    // be both canceled at the same time.
    bool IsCancelled() const;

    // Describes an executor callback.
    enum class ArgumentPassingType : uint8_t {
      // No callback. E.g. the RejectArgumentPassingType in a promise with a
      // resolve callback but no reject callback.
      kNoCallback,

      // Executor callback argument passed by value or by reference.
      kNormal,

      // Executor callback argument passed by r-value reference.
      kMove,
    };

#if DCHECK_IS_ON()
    // Returns details of the resolve and reject executor callbacks if any. This
    // data is used to diagnose double moves and missing catches.
    ArgumentPassingType ResolveArgumentPassingType() const;
    ArgumentPassingType RejectArgumentPassingType() const;
    bool CanResolve() const;
    bool CanReject() const;
#endif

    // Invokes the associate callback for |promise|. If the callback was
    // cancelled it should call |promise->OnCanceled()|. If the callback
    // resolved it should store the resolve result via |promise->emplace()| and
    // call |promise->OnResolved()|. If the callback was rejected it should
    // store the reject result in |promise->state()| and call
    // |promise->OnResolved()|.
    // Caution the Executor will be destructed when |promise->state()| is
    // written to.
    void Execute(AbstractPromise* promise);

   private:
    static constexpr size_t MaxSize = sizeof(void*) * 2;

    struct VTable {
      void (*destructor)(void* self);
      PrerequisitePolicy (*get_prerequsite_policy)(const void* self);
      bool (*is_cancelled)(const void* self);
#if DCHECK_IS_ON()
      ArgumentPassingType (*resolve_argument_passing_type)(const void* self);
      ArgumentPassingType (*reject_argument_passing_type)(const void* self);
      bool (*can_resolve)(const void* self);
      bool (*can_reject)(const void* self);
#endif
      void (*execute)(void* self, AbstractPromise* promise);

     private:
      DISALLOW_COPY_AND_ASSIGN(VTable);
    };

    template <typename DerivedType>
    struct VTableHelper {
      VTableHelper(const VTableHelper& other) = delete;
      VTableHelper& operator=(const VTableHelper& other) = delete;

      static void Destructor(void* self) {
        static_cast<DerivedType*>(self)->~DerivedType();
      }

      static PrerequisitePolicy GetPrerequisitePolicy(const void* self) {
        return static_cast<const DerivedType*>(self)->GetPrerequisitePolicy();
      }

      static bool IsCancelled(const void* self) {
        return static_cast<const DerivedType*>(self)->IsCancelled();
      }

#if DCHECK_IS_ON()
      static ArgumentPassingType ResolveArgumentPassingType(const void* self) {
        return static_cast<const DerivedType*>(self)
            ->ResolveArgumentPassingType();
      }

      static ArgumentPassingType RejectArgumentPassingType(const void* self) {
        return static_cast<const DerivedType*>(self)
            ->RejectArgumentPassingType();
      }

      static bool CanResolve(const void* self) {
        return static_cast<const DerivedType*>(self)->CanResolve();
      }

      static bool CanReject(const void* self) {
        return static_cast<const DerivedType*>(self)->CanReject();
      }
#endif

      static void Execute(void* self, AbstractPromise* promise) {
        return static_cast<DerivedType*>(self)->Execute(promise);
      }

      static constexpr VTable vtable_ = {
        &VTableHelper::Destructor,
        &VTableHelper::GetPrerequisitePolicy,
        &VTableHelper::IsCancelled,
#if DCHECK_IS_ON()
        &VTableHelper::ResolveArgumentPassingType,
        &VTableHelper::RejectArgumentPassingType,
        &VTableHelper::CanResolve,
        &VTableHelper::CanReject,
#endif
        &VTableHelper::Execute,
      };
    };

    const VTable* vtable_;
    char storage_[MaxSize];
  };

  // Signals that this promise was cancelled. If executor hasn't run yet, this
  // will prevent it from running and cancels any dependent promises unless they
  // have PrerequisitePolicy::kAny, in which case they will only be canceled if
  // all of their prerequisites are canceled. If OnCanceled() or OnResolved() or
  // OnRejected() has already run, this does nothing.
  void OnCanceled();

  // Signals that |value_| now contains a resolve value. Dependent promises may
  // scheduled for execution.
  void OnResolved();

  // Signals that |value_| now contains a reject value. Dependent promises may
  // scheduled for execution.
  void OnRejected();

  struct BASE_EXPORT AdjacencyListNode {
    AdjacencyListNode();
    explicit AdjacencyListNode(scoped_refptr<AbstractPromise> prerequisite);
    explicit AdjacencyListNode(AdjacencyListNode&& other) noexcept;
    ~AdjacencyListNode();

    scoped_refptr<AbstractPromise> prerequisite;
    DependentList::Node dependent_node;
  };

  // This is separate from AbstractPromise to reduce the memory footprint of
  // regular PostTask without promise chains.
  struct BASE_EXPORT AdjacencyList {
    AdjacencyList();
    explicit AdjacencyList(scoped_refptr<AbstractPromise> prerequisite);
    explicit AdjacencyList(std::vector<AdjacencyListNode> prerequisite_list);
    ~AdjacencyList();

    void ResetWithSingleDependency(scoped_refptr<AbstractPromise> prerequisite);

    bool DecrementPrerequisiteCountAndCheckIfZero();

    std::vector<AdjacencyListNode> prerequisite_list;

    // PrerequisitePolicy::kAny waits for at most 1 resolve or N cancellations.
    // PrerequisitePolicy::kAll waits for N resolves or at most 1 cancellation.
    // PrerequisitePolicy::kNever doesn't use this.
    std::atomic_int action_prerequisite_count;

    // Stores the address of the first rejecting promise. The purpose of this is
    // two-fold, first to ensure that Promises::All/Race return the first
    // prerequisite that rejected and secondly to prevent the executor from
    // being run multiple times if there's multiple rejection.
    std::atomic<uintptr_t> first_rejecting_promise{0};
  };

  const std::vector<AdjacencyListNode>* prerequisite_list() const {
    if (!prerequisites_)
      return nullptr;
    return &prerequisites_->prerequisite_list;
  }

  // Returns the first and only prerequisite AbstractPromise.  It's an error to
  // call this if the number of prerequisites isn't exactly one.
  AbstractPromise* GetOnlyPrerequisite() const {
    DCHECK(prerequisites_);
    DCHECK_EQ(prerequisites_->prerequisite_list.size(), 1u);
    return prerequisites_->prerequisite_list[0].prerequisite.get();
  }

  AbstractPromise* GetFirstRejectedPrerequisite() const;

  // Calls |RunExecutor()| or posts a task to do so if |from_here_| is not
  // nullopt.
  void Execute();

  void IgnoreUncaughtCatchForTesting();

 private:
  friend base::RefCountedThreadSafe<AbstractPromise>;

  template <typename ConstructType,
            typename DerivedExecutorType,
            typename... ExecutorArgs>
  AbstractPromise(scoped_refptr<TaskRunner>&& task_runner,
                  const Location& from_here,
                  std::unique_ptr<AdjacencyList> prerequisites,
                  RejectPolicy reject_policy,
                  ConstructWith<ConstructType, DerivedExecutorType>,
                  ExecutorArgs&&... executor_args) noexcept
      : task_runner_(std::move(task_runner)),
        from_here_(std::move(from_here)),
        value_(in_place_type_t<Executor>(),
               in_place_type_t<DerivedExecutorType>(),
               std::forward<ExecutorArgs>(executor_args)...),
#if DCHECK_IS_ON()
        reject_policy_(reject_policy),
        resolve_argument_passing_type_(
            GetExecutor()->ResolveArgumentPassingType()),
        reject_argument_passing_type_(
            GetExecutor()->RejectArgumentPassingType()),
        executor_can_resolve_(GetExecutor()->CanResolve()),
        executor_can_reject_(GetExecutor()->CanReject()),
#endif
        dependents_(ConstructType()),
        prerequisites_(std::move(prerequisites)) {
#if DCHECK_IS_ON()
    {
      CheckedAutoLock lock(GetCheckedLock());
      if (executor_can_resolve_) {
        this_resolve_ =
            MakeRefCounted<DoubleMoveDetector>(from_here_, "resolve");
      }

      if (executor_can_reject_) {
        this_reject_ = MakeRefCounted<DoubleMoveDetector>(from_here_, "reject");

        if (reject_policy_ == RejectPolicy::kMustCatchRejection) {
          this_must_catch_ = MakeRefCounted<LocationRef>(from_here_);
        }
      }
    }
#endif
  }

  NOINLINE ~AbstractPromise();

  // Returns the associated Executor if there is one.
  const Executor* GetExecutor() const;

  Executor* GetExecutor() {
    return const_cast<Executor*>(
        const_cast<const AbstractPromise*>(this)->GetExecutor());
  }

  // With the exception of curried promises, this may only be called before the
  // executor has run.
  Executor::PrerequisitePolicy GetPrerequisitePolicy();

  void AddAsDependentForAllPrerequisites();

  // If the promise hasn't executed then |node| is added to the list. If it has
  // and it was resolved or rejected then the corresponding promise is scheduled
  // for execution if necessary. If this promise was canceled this is a NOP.
  // Returns false if this operation failed because this promise became canceled
  // as a result of adding a dependency on a canceled |node|.
  bool InsertDependentOnAnyThread(DependentList::Node* node);

  // Checks if the promise is now ready to be executed and if so posts it on the
  // given task runner.
  void OnPrerequisiteResolved();

  // Schedules the promise for execution.
  void OnPrerequisiteRejected(AbstractPromise* rejected_promise);

  // Returns true if we are still potentially eligible to run despite the
  // cancellation.
  bool OnPrerequisiteCancelled();

  // This promise was resolved, post any dependent promises that are now ready
  // as a result.
  void OnResolveDispatchReadyDependents();

  // This promise was rejected, post any dependent promises that are now ready
  // as a result.
  void OnRejectDispatchReadyDependents();

  void DispatchPromise();

  // Reverses |list| so dependents can be dispatched in the order they where
  // added. Assumes no other thread is accessing |list|.
  static DependentList::Node* NonThreadSafeReverseList(
      DependentList::Node* list);

  // Finds the non-curried root, and if settled ready dependents are posted.
  // Returns true if the non-curried root was settled.
  bool DispatchIfNonCurriedRootSettled();

  scoped_refptr<TaskRunner> task_runner_;

  const Location from_here_;

  // To save memory |value_| contains Executor (which is stored inline) before
  // it has run and afterwards it contains one of:
  // * Resolved<T>
  // * Rejected<T>
  // * scoped_refptr<AbstractPromise> (for curried promises - i.e. a promise
  //   which is resolved with a promise).
  unique_any value_;

#if DCHECK_IS_ON()
  void MaybeInheritChecks(AbstractPromise* source)
      EXCLUSIVE_LOCKS_REQUIRED(GetCheckedLock());

  // Controls how we deal with unhandled rejection.
  const RejectPolicy reject_policy_;

  // Cached because we need to access these values after the Executor they came
  // from has gone away.
  const Executor::ArgumentPassingType resolve_argument_passing_type_;
  const Executor::ArgumentPassingType reject_argument_passing_type_;
  const bool executor_can_resolve_;
  const bool executor_can_reject_;

  // Whether responsibility for catching rejected promise has been passed on to
  // this promise's dependents.
  bool passed_catch_responsibility_ GUARDED_BY(GetCheckedLock()) = false;

  static CheckedLock& GetCheckedLock();

  // Used to avoid refcounting cycles.
  class BASE_EXPORT LocationRef : public RefCountedThreadSafe<LocationRef> {
   public:
    explicit LocationRef(const Location& from_here);

    const Location& from_here() const { return from_here_; }

   private:
    Location from_here_;

    friend class RefCountedThreadSafe<LocationRef>;
    ~LocationRef();
  };

  // For catching missing catches.
  scoped_refptr<LocationRef> must_catch_ancestor_that_could_reject_
      GUARDED_BY(GetCheckedLock());

  // Used to supply all child nodes with a single LocationRef.
  scoped_refptr<LocationRef> this_must_catch_ GUARDED_BY(GetCheckedLock());

  class BASE_EXPORT DoubleMoveDetector
      : public RefCountedThreadSafe<DoubleMoveDetector> {
   public:
    DoubleMoveDetector(const Location& from_here, const char* callback_type);

    void CheckForDoubleMoveErrors(
        const base::Location& new_dependent_location,
        Executor::ArgumentPassingType new_dependent_executor_type);

   private:
    const Location from_here_;
    const char* callback_type_;
    std::unique_ptr<Location> dependent_move_only_promise_;
    std::unique_ptr<Location> dependent_normal_promise_;

    friend class RefCountedThreadSafe<DoubleMoveDetector>;
    ~DoubleMoveDetector();
  };

  // Used to supply all child nodes with a single DoubleMoveDetector.
  scoped_refptr<DoubleMoveDetector> this_resolve_ GUARDED_BY(GetCheckedLock());

  // Used to supply all child nodes with a single DoubleMoveDetector.
  scoped_refptr<DoubleMoveDetector> this_reject_ GUARDED_BY(GetCheckedLock());

  // Validates that the value of this promise, or the value of the closest
  // ancestor that can resolve if this promise can't resolve, is not
  // double-moved.
  scoped_refptr<DoubleMoveDetector> ancestor_that_could_resolve_
      GUARDED_BY(GetCheckedLock());

  // Validates that the value of this promise, or the value of the closest
  // ancestor that can reject if this promise can't reject, is not
  // double-moved.
  scoped_refptr<DoubleMoveDetector> ancestor_that_could_reject_
      GUARDED_BY(GetCheckedLock());
#endif

  // List of promises which are dependent on this one.
  DependentList dependents_;

  // Details of any promises this promise is dependent on. If there are none
  // |prerequisites_| will be null. This is a space optimization for the common
  // case of a non-chained PostTask.
  std::unique_ptr<AdjacencyList> prerequisites_;
};

// static
template <typename T>
const AbstractPromise::Executor::VTable
    AbstractPromise::Executor::VTableHelper<T>::vtable_;

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_PROMISE_ABSTRACT_PROMISE_H_
