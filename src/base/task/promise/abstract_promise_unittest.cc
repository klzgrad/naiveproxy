// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/promise/abstract_promise.h"

#include "base/test/bind_test_util.h"
#include "base/test/do_nothing_promise.h"
#include "base/test/gtest_util.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/thread.h"
#include "base/threading/thread_task_runner_handle.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

// Even trivial DCHECK_DEATH_TESTs like
// AbstractPromiseTest.CantRejectIfpromiseDeclaredAsNonRejecting can flakily
// timeout on the chromeos bots.
#if defined(OS_CHROMEOS)
#define ABSTRACT_PROMISE_DEATH_TEST(test_name) DISABLED_##test_name
#else
#define ABSTRACT_PROMISE_DEATH_TEST(test_name) test_name
#endif

using testing::ElementsAre;

using ArgumentPassingType =
    base::internal::AbstractPromise::Executor::ArgumentPassingType;

using PrerequisitePolicy =
    base::internal::AbstractPromise::Executor::PrerequisitePolicy;

namespace base {
namespace internal {

class TestExecutor {
 public:
  TestExecutor(PrerequisitePolicy policy,
#if DCHECK_IS_ON()
               ArgumentPassingType resolve_executor_type,
               ArgumentPassingType reject_executor_type,
               bool can_resolve,
               bool can_reject,
#endif
               base::OnceCallback<void(AbstractPromise*)> callback)
      : callback_(std::move(callback)),
#if DCHECK_IS_ON()
        resolve_argument_passing_type_(resolve_executor_type),
        reject_argument_passing_type_(reject_executor_type),
        resolve_flags_(can_resolve + (can_reject << 1)),
#endif
        policy_(policy) {
  }

#if DCHECK_IS_ON()
  ArgumentPassingType ResolveArgumentPassingType() const {
    return resolve_argument_passing_type_;
  }

  ArgumentPassingType RejectArgumentPassingType() const {
    return reject_argument_passing_type_;
  }

  bool CanResolve() const { return resolve_flags_ & 1; }

  bool CanReject() const { return resolve_flags_ & 2; }
#endif

  PrerequisitePolicy GetPrerequisitePolicy() const { return policy_; }

  bool IsCancelled() const { return false; }

  void Execute(AbstractPromise* p) { std::move(callback_).Run(p); }

 private:
  base::OnceCallback<void(AbstractPromise*)> callback_;
#if DCHECK_IS_ON()
  const ArgumentPassingType resolve_argument_passing_type_;
  const ArgumentPassingType reject_argument_passing_type_;
  // On 32 bit platform we need to pack to fit in the space requirement of 3x
  // void*.
  uint8_t resolve_flags_;
#endif
  const PrerequisitePolicy policy_;
};

class AbstractPromiseTest : public testing::Test {
 public:
  enum class CallbackResultType : uint8_t {
    kNoCallback,
    kCanResolve,
    kCanReject,
    kCanResolveOrReject,
  };

  struct PromiseSettings {
    PromiseSettings(
        Location from_here,
        std::unique_ptr<AbstractPromise::AdjacencyList> prerequisites)
        : from_here(from_here), prerequisites(std::move(prerequisites)) {}

    Location from_here;

    std::unique_ptr<AbstractPromise::AdjacencyList> prerequisites;

    PrerequisitePolicy prerequisite_policy =
        AbstractPromise::Executor::PrerequisitePolicy::kAll;

    bool executor_can_resolve = true;

    bool executor_can_reject = false;

    ArgumentPassingType resolve_executor_type = ArgumentPassingType::kNormal;

    ArgumentPassingType reject_executor_type = ArgumentPassingType::kNoCallback;

    RejectPolicy reject_policy = RejectPolicy::kMustCatchRejection;

    base::OnceCallback<void(AbstractPromise*)> callback;

    scoped_refptr<TaskRunner> task_runner = ThreadTaskRunnerHandle::Get();
  };

  class PromiseSettingsBuilder {
   public:
    PromiseSettingsBuilder(
        Location from_here,
        std::unique_ptr<AbstractPromise::AdjacencyList> prerequisites)
        : settings(from_here, std::move(prerequisites)) {}

    PromiseSettingsBuilder& With(PrerequisitePolicy prerequisite_policy) {
      settings.prerequisite_policy = prerequisite_policy;
      return *this;
    }

    PromiseSettingsBuilder& With(const scoped_refptr<TaskRunner>& task_runner) {
      settings.task_runner = task_runner;
      return *this;
    }

    PromiseSettingsBuilder& With(RejectPolicy reject_policy) {
      settings.reject_policy = reject_policy;
      return *this;
    }

    PromiseSettingsBuilder& With(
        base::OnceCallback<void(AbstractPromise*)> callback) {
      settings.callback = std::move(callback);
      return *this;
    }

    PromiseSettingsBuilder& With(CallbackResultType callback_result_type) {
      switch (callback_result_type) {
        case CallbackResultType::kNoCallback:
          settings.executor_can_resolve = false;
          settings.executor_can_reject = false;
          break;
        case CallbackResultType::kCanResolve:
          settings.executor_can_resolve = true;
          settings.executor_can_reject = false;
          break;
        case CallbackResultType::kCanReject:
          settings.executor_can_resolve = false;
          settings.executor_can_reject = true;
          break;
        case CallbackResultType::kCanResolveOrReject:
          settings.executor_can_resolve = true;
          settings.executor_can_reject = true;
          break;
      };
      return *this;
    }

    PromiseSettingsBuilder& WithResolve(
        ArgumentPassingType resolve_executor_type) {
      settings.resolve_executor_type = resolve_executor_type;
      return *this;
    }

    PromiseSettingsBuilder& WithReject(
        ArgumentPassingType reject_executor_type) {
      settings.reject_executor_type = reject_executor_type;
      return *this;
    }

    operator scoped_refptr<AbstractPromise>() {
      return AbstractPromise::Create(
          std::move(settings.task_runner), settings.from_here,
          std::move(settings.prerequisites), settings.reject_policy,
          AbstractPromise::ConstructWith<DependentList::ConstructUnresolved,
                                         TestExecutor>(),
          settings.prerequisite_policy,
#if DCHECK_IS_ON()
          settings.resolve_executor_type, settings.reject_executor_type,
          settings.executor_can_resolve, settings.executor_can_reject,
#endif
          std::move(settings.callback));
    }

   private:
    PromiseSettings settings;
  };

  PromiseSettingsBuilder ThenPromise(Location from_here,
                                     scoped_refptr<AbstractPromise> parent) {
    PromiseSettingsBuilder builder(
        from_here, parent ? std::make_unique<AbstractPromise::AdjacencyList>(
                                std::move(parent))
                          : std::make_unique<AbstractPromise::AdjacencyList>());
    builder.With(BindOnce([](AbstractPromise* p) {
      AbstractPromise* prerequisite = p->GetOnlyPrerequisite();
      if (prerequisite->IsResolved()) {
        p->emplace(Resolved<void>());
        p->OnResolved();
      } else if (prerequisite->IsRejected()) {
        // Consistent with BaseThenAndCatchExecutor::ProcessNullExecutor.
        p->emplace(scoped_refptr<AbstractPromise>(prerequisite));
        p->OnResolved();
      }
    }));
    return builder;
  }

