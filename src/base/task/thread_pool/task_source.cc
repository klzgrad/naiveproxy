// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/thread_pool/task_source.h"

#include <utility>

#include "base/feature_list.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/task/task_features.h"
#include "base/task/thread_pool/task_tracker.h"

namespace base {
namespace internal {

TaskSource::RunIntent::RunIntent(RunIntent&& other) noexcept
    : task_source_(other.task_source_),
      run_step_(other.run_step_),
      is_saturated_(other.is_saturated_) {
  other.task_source_ = nullptr;
}

TaskSource::RunIntent::~RunIntent() {
  DCHECK_EQ(task_source_, nullptr);
}

TaskSource::RunIntent& TaskSource::RunIntent::operator=(RunIntent&& other) {
  DCHECK_EQ(task_source_, nullptr);
  task_source_ = other.task_source_;
  other.task_source_ = nullptr;
  run_step_ = other.run_step_;
  is_saturated_ = other.is_saturated_;
  return *this;
}

TaskSource::RunIntent::RunIntent(const TaskSource* task_source,
                                 Saturated is_saturated)
    : task_source_(task_source), is_saturated_(is_saturated) {}

TaskSource::Transaction::Transaction(TaskSource* task_source)
    : task_source_(task_source) {
  task_source->lock_.Acquire();
}

TaskSource::Transaction::Transaction(TaskSource::Transaction&& other)
    : task_source_(other.task_source()) {
  other.task_source_ = nullptr;
}

TaskSource::Transaction::~Transaction() {
  if (task_source_) {
    task_source_->lock_.AssertAcquired();
    task_source_->lock_.Release();
  }
}

Optional<Task> TaskSource::Transaction::TakeTask(RunIntent* intent) {
  DCHECK_EQ(intent->task_source_, task_source());
  DCHECK_EQ(intent->run_step_, RunIntent::State::kInitial);
  intent->run_step_ = RunIntent::State::kTaskAcquired;
  return task_source_->TakeTask();
}

bool TaskSource::Transaction::DidProcessTask(RunIntent intent,
                                             RunResult run_result) {
  DCHECK_EQ(intent.task_source_, task_source());
  DCHECK_EQ(intent.run_step_, RunIntent::State::kTaskAcquired);
  intent.run_step_ = RunIntent::State::kCompleted;
  intent.Release();
  return task_source_->DidProcessTask(run_result);
}

SequenceSortKey TaskSource::Transaction::GetSortKey() const {
  return task_source_->GetSortKey();
}

void TaskSource::Transaction::Clear() {
  task_source_->Clear();
}

void TaskSource::Transaction::UpdatePriority(TaskPriority priority) {
  if (FeatureList::IsEnabled(kAllTasksUserBlocking))
    return;
  task_source_->traits_.UpdatePriority(priority);
}

TaskSource::RunIntent TaskSource::MakeRunIntent(Saturated is_saturated) const {
  return RunIntent(this, is_saturated);
}

void TaskSource::SetHeapHandle(const HeapHandle& handle) {
  heap_handle_ = handle;
}

void TaskSource::ClearHeapHandle() {
  heap_handle_ = HeapHandle();
}

TaskSource::TaskSource(const TaskTraits& traits,
                       TaskRunner* task_runner,
                       TaskSourceExecutionMode execution_mode)
    : traits_(traits),
      task_runner_(task_runner),
      execution_mode_(execution_mode) {
  DCHECK(task_runner_ ||
         execution_mode_ == TaskSourceExecutionMode::kParallel ||
         execution_mode_ == TaskSourceExecutionMode::kJob);
}

TaskSource::~TaskSource() = default;

TaskSource::Transaction TaskSource::BeginTransaction() {
  return Transaction(this);
}

RegisteredTaskSource::RegisteredTaskSource() = default;

RegisteredTaskSource::RegisteredTaskSource(std::nullptr_t)
    : RegisteredTaskSource() {}

RegisteredTaskSource::RegisteredTaskSource(
    RegisteredTaskSource&& other) noexcept = default;

RegisteredTaskSource::~RegisteredTaskSource() {
  Unregister();
}

//  static
RegisteredTaskSource RegisteredTaskSource::CreateForTesting(
    scoped_refptr<TaskSource> task_source,
    TaskTracker* task_tracker) {
  return RegisteredTaskSource(std::move(task_source), task_tracker);
}

scoped_refptr<TaskSource> RegisteredTaskSource::Unregister() {
  if (task_source_ && task_tracker_)
    return task_tracker_->UnregisterTaskSource(std::move(task_source_));
  return std::move(task_source_);
}

RegisteredTaskSource& RegisteredTaskSource::operator=(
    RegisteredTaskSource&& other) {
  Unregister();
  task_source_ = std::move(other.task_source_);
  task_tracker_ = other.task_tracker_;
  other.task_tracker_ = nullptr;
  return *this;
}

RegisteredTaskSource::RegisteredTaskSource(
    scoped_refptr<TaskSource> task_source,
    TaskTracker* task_tracker)
    : task_source_(std::move(task_source)), task_tracker_(task_tracker) {}

}  // namespace internal
}  // namespace base
