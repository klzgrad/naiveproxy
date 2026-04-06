/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "statsd_binder_data_source.h"

#include <unistd.h>

#include <map>
#include <mutex>
#include <optional>

#include "perfetto/base/time.h"
#include "perfetto/ext/base/no_destructor.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "src/android_internal/lazy_library_loader.h"
#include "src/android_internal/statsd.h"
#include "src/traced/probes/statsd_client/common.h"

#include "protos/perfetto/config/statsd/statsd_tracing_config.pbzero.h"
#include "protos/perfetto/trace/statsd/statsd_atom.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/third_party/statsd/shell_config.pbzero.h"
#include "protos/third_party/statsd/shell_data.pbzero.h"

using ::perfetto::protos::pbzero::StatsdPullAtomConfig;
using ::perfetto::protos::pbzero::StatsdShellSubscription;
using ::perfetto::protos::pbzero::StatsdTracingConfig;

using ShellDataDecoder = ::perfetto::proto::pbzero::ShellData_Decoder;

namespace perfetto {
namespace {

int32_t AddAtomSubscription(const uint8_t* subscription_config,
                            size_t num_bytes,
                            android_internal::AtomCallback callback,
                            void* cookie) {
  PERFETTO_LAZY_LOAD(android_internal::AddAtomSubscription, fn);
  if (fn) {
    return fn(subscription_config, num_bytes, callback, cookie);
  }
  return -1;
}

bool RemoveAtomSubscription(int32_t subscription_id) {
  PERFETTO_LAZY_LOAD(android_internal::RemoveAtomSubscription, fn);
  if (fn) {
    fn(subscription_id);
    return true;
  }
  return false;
}

bool FlushAtomSubscription(int32_t subscription_id) {
  PERFETTO_LAZY_LOAD(android_internal::FlushAtomSubscription, fn);
  if (fn) {
    fn(subscription_id);
    return true;
  }
  return false;
}

// This is a singleton for mapping Statsd subscriptions to their data source.
// It is needed to deal with all the threading weirdness binder introduces. The
// AtomCallback from AddAtomSubscription can happen on any of a pool of binder
// threads while StatsdBinderDatasource runs on the single main thread.
// This means that StatsdBinderDatasource could be destroyed while a
// AtomCallback is in progress. To guard against this all the mapping
// to/from subscription_id/StatsdBinderDatasource happens under the lock
// of SubscriptionTracker.
class SubscriptionTracker {
 public:
  struct Entry {
    base::TaskRunner* task_runner;
    base::WeakPtr<StatsdBinderDataSource> data_source;
  };

  static SubscriptionTracker* Get();
  void OnData(int32_t subscription_id,
              uint32_t reason,
              uint8_t* data,
              size_t sz);
  int32_t Register(base::TaskRunner* task_runner,
                   base::WeakPtr<StatsdBinderDataSource> data_source,
                   const std::string& config);
  void Unregister(int32_t subscription_id);

 private:
  friend base::NoDestructor<SubscriptionTracker>;

  SubscriptionTracker();
  virtual ~SubscriptionTracker();
  SubscriptionTracker(const SubscriptionTracker&) = delete;
  SubscriptionTracker& operator=(const SubscriptionTracker&) = delete;