  PromiseSettingsBuilder CatchPromise(Location from_here,
                                      scoped_refptr<AbstractPromise> parent) {
    PromiseSettingsBuilder builder(
        from_here, parent ? std::make_unique<AbstractPromise::AdjacencyList>(
                                std::move(parent))
                          : std::make_unique<AbstractPromise::AdjacencyList>());
    builder.With(CallbackResultType::kNoCallback)
        .With(CallbackResultType::kCanResolve)
        .WithResolve(ArgumentPassingType::kNoCallback)
        .WithReject(ArgumentPassingType::kNormal)
        .With(BindOnce([](AbstractPromise* p) {
          AbstractPromise* prerequisite = p->GetOnlyPrerequisite();
          if (prerequisite->IsResolved()) {
            // Consistent with BaseThenAndCatchExecutor::ProcessNullExecutor.
            p->emplace(scoped_refptr<AbstractPromise>(prerequisite));
            p->OnResolved();
          } else if (prerequisite->IsRejected()) {
            p->emplace(Resolved<void>());
            p->OnResolved();
          }
        }));
    return builder;
  }

  PromiseSettingsBuilder AllPromise(
      Location from_here,
      std::vector<internal::AbstractPromise::AdjacencyListNode>
          prerequisite_list) {
    PromiseSettingsBuilder builder(
        from_here, std::make_unique<AbstractPromise::AdjacencyList>(
                       std::move(prerequisite_list)));
    builder.With(PrerequisitePolicy::kAll)
        .With(BindOnce([](AbstractPromise* p) {
          // Reject if any prerequisites rejected.
          for (const AbstractPromise::AdjacencyListNode& node :
               *p->prerequisite_list()) {
            if (node.prerequisite->IsRejected()) {
              p->emplace(Rejected<void>());
              p->OnRejected();
              return;
            }
          }
          p->emplace(Resolved<void>());
          p->OnResolved();
        }));
    return builder;
  }

  PromiseSettingsBuilder AnyPromise(
      Location from_here,
      std::vector<internal::AbstractPromise::AdjacencyListNode>
          prerequisite_list) {
    PromiseSettingsBuilder builder(
        from_here, std::make_unique<AbstractPromise::AdjacencyList>(
                       std::move(prerequisite_list)));
    builder.With(PrerequisitePolicy::kAny)
        .With(BindOnce([](AbstractPromise* p) {
          // Reject if any prerequisites rejected.
          for (const AbstractPromise::AdjacencyListNode& node :
               *p->prerequisite_list()) {
            if (node.prerequisite->IsRejected()) {
              p->emplace(Rejected<void>());
              p->OnRejected();
              return;
            }
          }
          p->emplace(Resolved<void>());
          p->OnResolved();
        }));
    return builder;
  }

  test::ScopedTaskEnvironment scoped_task_environment_;
};

TEST_F(AbstractPromiseTest, UnfulfilledPromise) {
  scoped_refptr<AbstractPromise> promise =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  EXPECT_FALSE(promise->IsResolved());
  EXPECT_FALSE(promise->IsRejected());
  EXPECT_FALSE(promise->IsCanceled());
}

TEST_F(AbstractPromiseTest, OnResolve) {
  scoped_refptr<AbstractPromise> promise =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  EXPECT_FALSE(promise->IsResolved());
  promise->OnResolved();
  EXPECT_TRUE(promise->IsResolved());
}

TEST_F(AbstractPromiseTest, OnReject) {
  scoped_refptr<AbstractPromise> promise =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetRejectPolicy(
          RejectPolicy::kCatchNotRequired);
  EXPECT_FALSE(promise->IsRejected());
  promise->OnRejected();
  EXPECT_TRUE(promise->IsRejected());
}

TEST_F(AbstractPromiseTest, ExecuteOnResolve) {
  scoped_refptr<AbstractPromise> promise =
      ThenPromise(FROM_HERE, nullptr).With(BindOnce([](AbstractPromise* p) {
        p->emplace(Resolved<void>());
        p->OnResolved();
      }));

  EXPECT_FALSE(promise->IsResolved());
  promise->Execute();
  EXPECT_TRUE(promise->IsResolved());
}

TEST_F(AbstractPromiseTest, ExecuteOnReject) {
  scoped_refptr<AbstractPromise> promise =
      ThenPromise(FROM_HERE, nullptr)
          .With(RejectPolicy::kCatchNotRequired)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  EXPECT_FALSE(promise->IsRejected());
  promise->Execute();
  EXPECT_TRUE(promise->IsRejected());
}

TEST_F(AbstractPromiseTest, ExecutionChain) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);
  scoped_refptr<AbstractPromise> p4 = ThenPromise(FROM_HERE, p3);
  scoped_refptr<AbstractPromise> p5 = ThenPromise(FROM_HERE, p4);

  p1->OnResolved();

  EXPECT_FALSE(p2->IsResolved());
  EXPECT_FALSE(p3->IsResolved());
  EXPECT_FALSE(p4->IsResolved());
  EXPECT_FALSE(p5->IsResolved());
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p1->IsResolved());
  EXPECT_TRUE(p3->IsResolved());
  EXPECT_TRUE(p4->IsResolved());
  EXPECT_TRUE(p5->IsResolved());
}

TEST_F(AbstractPromiseTest, MoveExecutionChain) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p1).WithResolve(ArgumentPassingType::kMove);

  scoped_refptr<AbstractPromise> p3 =
      ThenPromise(FROM_HERE, p2).WithResolve(ArgumentPassingType::kMove);

  scoped_refptr<AbstractPromise> p4 =
      ThenPromise(FROM_HERE, p3).WithResolve(ArgumentPassingType::kMove);

  scoped_refptr<AbstractPromise> p5 =
      ThenPromise(FROM_HERE, p4).WithResolve(ArgumentPassingType::kMove);

  p1->OnResolved();

  EXPECT_FALSE(p2->IsResolved());
  EXPECT_FALSE(p3->IsResolved());
  EXPECT_FALSE(p4->IsResolved());
  EXPECT_FALSE(p5->IsResolved());
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p1->IsResolved());
  EXPECT_TRUE(p3->IsResolved());
  EXPECT_TRUE(p4->IsResolved());
  EXPECT_TRUE(p5->IsResolved());
}

