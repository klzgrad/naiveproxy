// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/gn/scheduler.h"

#include <algorithm>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"
#include "tools/gn/standard_out.h"
#include "tools/gn/switches.h"
#include "tools/gn/target.h"

#if defined(OS_WIN)
#include <windows.h>
#else
#include <unistd.h>
#endif

Scheduler* g_scheduler = nullptr;

namespace {

#if defined(OS_WIN)
int GetCPUCount() {
  SYSTEM_INFO sysinfo;
  ::GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
}
#else
int GetCPUCount() {
  return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
}
#endif

int GetThreadCount() {
  std::string thread_count =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kThreads);

  // See if an override was specified on the command line.
  int result;
  if (!thread_count.empty() && base::StringToInt(thread_count, &result))
    return result;

  // Base the default number of worker threads on number of cores in the
  // system. When building large projects, the speed can be limited by how fast
  // the main thread can dispatch work and connect the dependency graph. If
  // there are too many worker threads, the main thread can be starved and it
  // will run slower overall.
  //
  // One less worker thread than the number of physical CPUs seems to be a
  // good value, both theoretically and experimentally. But always use at
  // least some workers to prevent us from being too sensitive to I/O latency
  // on low-end systems.
  //
  // The minimum thread count is based on measuring the optimal threads for the
  // Chrome build on a several-year-old 4-core MacBook.
  int num_cores = GetCPUCount() / 2;  // Almost all CPUs now are hyperthreaded.
  return std::max(num_cores - 1, 8);
}

}  // namespace

Scheduler::Scheduler()
    : pool_(new base::SequencedWorkerPool(GetThreadCount(),
                                          "worker_",
                                          base::TaskPriority::USER_VISIBLE)),
      input_file_manager_(new InputFileManager),
      verbose_logging_(false),
      work_count_(0),
      is_failed_(false),
      has_been_shutdown_(false) {
  g_scheduler = this;
}

Scheduler::~Scheduler() {
  if (!has_been_shutdown_)
    pool_->Shutdown();
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
  // Don't do this inside the lock since it will block on the workers, which
  // may be in turn waiting on the lock.
  pool_->Shutdown();
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

void Scheduler::ScheduleWork(const base::Closure& work) {
  IncrementWorkCount();
  pool_->PostWorkerTaskWithShutdownBehavior(
      FROM_HERE, base::Bind(&Scheduler::DoWork,
                            base::Unretained(this), work),
      base::SequencedWorkerPool::BLOCK_SHUTDOWN);
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

void Scheduler::DoWork(const base::Closure& closure) {
  closure.Run();
  DecrementWorkCount();
}

void Scheduler::OnComplete() {
  // Should be called on the main thread.
  DCHECK(task_runner()->BelongsToCurrentThread());
  runner_.Quit();
}
