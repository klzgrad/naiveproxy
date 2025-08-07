// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/self_compaction_manager.h"

#include <sys/mman.h>

#include "base/feature_list.h"
#include "base/logging.h"
#include "base/memory/page_size.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_functions.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/task/thread_pool.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/trace_event/named_trigger.h"  // no-presubmit-check
#include "base/trace_event/trace_event.h"

namespace base::android {
BASE_FEATURE(kShouldFreezeSelf, "ShouldFreezeSelf", FEATURE_ENABLED_BY_DEFAULT);

// Max amount of compaction to do in each chunk, measured in MiB.
BASE_FEATURE_PARAM(size_t,
                   kShouldFreezeSelfMaxSize,
                   &kShouldFreezeSelf,
                   "max_chunk_size",
                   100);

// Delay between running pre-freeze tasks and doing self-freeze, measured in s.
BASE_FEATURE_PARAM(size_t,
                   kShouldFreezeSelfDelayAfterPreFreezeTasks,
                   &kShouldFreezeSelf,
                   "delay_after_tasks",
                   30);

BASE_FEATURE(kUseRunningCompact,
             "UseRunningCompact",
             FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE_PARAM(size_t,
                   kUseRunningCompactDelayAfterPreFreezeTasks,
                   &kUseRunningCompact,
                   "running_compact_delay_after_tasks",
                   30);
BASE_FEATURE_PARAM(size_t,
                   kUseRunningCompactMaxSize,
                   &kUseRunningCompact,
                   "running_compact_max_chunk_size",
                   10);

namespace {

bool IsMadvisePageoutSupported() {
  static bool supported = []() -> bool {
#if defined(MADV_PAGEOUT)
    // To determine if MADV_PAGEOUT is supported we will try calling it with an
    // invalid memory area.
    // madvise(2) first checks the mode first, returning -EINVAL if it's
    // unknown. Next, it will always return 0 for a zero length VMA before
    // validating if it's mapped.
    // So, in this case, we can test for support with any page aligned address
    // with a zero length.
    int res =
        madvise(reinterpret_cast<void*>(base::GetPageSize()), 0, MADV_PAGEOUT);
    if (res < 0 && errno == -EINVAL)
      return false;
    PLOG_IF(ERROR, res < 0) << "Unexpected return from madvise";
    if (res == 0)
      return true;
#endif
    return false;
  }();
  return supported;
}

// Based on UMA data, >99.5% of the compaction should take < 6s, so 10s should
// be more than enough.
constexpr base::TimeDelta kCompactionTimeout = base::Seconds(10);

uint64_t MiBToBytes(uint64_t v) {
  return v * 1024 * 1024;
}

std::string GetSelfCompactionMetricName(std::string_view name) {
  return StrCat({"Memory.SelfCompact2.Renderer.", name});
}

std::string GetRunningCompactionMetricName(std::string_view name) {
  return StrCat({"Memory.RunningCompact.Renderer.", name});
}

class SelfCompactionState final
    : public SelfCompactionManager::CompactionState {
 public:
  SelfCompactionState(scoped_refptr<SequencedTaskRunner> task_runner,
                      base::TimeTicks triggered_at)
      : SelfCompactionState(std::move(task_runner),
                            triggered_at,
                            MiBToBytes(kShouldFreezeSelfMaxSize.Get())) {}

  SelfCompactionState(scoped_refptr<SequencedTaskRunner> task_runner,
                      base::TimeTicks triggered_at,
                      uint64_t max_bytes)
      : CompactionState(std::move(task_runner), triggered_at, max_bytes) {}

  bool IsFeatureEnabled() const override {
    return base::FeatureList::IsEnabled(kShouldFreezeSelf);
  }

  base::TimeDelta GetDelayAfterPreFreezeTasks() const override {
    return base::Seconds(kShouldFreezeSelfDelayAfterPreFreezeTasks.Get());
  }

  std::string GetMetricName(std::string_view name) const override {
    return GetSelfCompactionMetricName(name);
  }

  scoped_refptr<SelfCompactionManager::CompactionMetric> MakeCompactionMetric(
      base::TimeTicks started_at) const override {
    return MakeRefCounted<SelfCompactionManager::CompactionMetric>(
        "Memory.SelfCompact2.Renderer.", triggered_at_, started_at);
  }
};

class RunningCompactionState final
    : public SelfCompactionManager::CompactionState {
 public:
  RunningCompactionState(scoped_refptr<SequencedTaskRunner> task_runner,
                         base::TimeTicks triggered_at)
      : RunningCompactionState(std::move(task_runner),
                               triggered_at,
                               MiBToBytes(kUseRunningCompactMaxSize.Get())) {}

  RunningCompactionState(scoped_refptr<SequencedTaskRunner> task_runner,
                         base::TimeTicks triggered_at,
                         uint64_t max_bytes)
      : CompactionState(std::move(task_runner), triggered_at, max_bytes) {}

  bool IsFeatureEnabled() const override {
    return base::FeatureList::IsEnabled(kUseRunningCompact);
  }

  base::TimeDelta GetDelayAfterPreFreezeTasks() const override {
    return base::Seconds(kUseRunningCompactDelayAfterPreFreezeTasks.Get());
  }

  std::string GetMetricName(std::string_view name) const override {
    return GetRunningCompactionMetricName(name);
  }

  scoped_refptr<SelfCompactionManager::CompactionMetric> MakeCompactionMetric(
      base::TimeTicks started_at) const override {
    return MakeRefCounted<SelfCompactionManager::CompactionMetric>(
        "Memory.RunningCompact.Renderer.", triggered_at_, started_at);
  }
};

}  // namespace

SelfCompactionManager::SelfCompactionManager() = default;

// static
SelfCompactionManager& SelfCompactionManager::Instance() {
  static base::NoDestructor<SelfCompactionManager> instance;
  return *instance;
}

// static
void SelfCompactionManager::SetOnStartSelfCompactionCallback(
    base::RepeatingCallback<void(void)> callback) {
  base::AutoLock locker(PreFreezeBackgroundMemoryTrimmer::lock());
  Instance().on_self_compact_callback_ = callback;
}

// static
bool SelfCompactionManager::ShouldContinueCompaction(
    const SelfCompactionManager::CompactionState& state) {
  return ShouldContinueCompaction(state.triggered_at_);
}

// static
bool SelfCompactionManager::TimeoutExceeded() {
  base::AutoLock locker(lock());
  return Instance().compaction_last_started_ + kCompactionTimeout <=
         base::TimeTicks::Now();
}

// static
bool SelfCompactionManager::CompactionIsSupported() {
  return IsMadvisePageoutSupported();
}

// static
bool SelfCompactionManager::ShouldContinueCompaction(
    base::TimeTicks compaction_triggered_at) {
  base::AutoLock locker(lock());
  return Instance().compaction_last_cancelled_ < compaction_triggered_at;
}

// static
base::TimeDelta SelfCompactionManager::GetDelayBetweenCompaction() {
  // We choose a random, small amount of time here, so that we are not trying
  // to compact in every process at the same time.
  return base::Milliseconds(base::RandInt(100, 300));
}

void SelfCompactionManager::MaybeRunOnSelfCompactCallback() {
  if (on_self_compact_callback_) {
    on_self_compact_callback_.Run();
  }
}

void SelfCompactionManager::MaybeCancelCompaction(
    base::android::CompactCancellationReason cancellation_reason) {
  base::AutoLock locker(lock());
  Instance().process_compacted_metadata_.reset();
  Instance().MaybeCancelCompactionInternal(cancellation_reason);
}

void SelfCompactionManager::MaybeCancelCompactionInternal(
    CompactCancellationReason cancellation_reason) {
  // Check for the last time cancelled here in order to avoid recording this
  // metric multiple times. Also, only record this metric if a compaction is
  // currently running.
  if (compaction_last_cancelled_ < compaction_last_triggered_ &&
      compaction_last_finished_ < compaction_last_triggered_) {
    UmaHistogramEnumeration(
        "Memory.RunningOrSelfCompact.Renderer.Cancellation.Reason",
        cancellation_reason);
  }
  compaction_last_finished_ = compaction_last_cancelled_ =
      base::TimeTicks::Now();
}

// static
void SelfCompactionManager::OnRunningCompact() {
  TRACE_EVENT0("base", "OnRunningCompact");

  auto task_runner = base::ThreadPool::CreateSequencedTaskRunner(
      {base::TaskPriority::BEST_EFFORT, MayBlock()});
  Instance().OnTriggerCompact<RunningCompactionState>(std::move(task_runner));
}

// static
void SelfCompactionManager::OnSelfFreeze() {
  TRACE_EVENT0("base", "OnSelfFreeze");

  auto task_runner = base::ThreadPool::CreateSequencedTaskRunner(
      {base::TaskPriority::BEST_EFFORT, MayBlock()});
  Instance().OnTriggerCompact<SelfCompactionState>(std::move(task_runner));
}

void SelfCompactionManager::OnTriggerCompact(
    std::unique_ptr<CompactionState> state) {
  if (state->IsFeatureEnabled()) {
    PreFreezeBackgroundMemoryTrimmer::Instance().RunPreFreezeTasks();
  }
  const auto delay_after_pre_freeze_tasks =
      state->GetDelayAfterPreFreezeTasks();
  const auto task_runner = state->task_runner_;
  task_runner->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SelfCompactionManager::CompactSelf, std::move(state)),
      delay_after_pre_freeze_tasks);
}