TEST_F(AbstractPromiseTest, MoveResolveCatchExecutionChain) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p1)
          .With(CallbackResultType::kCanReject)
          .WithResolve(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p3 =
      CatchPromise(FROM_HERE, p2)
          .With(CallbackResultType::kCanResolve)
          .WithReject(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  scoped_refptr<AbstractPromise> p4 =
      ThenPromise(FROM_HERE, p3)
          .With(CallbackResultType::kCanReject)
          .WithResolve(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p5 =
      CatchPromise(FROM_HERE, p4)
          .With(CallbackResultType::kCanResolve)
          .WithReject(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  p1->OnResolved();

  EXPECT_FALSE(p2->IsRejected());
  EXPECT_FALSE(p3->IsResolved());
  EXPECT_FALSE(p4->IsRejected());
  EXPECT_FALSE(p5->IsResolved());
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p2->IsRejected());
  EXPECT_TRUE(p3->IsResolved());
  EXPECT_TRUE(p4->IsRejected());
  EXPECT_TRUE(p5->IsResolved());
}

TEST_F(AbstractPromiseTest, MoveResolveCatchExecutionChainType2) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p1)
          .With(CallbackResultType::kCanReject)
          .WithResolve(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p3 =
      CatchPromise(FROM_HERE, p2)
          .With(CallbackResultType::kCanReject)
          .WithReject(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p4 =
      CatchPromise(FROM_HERE, p3)
          .With(CallbackResultType::kCanResolve)
          .WithReject(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  scoped_refptr<AbstractPromise> p5 =
      ThenPromise(FROM_HERE, p4)
          .With(CallbackResultType::kCanResolve)
          .WithResolve(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  scoped_refptr<AbstractPromise> p6 =
      ThenPromise(FROM_HERE, p5)
          .With(CallbackResultType::kCanReject)
          .WithResolve(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p7 =
      CatchPromise(FROM_HERE, p6)
          .With(CallbackResultType::kCanReject)
          .WithReject(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p8 =
      CatchPromise(FROM_HERE, p7)
          .With(CallbackResultType::kCanResolve)
          .WithReject(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  scoped_refptr<AbstractPromise> p9 =
      ThenPromise(FROM_HERE, p8)
          .With(CallbackResultType::kCanResolve)
          .WithResolve(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));
  p1->OnResolved();

  EXPECT_FALSE(p2->IsRejected());
  EXPECT_FALSE(p3->IsRejected());
  EXPECT_FALSE(p4->IsResolved());
  EXPECT_FALSE(p5->IsResolved());
  EXPECT_FALSE(p6->IsRejected());
  EXPECT_FALSE(p7->IsRejected());
  EXPECT_FALSE(p8->IsResolved());
  EXPECT_FALSE(p9->IsResolved());
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p2->IsRejected());
  EXPECT_TRUE(p3->IsRejected());
  EXPECT_TRUE(p4->IsResolved());
  EXPECT_TRUE(p5->IsResolved());
  EXPECT_TRUE(p6->IsRejected());
  EXPECT_TRUE(p7->IsRejected());
  EXPECT_TRUE(p8->IsResolved());
  EXPECT_TRUE(p9->IsResolved());
}

TEST_F(AbstractPromiseTest, MixedMoveAndNormalExecutionChain) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p1).WithResolve(ArgumentPassingType::kMove);

  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);

  scoped_refptr<AbstractPromise> p4 =
      ThenPromise(FROM_HERE, p3).WithResolve(ArgumentPassingType::kMove);

  scoped_refptr<AbstractPromise> p5 = ThenPromise(FROM_HERE, p4);

  p1->OnResolved();

  EXPECT_FALSE(p2->IsResolved());
  EXPECT_FALSE(p3->IsResolved());
  EXPECT_FALSE(p4->IsResolved());
  EXPECT_FALSE(p5->IsResolved());
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p1->IsResolved());
  EXPECT_TRUE(p3->IsResolved());
  EXPECT_TRUE(p4->IsResolved());
  EXPECT_TRUE(p5->IsResolved());
}

TEST_F(AbstractPromiseTest, MoveAtEndOfChain) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 =
      ThenPromise(FROM_HERE, p2).WithResolve(ArgumentPassingType::kMove);
}

TEST_F(AbstractPromiseTest, BranchedExecutionChain) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);
  scoped_refptr<AbstractPromise> p4 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p5 = ThenPromise(FROM_HERE, p4);

  p1->OnResolved();

  EXPECT_FALSE(p2->IsResolved());
  EXPECT_FALSE(p3->IsResolved());
  EXPECT_FALSE(p4->IsResolved());
  EXPECT_FALSE(p5->IsResolved());
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p2->IsResolved());
  EXPECT_TRUE(p3->IsResolved());
  EXPECT_TRUE(p4->IsResolved());
  EXPECT_TRUE(p5->IsResolved());
}

TEST_F(AbstractPromiseTest, PrerequisiteAlreadyResolved) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  p1->OnResolved();

  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);

  EXPECT_FALSE(p2->IsResolved());
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p2->IsResolved());
}

TEST_F(AbstractPromiseTest, PrerequisiteAlreadyRejected) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);
  p1->OnRejected();

  scoped_refptr<AbstractPromise> p2 =
      CatchPromise(FROM_HERE, p1)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            EXPECT_EQ(p->GetFirstRejectedPrerequisite(), p1);
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  EXPECT_FALSE(p2->IsResolved());
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p2->IsResolved());
}

TEST_F(AbstractPromiseTest, MultipleResolvedPrerequisitePolicyALL) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p4 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      4);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;
  prerequisite_list[3].prerequisite = p4;

  scoped_refptr<AbstractPromise> all_promise =
      AllPromise(FROM_HERE, std::move(prerequisite_list));

  p1->OnResolved();
  p2->OnResolved();
  p3->OnResolved();
  RunLoop().RunUntilIdle();

  EXPECT_FALSE(all_promise->IsResolved());
  p4->OnResolved();

  RunLoop().RunUntilIdle();
  EXPECT_TRUE(all_promise->IsResolved());
}

TEST_F(AbstractPromiseTest,
       MultithreadedMultipleResolvedPrerequisitePolicyALL) {
  constexpr int num_threads = 4;
  constexpr int num_promises = 1000;

  std::unique_ptr<Thread> thread[num_threads];
  for (int i = 0; i < num_threads; i++) {
    thread[i] = std::make_unique<Thread>("Test thread");
    thread[i]->Start();
  }

  scoped_refptr<AbstractPromise> promise[num_promises];
  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      num_promises);
  for (int i = 0; i < num_promises; i++) {
    promise[i] = DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
    prerequisite_list[i].prerequisite = promise[i];
  }

  scoped_refptr<AbstractPromise> all_promise =
      AllPromise(FROM_HERE, std::move(prerequisite_list));

  RunLoop run_loop;
  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, all_promise)
          .With(BindLambdaForTesting(
              [&](AbstractPromise* p) { run_loop.Quit(); }));

  for (int i = 0; i < num_promises; i++) {
    thread[i % num_threads]->task_runner()->PostTask(
        FROM_HERE, BindOnce(
                       [](scoped_refptr<AbstractPromise> all_promise,
                          scoped_refptr<AbstractPromise> promise) {
                         EXPECT_FALSE(all_promise->IsResolved());
                         promise->OnResolved();
                       },
                       all_promise, promise[i]));
  }

  run_loop.Run();

  for (int i = 0; i < num_promises; i++) {
    EXPECT_TRUE(promise[i]->IsResolved());
  }

  EXPECT_TRUE(all_promise->IsResolved());

  for (int i = 0; i < num_threads; i++) {
    thread[i]->Stop();
  }
}

TEST_F(AbstractPromiseTest, SingleRejectPrerequisitePolicyALL) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);
  scoped_refptr<AbstractPromise> p4 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      4);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;
  prerequisite_list[3].prerequisite = p4;

  scoped_refptr<AbstractPromise> all_promise =
      AllPromise(FROM_HERE, std::move(prerequisite_list))
          .With(CallbackResultType::kCanResolveOrReject)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            EXPECT_EQ(p->GetFirstRejectedPrerequisite(), p3);
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p5 = CatchPromise(FROM_HERE, all_promise);

  p3->OnRejected();
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(all_promise->IsRejected());
  EXPECT_TRUE(p5->IsResolved());
}

