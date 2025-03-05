// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_PRE_FREEZE_BACKGROUND_MEMORY_TRIMMER_H_
#define BASE_ANDROID_PRE_FREEZE_BACKGROUND_MEMORY_TRIMMER_H_

#include <deque>

#include "base/compiler_specific.h"
#include "base/debug/proc_maps_linux.h"
#include "base/feature_list.h"
#include "base/functional/callback.h"
#include "base/memory/post_delayed_memory_reduction_task.h"
#include "base/no_destructor.h"
#include "base/profiler/sample_metadata.h"
#include "base/task/delayed_task_handle.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"

namespace base::android {
class MemoryPurgeManagerAndroid;

BASE_EXPORT BASE_DECLARE_FEATURE(kShouldFreezeSelf);

// Starting from Android U, apps are frozen shortly after being backgrounded
// (with some exceptions). This causes some background tasks for reclaiming
// resources in Chrome to not be run until Chrome is foregrounded again (which
// completely defeats their purpose).
//
// To try to avoid this problem, we use the |PostDelayedBackgroundTask| found
// below. Prior to Android U, this will simply post the task in the background
// with the given delay. From Android U onwards, this will post the task in the
// background with the given delay, but will run it sooner if we are about to
// be frozen.
class BASE_EXPORT PreFreezeBackgroundMemoryTrimmer {
 public:
  static PreFreezeBackgroundMemoryTrimmer& Instance();
  ~PreFreezeBackgroundMemoryTrimmer() = delete;

  // Posts a delayed task. On versions of Android starting from U, may run the
  // task sooner if we are backgrounded. In this case, we run the task on the
  // correct task runner, ignoring the given delay.
  static void PostDelayedBackgroundTask(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      const base::Location& from_here,
      OnceCallback<void(void)> task,
      base::TimeDelta delay) LOCKS_EXCLUDED(lock_) {
    PostDelayedBackgroundTask(
        task_runner, from_here,
        BindOnce(
            [](OnceClosure task,
               MemoryReductionTaskContext called_from_prefreeze) {
              std::move(task).Run();
            },
            std::move(task)),
        delay);
  }
  static void PostDelayedBackgroundTask(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      const base::Location& from_here,
      OnceCallback<void(MemoryReductionTaskContext)> task,
      base::TimeDelta delay) LOCKS_EXCLUDED(lock_);

  class PreFreezeMetric {
   public:
    virtual ~PreFreezeMetric();

    // |Measure| should return an amount of memory in bytes, or nullopt if
    // unable to record the metric for any reason. It is called underneath a
    // lock, so it should be fast enough to avoid delays (the same lock is held
    // when unregistering metrics).
    virtual std::optional<uint64_t> Measure() const = 0;

    const std::string& name() const LIFETIME_BOUND { return name_; }

   protected:
    friend class PreFreezeBackgroundMemoryTrimmer;
    explicit PreFreezeMetric(const std::string& name);

   private:
    const std::string name_;
  };

  // Registers a new metric to record before and after PreFreeze. Callers are
  // responsible for making sure that the same metric is not registered
  // multiple times.
  //
  // See |PreFreezeMetric| for details on the metric itself.
  //
  // Each time |OnPreFreeze| is run, |metric->Measure()| will be called twice:
  // - Once directly before any tasks are run; and
  // - Once two seconds after the first time it was called.
  //
  // As an example, calling RegisterMemoryMetric(PrivateMemoryFootprintMetric)
  // in the Browser process would cause the following metrics to be recorded 2
  // seconds after the next time |OnPreFreeze| is run:
  // - "Memory.PreFreeze2.Browser.PrivateMemoryFootprint.Before"
  // - "Memory.PreFreeze2.Browser.PrivateMemoryFootprint.After"
  // - "Memory.PreFreeze2.Browser.PrivateMemoryFootprint.Diff"
  //
  // See "Memory.PreFreeze2.{process_type}.{name}.{suffix}" for details on the
  // exact metrics.
  static void RegisterMemoryMetric(const PreFreezeMetric* metric)
      LOCKS_EXCLUDED(Instance().lock_);

  static void UnregisterMemoryMetric(const PreFreezeMetric* metric)
      LOCKS_EXCLUDED(Instance().lock_);

  static bool SelfCompactionIsSupported();

  // Compacts the memory for the process.
  void CompactSelf();

  // If we are currently running self compaction, cancel it.
  static void MaybeCancelSelfCompaction();

