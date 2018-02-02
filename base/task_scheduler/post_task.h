// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SCHEDULER_POST_TASK_H_
#define BASE_TASK_SCHEDULER_POST_TASK_H_

#include <utility>

#include "base/base_export.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/post_task_and_reply_with_result_internal.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "base/task_runner.h"
#include "base/task_scheduler/single_thread_task_runner_thread_mode.h"
#include "base/task_scheduler/task_traits.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace base {

// This is the preferred interface to post tasks to the TaskScheduler.
//
// To post a simple one-off task with default traits:
//     PostTask(FROM_HERE, Bind(...));
//
// To post a high priority one-off task to respond to a user interaction:
//     PostTaskWithTraits(
//         FROM_HERE,
//         {TaskPriority::USER_BLOCKING},
//         Bind(...));
//
// To post tasks that must run in sequence with default traits:
//     scoped_refptr<SequencedTaskRunner> task_runner =
//         CreateSequencedTaskRunnerWithTraits(TaskTraits());
//     task_runner.PostTask(FROM_HERE, Bind(...));
//     task_runner.PostTask(FROM_HERE, Bind(...));
//
// To post tasks that may block, must run in sequence and can be skipped on
// shutdown:
//     scoped_refptr<SequencedTaskRunner> task_runner =
//         CreateSequencedTaskRunnerWithTraits(
//             {MayBlock(), TaskShutdownBehavior::SKIP_ON_SHUTDOWN});
//     task_runner.PostTask(FROM_HERE, Bind(...));
//     task_runner.PostTask(FROM_HERE, Bind(...));
//
// The default traits apply to tasks that:
//     (1) don't block (ref. MayBlock() and WithBaseSyncPrimitives()),
//     (2) prefer inheriting the current priority to specifying their own, and
//     (3) can either block shutdown or be skipped on shutdown
//         (TaskScheduler implementation is free to choose a fitting default).
// Explicit traits must be specified for tasks for which these loose
// requirements are not sufficient.
//
// Tasks posted through functions below will run on threads owned by the
// registered TaskScheduler (i.e. not on the main thread). Tasks posted through
// functions below with a delay may be coalesced (i.e. delays may be adjusted to
// reduce the number of wakeups and hence power consumption).
//
// Prerequisite: A TaskScheduler must have been registered for the current
// process via TaskScheduler::SetInstance() before the functions below are
// valid. This is typically done during the initialization phase in each
// process. If your code is not running in that phase, you most likely don't
// have to worry about this. You will encounter DCHECKs or nullptr dereferences
// if this is violated. For tests, prefer base::test::ScopedTaskEnvironment.

// Posts |task| to the TaskScheduler. Calling this is equivalent to calling
// PostTaskWithTraits with plain TaskTraits.
BASE_EXPORT void PostTask(const Location& from_here, OnceClosure task);

// Posts |task| to the TaskScheduler. |task| will not run before |delay|
// expires. Calling this is equivalent to calling PostDelayedTaskWithTraits with
// plain TaskTraits.
//
// Use PostDelayedTaskWithTraits to specify a BACKGROUND priority if the task
// doesn't have to run as soon as |delay| expires.
BASE_EXPORT void PostDelayedTask(const Location& from_here,
                                 OnceClosure task,
                                 TimeDelta delay);

// Posts |task| to the TaskScheduler and posts |reply| on the caller's execution
// context (i.e. same sequence or thread and same TaskTraits if applicable) when
// |task| completes. Calling this is equivalent to calling
// PostTaskWithTraitsAndReply with plain TaskTraits. Can only be called when
// SequencedTaskRunnerHandle::IsSet().
BASE_EXPORT void PostTaskAndReply(const Location& from_here,
                                  OnceClosure task,
                                  OnceClosure reply);