TEST_F(AbstractPromiseTest, MultipleRejectPrerequisitePolicyALL) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);
  scoped_refptr<AbstractPromise> p4 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      4);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;
  prerequisite_list[3].prerequisite = p4;

  scoped_refptr<AbstractPromise> all_promise =
      AllPromise(FROM_HERE, std::move(prerequisite_list))
          .With(CallbackResultType::kCanResolveOrReject)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            if (AbstractPromise* rejected = p->GetFirstRejectedPrerequisite()) {
              EXPECT_EQ(rejected, p2);
              p->emplace(Rejected<void>());
              p->OnRejected();
            } else {
              FAIL() << "A prerequisite was rejected";
            }
          }));

  scoped_refptr<AbstractPromise> p5 =
      CatchPromise(FROM_HERE, all_promise)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            EXPECT_FALSE(p->IsSettled());  // Should only happen once.
            EXPECT_EQ(p->GetFirstRejectedPrerequisite(), all_promise);
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  p2->OnRejected();
  p1->OnRejected();
  p3->OnRejected();
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(all_promise->IsRejected());
  EXPECT_TRUE(p5->IsResolved());
}

TEST_F(AbstractPromiseTest, SingleResolvedPrerequisitesPrerequisitePolicyANY) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p4 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      4);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;
  prerequisite_list[3].prerequisite = p4;

  scoped_refptr<AbstractPromise> any_promise =
      AnyPromise(FROM_HERE, std::move(prerequisite_list));

  p2->OnResolved();
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(any_promise->IsResolved());
}

TEST_F(AbstractPromiseTest, SingleRejectPrerequisitePolicyANY) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);
  scoped_refptr<AbstractPromise> p4 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      4);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;
  prerequisite_list[3].prerequisite = p4;

  scoped_refptr<AbstractPromise> any_promise =
      AnyPromise(FROM_HERE, std::move(prerequisite_list))
          .With(CallbackResultType::kCanResolveOrReject);

  scoped_refptr<AbstractPromise> p5 = CatchPromise(FROM_HERE, any_promise);

  p3->OnRejected();
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(any_promise->IsRejected());
  EXPECT_TRUE(p5->IsResolved());
}

TEST_F(AbstractPromiseTest, SingleResolvePrerequisitePolicyANY) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p4 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      4);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;
  prerequisite_list[3].prerequisite = p4;

  scoped_refptr<AbstractPromise> any_promise =
      AnyPromise(FROM_HERE, std::move(prerequisite_list));

  scoped_refptr<AbstractPromise> p5 = CatchPromise(FROM_HERE, any_promise);

  p3->OnResolved();
  RunLoop().RunUntilIdle();
  EXPECT_TRUE(any_promise->IsResolved());
  EXPECT_TRUE(p5->IsResolved());
}

TEST_F(AbstractPromiseTest, IsCanceled) {
  scoped_refptr<AbstractPromise> promise = ThenPromise(FROM_HERE, nullptr);
  EXPECT_FALSE(promise->IsCanceled());
  promise->OnCanceled();
  EXPECT_TRUE(promise->IsCanceled());
}

TEST_F(AbstractPromiseTest, OnCanceledPreventsExecution) {
  scoped_refptr<AbstractPromise> promise =
      ThenPromise(FROM_HERE, nullptr).With(BindOnce([](AbstractPromise* p) {
        FAIL() << "Should not be called";
      }));
  promise->OnCanceled();
  promise->Execute();
}

TEST_F(AbstractPromiseTest, CancelationStopsExecutionChain) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p1).With(BindOnce([](AbstractPromise* p) {
        p->OnCanceled();
        p->OnCanceled();  // NOP shouldn't crash.
      }));
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);
  scoped_refptr<AbstractPromise> p4 = ThenPromise(FROM_HERE, p3);

  p1->OnResolved();

  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p3->IsCanceled());
  EXPECT_TRUE(p4->IsCanceled());
}

TEST_F(AbstractPromiseTest, CancelationStopsBranchedExecutionChain) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p1).With(BindOnce([](AbstractPromise* p) {
        p->OnCanceled();
      }));

  // Branch one
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);
  scoped_refptr<AbstractPromise> p4 = ThenPromise(FROM_HERE, p3);

  // Branch two
  scoped_refptr<AbstractPromise> p5 = ThenPromise(FROM_HERE, p2);
  scoped_refptr<AbstractPromise> promise6 = ThenPromise(FROM_HERE, p5);

  p1->OnResolved();

  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p3->IsCanceled());
  EXPECT_TRUE(p4->IsCanceled());
  EXPECT_TRUE(p5->IsCanceled());
  EXPECT_TRUE(promise6->IsCanceled());
}

TEST_F(AbstractPromiseTest, CancelChainCanReject) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 = CatchPromise(FROM_HERE, p2);

  p0->OnCanceled();
  RunLoop().RunUntilIdle();
}

TEST_F(AbstractPromiseTest, CancelationPrerequisitePolicyALL) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      3);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;

  scoped_refptr<AbstractPromise> all_promise =
      AllPromise(FROM_HERE, std::move(prerequisite_list));

  p2->OnCanceled();
  EXPECT_TRUE(all_promise->IsCanceled());
}

TEST_F(AbstractPromiseTest, CancelationPrerequisitePolicyANY) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      3);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;

  scoped_refptr<AbstractPromise> any_promise =
      AnyPromise(FROM_HERE, std::move(prerequisite_list));

  p3->OnCanceled();
  p2->OnCanceled();
  EXPECT_FALSE(any_promise->IsCanceled());

  p1->OnCanceled();
  EXPECT_TRUE(any_promise->IsCanceled());
}

TEST_F(AbstractPromiseTest, AlreadyCanceledPrerequisitePolicyALL) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      3);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;
  p2->OnCanceled();

  scoped_refptr<AbstractPromise> all_promise =
      AllPromise(FROM_HERE, std::move(prerequisite_list));

  EXPECT_TRUE(all_promise->IsCanceled());
}

TEST_F(AbstractPromiseTest, SomeAlreadyCanceledPrerequisitePolicyANY) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      3);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;
  p2->OnCanceled();

  scoped_refptr<AbstractPromise> any_promise =
      AnyPromise(FROM_HERE, std::move(prerequisite_list));

  EXPECT_FALSE(any_promise->IsCanceled());
}

TEST_F(AbstractPromiseTest, AllAlreadyCanceledPrerequisitePolicyANY) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  scoped_refptr<AbstractPromise> p3 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  std::vector<internal::AbstractPromise::AdjacencyListNode> prerequisite_list(
      3);
  prerequisite_list[0].prerequisite = p1;
  prerequisite_list[1].prerequisite = p2;
  prerequisite_list[2].prerequisite = p3;
  p1->OnCanceled();
  p2->OnCanceled();
  p3->OnCanceled();

  scoped_refptr<AbstractPromise> any_promise =
      AnyPromise(FROM_HERE, std::move(prerequisite_list));

  EXPECT_TRUE(any_promise->IsCanceled());
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(DetectResolveDoubleMoveHazard)) {
  scoped_refptr<AbstractPromise> p0 = ThenPromise(FROM_HERE, nullptr);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0).WithResolve(ArgumentPassingType::kMove);

  EXPECT_DCHECK_DEATH({
    scoped_refptr<AbstractPromise> p2 =
        ThenPromise(FROM_HERE, p0).WithResolve(ArgumentPassingType::kMove);
  });
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(
           DetectMixedResolveCallbackMoveAndNonMoveHazard)) {
  scoped_refptr<AbstractPromise> p0 = ThenPromise(FROM_HERE, nullptr);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0).WithResolve(ArgumentPassingType::kMove);

  EXPECT_DCHECK_DEATH(
      { scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p0); });
}

