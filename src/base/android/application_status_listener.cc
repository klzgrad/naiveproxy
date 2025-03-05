// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/application_status_listener.h"

#include <jni.h>

#include "base/functional/callback.h"
#include "base/lazy_instance.h"
#include "base/metrics/user_metrics.h"
#include "base/observer_list_threadsafe.h"
#include "base/trace_event/base_tracing.h"

#if BUILDFLAG(ENABLE_BASE_TRACING)
#include "base/trace_event/application_state_proto_android.h"  // no-presubmit-check
#endif  // BUILDFLAG(ENABLE_BASE_TRACING)

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "base/tasks_jni/ApplicationStatus_jni.h"

namespace base {
namespace android {

namespace {

class ApplicationStatusListenerImpl;

struct LeakyLazyObserverListTraits
    : base::internal::LeakyLazyInstanceTraits<
          ObserverListThreadSafe<ApplicationStatusListenerImpl>> {
  static ObserverListThreadSafe<ApplicationStatusListenerImpl>* New(
      void* instance) {
    ObserverListThreadSafe<ApplicationStatusListenerImpl>* ret =
        base::internal::LeakyLazyInstanceTraits<ObserverListThreadSafe<
            ApplicationStatusListenerImpl>>::New(instance);
    // Leaky.
    ret->AddRef();
    return ret;
  }
};

LazyInstance<ObserverListThreadSafe<ApplicationStatusListenerImpl>,
             LeakyLazyObserverListTraits>
    g_observers = LAZY_INSTANCE_INITIALIZER;

class ApplicationStatusListenerImpl : public ApplicationStatusListener {
 public:
  ApplicationStatusListenerImpl(
      const ApplicationStateChangeCallback& callback) {
    SetCallback(callback);
    g_observers.Get().AddObserver(this);

    Java_ApplicationStatus_registerThreadSafeNativeApplicationStateListener(
        AttachCurrentThread());
  }

  ~ApplicationStatusListenerImpl() override {
    g_observers.Get().RemoveObserver(this);
  }

  void SetCallback(const ApplicationStateChangeCallback& callback) override {
    DCHECK(!callback_);
    DCHECK(callback);
    callback_ = callback;
  }

  void Notify(ApplicationState state) override {
    if (callback_) {
      callback_.Run(state);
    }
  }

 private:
  ApplicationStateChangeCallback callback_;
};

}  // namespace

ApplicationStatusListener::ApplicationStatusListener() = default;
ApplicationStatusListener::~ApplicationStatusListener() = default;

// static
std::unique_ptr<ApplicationStatusListener> ApplicationStatusListener::New(
    const ApplicationStateChangeCallback& callback) {
  return std::make_unique<ApplicationStatusListenerImpl>(callback);
}

// static
void ApplicationStatusListener::NotifyApplicationStateChange(
    ApplicationState state) {
  TRACE_APPLICATION_STATE(state);
  switch (state) {
    case APPLICATION_STATE_UNKNOWN:
    case APPLICATION_STATE_HAS_DESTROYED_ACTIVITIES:
      break;
    case APPLICATION_STATE_HAS_RUNNING_ACTIVITIES:
      RecordAction(UserMetricsAction("Android.LifeCycle.HasRunningActivities"));
      break;
    case APPLICATION_STATE_HAS_PAUSED_ACTIVITIES:
      RecordAction(UserMetricsAction("Android.LifeCycle.HasPausedActivities"));
      break;
    case APPLICATION_STATE_HAS_STOPPED_ACTIVITIES:
      RecordAction(UserMetricsAction("Android.LifeCycle.HasStoppedActivities"));
      break;
  }
  g_observers.Get().Notify(FROM_HERE, &ApplicationStatusListenerImpl::Notify,
                           state);
}

// static
ApplicationState ApplicationStatusListener::GetState() {
  return static_cast<ApplicationState>(
      Java_ApplicationStatus_getStateForApplication(AttachCurrentThread()));
}

static void JNI_ApplicationStatus_OnApplicationStateChange(JNIEnv* env,
                                                           jint new_state) {
  ApplicationState application_state = static_cast<ApplicationState>(new_state);
  ApplicationStatusListener::NotifyApplicationStateChange(application_state);
}

// static
bool ApplicationStatusListener::HasVisibleActivities() {
  return Java_ApplicationStatus_hasVisibleActivities(AttachCurrentThread());
}

}  // namespace android
}  // namespace base