template <class State>
void SelfCompactionManager::OnTriggerCompact(
    scoped_refptr<SequencedTaskRunner> task_runner) {
  const auto triggered_at = base::TimeTicks::Now();
  base::AutoLock locker(lock());
  compaction_last_triggered_ = triggered_at;
  auto state = std::make_unique<State>(task_runner, triggered_at);
  OnTriggerCompact(std::move(state));
}

void SelfCompactionManager::StartCompaction(
    std::unique_ptr<CompactionState> state) {
  scoped_refptr<CompactionMetric> metric;
  {
    base::AutoLock locker(lock());
    compaction_last_started_ = base::TimeTicks::Now();
    metric = state->MakeCompactionMetric(compaction_last_started_);
    TRACE_EVENT0("base", "StartCompaction");
    base::trace_event::EmitNamedTrigger("start-self-compaction");
    process_compacted_metadata_.emplace(
        "PreFreezeBackgroundMemoryTrimmer.ProcessCompacted",
        /*is_compacted=*/1, base::SampleMetadataScope::kProcess);
    MaybeRunOnSelfCompactCallback();
  }
  metric->RecordBeforeMetrics();
  MaybePostCompactionTask(std::move(state), std::move(metric));
}

void SelfCompactionManager::MaybePostCompactionTask(
    std::unique_ptr<CompactionState> state,
    scoped_refptr<CompactionMetric> metric) {
  TRACE_EVENT0("base", "MaybePostCompactionTask");
  // Compaction is taking too long, so cancel it. This happens in practice in
  // the field sometimes, according to UMA data.
  if (TimeoutExceeded()) {
    MaybeCancelCompaction(CompactCancellationReason::kTimeout);
    // We do not return here, despite the fact that we will not be doing any
    // more compaction, in order to run |FinishCompaction| below.
  }

  if (ShouldContinueCompaction(*state) && !state->regions_.empty()) {
    auto task_runner = state->task_runner_;
    task_runner->PostDelayedTask(
        FROM_HERE,
        // |base::Unretained| is safe here because we never destroy |this|.
        base::BindOnce(&SelfCompactionManager::CompactionTask,
                       base::Unretained(this), std::move(state),
                       std::move(metric)),
        GetDelayBetweenCompaction());
  } else {
    FinishCompaction(std::move(state), std::move(metric));
  }
}