TEST_F(AbstractPromiseTest, MultipleNonMoveCatchCallbacksAreOK) {
  /*
   * Key:  T = Then, C = Catch
   *
   *      P0
   *   T      T
   *   |      |
   *  \|      |/
   *  P1      P2
   *   T      T
   *   |      |
   *  \|      |/
   *  E3      E4
   *   C      C
   */
  scoped_refptr<AbstractPromise> p0 =
      ThenPromise(FROM_HERE, nullptr).With(CallbackResultType::kCanReject);

  scoped_refptr<AbstractPromise> p1 = ThenPromise(FROM_HERE, p0);
  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p0);
  scoped_refptr<AbstractPromise> p3 = CatchPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p4 = CatchPromise(FROM_HERE, p2);
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(DetectCatchCallbackDoubleMoveHazard)) {
  /*
   * Key:  T = Then, C = Catch
   *
   *      P0
   *   T      T
   *   |      |
   *  \|      |/
   *  P1      P2
   *   C      C
   *
   * We need to make sure P3 & P4's reject callback don't both use move
   * semantics since they share a common ancestor with no intermediate catches.
   */
  scoped_refptr<AbstractPromise> p0 =
      ThenPromise(FROM_HERE, nullptr).With(CallbackResultType::kCanReject);

  scoped_refptr<AbstractPromise> p1 =
      CatchPromise(FROM_HERE, p0).WithReject(ArgumentPassingType::kMove);

  EXPECT_DCHECK_DEATH({
    scoped_refptr<AbstractPromise> p2 =
        CatchPromise(FROM_HERE, p0).WithReject(ArgumentPassingType::kMove);
  });
}

TEST_F(
    AbstractPromiseTest,
    ABSTRACT_PROMISE_DEATH_TEST(DetectCatchCallbackDoubleMoveHazardInChain)) {
  /*
   * Key:  T = Then, C = Catch
   *
   *      P0
   *   T      T
   *   |      |
   *  \|      |/
   *  P1      P2
   *   T      T
   *   |      |
   *  \|      |/
   *  P3      P4
   *   C      C
   *
   * We need to make sure P3 & P4's reject callback don't both use move
   * semantics since they share a common ancestor with no intermediate catches.
   */
  scoped_refptr<AbstractPromise> p0 =
      ThenPromise(FROM_HERE, nullptr).With(CallbackResultType::kCanReject);

  scoped_refptr<AbstractPromise> p1 = ThenPromise(FROM_HERE, p0);
  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p0);

  scoped_refptr<AbstractPromise> p3 =
      CatchPromise(FROM_HERE, p1).WithReject(ArgumentPassingType::kMove);

  EXPECT_DCHECK_DEATH({
    scoped_refptr<AbstractPromise> p4 =
        CatchPromise(FROM_HERE, p2).WithReject(ArgumentPassingType::kMove);
  });
}

TEST_F(
    AbstractPromiseTest,
    ABSTRACT_PROMISE_DEATH_TEST(
        DetectCatchCallbackDoubleMoveHazardInChainIntermediateThensCanReject)) {
  /*
   * Key:  T = Then, C = Catch
   *
   *      P0
   *   T      T
   *   |      |
   *  \|      |/
   *  P1      P2
   *   T      T
   *   |      |
   *  \|      |/
   *  P3      P4
   *   C      C
   *
   * We need to make sure P3 & P4's reject callback don't both use move
   * semantics since they share a common ancestor with no intermediate catches.
   */
  scoped_refptr<AbstractPromise> p0 =
      ThenPromise(FROM_HERE, nullptr).With(CallbackResultType::kCanReject);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0).With(CallbackResultType::kCanReject);

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p0).With(CallbackResultType::kCanReject);

  scoped_refptr<AbstractPromise> p3 =
      CatchPromise(FROM_HERE, p1).WithReject(ArgumentPassingType::kMove);

  EXPECT_DCHECK_DEATH({
    scoped_refptr<AbstractPromise> p4 =
        CatchPromise(FROM_HERE, p2).WithReject(ArgumentPassingType::kMove);
  });
}

TEST_F(
    AbstractPromiseTest,
    ABSTRACT_PROMISE_DEATH_TEST(DetectMixedCatchCallbackMoveAndNonMoveHazard)) {
  /*
   * Key:  T = Then, C = Catch
   *
   *      P0
   *   T      T
   *   |      |
   *  \|      |/
   *  P1      P2
   *   T      T
   *   |      |
   *  \|      |/
   *  P3      P4
   *   C      C
   *
   * We can't guarantee the order in which P3 and P4's reject callbacks run so
   * we need to need to catch the case where move and non-move semantics are
   * mixed.
   */
  scoped_refptr<AbstractPromise> p0 =
      ThenPromise(FROM_HERE, nullptr).With(CallbackResultType::kCanReject);

  scoped_refptr<AbstractPromise> p1 = ThenPromise(FROM_HERE, p0);
  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p0);

  scoped_refptr<AbstractPromise> p3 =
      CatchPromise(FROM_HERE, p1).WithReject(ArgumentPassingType::kMove);

  EXPECT_DCHECK_DEATH(
      { scoped_refptr<AbstractPromise> p4 = CatchPromise(FROM_HERE, p2); });
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(DetectThenCallbackDoubleMoveHazardInChain)) {
  /*
   * Key:  T = Then, C = Catch
   *
   *      P0
   *   C      C
   *   |      |
   *  \|      |/
   *  P1      P2
   *   C      C
   *   |      |
   *  \|      |/
   *  P3      P4
   *   T      T
   *
   * We need to make sure P3 & P4's resolve callback don't both use move
   * semantics since they share a common ancestor with no intermediate then's.
   */
  scoped_refptr<AbstractPromise> p0 = ThenPromise(FROM_HERE, nullptr);
  scoped_refptr<AbstractPromise> p1 = CatchPromise(FROM_HERE, p0);
  scoped_refptr<AbstractPromise> p2 = CatchPromise(FROM_HERE, p0);

  scoped_refptr<AbstractPromise> p3 =
      ThenPromise(FROM_HERE, p1).WithResolve(ArgumentPassingType::kMove);

  EXPECT_DCHECK_DEATH({
    scoped_refptr<AbstractPromise> p4 =
        ThenPromise(FROM_HERE, p2).WithResolve(ArgumentPassingType::kMove);
  });
}

TEST_F(AbstractPromiseTest, SimpleMissingCatch) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  // This should DCHECK when |p1| is deleted.
  EXPECT_DCHECK_DEATH({ p1 = nullptr; });

  // Under the hood EXPECT_DCHECK_DEATH uses fork() so |p1| isn't actually
  // cleared so we need to tidy up.
  p1->IgnoreUncaughtCatchForTesting();
}

