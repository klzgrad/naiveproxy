// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/features.h"

#include <atomic>

#include "base/files/file_path.h"
#include "base/task/sequence_manager/sequence_manager_impl.h"
#include "base/threading/platform_thread.h"
#include "build/blink_buildflags.h"
#include "build/buildflag.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
#include "base/message_loop/message_pump_epoll.h"
#endif

#if BUILDFLAG(IS_APPLE)
#include "base/files/file.h"
#include "base/message_loop/message_pump_apple.h"
#include "base/synchronization/condition_variable.h"

#if !BUILDFLAG(IS_IOS) || !BUILDFLAG(USE_BLINK)
#include "base/message_loop/message_pump_kqueue.h"
#endif

#endif

#if BUILDFLAG(IS_ANDROID)
#include "base/android/input_hint_checker.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "base/task/sequence_manager/thread_controller_power_monitor.h"
#endif

namespace base::features {

namespace {

// An atomic is used because this can be queried racily by a thread checking if
// an optimization is enabled and a thread initializing this from the
// FeatureList. All operations use std::memory_order_relaxed because there are
// no dependent memory operations.
std::atomic_bool g_is_reduce_ppms_enabled{false};

}  // namespace

// Alphabetical:

// Controls caching within BASE_FEATURE_PARAM(). This is feature-controlled
// so that ScopedFeatureList can disable it to turn off caching.
BASE_FEATURE(kFeatureParamWithCache,
             "FeatureParamWithCache",
             FEATURE_ENABLED_BY_DEFAULT);

// Whether a fast implementation of FilePath::IsParent is used. This feature
// exists to ensure that the fast implementation can be disabled quickly if
// issues are found with it.
BASE_FEATURE(kFastFilePathIsParent,
             "FastFilePathIsParent",
             FEATURE_ENABLED_BY_DEFAULT);

// Use non default low memory device threshold.
// Value should be given via |LowMemoryDeviceThresholdMB|.
#if BUILDFLAG(IS_ANDROID)
// LINT.IfChange
#define LOW_MEMORY_DEVICE_THRESHOLD_MB 1024
// LINT.ThenChange(//base/android/java/src/org/chromium/base/SysUtils.java)
#elif BUILDFLAG(IS_IOS)
// For M99, 45% of devices have 2GB of RAM, and 55% have more.
#define LOW_MEMORY_DEVICE_THRESHOLD_MB 1024
#else
// Updated Desktop default threshold to match the Android 2021 definition.
#define LOW_MEMORY_DEVICE_THRESHOLD_MB 2048
#endif
BASE_FEATURE(kLowEndMemoryExperiment,
             "LowEndMemoryExperiment",
             FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE_PARAM(size_t,
                   kLowMemoryDeviceThresholdMB,
                   &kLowEndMemoryExperiment,
                   "LowMemoryDeviceThresholdMB",
                   LOW_MEMORY_DEVICE_THRESHOLD_MB);

BASE_FEATURE(kReducePPMs, "ReducePPMs", FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHROMEOS)
// Force to enable LowEndDeviceMode partially on Android 3Gb devices.
// (see PartialLowEndModeOnMidRangeDevices below)
BASE_FEATURE(kPartialLowEndModeOn3GbDevices,
             "PartialLowEndModeOn3GbDevices",
             FEATURE_DISABLED_BY_DEFAULT);

// Used to enable LowEndDeviceMode partially on Android and ChromeOS mid-range
// devices. Such devices aren't considered low-end, but we'd like experiment
// with a subset of low-end features to see if we get a good memory vs.
// performance tradeoff.
//
// TODO(crbug.com/40264947): |#if| out 32-bit before launching or going to
// high Stable %, because we will enable the feature only for <8GB 64-bit
// devices, where we didn't ship yet. However, we first need a larger
// population to collect data.
BASE_FEATURE(kPartialLowEndModeOnMidRangeDevices,
             "PartialLowEndModeOnMidRangeDevices",
#if BUILDFLAG(IS_ANDROID)
             FEATURE_ENABLED_BY_DEFAULT);
#elif BUILDFLAG(IS_CHROMEOS)
             FEATURE_DISABLED_BY_DEFAULT);
#endif

#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_ANDROID)
// Enable not perceptible binding without cpu priority boosting.
BASE_FEATURE(kBackgroundNotPerceptibleBinding,
             "BackgroundNotPerceptibleBinding",
             FEATURE_DISABLED_BY_DEFAULT);

// Whether to report frame metrics to the Android.FrameTimeline.* histograms.
BASE_FEATURE(kCollectAndroidFrameTimelineMetrics,
             "CollectAndroidFrameTimelineMetrics",
             FEATURE_DISABLED_BY_DEFAULT);

// If enabled, post registering PowerMonitor broadcast receiver to a background
// thread,
BASE_FEATURE(kPostPowerMonitorBroadcastReceiverInitToBackground,
             "PostPowerMonitorBroadcastReceiverInitToBackground",
             FEATURE_ENABLED_BY_DEFAULT);
// If enabled, getMyMemoryState IPC will be posted to background.
BASE_FEATURE(kPostGetMyMemoryStateToBackground,
             "PostGetMyMemoryStateToBackground",
             FEATURE_ENABLED_BY_DEFAULT);

// Update child process binding state before unbinding.
BASE_FEATURE(kUpdateStateBeforeUnbinding,
            "UpdateStateBeforeUnbinding",
            FEATURE_DISABLED_BY_DEFAULT);

// Use shared service connection to rebind a service binding to update the LRU
// in the ProcessList of OomAdjuster.
BASE_FEATURE(kUseSharedRebindServiceConnection,
             "UseSharedRebindServiceConnection",
             FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_ANDROID)

bool IsReducePPMsEnabled() {
  return g_is_reduce_ppms_enabled.load(std::memory_order_relaxed);
}

void Init(EmitThreadControllerProfilerMetadata
              emit_thread_controller_profiler_metadata) {
  g_is_reduce_ppms_enabled.store(FeatureList::IsEnabled(kReducePPMs),
                                 std::memory_order_relaxed);

  sequence_manager::internal::SequenceManagerImpl::InitializeFeatures();
  sequence_manager::internal::ThreadController::InitializeFeatures(
      emit_thread_controller_profiler_metadata);

  FilePath::InitializeFeatures();

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  MessagePumpEpoll::InitializeFeatures();
#endif

#if BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_CHROMEOS)
  PlatformThread::InitializeFeatures();
#endif

#if BUILDFLAG(IS_APPLE)
  File::InitializeFeatures();
  MessagePumpCFRunLoopBase::InitializeFeatures();

// Kqueue is not used for ios blink.
#if !BUILDFLAG(IS_IOS) || !BUILDFLAG(USE_BLINK)
  MessagePumpKqueue::InitializeFeatures();
#endif

#endif

#if BUILDFLAG(IS_ANDROID)
  android::InputHintChecker::InitializeFeatures();
#endif

#if BUILDFLAG(IS_WIN)
  sequence_manager::internal::ThreadControllerPowerMonitor::
      InitializeFeatures();
#endif
}

}  // namespace base::features