  static void SetSupportsModernTrimForTesting(bool is_supported);
  static void ClearMetricsForTesting() LOCKS_EXCLUDED(lock_);
  size_t GetNumberOfPendingBackgroundTasksForTesting() const
      LOCKS_EXCLUDED(lock_);
  size_t GetNumberOfKnownMetricsForTesting() const LOCKS_EXCLUDED(lock_);
  size_t GetNumberOfValuesBeforeForTesting() const LOCKS_EXCLUDED(lock_);
  bool DidRegisterTasksForTesting() const;

  static void OnPreFreezeForTesting() LOCKS_EXCLUDED(lock_) { OnPreFreeze(); }
  static void ResetSelfCompactionLastCancelledForTesting();

  static std::optional<uint64_t> CompactRegion(
      debug::MappedMemoryRegion region);

  // Called when Chrome is about to be frozen. Runs as many delayed tasks as
  // possible immediately, before we are frozen.
  static void OnPreFreeze() LOCKS_EXCLUDED(lock_);

  static void OnSelfFreeze() LOCKS_EXCLUDED(lock_);

  static bool SupportsModernTrim();
  static bool ShouldUseModernTrim();
  static bool IsTrimMemoryBackgroundCritical();

 private:
  friend class base::NoDestructor<PreFreezeBackgroundMemoryTrimmer>;
  friend jboolean JNI_MemoryPurgeManager_IsOnPreFreezeMemoryTrimEnabled(
      JNIEnv* env);
  friend class base::android::MemoryPurgeManagerAndroid;
  friend class base::OneShotDelayedBackgroundTimer;
  friend class PreFreezeBackgroundMemoryTrimmerTest;
  friend class PreFreezeSelfCompactionTest;
  FRIEND_TEST_ALL_PREFIXES(PreFreezeSelfCompactionTest, Cancel);
  FRIEND_TEST_ALL_PREFIXES(PreFreezeSelfCompactionTest, NotCanceled);

  // We use our own implementation here, based on |PostCancelableDelayedTask|,
  // rather than relying on something like |base::OneShotTimer|, since
  // |base::OneShotTimer| doesn't support things like immediately running our
  // task from a different sequence, and some |base::OneShotTimer|
  // functionality (e.g. |FireNow|) only works with the default task runner.
  class BackgroundTask final {
   public:
    static std::unique_ptr<BackgroundTask> Create(
        scoped_refptr<base::SequencedTaskRunner> task_runner,
        const base::Location& from_here,
        OnceCallback<void(MemoryReductionTaskContext)> task,
        base::TimeDelta delay);

    explicit BackgroundTask(
        scoped_refptr<base::SequencedTaskRunner> task_runner);
    ~BackgroundTask();

    static void RunNow(std::unique_ptr<BackgroundTask> background_task);

    void Run(MemoryReductionTaskContext from_pre_freeze);

    void CancelTask();

   private:
    friend class PreFreezeBackgroundMemoryTrimmer;
    void Start(const Location& from_here,
               TimeDelta delay,
               OnceCallback<void(MemoryReductionTaskContext)> task);
    void StartInternal(const Location& from_here,
                       TimeDelta delay,
                       OnceClosure task);
    scoped_refptr<base::SequencedTaskRunner> task_runner_;
    base::DelayedTaskHandle task_handle_;

    OnceCallback<void(MemoryReductionTaskContext)> task_;
  };

 private:
  class CompactionMetric : public RefCountedThreadSafe<CompactionMetric> {
   public:
    explicit CompactionMetric(base::TimeTicks started_at);

    void RecordDelayedMetrics();

    void RecordBeforeMetrics();
    void MaybeRecordCompactionMetrics();

   private:
    friend class RefCountedThreadSafe<CompactionMetric>;
    ~CompactionMetric();
    void RecordSmapsRollup(std::optional<debug::SmapsRollup>* target);
    void RecordSmapsRollupWithDelay(std::optional<debug::SmapsRollup>* target,
                                    base::TimeDelta delay);
    base::TimeTicks started_at_;
    // We use std::optional here because:
    // - We record these incrementally.
    // - We may stop recording at some point.
    // - We only want to emit histograms if all values were recorded.
    std::optional<debug::SmapsRollup> smaps_before_;
    std::optional<debug::SmapsRollup> smaps_after_;
    std::optional<debug::SmapsRollup> smaps_after_1s_;
    std::optional<debug::SmapsRollup> smaps_after_10s_;
    std::optional<debug::SmapsRollup> smaps_after_60s_;
  };

  PreFreezeBackgroundMemoryTrimmer();

