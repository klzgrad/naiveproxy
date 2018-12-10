// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/scheduler.h"

#include <algorithm>

#include "base/bind.h"
#include "tools/gn/standard_out.h"
#include "tools/gn/target.h"

namespace {}  // namespace

Scheduler* g_scheduler = nullptr;

Scheduler::Scheduler()
    : main_thread_run_loop_(MsgLoop::Current()),
      input_file_manager_(new InputFileManager),
      verbose_logging_(false),
      pool_work_count_lock_(),
      pool_work_count_cv_(),
      worker_pool_(),
      is_failed_(false),
      suppress_output_for_testing_(false),
      has_been_shutdown_(false) {
  g_scheduler = this;
}

Scheduler::~Scheduler() {
  WaitForPoolTasks();
  g_scheduler = nullptr;
}

bool Scheduler::Run() {
  main_thread_run_loop_->Run();
  bool local_is_failed;
  {
    std::lock_guard<std::mutex> lock(lock_);
    local_is_failed = is_failed();
    has_been_shutdown_ = true;
  }
  // Don't do this while holding |lock_|, since it will block on the workers,
  // which may be in turn waiting on the lock.
  WaitForPoolTasks();
  return !local_is_failed;
}

void Scheduler::Log(const std::string& verb, const std::string& msg) {
  task_runner()->PostTask(base::BindOnce(&Scheduler::LogOnMainThread,
                                         base::Unretained(this), verb, msg));
}

void Scheduler::FailWithError(const Err& err) {
  DCHECK(err.has_error());
  {
    std::lock_guard<std::mutex> lock(lock_);

    if (is_failed_ || has_been_shutdown_)
      return;  // Ignore errors once we see one.
    is_failed_ = true;
  }

  task_runner()->PostTask(base::BindOnce(&Scheduler::FailWithErrorOnMainThread,
                                         base::Unretained(this), err));
}

void Scheduler::ScheduleWork(Task work) {
  IncrementWorkCount();
  pool_work_count_.Increment();
  worker_pool_.PostTask(base::BindOnce(
      [](Scheduler* self, Task work) {
        std::move(work).Run();
        self->DecrementWorkCount();
        if (!self->pool_work_count_.Decrement()) {
          std::unique_lock<std::mutex> auto_lock(self->pool_work_count_lock_);
          self->pool_work_count_cv_.notify_one();
        }
      },
      this, std::move(work)));
}

void Scheduler::AddGenDependency(const base::FilePath& file) {
  std::lock_guard<std::mutex> lock(lock_);
  gen_dependencies_.push_back(file);
}

std::vector<base::FilePath> Scheduler::GetGenDependencies() const {
  std::lock_guard<std::mutex> lock(lock_);
  return gen_dependencies_;
}

void Scheduler::AddWrittenFile(const SourceFile& file) {
  std::lock_guard<std::mutex> lock(lock_);
  written_files_.push_back(file);
}

void Scheduler::AddUnknownGeneratedInput(const Target* target,
                                         const SourceFile& file) {
  std::lock_guard<std::mutex> lock(lock_);
  unknown_generated_inputs_.insert(std::make_pair(file, target));
}

void Scheduler::AddWriteRuntimeDepsTarget(const Target* target) {
  std::lock_guard<std::mutex> lock(lock_);
  write_runtime_deps_targets_.push_back(target);
}

std::vector<const Target*> Scheduler::GetWriteRuntimeDepsTargets() const {
  std::lock_guard<std::mutex> lock(lock_);
  return write_runtime_deps_targets_;
}

bool Scheduler::IsFileGeneratedByWriteRuntimeDeps(
    const OutputFile& file) const {
  std::lock_guard<std::mutex> lock(lock_);
  // Number of targets should be quite small, so brute-force search is fine.
  for (const Target* target : write_runtime_deps_targets_) {
    if (file == target->write_runtime_deps_output()) {
      return true;
    }
  }
  return false;
}

std::multimap<SourceFile, const Target*> Scheduler::GetUnknownGeneratedInputs()
    const {
  std::lock_guard<std::mutex> lock(lock_);

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
  std::lock_guard<std::mutex> lock(lock_);
  unknown_generated_inputs_.clear();
  written_files_.clear();
}

void Scheduler::IncrementWorkCount() {
  work_count_.Increment();
}

void Scheduler::DecrementWorkCount() {
  if (!work_count_.Decrement()) {
    task_runner()->PostTask(
        base::BindOnce(&Scheduler::OnComplete, base::Unretained(this)));
  }
}

void Scheduler::SuppressOutputForTesting(bool suppress) {
  std::lock_guard<std::mutex> lock(lock_);
  suppress_output_for_testing_ = suppress;
}

void Scheduler::LogOnMainThread(const std::string& verb,
                                const std::string& msg) {
  OutputString(verb, DECORATION_YELLOW);
  OutputString(" " + msg + "\n");
}

void Scheduler::FailWithErrorOnMainThread(const Err& err) {
  if (!suppress_output_for_testing_)
    err.PrintToStdout();
  task_runner()->PostQuit();
}

void Scheduler::OnComplete() {
  // Should be called on the main thread.
  DCHECK(task_runner() == MsgLoop::Current());
  task_runner()->PostQuit();
}

void Scheduler::WaitForPoolTasks() {
  std::unique_lock<std::mutex> lock(pool_work_count_lock_);
  while (!pool_work_count_.IsZero())
    pool_work_count_cv_.wait(lock);
}
