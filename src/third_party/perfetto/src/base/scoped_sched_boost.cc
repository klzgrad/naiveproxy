/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/ext/base/scoped_sched_boost.h"

#include "perfetto/ext/base/status_macros.h"

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
#include <sched.h>         // for 'SCHED_' macros and 'sched_' functions
#include <sys/resource.h>  // for 'setpriority', 'getpriority', 'PRIO_PROCESS'
#include <sys/types.h>     // for 'pid_t'

#include <algorithm>
#include <vector>

#endif  // PERFETTO_OS_LINUX || PERFETTO_OS_ANDROID

namespace perfetto::base {

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)

namespace {
constexpr pid_t kCurrentPid = 0;

class ThreadMgr {
 public:
  // In this class we manage the boost state for the whole process.
  // If we want to use it for each single thread, this needs to become
  // 'GetInstanceForThread()'.
  static ThreadMgr* GetInstance();

  explicit ThreadMgr(SchedOsHooks*);

  Status Add(SchedPolicyAndPrio);
  void Remove(SchedPolicyAndPrio);
  Status RecalcAndUpdatePrio();

  ThreadMgr(const ThreadMgr&) = delete;
  ThreadMgr& operator=(const ThreadMgr&) = delete;
  ThreadMgr(ThreadMgr&&) = delete;
  ThreadMgr& operator=(ThreadMgr&&) = delete;

  void ResetForTesting(SchedOsHooks*);

 private:
  SchedOsHooks* os_hooks_;
  SchedOsHooks::SchedOsConfig initial_config_{};
  std::vector<SchedPolicyAndPrio> prios_;
};

ThreadMgr* ThreadMgr::GetInstance() {
  static auto* instance = new ThreadMgr(SchedOsHooks::GetInstance());
  return instance;
}
ThreadMgr::ThreadMgr(SchedOsHooks* os_hooks) : os_hooks_(os_hooks) {
  auto res = os_hooks_->GetCurrentSchedConfig();
  if (!res.ok()) {
    // Should never fail: even without CAP_SYS_NICE we can always get our own
    // policy and prio. If something goes very wrong, log an error and use
    // SCHED_OTHER as initial config.
    PERFETTO_DFATAL_OR_ELOG("Failed to get default sched config: %s",
                            res.status().c_message());
    initial_config_ = SchedOsHooks::SchedOsConfig{SCHED_OTHER, 0, 0};
  } else {
    initial_config_ = res.value();
  }
}

Status ThreadMgr::Add(SchedPolicyAndPrio spp) {
  prios_.push_back(spp);
  return RecalcAndUpdatePrio();
}

void ThreadMgr::Remove(SchedPolicyAndPrio spp) {
  auto it = std::find(prios_.begin(), prios_.end(), spp);
  if (it != prios_.end()) {
    prios_.erase(it);
  }
  // It is possible that we previously added the wrongly configured policy,
  // that wasn't the max policy. In that case the policy will be validated now,
  // in 'RecalcAndUpdatePrio'.
  // This for loop makes us ignore all wrongly configured policies and fallback
  // to the first correct (or the initial one).
  for (;;) {
    if (auto res = RecalcAndUpdatePrio(); !res.ok()) {
      PERFETTO_ELOG("%s", res.c_message());
    } else {
      break;
    }
  }
}

Status ThreadMgr::RecalcAndUpdatePrio() {
  if (prios_.empty()) {
    return os_hooks_->SetSchedConfig(initial_config_);
  }
  // TODO(ktimofeev): Check previously set prio to skip unnecessary syscall?
  auto max_prio = std::max_element(prios_.begin(), prios_.end());
  SchedOsHooks::SchedOsConfig os_config{};
  switch (max_prio->policy) {
    case SchedPolicyAndPrio::Policy::kSchedOther:
      os_config = SchedOsHooks::SchedOsConfig{
          SCHED_OTHER, 0, -1 * static_cast<int>(max_prio->prio)};
      break;
    case SchedPolicyAndPrio::Policy::kSchedFifo:
      os_config = SchedOsHooks::SchedOsConfig{
          SCHED_FIFO, static_cast<int>(max_prio->prio), 0};
      break;
  }
  Status res = os_hooks_->SetSchedConfig(os_config);
  if (!res.ok()) {
    prios_.erase(max_prio);
    return res;
  }
  return OkStatus();
}

void ThreadMgr::ResetForTesting(SchedOsHooks* os_hooks) {
  os_hooks_ = os_hooks;
  initial_config_ = os_hooks_->GetCurrentSchedConfig().value();
  prios_.clear();
}

}  // namespace