  void StartSelfCompaction(scoped_refptr<base::SequencedTaskRunner> task_runner,
                           std::vector<debug::MappedMemoryRegion> regions,
                           scoped_refptr<CompactionMetric> metric,
                           uint64_t max_size,
                           base::TimeTicks started_at);
  static base::TimeDelta GetDelayBetweenSelfCompaction();
  void MaybePostSelfCompactionTask(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      std::vector<debug::MappedMemoryRegion> regions,
      scoped_refptr<CompactionMetric> metric,
      uint64_t max_size,
      base::TimeTicks started_at);
  void SelfCompactionTask(scoped_refptr<base::SequencedTaskRunner> task_runner,
                          std::vector<debug::MappedMemoryRegion> regions,
                          scoped_refptr<CompactionMetric> metric,
                          uint64_t max_size,
                          base::TimeTicks started_at);
  void FinishSelfCompaction(scoped_refptr<CompactionMetric> metric,
                            base::TimeTicks started_at);

  static bool ShouldContinueSelfCompaction(
      base::TimeTicks compaction_started_at) LOCKS_EXCLUDED(Instance().lock_);

  static std::optional<uint64_t> CompactMemory(
      std::vector<debug::MappedMemoryRegion>* regions,
      const uint64_t max_bytes);

  void RegisterMemoryMetricInternal(const PreFreezeMetric* metric)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);

  void UnregisterMemoryMetricInternal(const PreFreezeMetric* metric)
      EXCLUSIVE_LOCKS_REQUIRED(lock_);
  static void UnregisterBackgroundTask(BackgroundTask*) LOCKS_EXCLUDED(lock_);

  void UnregisterBackgroundTaskInternal(BackgroundTask*) LOCKS_EXCLUDED(lock_);

  static void RegisterPrivateMemoryFootprintMetric() LOCKS_EXCLUDED(lock_);
  void RegisterPrivateMemoryFootprintMetricInternal() LOCKS_EXCLUDED(lock_);

  void PostDelayedBackgroundTaskInternal(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      const base::Location& from_here,
      OnceCallback<void(MemoryReductionTaskContext)> task,
      base::TimeDelta delay) LOCKS_EXCLUDED(lock_);
  void PostDelayedBackgroundTaskModern(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      const base::Location& from_here,
      OnceCallback<void(MemoryReductionTaskContext)> task,
      base::TimeDelta delay) LOCKS_EXCLUDED(lock_);
  BackgroundTask* PostDelayedBackgroundTaskModernHelper(
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      const base::Location& from_here,
      OnceCallback<void(MemoryReductionTaskContext)> task,
      base::TimeDelta delay) EXCLUSIVE_LOCKS_REQUIRED(lock_);

  void OnPreFreezeInternal() LOCKS_EXCLUDED(lock_);
  void RunPreFreezeTasks() EXCLUSIVE_LOCKS_REQUIRED(lock_);

  void OnSelfFreezeInternal();

  void MaybeCancelSelfCompactionInternal() LOCKS_EXCLUDED(lock_);

  void PostMetricsTasksIfModern() EXCLUSIVE_LOCKS_REQUIRED(lock_);
  void PostMetricsTask() EXCLUSIVE_LOCKS_REQUIRED(lock_);
  void RecordMetrics() LOCKS_EXCLUDED(lock_);

  void RecordSmapsRollup(std::optional<debug::SmapsRollup>* target,
                         base::TimeTicks started_at);

  mutable base::Lock lock_;
  std::deque<std::unique_ptr<BackgroundTask>> background_tasks_
      GUARDED_BY(lock_);
  std::vector<const PreFreezeMetric*> metrics_ GUARDED_BY(lock_);
  // When a metrics task is posted (see |RecordMetrics|), the values of each
  // metric before any tasks are run are saved here. The "i"th entry corresponds
  // to the "i"th entry in |metrics_|. When there is no pending metrics task,
  // |values_before_| should be empty.
  std::vector<std::optional<uint64_t>> values_before_ GUARDED_BY(lock_);
  // Whether or not we should continue self compaction. There are two reasons
  // why we would cancel:
  // (1) We have resumed, meaning we are likely to touch much of the process
  //     memory soon, and we do not want to waste CPU time with compaction,
  //     since it can block other work that needs to be done.
  // (2) We are going to be frozen by App Freezer, which will do the compaction
  //     work for us. This situation should be relatively rare, because we
  //     attempt to not do self compaction if we know that we are going to
  //     frozen by App Freezer.
  base::TimeTicks self_compaction_last_cancelled_ GUARDED_BY(lock_) =
      base::TimeTicks::Min();
  std::optional<base::ScopedSampleMetadata> process_compacted_metadata_
      GUARDED_BY(lock_);
  bool supports_modern_trim_;
};

}  // namespace base::android

#endif  // BASE_ANDROID_PRE_FREEZE_BACKGROUND_MEMORY_TRIMMER_H_
