// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_POST_TASK_H_
#define BASE_TASK_POST_TASK_H_

#include <memory>
#include <utility>

#include "base/base_export.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/post_task_and_reply_with_result_internal.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "base/task/single_thread_task_runner_thread_mode.h"
#include "base/task/task_traits.h"
#include "base/task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace base {

// This is the interface to post tasks.
//
// To post a simple one-off task with default traits:
//     PostTask(FROM_HERE, BindOnce(...));
//
// To post a high priority one-off task to respond to a user interaction:
//     PostTaskWithTraits(
//         FROM_HERE,
//         {TaskPriority::USER_BLOCKING},
//         BindOnce(...));
//
// To post tasks that must run in sequence with default traits:
//     scoped_refptr<SequencedTaskRunner> task_runner =
//         CreateSequencedTaskRunnerWithTraits(TaskTraits());
//     task_runner->PostTask(FROM_HERE, BindOnce(...));
//     task_runner->PostTask(FROM_HERE, BindOnce(...));
//
// To post tasks that may block, must run in sequence and can be skipped on
// shutdown:
//     scoped_refptr<SequencedTaskRunner> task_runner =
//         CreateSequencedTaskRunnerWithTraits(
//             {MayBlock(), TaskShutdownBehavior::SKIP_ON_SHUTDOWN});
//     task_runner->PostTask(FROM_HERE, BindOnce(...));
//     task_runner->PostTask(FROM_HERE, BindOnce(...));
//
// The default traits apply to tasks that:
//     (1) don't block (ref. MayBlock() and WithBaseSyncPrimitives()),
//     (2) prefer inheriting the current priority to specifying their own, and
//     (3) can either block shutdown or be skipped on shutdown
//         (implementation is free to choose a fitting default).
// Explicit traits must be specified for tasks for which these loose
// requirements are not sufficient.
//
// Tasks posted with only traits defined in base/task/task_traits.h run on
// threads owned by the registered TaskScheduler (i.e. not on the main thread).
// An embedder (e.g. Chrome) can define additional traits to make tasks run on
// threads of their choosing. TODO(https://crbug.com/863341): Make this a
// reality.
//
// Tasks posted with the same traits will be scheduled in the order they were
// posted. IMPORTANT: Please note however that, unless the traits imply a
// single thread or sequence, this doesn't guarantee any *execution ordering*
// for tasks posted in a given order (being scheduled first doesn't mean it will
// run first -- could run in parallel or have its physical thread preempted).
//
// Prerequisite: A TaskScheduler must have been registered for the current
// process via TaskScheduler::SetInstance() before the functions below are
// valid. This is typically done during the initialization phase in each
// process. If your code is not running in that phase, you most likely don't
// have to worry about this. You will encounter DCHECKs or nullptr dereferences
// if this is violated. For tests, prefer base::test::ScopedTaskEnvironment.

// Equivalent to calling PostTaskWithTraits with default TaskTraits.
BASE_EXPORT bool PostTask(const Location& from_here, OnceClosure task);

// Equivalent to calling PostDelayedTaskWithTraits with default TaskTraits.
//
// Use PostDelayedTaskWithTraits to specify a BEST_EFFORT priority if the task
// doesn't have to run as soon as |delay| expires.
BASE_EXPORT bool PostDelayedTask(const Location& from_here,
                                 OnceClosure task,
                                 TimeDelta delay);

// Equivalent to calling PostTaskWithTraitsAndReply with default TaskTraits.
BASE_EXPORT bool PostTaskAndReply(const Location& from_here,
                                  OnceClosure task,
                                  OnceClosure reply);

// Equivalent to calling PostTaskWithTraitsAndReplyWithResult with default
// TaskTraits.
template <typename TaskReturnType, typename ReplyArgType>
bool PostTaskAndReplyWithResult(const Location& from_here,
                                OnceCallback<TaskReturnType()> task,
                                OnceCallback<void(ReplyArgType)> reply) {
  return PostTaskWithTraitsAndReplyWithResult(
      from_here, TaskTraits(), std::move(task), std::move(reply));
}

// Callback version of PostTaskAndReplyWithResult above.
// Though RepeatingCallback is convertible to OnceCallback, we need this since
// we can not use template deduction and object conversion at once on the
// overload resolution.
// TODO(tzik): Update all callers of the Callback version to use OnceCallback.
template <typename TaskReturnType, typename ReplyArgType>
bool PostTaskAndReplyWithResult(const Location& from_here,
                                Callback<TaskReturnType()> task,
                                Callback<void(ReplyArgType)> reply) {
  return PostTaskAndReplyWithResult(
      from_here, OnceCallback<TaskReturnType()>(std::move(task)),
      OnceCallback<void(ReplyArgType)>(std::move(reply)));
}

// Posts |task| with specific |traits|. Returns false if the task definitely
// won't run because of current shutdown state.
BASE_EXPORT bool PostTaskWithTraits(const Location& from_here,
                                    const TaskTraits& traits,
                                    OnceClosure task);

// Posts |task| with specific |traits|. |task| will not run before |delay|
// expires. Returns false if the task definitely won't run because of current
// shutdown state.
//
// Specify a BEST_EFFORT priority via |traits| if the task doesn't have to run
// as soon as |delay| expires.
BASE_EXPORT bool PostDelayedTaskWithTraits(const Location& from_here,
                                           const TaskTraits& traits,
                                           OnceClosure task,
                                           TimeDelta delay);