void SelfCompactionManager::CompactionTask(
    std::unique_ptr<CompactionState> state,
    scoped_refptr<CompactionMetric> metric) {
  if (!ShouldContinueCompaction(*state)) {
    return;
  }

  TRACE_EVENT0("base", "CompactionTask");

  CompactMemory(&state->regions_, state->max_bytes_);

  MaybePostCompactionTask(std::move(state), std::move(metric));
}

void SelfCompactionManager::FinishCompaction(
    std::unique_ptr<CompactionState> state,
    scoped_refptr<CompactionMetric> metric) {
  TRACE_EVENT0("base", "FinishCompaction");
  {
    base::AutoLock locker(lock());
    compaction_last_finished_ = base::TimeTicks::Now();
  }
  if (ShouldContinueCompaction(*state)) {
    metric->RecordDelayedMetrics();
    base::AutoLock locker(lock());
    metric->RecordTimeMetrics(compaction_last_finished_,
                              compaction_last_cancelled_);
  }
}

// static
void SelfCompactionManager::CompactSelf(
    std::unique_ptr<CompactionState> state) {
  // MADV_PAGEOUT was only added in Linux 5.4, so do nothing in earlier
  // versions.
  if (!CompactionIsSupported()) {
    return;
  }

  if (!ShouldContinueCompaction(*state)) {
    return;
  }

  TRACE_EVENT0("base", "CompactSelf");
  state->MaybeReadProcMaps();

  // We still start the task in the control group, in order to record metrics.
  Instance().StartCompaction(std::move(state));
}

