// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/pre_freeze_background_memory_trimmer.h"

#include <sys/mman.h>
#include <sys/utsname.h>

#include <optional>
#include <string>

#include "base/android/build_info.h"
#include "base/android/pmf_utils.h"
#include "base/android/self_compaction_manager.h"
#include "base/cancelable_callback.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/page_size.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/strcat.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"
#include "base/trace_event/named_trigger.h"  // no-presubmit-check
#include "base/trace_event/trace_event.h"

namespace base::android {
namespace {

// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "PreFreezeMetricsFailureType" in tools/metrics/histograms/enums.xml.
enum class MetricsFailure {
  kAlreadyRunning,
  kSizeMismatch,
  kMeasureFailure,
  kMaxValue = kMeasureFailure
};

// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "PreFreezeReadProcMapsType" in tools/metrics/histograms/enums.xml.
enum class ReadProcMaps { kFailed, kEmpty, kSuccess, kMaxValue = kSuccess };

// This constant is chosen arbitrarily, to allow time for the background tasks
// to finish running BEFORE collecting metrics.
constexpr base::TimeDelta kDelayForMetrics = base::Seconds(2);

uint64_t BytesToMiB(uint64_t v) {
  return v / 1024 / 1024;
}

const char* GetProcessType() {
  CHECK(base::CommandLine::InitializedForCurrentProcess());
  const std::string type =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("type");
  const char* process_type = type == ""              ? "Browser"
                             : type == "renderer"    ? "Renderer"
                             : type == "gpu-process" ? "GPU"
                             : type == "utility"     ? "Utility"
                                                     : "Unknown";
  return process_type;
}

std::string GetPreFreezeMetricName(std::string_view name,
                                   std::string_view suffix) {
  const char* process_type = GetProcessType();
  return StrCat({"Memory.PreFreeze2.", process_type, ".", name, ".", suffix});
}

class PrivateMemoryFootprintMetric
    : public PreFreezeBackgroundMemoryTrimmer::PreFreezeMetric {
 public:
  PrivateMemoryFootprintMetric()
      : PreFreezeBackgroundMemoryTrimmer::PreFreezeMetric(
            "PrivateMemoryFootprint") {}
  std::optional<uint64_t> Measure() const override {
    return PmfUtils::GetPrivateMemoryFootprintForCurrentProcess();
  }

  ~PrivateMemoryFootprintMetric() override = default;

  // Whether the metric has been registered with
  // |PreFreezeBackgroundMemoryTrimmer| or not, which happens the first time a
  // task is posted via |PreFreezeBackgroundMemoryTrimmer| or
  // |OneShotDelayedBackgroundTimer|.
  static bool did_register_;
};

bool PrivateMemoryFootprintMetric::did_register_ = false;

void MaybeRecordPreFreezeMetric(std::optional<uint64_t> value_bytes,
                                std::string_view metric_name,
                                std::string_view suffix) {
  // Skip recording the metric if we failed to get the PMF.
  if (!value_bytes.has_value()) {
    return;
  }

  UmaHistogramMemoryMB(GetPreFreezeMetricName(metric_name, suffix),
                       static_cast<int>(BytesToMiB(value_bytes.value())));
}

std::optional<uint64_t> Diff(std::optional<uint64_t> before,
                             std::optional<uint64_t> after) {
  if (!before.has_value() || !before.has_value()) {
    return std::nullopt;
  }

  const uint64_t before_value = before.value();
  const uint64_t after_value = after.value();

  return after_value < before_value ? before_value - after_value : 0;
}

}  // namespace

PreFreezeBackgroundMemoryTrimmer::PreFreezeBackgroundMemoryTrimmer()
    : supports_modern_trim_(BuildInfo::GetInstance()->sdk_int() >=
                            SDK_VERSION_U) {}

// static
PreFreezeBackgroundMemoryTrimmer& PreFreezeBackgroundMemoryTrimmer::Instance() {
  static base::NoDestructor<PreFreezeBackgroundMemoryTrimmer> instance;
  return *instance;
}

void PreFreezeBackgroundMemoryTrimmer::RecordMetrics() {
  // We check that the command line is available here because we use it to
  // determine the current process, which is used for the names of metrics
  // below.
  CHECK(base::CommandLine::InitializedForCurrentProcess());
  base::AutoLock locker(lock());
  if (metrics_.size() != values_before_.size()) {
    UmaHistogramEnumeration("Memory.PreFreeze2.RecordMetricsFailureType",
                            MetricsFailure::kSizeMismatch);
    values_before_.clear();
    return;
  }

  for (size_t i = 0; i < metrics_.size(); i++) {
    const auto metric = metrics_[i];
    const std::optional<uint64_t> value_before = values_before_[i];

    std::optional<uint64_t> value_after = metric->Measure();

    if (!value_after) {
      UmaHistogramEnumeration("Memory.PreFreeze2.RecordMetricsFailureType",
                              MetricsFailure::kMeasureFailure);
      continue;
    }

    MaybeRecordPreFreezeMetric(value_before, metric->name(), "Before");
    MaybeRecordPreFreezeMetric(value_after, metric->name(), "After");
    MaybeRecordPreFreezeMetric(Diff(value_before, value_after), metric->name(),
                               "Diff");
  }

  values_before_.clear();
}

void PreFreezeBackgroundMemoryTrimmer::PostMetricsTask() {
  // PreFreeze is only for Android U and greater, so no need to record metrics
  // for older versions.
  if (!SupportsModernTrim()) {
    return;
  }

  // We need the process type to record the metrics below, which we get from
  // the command line. We cannot post the task below if the thread pool is not
  // initialized yet.
  if (!base::CommandLine::InitializedForCurrentProcess() ||
      !base::ThreadPoolInstance::Get()) {
    return;
  }

  // The |RecordMetrics| task resets the |values_before_| after it uses them.
  // That task is posted with a 2 second delay from when |OnPreFreeze| is run.
  //
  // From the time that Chrome is backgrounded until Android delivers the signal
  // to run PreFreeze always takes at least 10 seconds.
  //
  // Therefore, even if we:
  // - Post |RecordMetrics|
  // - and then immediately return to foreground and immediately back to
  //   background.
  // We still will have to wait at least 10 seconds before we get the PreFreeze
  // signal again, by which time the original RecordMetrics task will have
  // already finished.
  if (values_before_.size() > 0) {
    UmaHistogramEnumeration("Memory.PreFreeze2.RecordMetricsFailureType",
                            MetricsFailure::kAlreadyRunning);
    return;
  }
  for (const auto& metric : metrics_) {
    values_before_.push_back(metric->Measure());
  }

  // The posted task will be more likely to survive background killing in
  // experiments that change the memory trimming behavior. Run as USER_BLOCKING
  // to reduce this sample imbalance in experiment groups. Normally tasks
  // collecting metrics should use BEST_EFFORT, but when running in background a
  // number of subtle effects may influence the real delay of those tasks. The
  // USER_BLOCKING will allow to estimate the number of better-survived tasks
  // more precisely.
  base::ThreadPool::PostDelayedTask(
      FROM_HERE, {base::TaskPriority::USER_BLOCKING, MayBlock()},
      base::BindOnce(&PreFreezeBackgroundMemoryTrimmer::RecordMetrics,
                     base::Unretained(this)),
      kDelayForMetrics);
}

void SelfCompactionManager::CompactionMetric::MaybeRecordCompactionMetrics() {
  // If we did not record smaps_rollup for any reason, such as returning to
  // foreground, being frozen by App Freezer, or failing to read
  // /proc/self/smaps_rollup, skip emitting metrics.
  if (!smaps_before_.has_value() || !smaps_after_.has_value() ||
      !smaps_after_1s_.has_value() || !smaps_after_10s_.has_value() ||
      !smaps_after_60s_.has_value()) {
    return;
  }

  if (!ShouldContinueCompaction(compaction_triggered_at_)) {
    return;
  }

  // Record absolute values of each metric.
  RecordCompactionMetrics(*smaps_before_, "Before");
  RecordCompactionMetrics(*smaps_after_, "After");
  RecordCompactionMetrics(*smaps_after_1s_, "After1s");
  RecordCompactionMetrics(*smaps_after_10s_, "After10s");
  RecordCompactionMetrics(*smaps_after_60s_, "After60s");

  // Record diff of before and after to see how much memory was compacted.
  RecordCompactionDiffMetrics(*smaps_before_, *smaps_after_, "BeforeAfter");

  // Record diff after a delay, so we can see if any memory comes back after
  // compaction.
  RecordCompactionDiffMetrics(*smaps_after_, *smaps_after_1s_, "After1s");
  RecordCompactionDiffMetrics(*smaps_after_, *smaps_after_10s_, "After10s");
  RecordCompactionDiffMetrics(*smaps_after_, *smaps_after_60s_, "After60s");
}

void SelfCompactionManager::CompactionMetric::RecordCompactionMetric(
    size_t value_bytes,
    std::string_view metric_name,
    std::string_view suffix) {
  UmaHistogramMemoryMB(GetMetricName(metric_name, suffix),
                       static_cast<int>(BytesToMiB(value_bytes)));
}

void SelfCompactionManager::CompactionMetric::RecordCompactionMetrics(
    const debug::SmapsRollup& value,
    std::string_view suffix) {
  RecordCompactionMetric(value.rss, "Rss", suffix);
  RecordCompactionMetric(value.pss, "Pss", suffix);
  RecordCompactionMetric(value.pss_anon, "PssAnon", suffix);
  RecordCompactionMetric(value.pss_file, "PssFile", suffix);
  RecordCompactionMetric(value.swap_pss, "SwapPss", suffix);
}

void SelfCompactionManager::CompactionMetric::RecordCompactionDiffMetric(
    size_t before_value_bytes,
    size_t after_value_bytes,
    std::string_view name,
    std::string_view suffix) {
  size_t diff_non_negative = std::max(before_value_bytes, after_value_bytes) -
                             std::min(before_value_bytes, after_value_bytes);
  const std::string full_suffix = StrCat(
      {"Diff.", suffix, ".",
       before_value_bytes < after_value_bytes ? "Increase" : "Decrease"});
  RecordCompactionMetric(diff_non_negative, name, full_suffix);
}

void SelfCompactionManager::CompactionMetric::RecordCompactionDiffMetrics(
    const debug::SmapsRollup& before,
    const debug::SmapsRollup& after,
    std::string_view suffix) {
  RecordCompactionDiffMetric(before.rss, after.rss, "Rss", suffix);
  RecordCompactionDiffMetric(before.pss, after.pss, "Pss", suffix);
  RecordCompactionDiffMetric(before.pss_anon, after.pss_anon, "PssAnon",
                             suffix);
  RecordCompactionDiffMetric(before.pss_file, after.pss_file, "PssFile",
                             suffix);
  RecordCompactionDiffMetric(before.swap_pss, after.swap_pss, "SwapPss",
                             suffix);
}

void SelfCompactionManager::CompactionMetric::RecordSmapsRollup(
    std::optional<debug::SmapsRollup>* target) {
  if (!ShouldContinueCompaction(compaction_triggered_at_)) {
    return;
  }

  *target = debug::ReadAndParseSmapsRollup();

  MaybeRecordCompactionMetrics();
}

void SelfCompactionManager::CompactionMetric::RecordSmapsRollupWithDelay(
    std::optional<debug::SmapsRollup>* target,
    base::TimeDelta delay) {
  base::ThreadPool::PostDelayedTask(
      FROM_HERE, {base::TaskPriority::BEST_EFFORT, MayBlock()},
      base::BindOnce(
          &SelfCompactionManager::CompactionMetric::RecordSmapsRollup,
          // |target| is a member a of |this|, so it's lifetime is
          // always ok here.
          this, base::Unretained(target)),
      delay);
}

// static
void PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTask(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    const base::Location& from_here,
    OnceCallback<void(MemoryReductionTaskContext)> task,
    base::TimeDelta delay) {
  // Preserve previous behaviour on versions before Android U.
  if (!SupportsModernTrim()) {
    task_runner->PostDelayedTask(
        from_here,
        BindOnce(std::move(task), MemoryReductionTaskContext::kDelayExpired),
        delay);
    return;
  }

  Instance().PostDelayedBackgroundTaskInternal(task_runner, from_here,
                                               std::move(task), delay);
}

void PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTaskInternal(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    const base::Location& from_here,
    OnceCallback<void(MemoryReductionTaskContext)> task,
    base::TimeDelta delay) {
  DCHECK(SupportsModernTrim());

  RegisterPrivateMemoryFootprintMetric();

  PostDelayedBackgroundTaskModern(task_runner, from_here, std::move(task),
                                  delay);
}

void PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTaskModern(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    const base::Location& from_here,
    OnceCallback<void(MemoryReductionTaskContext)> task,
    base::TimeDelta delay) {
  // We create a cancellable delayed task (below), which must be done on the
  // same TaskRunner that will run the task eventually, so we may need to
  // repost this on the correct TaskRunner.
  if (!task_runner->RunsTasksInCurrentSequence()) {
    // |base::Unretained(this)| is safe here because we never destroy |this|.
    task_runner->PostTask(
        FROM_HERE,
        base::BindOnce(
            &PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTaskModern,
            base::Unretained(this), task_runner, from_here, std::move(task),
            delay));
    return;
  }

  base::AutoLock locker(lock());
  PostDelayedBackgroundTaskModernHelper(std::move(task_runner), from_here,
                                        std::move(task), delay);
}

PreFreezeBackgroundMemoryTrimmer::BackgroundTask*
PreFreezeBackgroundMemoryTrimmer::PostDelayedBackgroundTaskModernHelper(
    scoped_refptr<SequencedTaskRunner> task_runner,
    const Location& from_here,
    OnceCallback<void(MemoryReductionTaskContext)> task,
    TimeDelta delay) {
  std::unique_ptr<BackgroundTask> background_task =
      BackgroundTask::Create(task_runner, from_here, std::move(task), delay);
  auto* ptr = background_task.get();
  background_tasks_.push_back(std::move(background_task));
  return ptr;
}

// static
void PreFreezeBackgroundMemoryTrimmer::RegisterMemoryMetric(
    const PreFreezeMetric* metric) {
  base::AutoLock locker(lock());
  Instance().RegisterMemoryMetricInternal(metric);
}

void PreFreezeBackgroundMemoryTrimmer::RegisterMemoryMetricInternal(
    const PreFreezeMetric* metric) {
  metrics_.push_back(metric);
  // If we are in the middle of recording metrics when we register this, add
  // a nullopt at the end so that metrics recording doesn't fail for all
  // metrics, just this one.
  if (values_before_.size() > 0) {
    values_before_.push_back(std::nullopt);
  }
}

// static
void PreFreezeBackgroundMemoryTrimmer::UnregisterMemoryMetric(
    const PreFreezeMetric* metric) {
  base::AutoLock locker(lock());
  Instance().UnregisterMemoryMetricInternal(metric);
}

void PreFreezeBackgroundMemoryTrimmer::UnregisterMemoryMetricInternal(
    const PreFreezeMetric* metric) {
  auto it = std::find(metrics_.begin(), metrics_.end(), metric);
  CHECK(it != metrics_.end());
  const long index = it - metrics_.begin();
  if (values_before_.size() > 0) {
    CHECK_EQ(values_before_.size(), metrics_.size());
    values_before_.erase(values_before_.begin() + index);
  }
  metrics_.erase(metrics_.begin() + index);
}

SelfCompactionManager::CompactionState::CompactionState(
    scoped_refptr<SequencedTaskRunner> task_runner,
    base::TimeTicks triggered_at,
    uint64_t max_bytes)
    : task_runner_(std::move(task_runner)),
      triggered_at_(triggered_at),
      max_bytes_(max_bytes) {}

SelfCompactionManager::CompactionState::~CompactionState() = default;

void SelfCompactionManager::CompactionState::MaybeReadProcMaps() {
  DCHECK(regions_.empty());
  auto did_read_proc_maps = ReadProcMaps::kSuccess;
  if (IsFeatureEnabled()) {
    std::string proc_maps;
    if (!debug::ReadProcMaps(&proc_maps) ||
        !ParseProcMaps(proc_maps, &regions_)) {
      did_read_proc_maps = ReadProcMaps::kFailed;
    } else if (regions_.size() == 0) {
      did_read_proc_maps = ReadProcMaps::kEmpty;
    }
  }

  UmaHistogramEnumeration(GetMetricName("ReadProcMaps"), did_read_proc_maps);
}

// static
void PreFreezeBackgroundMemoryTrimmer::OnPreFreeze() {
  // If we have scheduled a self compaction task, cancel it, since App Freezer
  // will handle the compaction for us, and we don't want to potentially run
  // self compaction after we have resumed.
  SelfCompactionManager::MaybeCancelCompaction(
      CompactCancellationReason::kAppFreezer);
  Instance().OnPreFreezeInternal();
}

void PreFreezeBackgroundMemoryTrimmer::RunPreFreezeTasks() {
  // We check |num_pending_tasks-- > 0| so that we have an upper limit on the
  // number of tasks that we run.
  // We check |!background_tasks_.empty()| so that we exit as soon as we have
  // no more tasks to run.
  //
  // This handles both the case where we have tasks that post other tasks (we
  // won't run endlessly because of the upper limit), and the case where tasks
  // cancel other tasks (we exit as soon as the queue is empty).
  //
  // Note that the current implementation may run some tasks that were posted
  // by earlier tasks, if some other tasks are also cancelled, but we
  // stop eventually due to the upper limit.
  size_t num_pending_tasks = background_tasks_.size();
  while (num_pending_tasks-- > 0 && !background_tasks_.empty()) {
    auto background_task = std::move(background_tasks_.front());
    background_tasks_.pop_front();
    // We release the lock here for two reasons:
    // (1) To avoid holding it too long while running all the background tasks.
    // (2) To prevent a deadlock if the |background_task| needs to acquire the
    //     lock (e.g. to post another task).
    base::AutoUnlock unlocker(lock());
    BackgroundTask::RunNow(std::move(background_task));
  }
}

void PreFreezeBackgroundMemoryTrimmer::OnPreFreezeInternal() {
  base::AutoLock locker(lock());
  PostMetricsTasksIfModern();

  if (!ShouldUseModernTrim()) {
    return;
  }

  RunPreFreezeTasks();
}

// static
void PreFreezeBackgroundMemoryTrimmer::UnregisterBackgroundTask(
    BackgroundTask* task) {
  return Instance().UnregisterBackgroundTaskInternal(task);
}

void PreFreezeBackgroundMemoryTrimmer::UnregisterBackgroundTaskInternal(
    BackgroundTask* timer) {
  base::AutoLock locker(lock());
  std::erase_if(background_tasks_, [&](auto& t) { return t.get() == timer; });
}

// static
void PreFreezeBackgroundMemoryTrimmer::RegisterPrivateMemoryFootprintMetric() {
  base::AutoLock locker(lock());
  static base::NoDestructor<PrivateMemoryFootprintMetric> pmf_metric;
  if (!PrivateMemoryFootprintMetric::did_register_) {
    PrivateMemoryFootprintMetric::did_register_ = true;
    Instance().RegisterMemoryMetricInternal(pmf_metric.get());
  }
}

// static
bool PreFreezeBackgroundMemoryTrimmer::SupportsModernTrim() {
  return Instance().supports_modern_trim_;
}

// static
bool PreFreezeBackgroundMemoryTrimmer::ShouldUseModernTrim() {
  return SupportsModernTrim();
}

// static
bool PreFreezeBackgroundMemoryTrimmer::IsTrimMemoryBackgroundCritical() {
  return SupportsModernTrim();
}

// static
void PreFreezeBackgroundMemoryTrimmer::SetSupportsModernTrimForTesting(
    bool is_supported) {
  Instance().supports_modern_trim_ = is_supported;
}

// static
void PreFreezeBackgroundMemoryTrimmer::ClearMetricsForTesting() {
  base::AutoLock locker(lock());
  Instance().metrics_.clear();
  PrivateMemoryFootprintMetric::did_register_ = false;
}

bool PreFreezeBackgroundMemoryTrimmer::DidRegisterTasksForTesting() const {
  base::AutoLock locker(lock());
  return metrics_.size() != 0;
}

size_t
PreFreezeBackgroundMemoryTrimmer::GetNumberOfPendingBackgroundTasksForTesting()
    const {
  base::AutoLock locker(lock());
  return background_tasks_.size();
}

size_t PreFreezeBackgroundMemoryTrimmer::GetNumberOfKnownMetricsForTesting()
    const {
  base::AutoLock locker(lock());
  return metrics_.size();
}

size_t PreFreezeBackgroundMemoryTrimmer::GetNumberOfValuesBeforeForTesting()
    const {
  base::AutoLock locker(lock());
  return values_before_.size();
}

// static
void PreFreezeBackgroundMemoryTrimmer::BackgroundTask::RunNow(
    std::unique_ptr<PreFreezeBackgroundMemoryTrimmer::BackgroundTask>
        background_task) {
  if (!background_task->task_runner_->RunsTasksInCurrentSequence()) {
    background_task->task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&BackgroundTask::RunNow, std::move(background_task)));
    return;
  }

  DCHECK_CALLED_ON_VALID_SEQUENCE(background_task->sequence_checker_);
  // We check that the task has not been run already. If it has, we do not run
  // it again.
  if (background_task->task_handle_.IsValid()) {
    background_task->task_handle_.CancelTask();
  } else {
    return;
  }

  background_task->Run(MemoryReductionTaskContext::kProactive);
}