TEST_F(AbstractPromiseTest, ABSTRACT_PROMISE_DEATH_TEST(MissingCatch)) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  // The missing catch here will get noticed.
  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  // This should DCHECK when |p2| is deleted.
  EXPECT_DCHECK_DEATH({ p2 = nullptr; });

  // Under the hood EXPECT_DCHECK_DEATH uses fork() so |p2| isn't actually
  // cleared so we need to tidy up.
  p2->IgnoreUncaughtCatchForTesting();
}

TEST_F(AbstractPromiseTest, MissingCatchNotRequired) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(RejectPolicy::kCatchNotRequired)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  // The missing catch here will gets ignored.
  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);

  p0->OnResolved();

  RunLoop().RunUntilIdle();
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(MissingCatchFromCurriedPromise)) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, nullptr)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p0)
          .With(BindOnce(
              [](scoped_refptr<AbstractPromise> p1, AbstractPromise* p) {
                // Resolve with a promise that can and does reject.
                ThreadTaskRunnerHandle::Get()->PostTask(
                    FROM_HERE, BindOnce(&AbstractPromise::Execute, p1));
                p->emplace(std::move(p1));
                p->OnResolved();
              },
              std::move(p1)));

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  // This should DCHECK when |p2| is deleted.
  EXPECT_DCHECK_DEATH({ p2 = nullptr; });

  // Under the hood EXPECT_DCHECK_DEATH uses fork() so |p2| isn't actually
  // cleared so we need to tidy up.
  p2->IgnoreUncaughtCatchForTesting();
}

TEST_F(
    AbstractPromiseTest,
    ABSTRACT_PROMISE_DEATH_TEST(MissingCatchFromCurriedPromiseWithDependent)) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, nullptr)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p0)
          .With(BindOnce(
              [&](scoped_refptr<AbstractPromise> p1, AbstractPromise* p) {
                // Resolve with a promise that can and does reject.
                ThreadTaskRunnerHandle::Get()->PostTask(
                    FROM_HERE, BindOnce(&AbstractPromise::Execute, p1));
                p->emplace(std::move(p1));
                p->OnResolved();
              },
              std::move(p1)));

  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  // This should DCHECK when |p3| is deleted.
  EXPECT_DCHECK_DEATH({ p3 = nullptr; });

  // Under the hood EXPECT_DCHECK_DEATH uses fork() so |p3| isn't actually
  // cleared so we need to tidy up.
  p3->IgnoreUncaughtCatchForTesting();
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(
           MissingCatchFromCurriedPromiseWithDependentAddedAfterExecution)) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, nullptr)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p0)
          .With(BindOnce(
              [&](scoped_refptr<AbstractPromise> p1, AbstractPromise* p) {
                // Resolve with a promise that can and does reject.
                ThreadTaskRunnerHandle::Get()->PostTask(
                    FROM_HERE, BindOnce(&AbstractPromise::Execute, p1));
                p->emplace(std::move(p1));
                p->OnResolved();
              },
              std::move(p1)));

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);
  RunLoop().RunUntilIdle();

  // This should DCHECK when |p3| is deleted.
  EXPECT_DCHECK_DEATH({ p3 = nullptr; });

  // Under the hood EXPECT_DCHECK_DEATH uses fork() so |p3| isn't actually
  // cleared so we need to tidy up.
  p3->IgnoreUncaughtCatchForTesting();
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(MissingCatchLongChain)) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);
  scoped_refptr<AbstractPromise> p4 = ThenPromise(FROM_HERE, p3);

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  // This should DCHECK when |p4| is deleted.
  EXPECT_DCHECK_DEATH({ p4 = nullptr; });

  // Under the hood EXPECT_DCHECK_DEATH uses fork() so |p4| isn't actually
  // cleared so we need to tidy up.
  p4->IgnoreUncaughtCatchForTesting();
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(
           ThenAddedToSettledPromiseWithMissingCatchAndSeveralDependents)) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);
  scoped_refptr<AbstractPromise> p4 = ThenPromise(FROM_HERE, p2);

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  scoped_refptr<AbstractPromise> p5 = ThenPromise(FROM_HERE, p2);

  RunLoop().RunUntilIdle();

  // This should DCHECK when |p5| is deleted.
  EXPECT_DCHECK_DEATH({ p5 = nullptr; });

  // Tidy up.
  p3->IgnoreUncaughtCatchForTesting();
  p4->IgnoreUncaughtCatchForTesting();
  p5->IgnoreUncaughtCatchForTesting();
}

TEST_F(
    AbstractPromiseTest,
    ABSTRACT_PROMISE_DEATH_TEST(ThenAddedAfterChainExecutionWithMissingCatch)) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  // The missing catch here will get noticed.
  scoped_refptr<AbstractPromise> p4 = ThenPromise(FROM_HERE, p3);
  RunLoop().RunUntilIdle();

  // This should DCHECK when |p4| is deleted.
  EXPECT_DCHECK_DEATH({ p4 = nullptr; });

  // Under the hood EXPECT_DCHECK_DEATH uses fork() so |p4| isn't actually
  // cleared so we need to tidy up.
  p4->IgnoreUncaughtCatchForTesting();
}

TEST_F(AbstractPromiseTest, CatchAddedAfterChainExecution) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  scoped_refptr<AbstractPromise> p4 = CatchPromise(FROM_HERE, p3);

  // We shouldn't get a DCHECK failure because |p4| catches the rejection
  // from |p1|.
  RunLoop().RunUntilIdle();
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(MultipleThensAddedAfterChainExecution)) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);

  // |p5| - |p7| should still inherit catch responsibility despite this.
  scoped_refptr<AbstractPromise> p4 = CatchPromise(FROM_HERE, p3);

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  // The missing catches will get noticed.
  scoped_refptr<AbstractPromise> p5 = ThenPromise(FROM_HERE, p3);
  scoped_refptr<AbstractPromise> p6 = ThenPromise(FROM_HERE, p3);
  scoped_refptr<AbstractPromise> p7 = ThenPromise(FROM_HERE, p3);
  RunLoop().RunUntilIdle();

  // This should DCHECK when |p5|, |p6| or |p7| are deleted.
  EXPECT_DCHECK_DEATH({ p5 = nullptr; });
  EXPECT_DCHECK_DEATH({ p6 = nullptr; });
  EXPECT_DCHECK_DEATH({ p7 = nullptr; });

  // Under the hood EXPECT_DCHECK_DEATH uses fork() so |p5|, |p6| & |p7| aren't
  // actually cleared so we need to tidy up.
  p5->IgnoreUncaughtCatchForTesting();
  p6->IgnoreUncaughtCatchForTesting();
  p7->IgnoreUncaughtCatchForTesting();
}

TEST_F(AbstractPromiseTest, MultipleDependentsAddedAfterChainExecution) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);

  p0->OnResolved();
  RunLoop().RunUntilIdle();

  scoped_refptr<AbstractPromise> p4 = ThenPromise(FROM_HERE, p3);
  scoped_refptr<AbstractPromise> p5 = CatchPromise(FROM_HERE, p4);
  scoped_refptr<AbstractPromise> p6 = ThenPromise(FROM_HERE, p3);
  scoped_refptr<AbstractPromise> p7 = CatchPromise(FROM_HERE, p6);

  // We shouldn't get a DCHECK failure because |p6| and |p7| catch the rejection
  // from |p1|.
  RunLoop().RunUntilIdle();
}

