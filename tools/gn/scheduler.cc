// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/scheduler.h"

#include <algorithm>

#include "base/bind.h"
#include "base/single_thread_task_runner.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/thread_task_runner_handle.h"
#include "tools/gn/standard_out.h"
#include "tools/gn/target.h"

Scheduler* g_scheduler = nullptr;

Scheduler::Scheduler()
    : main_thread_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      input_file_manager_(new InputFileManager),
      verbose_logging_(false),
      pool_work_count_cv_(&pool_work_count_lock_),
      is_failed_(false),
      has_been_shutdown_(false) {
  g_scheduler = this;
}

Scheduler::~Scheduler() {
  WaitForPoolTasks();
  g_scheduler = nullptr;
}

bool Scheduler::Run() {
  runner_.Run();
  bool local_is_failed;
  {
    base::AutoLock lock(lock_);
    local_is_failed = is_failed();
    has_been_shutdown_ = true;
  }
  // Don't do this while holding |lock_|, since it will block on the workers,
  // which may be in turn waiting on the lock.
  WaitForPoolTasks();
  return !local_is_failed;
}

void Scheduler::Log(const std::string& verb, const std::string& msg) {
  if (task_runner()->BelongsToCurrentThread()) {
    LogOnMainThread(verb, msg);
  } else {
    // The run loop always joins on the sub threads, so the lifetime of this
    // object outlives the invocations of this function, hence "unretained".
    task_runner()->PostTask(FROM_HERE,
                            base::Bind(&Scheduler::LogOnMainThread,
                                       base::Unretained(this), verb, msg));
  }
}

void Scheduler::FailWithError(const Err& err) {
  DCHECK(err.has_error());
  {
    base::AutoLock lock(lock_);

    if (is_failed_ || has_been_shutdown_)
      return;  // Ignore errors once we see one.
    is_failed_ = true;
  }

  if (task_runner()->BelongsToCurrentThread()) {
    FailWithErrorOnMainThread(err);
  } else {
    // The run loop always joins on the sub threads, so the lifetime of this
    // object outlives the invocations of this function, hence "unretained".
    task_runner()->PostTask(FROM_HERE,
                            base::Bind(&Scheduler::FailWithErrorOnMainThread,
                                       base::Unretained(this), err));
  }
}

void Scheduler::ScheduleWork(base::OnceClosure work) {
  IncrementWorkCount();
  pool_work_count_.Increment();
  base::PostTaskWithTraits(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&Scheduler::DoWork, base::Unretained(this),
                     std::move(work)));
}

void Scheduler::AddGenDependency(const base::FilePath& file) {
  base::AutoLock lock(lock_);
  gen_dependencies_.push_back(file);
}

std::vector<base::FilePath> Scheduler::GetGenDependencies() const {
  base::AutoLock lock(lock_);
  return gen_dependencies_;
}

void Scheduler::AddWrittenFile(const SourceFile& file) {
  base::AutoLock lock(lock_);
  written_files_.push_back(file);
}

void Scheduler::AddUnknownGeneratedInput(const Target* target,
                                         const SourceFile& file) {
  base::AutoLock lock(lock_);
  unknown_generated_inputs_.insert(std::make_pair(file, target));
}

void Scheduler::AddWriteRuntimeDepsTarget(const Target* target) {
  base::AutoLock lock(lock_);
  write_runtime_deps_targets_.push_back(target);
}

std::vector<const Target*> Scheduler::GetWriteRuntimeDepsTargets() const {
  base::AutoLock lock(lock_);
  return write_runtime_deps_targets_;
}

bool Scheduler::IsFileGeneratedByWriteRuntimeDeps(
    const OutputFile& file) const {
  base::AutoLock lock(lock_);
  // Number of targets should be quite small, so brute-force search is fine.
  for (const Target* target : write_runtime_deps_targets_) {
    if (file == target->write_runtime_deps_output()) {
      return true;
    }
  }
  return false;
}

std::multimap<SourceFile, const Target*>
    Scheduler::GetUnknownGeneratedInputs() const {
  base::AutoLock lock(lock_);

  // Remove all unknown inputs that were written files. These are OK as inputs
  // to build steps since they were written as a side-effect of running GN.
  //
  // It's assumed that this function is called once during cleanup to check for
  // errors, so performing this work in the lock doesn't matter.
  std::multimap<SourceFile, const Target*> filtered = unknown_generated_inputs_;
  for (const SourceFile& file : written_files_)
    filtered.erase(file);

  return filtered;
}

void Scheduler::ClearUnknownGeneratedInputsAndWrittenFiles() {
  base::AutoLock lock(lock_);
  unknown_generated_inputs_.clear();
  written_files_.clear();
}

void Scheduler::IncrementWorkCount() {
  work_count_.Increment();
}

void Scheduler::DecrementWorkCount() {
  if (!work_count_.Decrement()) {
    if (task_runner()->BelongsToCurrentThread()) {
      OnComplete();
    } else {
      task_runner()->PostTask(FROM_HERE, base::Bind(&Scheduler::OnComplete,
                                                    base::Unretained(this)));
    }
  }
}

void Scheduler::LogOnMainThread(const std::string& verb,
                                const std::string& msg) {
  OutputString(verb, DECORATION_YELLOW);
  OutputString(" " + msg + "\n");
}

void Scheduler::FailWithErrorOnMainThread(const Err& err) {
  err.PrintToStdout();
  runner_.Quit();
}

void Scheduler::DoWork(base::OnceClosure closure) {
  std::move(closure).Run();
  DecrementWorkCount();
  if (!pool_work_count_.Decrement()) {
    base::AutoLock auto_lock(pool_work_count_lock_);
    pool_work_count_cv_.Signal();
  }
}

void Scheduler::OnComplete() {
  // Should be called on the main thread.
  DCHECK(task_runner()->BelongsToCurrentThread());
  runner_.Quit();
}

void Scheduler::WaitForPoolTasks() {
  base::AutoLock lock(pool_work_count_lock_);
  while (!pool_work_count_.IsZero())
    pool_work_count_cv_.Wait();
}