void PreFreezeBackgroundMemoryTrimmer::BackgroundTask::CancelTask() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (task_handle_.IsValid()) {
    task_handle_.CancelTask();
    PreFreezeBackgroundMemoryTrimmer::UnregisterBackgroundTask(this);
  }
}

// static
std::unique_ptr<PreFreezeBackgroundMemoryTrimmer::BackgroundTask>
PreFreezeBackgroundMemoryTrimmer::BackgroundTask::Create(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    const base::Location& from_here,
    OnceCallback<void(MemoryReductionTaskContext)> task,
    base::TimeDelta delay) {
  DCHECK(task_runner->RunsTasksInCurrentSequence());
  auto background_task = std::make_unique<BackgroundTask>(task_runner);
  background_task->Start(from_here, delay, std::move(task));
  return background_task;
}

PreFreezeBackgroundMemoryTrimmer::BackgroundTask::BackgroundTask(
    scoped_refptr<base::SequencedTaskRunner> task_runner)
    : task_runner_(task_runner) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

PreFreezeBackgroundMemoryTrimmer::BackgroundTask::~BackgroundTask() = default;

void PreFreezeBackgroundMemoryTrimmer::BackgroundTask::Run(
    MemoryReductionTaskContext from_pre_freeze) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!task_handle_.IsValid());
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  std::move(task_).Run(from_pre_freeze);
}