TEST_F(AbstractPromiseTest, CatchAfterLongChain) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, p0)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p3 = ThenPromise(FROM_HERE, p2);
  scoped_refptr<AbstractPromise> p4 = CatchPromise(FROM_HERE, p3);

  p0->OnResolved();

  RunLoop().RunUntilIdle();
}

TEST_F(
    AbstractPromiseTest,
    ABSTRACT_PROMISE_DEATH_TEST(MissingCatchOneSideOfBranchedExecutionChain)) {
  /*
   * Key:  T = Then, C = Catch
   *
   *      P0
   *   T      T
   *   |      |
   *  \|      |/
   *  P1      P2
   *   T      T
   *   |      |
   *  \|      |/
   *  P3      P4
   *   C      T
   *
   * The missing catch for P4 should get noticed.
   */
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);

  scoped_refptr<AbstractPromise> p1 = ThenPromise(FROM_HERE, p0);
  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p0);
  scoped_refptr<AbstractPromise> p3 = CatchPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p4 = ThenPromise(FROM_HERE, p2);

  p0->OnRejected();

  RunLoop().RunUntilIdle();
  // This should DCHECK when |p4| is deleted.
  EXPECT_DCHECK_DEATH({ p4 = nullptr; });

  // Under the hood EXPECT_DCHECK_DEATH uses fork() so |p4| isn't actually
  // cleared so we need to tidy up.
  p4->IgnoreUncaughtCatchForTesting();
}

TEST_F(
    AbstractPromiseTest,
    ABSTRACT_PROMISE_DEATH_TEST(CantResolveIfpromiseDeclaredAsNonResolving)) {
  scoped_refptr<AbstractPromise> p = DoNothingPromiseBuilder(FROM_HERE);

  EXPECT_DCHECK_DEATH({ p->OnResolved(); });
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(CantRejectIfpromiseDeclaredAsNonRejecting)) {
  scoped_refptr<AbstractPromise> p = DoNothingPromiseBuilder(FROM_HERE);

  EXPECT_DCHECK_DEATH({ p->OnRejected(); });
}

TEST_F(AbstractPromiseTest,
       ABSTRACT_PROMISE_DEATH_TEST(DoubleMoveDoNothingPromise)) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p1)
          .WithResolve(ArgumentPassingType::kMove)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Resolved<int>(42));
            p->OnResolved();
          }));

  EXPECT_DCHECK_DEATH({
    scoped_refptr<AbstractPromise> p3 =
        ThenPromise(FROM_HERE, p1)
            .WithResolve(ArgumentPassingType::kMove)
            .With(BindOnce([](AbstractPromise* p) {
              p->emplace(Resolved<int>(42));
              p->OnResolved();
            }));
  });
}

TEST_F(AbstractPromiseTest, CatchBothSidesOfBranchedExecutionChain) {
  /*
   * Key:  T = Then, C = Catch
   *
   *      P0
   *   T      T
   *   |      |
   *  \|      |/
   *  P1      P2
   *   T      T
   *   |      |
   *  \|      |/
   *  P3      P4
   *   C      C
   *
   * This should execute without DCHECKS.
   */
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanReject(true).SetCanResolve(
          false);

  scoped_refptr<AbstractPromise> p1 = ThenPromise(FROM_HERE, p0);
  scoped_refptr<AbstractPromise> p2 = ThenPromise(FROM_HERE, p0);
  scoped_refptr<AbstractPromise> p3 = CatchPromise(FROM_HERE, p1);
  scoped_refptr<AbstractPromise> p4 = CatchPromise(FROM_HERE, p2);

  p0->OnRejected();

  RunLoop().RunUntilIdle();
}

TEST_F(AbstractPromiseTest, ResolvedCurriedPromise) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  std::vector<int> run_order;

  // Promise |p3| will be resolved with.
  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, nullptr)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            run_order.push_back(2);
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  scoped_refptr<AbstractPromise> p3 =
      ThenPromise(FROM_HERE, p1)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            run_order.push_back(3);
            // Resolve with a promise.
            ThreadTaskRunnerHandle::Get()->PostTask(
                FROM_HERE, BindOnce(&AbstractPromise::Execute, p2));
            p->emplace(std::move(p2));
            p->OnResolved();

            EXPECT_TRUE(p3->IsResolvedWithPromise());
          }));

  scoped_refptr<AbstractPromise> p4 =
      ThenPromise(FROM_HERE, p3)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            run_order.push_back(4);
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  p1->OnResolved();
  RunLoop().RunUntilIdle();

  EXPECT_THAT(run_order, ElementsAre(3, 2, 4));
}

TEST_F(AbstractPromiseTest, UnresolvedCurriedPromise) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  std::vector<int> run_order;

  // Promise |p3| will be resolved with.
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p3 =
      ThenPromise(FROM_HERE, p1)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            run_order.push_back(3);
            // Resolve with a promise.
            ThreadTaskRunnerHandle::Get()->PostTask(
                FROM_HERE, BindOnce(&AbstractPromise::Execute, p2));
            p->emplace(p2);
            p->OnResolved();

            EXPECT_TRUE(p3->IsResolvedWithPromise());
          }));

  scoped_refptr<AbstractPromise> p4 =
      ThenPromise(FROM_HERE, p3)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            run_order.push_back(4);
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  p1->OnResolved();
  RunLoop().RunUntilIdle();
  EXPECT_THAT(run_order, ElementsAre(3));

  p2->OnResolved();
  RunLoop().RunUntilIdle();
  EXPECT_THAT(run_order, ElementsAre(3, 4));
}

TEST_F(AbstractPromiseTest, CanceledCurriedPromise) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  // Promise |p3| will be resolved with.
  scoped_refptr<AbstractPromise> p2 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  p2->OnCanceled();

  scoped_refptr<AbstractPromise> p3 =
      ThenPromise(FROM_HERE, p1)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            // Resolve with a promise.
            ThreadTaskRunnerHandle::Get()->PostTask(
                FROM_HERE, BindOnce(&AbstractPromise::Execute, p2));
            p->emplace(p2);
            p->OnResolved();
          }));

  scoped_refptr<AbstractPromise> p4 =
      ThenPromise(FROM_HERE, p3)
          .With(BindLambdaForTesting(
              [&](AbstractPromise* p) { FAIL() << "Should not get here"; }));

  p1->OnResolved();
  RunLoop().RunUntilIdle();

  EXPECT_TRUE(p4->IsCanceled());
}