SchedOsHooks* SchedOsHooks::GetInstance() {
  static auto* instance = new SchedOsHooks();
  return instance;
}

Status SchedOsHooks::SetSchedConfig(const SchedOsConfig& arg) {
  sched_param param{};
  param.sched_priority = arg.rt_prio;
  int ret = sched_setscheduler(kCurrentPid, arg.policy, &param);
  if (ret == -1) {
    return ErrStatus("sched_setscheduler(%d, %d) failed (errno: %d, %s)",
                     arg.policy, arg.rt_prio, errno, strerror(errno));
  }
  // For the real-time policies the 'arg.rt_prio' value is >= 1.
  // The userspace policies (SCHED_OTHER, SCHED_BATCH, SCHED_IDLE) use the nice
  // value.
  if (arg.rt_prio == 0) {
    ret = setpriority(PRIO_PROCESS, kCurrentPid, arg.nice);
    if (ret == -1) {
      return ErrStatus("setpriority(%d) failed (errno: %d, %s)", arg.nice,
                       errno, strerror(errno));
    }
  }
  return OkStatus();
}

StatusOr<SchedOsHooks::SchedOsConfig> SchedOsHooks::GetCurrentSchedConfig()
    const {
  int policy = sched_getscheduler(kCurrentPid);
  if (policy == -1) {
    return ErrStatus("sched_getscheduler failed (errno: %d, %s)", errno,
                     strerror(errno));
  }
  sched_param param{};
  if (sched_getparam(kCurrentPid, &param) == -1) {
    return ErrStatus("sched_getparam failed (errno: %d, %s)", errno,
                     strerror(errno));
  }
  int nice = 0;
  // For the real-time policies the 'param.sched_priority' value is >= 1.
  // The userspace policies (SCHED_OTHER, SCHED_BATCH, SCHED_IDLE) use the nice
  // value.
  if (param.sched_priority == 0) {
    errno = 0;
    nice = getpriority(PRIO_PROCESS, kCurrentPid);
    if (nice == -1 && errno != 0) {
      return ErrStatus("getpriority failed (errno: %d, %s)", errno,
                       strerror(errno));
    }
  }
  return SchedOsConfig{policy, param.sched_priority, nice};
}

// static
StatusOr<ScopedSchedBoost> ScopedSchedBoost::Boost(SchedPolicyAndPrio spp) {
  auto res = ThreadMgr::GetInstance()->Add(spp);
  RETURN_IF_ERROR(res);
  return ScopedSchedBoost(spp);
}

ScopedSchedBoost::ScopedSchedBoost(ScopedSchedBoost&& other) noexcept {
  this->policy_and_prio_ = other.policy_and_prio_;
  other.policy_and_prio_ = std::nullopt;
}

ScopedSchedBoost& ScopedSchedBoost::operator=(
    ScopedSchedBoost&& other) noexcept {
  if (this != &other) {
    this->~ScopedSchedBoost();
    new (this) ScopedSchedBoost(std::move(other));
  }
  return *this;
}

ScopedSchedBoost::~ScopedSchedBoost() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!policy_and_prio_.has_value())
    return;
  ThreadMgr::GetInstance()->Remove(*policy_and_prio_);
  policy_and_prio_ = std::nullopt;
}

// static
void ScopedSchedBoost::ResetForTesting(SchedOsHooks* os_manager) {
  ThreadMgr::GetInstance()->ResetForTesting(os_manager);
}

#else

// static
SchedOsHooks* SchedOsHooks::GetInstance() {
  return nullptr;
}

Status SchedOsHooks::SetSchedConfig(const SchedOsHooks::SchedOsConfig&) {
  return ErrStatus("SchedOsHooks is supported only on Linux/Android");
}

StatusOr<SchedOsHooks::SchedOsConfig> SchedOsHooks::GetCurrentSchedConfig()
    const {
  return ErrStatus("SchedOsHooks is supported only on Linux/Android");
}

// static
StatusOr<ScopedSchedBoost> ScopedSchedBoost::Boost(SchedPolicyAndPrio) {
  return ErrStatus("ScopedSchedBoost is supported only on Linux/Android");
}

ScopedSchedBoost::ScopedSchedBoost(ScopedSchedBoost&&) noexcept = default;
ScopedSchedBoost& ScopedSchedBoost::operator=(ScopedSchedBoost&&) noexcept =
    default;
ScopedSchedBoost::~ScopedSchedBoost() = default;

#endif  // PERFETTO_OS_LINUX || PERFETTO_OS_ANDROID

}  // namespace perfetto::base