void PreFreezeBackgroundMemoryTrimmer::BackgroundTask::Start(
    const base::Location& from_here,
    base::TimeDelta delay,
    OnceCallback<void(MemoryReductionTaskContext)> task) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  task_ = std::move(task);
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  task_handle_ = task_runner_->PostCancelableDelayedTask(
      subtle::PostDelayedTaskPassKey(), from_here,
      base::BindOnce(
          [](BackgroundTask* p) {
            p->Run(MemoryReductionTaskContext::kDelayExpired);
            UnregisterBackgroundTask(p);
          },
          this),
      delay);
}

PreFreezeBackgroundMemoryTrimmer::PreFreezeMetric::PreFreezeMetric(
    const std::string& name)
    : name_(name) {}

PreFreezeBackgroundMemoryTrimmer::PreFreezeMetric::~PreFreezeMetric() = default;

SelfCompactionManager::CompactionMetric::CompactionMetric(
    const std::string& name,
    base::TimeTicks triggered_at,
    base::TimeTicks started_at)
    : name_(name),
      compaction_triggered_at_(triggered_at),
      compaction_started_at_(started_at) {}
SelfCompactionManager::CompactionMetric::~CompactionMetric() = default;

std::string SelfCompactionManager::CompactionMetric::GetMetricName(
    std::string_view name) const {
  return StrCat({name_, name});
}

std::string SelfCompactionManager::CompactionMetric::GetMetricName(
    std::string_view name,
    std::string_view suffix) const {
  return StrCat({name_, name, ".", suffix});
}

void SelfCompactionManager::CompactionMetric::RecordBeforeMetrics() {
  RecordSmapsRollup(&smaps_before_);
}

void SelfCompactionManager::CompactionMetric::RecordDelayedMetrics() {
  RecordSmapsRollup(&smaps_after_);
  RecordSmapsRollupWithDelay(&smaps_after_1s_, base::Seconds(1));
  RecordSmapsRollupWithDelay(&smaps_after_10s_, base::Seconds(10));
  RecordSmapsRollupWithDelay(&smaps_after_60s_, base::Seconds(60));
}

void SelfCompactionManager::CompactionMetric::RecordTimeMetrics(
    base::TimeTicks last_finished,
    base::TimeTicks last_cancelled) {
  UmaHistogramMediumTimes(GetMetricName("SelfCompactionTime"),
                          last_finished - compaction_started_at_);
  UmaHistogramMediumTimes(GetMetricName("TimeSinceLastCancel"),
                          last_finished - last_cancelled);
}

}  // namespace base::android