// static
std::optional<uint64_t> SelfCompactionManager::CompactRegion(
    debug::MappedMemoryRegion region) {
#if defined(MADV_PAGEOUT)
  using Permission = debug::MappedMemoryRegion::Permission;
  // Skip file-backed regions
  if (region.inode != 0 || region.dev_major != 0) {
    return 0;
  }
  // Skip shared regions
  if ((region.permissions & Permission::PRIVATE) == 0) {
    return 0;
  }

  const bool is_inaccessible =
      (region.permissions &
       (Permission::READ | Permission::WRITE | Permission::EXECUTE)) == 0;

  TRACE_EVENT1("base", __PRETTY_FUNCTION__, "size", region.end - region.start);

  int error = madvise(reinterpret_cast<void*>(region.start),
                      region.end - region.start, MADV_PAGEOUT);

  if (error < 0) {
    // We may fail on some regions, such as [vvar], or a locked region. It's
    // not worth it to try to filter these all out, so we just skip them, and
    // rely on metrics to verify that this is working correctly for most
    // regions.
    //
    // EINVAL could be [vvar] or a locked region. ENOMEM would be a moved or
    // unmapped region.
    if (errno != EINVAL && errno != ENOMEM) {
      PLOG(ERROR) << "Unexpected error from madvise.";
      return std::nullopt;
    }
    return 0;
  }

  return is_inaccessible ? 0 : region.end - region.start;
#else
  return std::nullopt;
#endif
}

// static
std::optional<uint64_t> SelfCompactionManager::CompactMemory(
    std::vector<debug::MappedMemoryRegion>* regions,
    const uint64_t max_bytes) {
  TRACE_EVENT1("base", __PRETTY_FUNCTION__, "count", regions->size());
  DCHECK(!regions->empty());

  uint64_t total_bytes_processed = 0;
  do {
    const auto region = regions->back();
    regions->pop_back();
    const auto bytes_processed = CompactRegion(region);
    if (!bytes_processed) {
      return std::nullopt;
    }
    total_bytes_processed += bytes_processed.value();
  } while (!regions->empty() && total_bytes_processed < max_bytes);

  return total_bytes_processed;
}

void PreFreezeBackgroundMemoryTrimmer::PostMetricsTasksIfModern() {
  if (!SupportsModernTrim()) {
    return;
  }
  PostMetricsTask();
}

// static
void SelfCompactionManager::ResetCompactionForTesting() {
  base::AutoLock locker(lock());
  Instance().compaction_last_cancelled_ = base::TimeTicks::Min();
  Instance().compaction_last_finished_ = base::TimeTicks::Min();
  Instance().compaction_last_triggered_ = base::TimeTicks::Min();
}

std::unique_ptr<SelfCompactionManager::CompactionState>
SelfCompactionManager::GetSelfCompactionStateForTesting(
    scoped_refptr<SequencedTaskRunner> task_runner,
    const TimeTicks& triggered_at) {
  return std::make_unique<SelfCompactionState>(std::move(task_runner),
                                               triggered_at, 1);
}

std::unique_ptr<SelfCompactionManager::CompactionState>
SelfCompactionManager::GetRunningCompactionStateForTesting(
    scoped_refptr<SequencedTaskRunner> task_runner,
    const TimeTicks& triggered_at) {
  return std::make_unique<RunningCompactionState>(std::move(task_runner),
                                                  triggered_at, 1);
}

}  // namespace base::android