// Posts |task| with specific |traits| and posts |reply| on the caller's
// execution context (i.e. same sequence or thread and same TaskTraits if
// applicable) when |task| completes. Returns false if the task definitely won't
// run because of current shutdown state. Can only be called when
// SequencedTaskRunnerHandle::IsSet().
BASE_EXPORT bool PostTaskWithTraitsAndReply(const Location& from_here,
                                            const TaskTraits& traits,
                                            OnceClosure task,
                                            OnceClosure reply);

// Posts |task| with specific |traits| and posts |reply| with the return value
// of |task| as argument on the caller's execution context (i.e. same sequence
// or thread and same TaskTraits if applicable) when |task| completes. Returns
// false if the task definitely won't run because of current shutdown state. Can
// only be called when SequencedTaskRunnerHandle::IsSet().
template <typename TaskReturnType, typename ReplyArgType>
bool PostTaskWithTraitsAndReplyWithResult(
    const Location& from_here,
    const TaskTraits& traits,
    OnceCallback<TaskReturnType()> task,
    OnceCallback<void(ReplyArgType)> reply) {
  auto* result = new std::unique_ptr<TaskReturnType>();
  return PostTaskWithTraitsAndReply(
      from_here, traits,
      BindOnce(&internal::ReturnAsParamAdapter<TaskReturnType>, std::move(task),
               result),
      BindOnce(&internal::ReplyAdapter<TaskReturnType, ReplyArgType>,
               std::move(reply), Owned(result)));
}

// Callback version of PostTaskWithTraitsAndReplyWithResult above.
// Though RepeatingCallback is convertible to OnceCallback, we need this since
// we can not use template deduction and object conversion at once on the
// overload resolution.
// TODO(tzik): Update all callers of the Callback version to use OnceCallback.
template <typename TaskReturnType, typename ReplyArgType>
bool PostTaskWithTraitsAndReplyWithResult(const Location& from_here,
                                          const TaskTraits& traits,
                                          Callback<TaskReturnType()> task,
                                          Callback<void(ReplyArgType)> reply) {
  return PostTaskWithTraitsAndReplyWithResult(
      from_here, traits, OnceCallback<TaskReturnType()>(std::move(task)),
      OnceCallback<void(ReplyArgType)>(std::move(reply)));
}

// Returns a TaskRunner whose PostTask invocations result in scheduling tasks
// using |traits|. Tasks may run in any order and in parallel.
BASE_EXPORT scoped_refptr<TaskRunner> CreateTaskRunnerWithTraits(
    const TaskTraits& traits);

// Returns a SequencedTaskRunner whose PostTask invocations result in scheduling
// tasks using |traits|. Tasks run one at a time in posting order.
BASE_EXPORT scoped_refptr<SequencedTaskRunner>
CreateSequencedTaskRunnerWithTraits(const TaskTraits& traits);

// Returns a SingleThreadTaskRunner whose PostTask invocations result in
// scheduling tasks using |traits| on a thread determined by |thread_mode|. See
// base/task/single_thread_task_runner_thread_mode.h for |thread_mode| details.
// If |traits| identifies an existing thread,
// SingleThreadTaskRunnerThreadMode::SHARED must be used. Tasks run on a single
// thread in posting order.
//
// If all you need is to make sure that tasks don't run concurrently (e.g.
// because they access a data structure which is not thread-safe), use
// CreateSequencedTaskRunnerWithTraits(). Only use this if you rely on a thread-
// affine API (it might be safer to assume thread-affinity when dealing with
// under-documented third-party APIs, e.g. other OS') or share data across tasks
// using thread-local storage.
BASE_EXPORT scoped_refptr<SingleThreadTaskRunner>
CreateSingleThreadTaskRunnerWithTraits(
    const TaskTraits& traits,
    SingleThreadTaskRunnerThreadMode thread_mode =
        SingleThreadTaskRunnerThreadMode::SHARED);

#if defined(OS_WIN)
// Returns a SingleThreadTaskRunner whose PostTask invocations result in
// scheduling tasks using |traits| in a COM Single-Threaded Apartment on a
// thread determined by |thread_mode|. See
// base/task/single_thread_task_runner_thread_mode.h for |thread_mode| details.
// If |traits| identifies an existing thread,
// SingleThreadTaskRunnerThreadMode::SHARED must be used. Tasks run in the same
// Single-Threaded Apartment in posting order for the returned
// SingleThreadTaskRunner. There is not necessarily a one-to-one correspondence
// between SingleThreadTaskRunners and Single-Threaded Apartments. The
// implementation is free to share apartments or create new apartments as
// necessary. In either case, care should be taken to make sure COM pointers are
// not smuggled across apartments.
BASE_EXPORT scoped_refptr<SingleThreadTaskRunner>
CreateCOMSTATaskRunnerWithTraits(const TaskTraits& traits,
                                 SingleThreadTaskRunnerThreadMode thread_mode =
                                     SingleThreadTaskRunnerThreadMode::SHARED);
#endif  // defined(OS_WIN)

}  // namespace base

#endif  // BASE_TASK_POST_TASK_H_