TEST_F(AbstractPromiseTest, CurriedPromiseChain) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);
  std::vector<int> run_order;

  // Promise |p3| will be resolved with.
  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, nullptr)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            run_order.push_back(2);
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  // Promise |p4| will be resolved with.
  scoped_refptr<AbstractPromise> p3 =
      ThenPromise(FROM_HERE, nullptr)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            run_order.push_back(3);
            // Resolve with a promise.
            ThreadTaskRunnerHandle::Get()->PostTask(
                FROM_HERE, BindOnce(&AbstractPromise::Execute, p2));
            p->emplace(std::move(p2));
            p->OnResolved();
          }));

  // Promise |p5| will be resolved with.
  scoped_refptr<AbstractPromise> p4 =
      ThenPromise(FROM_HERE, nullptr)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            run_order.push_back(4);
            // Resolve with a promise.
            ThreadTaskRunnerHandle::Get()->PostTask(
                FROM_HERE, BindOnce(&AbstractPromise::Execute, p3));
            p->emplace(std::move(p3));
            p->OnResolved();
          }));

  scoped_refptr<AbstractPromise> p5 =
      ThenPromise(FROM_HERE, p1)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            run_order.push_back(5);
            // Resolve with a promise.
            ThreadTaskRunnerHandle::Get()->PostTask(
                FROM_HERE, BindOnce(&AbstractPromise::Execute, p4));
            p->emplace(std::move(p4));
            p->OnResolved();
          }));

  scoped_refptr<AbstractPromise> p6 =
      ThenPromise(FROM_HERE, p3)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            run_order.push_back(6);
            p->emplace(Resolved<void>());
            p->OnResolved();
          }));

  p1->OnResolved();
  RunLoop().RunUntilIdle();

  EXPECT_THAT(run_order, ElementsAre(5, 4, 3, 2, 6));
}

TEST_F(AbstractPromiseTest, CurriedPromiseChainType2) {
  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p1)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            p->emplace(p1);
            p->OnResolved();
          }));

  scoped_refptr<AbstractPromise> p3 =
      ThenPromise(FROM_HERE, p2)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            p->emplace(p2);
            p->OnResolved();
          }));

  scoped_refptr<AbstractPromise> p4 =
      ThenPromise(FROM_HERE, p3)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            p->emplace(p3);
            p->OnResolved();
          }));

  p1->OnResolved();
  RunLoop().RunUntilIdle();

  EXPECT_TRUE(p4->IsResolved());
  EXPECT_EQ(p1.get(), p4->FindNonCurriedAncestor());
}

TEST_F(AbstractPromiseTest, CurriedPromiseMoveArg) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, nullptr)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
          }))
          .WithResolve(ArgumentPassingType::kMove);
  ;

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p0)
          .With(BindOnce(
              [](scoped_refptr<AbstractPromise> p1, AbstractPromise* p) {
                ThreadTaskRunnerHandle::Get()->PostTask(
                    FROM_HERE, BindOnce(&AbstractPromise::Execute, p1));
                p->emplace(std::move(p1));
                p->OnResolved();
              },
              std::move(p1)))
          .WithResolve(ArgumentPassingType::kMove);

  p0->OnResolved();
  RunLoop().RunUntilIdle();
}

TEST_F(AbstractPromiseTest, CatchCurriedPromise) {
  scoped_refptr<AbstractPromise> p0 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p1 =
      ThenPromise(FROM_HERE, nullptr)
          .With(CallbackResultType::kCanReject)
          .With(BindOnce([](AbstractPromise* p) {
            p->emplace(Rejected<void>());
            p->OnRejected();
          }));

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p0)
          .With(BindOnce(
              [&](scoped_refptr<AbstractPromise> p1, AbstractPromise* p) {
                // Resolve with a promise that can and does reject.
                ThreadTaskRunnerHandle::Get()->PostTask(
                    FROM_HERE, BindOnce(&AbstractPromise::Execute, p1));
                p->emplace(std::move(p1));
                p->OnResolved();
              },
              std::move(p1)));

  scoped_refptr<AbstractPromise> p3 = CatchPromise(FROM_HERE, p2);

  p0->OnResolved();
  EXPECT_FALSE(p3->IsResolved());

  RunLoop().RunUntilIdle();
  EXPECT_TRUE(p3->IsResolved());
}

TEST_F(AbstractPromiseTest, ThreadHopping) {
  std::unique_ptr<Thread> thread_a(new Thread("AbstractPromiseTest_Thread_A"));
  std::unique_ptr<Thread> thread_b(new Thread("AbstractPromiseTest_Thread_B"));
  std::unique_ptr<Thread> thread_c(new Thread("AbstractPromiseTest_Thread_C"));
  thread_a->Start();
  thread_b->Start();
  thread_c->Start();
  RunLoop run_loop;

  scoped_refptr<AbstractPromise> p1 =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  scoped_refptr<AbstractPromise> p2 =
      ThenPromise(FROM_HERE, p1)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
            CHECK(thread_a->task_runner()->BelongsToCurrentThread());
          }))
          .With(thread_a->task_runner());

  scoped_refptr<AbstractPromise> p3 =
      ThenPromise(FROM_HERE, p2)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
            CHECK(thread_b->task_runner()->BelongsToCurrentThread());
          }))
          .With(thread_b->task_runner());

  scoped_refptr<AbstractPromise> p4 =
      ThenPromise(FROM_HERE, p3)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
            CHECK(thread_c->task_runner()->BelongsToCurrentThread());
          }))
          .With(thread_c->task_runner());

  scoped_refptr<SingleThreadTaskRunner> main_thread =
      ThreadTaskRunnerHandle::Get();
  scoped_refptr<AbstractPromise> p5 =
      ThenPromise(FROM_HERE, p4)
          .With(BindLambdaForTesting([&](AbstractPromise* p) {
            p->emplace(Resolved<void>());
            p->OnResolved();
            run_loop.Quit();
            CHECK(main_thread->BelongsToCurrentThread());
          }))
          .With(main_thread);

  p1->OnResolved();

  EXPECT_FALSE(p5->IsResolved());
  run_loop.Run();
  EXPECT_TRUE(p2->IsResolved());
  EXPECT_TRUE(p3->IsResolved());
  EXPECT_TRUE(p4->IsResolved());
  EXPECT_TRUE(p5->IsResolved());

  thread_a->Stop();
  thread_b->Stop();
  thread_c->Stop();
}

TEST_F(AbstractPromiseTest, MutipleThreadsAddingDependants) {
  constexpr int num_threads = 4;
  constexpr int num_promises = 10000;

  std::unique_ptr<Thread> thread[num_threads];
  for (int i = 0; i < num_threads; i++) {
    thread[i] = std::make_unique<Thread>("Test thread");
    thread[i]->Start();
  }

  scoped_refptr<AbstractPromise> root =
      DoNothingPromiseBuilder(FROM_HERE).SetCanResolve(true);

  RunLoop run_loop;
  std::atomic<int> pending_count(num_promises);

  // After being called |num_promises| times |decrement_cb| will quit |run_loop|
  auto decrement_cb = BindLambdaForTesting([&](AbstractPromise* p) {
    int count = pending_count.fetch_sub(1, std::memory_order_acq_rel);
    if (count == 1)
      run_loop.Quit();
  });

  // Post a bunch of tasks on multiple threads that create Then promises
  // dependent on |root| which call |decrement_cb| when resolved.
  for (int i = 0; i < num_promises; i++) {
    thread[i % num_threads]->task_runner()->PostTask(
        FROM_HERE, BindLambdaForTesting([&]() {
          scoped_refptr<AbstractPromise> p =
              ThenPromise(FROM_HERE, root).With(decrement_cb);
        }));

    // Mid way through post a task to resolve |root|.
    if (i == num_promises / 2) {
      thread[i % num_threads]->task_runner()->PostTask(
          FROM_HERE, BindOnce(&AbstractPromise::OnResolved, root));
    }
  }

  // This should exit cleanly without any TSan errors.
  run_loop.Run();

  for (int i = 0; i < num_threads; i++) {
    thread[i]->Stop();
  }
}

}  // namespace internal
}  // namespace base