  // lock_ guards access to subscriptions_
  std::mutex lock_;
  std::map<int32_t, Entry> subscriptions_;
};

// static
SubscriptionTracker* SubscriptionTracker::Get() {
  static base::NoDestructor<SubscriptionTracker> instance;
  return &(instance.ref());
}

SubscriptionTracker::SubscriptionTracker() {}
SubscriptionTracker::~SubscriptionTracker() = default;

void SubscriptionTracker::OnData(int32_t subscription_id,
                                 uint32_t reason,
                                 uint8_t* data,
                                 size_t sz) {
  // Allocate and copy before we take the lock:
  std::shared_ptr<uint8_t> copy(new uint8_t[sz],
                                std::default_delete<uint8_t[]>());
  memcpy(copy.get(), data, sz);

  std::lock_guard<std::mutex> scoped_lock(lock_);

  auto it = subscriptions_.find(subscription_id);
  if (it == subscriptions_.end()) {
    // This is very paranoid and should not be required (since
    // ~StatsdBinderDataSource will call this) however it would be awful to get
    // stuck in a situation where statsd is sending us data forever and we're
    // immediately dropping it on the floor - so if nothing wants the data we
    // end the subscription. In the case the subscription is already gone this
    // is a noop in libstatspull.
    RemoveAtomSubscription(subscription_id);
    return;
  }

  base::TaskRunner* task_runner = it->second.task_runner;
  base::WeakPtr<StatsdBinderDataSource> data_source = it->second.data_source;

  task_runner->PostTask([data_source, reason, copy = std::move(copy), sz]() {
    if (data_source) {
      data_source->OnData(reason, copy.get(), sz);
    }
  });
}

int32_t SubscriptionTracker::Register(
    base::TaskRunner* task_runner,
    base::WeakPtr<StatsdBinderDataSource> data_source,
    const std::string& config) {
  std::lock_guard<std::mutex> scoped_lock(lock_);

  // We do this here (as opposed to in StatsdBinderDataSource) so that
  // we can hold the lock while we do and avoid the tiny race window between
  // getting the subscription id and putting that id in the subscriptions_ map
  auto* begin = reinterpret_cast<const uint8_t*>(config.data());
  size_t size = config.size();
  int32_t id = AddAtomSubscription(
      begin, size,
      [](int32_t subscription_id, uint32_t reason, uint8_t* payload,
         size_t num_bytes, void*) {
        SubscriptionTracker::Get()->OnData(subscription_id, reason, payload,
                                           num_bytes);
      },
      nullptr);

  if (id >= 0) {
    subscriptions_[id] = Entry{task_runner, data_source};
  }

  return id;
}

void SubscriptionTracker::Unregister(int32_t subscription_id) {
  std::lock_guard<std::mutex> scoped_lock(lock_);

  auto it = subscriptions_.find(subscription_id);
  if (it != subscriptions_.end()) {
    subscriptions_.erase(it);
  }

  // Unregister is called both when the data source is finishing
  // (~StatsdBinderDataSource) but also when we observe a
  // kAtomCallbackReasonSubscriptionEnded message. In the latter
  // case this call is unnecessary (the statsd subscription is already
  // gone) but it doesn't hurt.
  RemoveAtomSubscription(subscription_id);
}

}  // namespace

// static
const ProbesDataSource::Descriptor StatsdBinderDataSource::descriptor = {
    /*name*/ "android.statsd",
    /*flags*/ Descriptor::kFlagsNone,
    /*fill_descriptor_func*/ nullptr,
};

StatsdBinderDataSource::StatsdBinderDataSource(
    base::TaskRunner* task_runner,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer,
    const DataSourceConfig& ds_config)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      shell_subscription_(CreateStatsdShellConfig(ds_config)),
      weak_factory_(this) {}

StatsdBinderDataSource::~StatsdBinderDataSource() {
  if (subscription_id_ >= 0) {
    SubscriptionTracker::Get()->Unregister(subscription_id_);
    subscription_id_ = -1;
  }
}

void StatsdBinderDataSource::Start() {
  // Don't bother actually connecting to statsd if no pull/push atoms
  // were configured:
  if (shell_subscription_.empty()) {
    PERFETTO_LOG("Empty statsd config. Not connecting to statsd.");
    return;
  }

  auto weak_this = weak_factory_.GetWeakPtr();
  subscription_id_ = SubscriptionTracker::Get()->Register(
      task_runner_, weak_this, shell_subscription_);
}

void StatsdBinderDataSource::OnData(uint32_t reason,
                                    const uint8_t* data,
                                    size_t sz) {
  ShellDataDecoder message(data, sz);
  if (message.has_atom()) {
    TraceWriter::TracePacketHandle packet = writer_->NewTracePacket();

    // The root packet gets the timestamp of *now* to aid in
    // a) Packet sorting in trace_processor
    // b) So we have some useful record of timestamp in case the statsd
    //    one gets broken in some exciting way.
    packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));

    // Now put all the data. We rely on ShellData and StatsdAtom
    // matching format exactly.
    packet->AppendBytes(protos::pbzero::TracePacket::kStatsdAtomFieldNumber,
                        message.begin(),
                        static_cast<size_t>(message.end() - message.begin()));
  }

  // If we have the pending flush in progress resolve that:
  if (reason == android_internal::kAtomCallbackReasonFlushRequested &&
      pending_flush_callback_) {
    writer_->Flush(pending_flush_callback_);
    pending_flush_callback_ = nullptr;
  }

  if (reason == android_internal::kAtomCallbackReasonSubscriptionEnded) {
    // This is the last packet so unregister self. It's not required to do this
    // since we clean up in the destructor but it doesn't hurt.
    SubscriptionTracker::Get()->Unregister(subscription_id_);
    subscription_id_ = -1;
  }
}

void StatsdBinderDataSource::Flush(FlushRequestID,
                                   std::function<void()> callback) {
  if (subscription_id_ < 0) {
    writer_->Flush(callback);
  } else {
    // We don't want to queue up pending flushes to avoid a situation where
    // we end up will giant queue of unresolved flushes if statsd never replies.
    // To avoid this if there is already a flush in flight finish that one now:
    if (pending_flush_callback_) {
      writer_->Flush(pending_flush_callback_);
    }

    // Remember the callback for later.
    pending_flush_callback_ = callback;

    // Start the flush
    if (!FlushAtomSubscription(subscription_id_)) {
      // If it fails immediately we're done:
      writer_->Flush(pending_flush_callback_);
      pending_flush_callback_ = nullptr;
    }
  }
}

void StatsdBinderDataSource::ClearIncrementalState() {}

}  // namespace perfetto