// Posts |task| to the TaskScheduler and posts |reply| with the return value of
// |task| as argument on the caller's execution context (i.e. same sequence or
// thread and same TaskTraits if applicable) when |task| completes. Calling this
// is equivalent to calling PostTaskWithTraitsAndReplyWithResult with plain
// TaskTraits. Can only be called when SequencedTaskRunnerHandle::IsSet().
template <typename TaskReturnType, typename ReplyArgType>
void PostTaskAndReplyWithResult(const Location& from_here,
                                OnceCallback<TaskReturnType()> task,
                                OnceCallback<void(ReplyArgType)> reply) {
  PostTaskWithTraitsAndReplyWithResult(from_here, TaskTraits(), std::move(task),
                                       std::move(reply));
}

// Callback version of PostTaskAndReplyWithResult above.
// Though RepeatingCallback is convertible to OnceCallback, we need this since
// we can not use template deduction and object conversion at once on the
// overload resolution.
// TODO(tzik): Update all callers of the Callback version to use OnceCallback.
template <typename TaskReturnType, typename ReplyArgType>
void PostTaskAndReplyWithResult(const Location& from_here,
                                Callback<TaskReturnType()> task,
                                Callback<void(ReplyArgType)> reply) {
  PostTaskAndReplyWithResult(
      from_here, OnceCallback<TaskReturnType()>(std::move(task)),
      OnceCallback<void(ReplyArgType)>(std::move(reply)));
}

// Posts |task| with specific |traits| to the TaskScheduler.
BASE_EXPORT void PostTaskWithTraits(const Location& from_here,
                                    const TaskTraits& traits,
                                    OnceClosure task);

// Posts |task| with specific |traits| to the TaskScheduler. |task| will not run
// before |delay| expires.
//
// Specify a BACKGROUND priority via |traits| if the task doesn't have to run as
// soon as |delay| expires.
BASE_EXPORT void PostDelayedTaskWithTraits(const Location& from_here,
                                           const TaskTraits& traits,
                                           OnceClosure task,
                                           TimeDelta delay);

// Posts |task| with specific |traits| to the TaskScheduler and posts |reply| on
// the caller's execution context (i.e. same sequence or thread and same
// TaskTraits if applicable) when |task| completes. Can only be called when
// SequencedTaskRunnerHandle::IsSet().
BASE_EXPORT void PostTaskWithTraitsAndReply(const Location& from_here,
                                            const TaskTraits& traits,
                                            OnceClosure task,
                                            OnceClosure reply);

// Posts |task| with specific |traits| to the TaskScheduler and posts |reply|
// with the return value of |task| as argument on the caller's execution context
// (i.e. same sequence or thread and same TaskTraits if applicable) when |task|
// completes. Can only be called when SequencedTaskRunnerHandle::IsSet().
template <typename TaskReturnType, typename ReplyArgType>
void PostTaskWithTraitsAndReplyWithResult(
    const Location& from_here,
    const TaskTraits& traits,
    OnceCallback<TaskReturnType()> task,
    OnceCallback<void(ReplyArgType)> reply) {
  TaskReturnType* result = new TaskReturnType();
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
void PostTaskWithTraitsAndReplyWithResult(const Location& from_here,
                                          const TaskTraits& traits,
                                          Callback<TaskReturnType()> task,
                                          Callback<void(ReplyArgType)> reply) {
  PostTaskWithTraitsAndReplyWithResult(
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
// base/task_scheduler/single_thread_task_runner_thread_mode.h for |thread_mode|
// details. Tasks run on a single thread in posting order.
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
// base/task_scheduler/single_thread_task_runner_thread_mode.h for |thread_mode|
// details. Tasks run in the same Single-Threaded Apartment in posting order for
// the returned SingleThreadTaskRunner. There is not necessarily a one-to-one
// correspondence between SingleThreadTaskRunners and Single-Threaded
// Apartments. The implementation is free to share apartments or create new
// apartments as necessary. In either case, care should be taken to make sure
// COM pointers are not smuggled across apartments.
BASE_EXPORT scoped_refptr<SingleThreadTaskRunner>
CreateCOMSTATaskRunnerWithTraits(const TaskTraits& traits,
                                 SingleThreadTaskRunnerThreadMode thread_mode =
                                     SingleThreadTaskRunnerThreadMode::SHARED);
#endif  // defined(OS_WIN)

}  // namespace base

#endif  // BASE_TASK_SCHEDULER_POST_TASK_H_
