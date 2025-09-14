/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "perfetto/public/abi/data_source_abi.h"

#include <bitset>

#include "perfetto/base/thread_annotations.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "perfetto/tracing/data_source.h"
#include "perfetto/tracing/internal/basic_types.h"
#include "protos/perfetto/common/data_source_descriptor.gen.h"
#include "protos/perfetto/config/data_source_config.gen.h"
#include "src/shared_lib/reset_for_testing.h"
#include "src/shared_lib/stream_writer.h"

namespace {

using ::perfetto::internal::DataSourceInstanceThreadLocalState;
using ::perfetto::internal::DataSourceThreadLocalState;
using ::perfetto::internal::DataSourceType;

thread_local DataSourceThreadLocalState*
    g_tls_cache[perfetto::internal::kMaxDataSources];

}  // namespace

// Implementation of a shared library data source type (there's one of these per
// type, not per instance).
//
// Returned to the C side when invoking PerfettoDsCreateImpl(). The C side only
// has an opaque pointer to this.
struct PerfettoDsImpl {
  // Instance lifecycle callbacks.
  PerfettoDsOnSetupCb on_setup_cb = nullptr;
  PerfettoDsOnStartCb on_start_cb = nullptr;
  PerfettoDsOnStopCb on_stop_cb = nullptr;
  PerfettoDsOnDestroyCb on_destroy_cb = nullptr;
  PerfettoDsOnFlushCb on_flush_cb = nullptr;

  // These are called to create/delete custom thread-local instance state.
  PerfettoDsOnCreateCustomState on_create_tls_cb = nullptr;
  PerfettoDsOnDeleteCustomState on_delete_tls_cb = nullptr;

  // These are called to create/delete custom thread-local instance incremental
  // state.
  PerfettoDsOnCreateCustomState on_create_incr_cb = nullptr;
  PerfettoDsOnDeleteCustomState on_delete_incr_cb = nullptr;

  // Passed to all the callbacks as the `user_arg` param.
  void* cb_user_arg;

  perfetto::BufferExhaustedPolicy buffer_exhausted_policy =
      perfetto::BufferExhaustedPolicy::kDrop;

  bool buffer_exhausted_policy_configurable = false;

  DataSourceType cpp_type;
  std::atomic<bool> enabled{false};
  std::mutex mu;
  std::bitset<perfetto::internal::kMaxDataSourceInstances> enabled_instances
      PERFETTO_GUARDED_BY(mu);

  bool IsRegistered() {
    return cpp_type.static_state()->index !=
           perfetto::internal::kMaxDataSources;
  }
};

struct PerfettoDsOnStopArgs {
  struct PerfettoDsAsyncStopper* stopper = nullptr;
};

struct PerfettoDsAsyncStopper {
  PerfettoDsImpl* ds_impl;
  uint32_t instance_idx;
  std::function<void()> async_stop_closure;

  void FinishStop() {
    std::lock_guard<std::mutex> lock(ds_impl->mu);
    ds_impl->enabled_instances.reset(instance_idx);
    if (ds_impl->enabled_instances.none()) {
      ds_impl->enabled.store(false, std::memory_order_release);
    }
    async_stop_closure();
  }
};

namespace perfetto {
namespace shlib {

// These are only exposed to tests.

void ResetDataSourceTls() {
  memset(g_tls_cache, 0, sizeof(g_tls_cache));
}

void DsImplDestroy(struct PerfettoDsImpl* ds_impl) {
  delete ds_impl;
}

}  // namespace shlib
}  // namespace perfetto

namespace {

// Represents a global data source instance (there can be more than one of these
// for a single data source type).
class ShlibDataSource : public perfetto::DataSourceBase {
 public:
  explicit ShlibDataSource(PerfettoDsImpl* type) : type_(*type) {}

  void OnSetup(const SetupArgs& args) override {
    if (type_.on_setup_cb) {
      std::vector<uint8_t> serialized_config = args.config->SerializeAsArray();
      inst_ctx_ = type_.on_setup_cb(
          &type_, args.internal_instance_index, serialized_config.data(),
          serialized_config.size(), type_.cb_user_arg, nullptr);
    }
    std::lock_guard<std::mutex> lock(type_.mu);
    const bool was_enabled = type_.enabled_instances.any();
    type_.enabled_instances.set(args.internal_instance_index);
    if (!was_enabled && type_.enabled_instances.any()) {
      type_.enabled.store(true, std::memory_order_release);
    }
  }

  void OnStart(const StartArgs& args) override {
    if (type_.on_start_cb) {
      type_.on_start_cb(&type_, args.internal_instance_index, type_.cb_user_arg,
                        inst_ctx_, nullptr);
    }
  }

  void OnStop(const StopArgs& args) override {
    PerfettoDsOnStopArgs c_args;
    c_args.stopper = new PerfettoDsAsyncStopper();
    // Capturing ds_impl is ok, because data sources cannot be unregistered.
    c_args.stopper->ds_impl = &type_;
    c_args.stopper->async_stop_closure = args.HandleStopAsynchronously();
    c_args.stopper->instance_idx = args.internal_instance_index;

    if (type_.on_stop_cb) {
      type_.on_stop_cb(&type_, args.internal_instance_index, type_.cb_user_arg,
                       inst_ctx_, &c_args);
    }

    // If c_args.stopper is nullptr, the user must have called
    // PerfettoDsOnStopArgsPostpone() in the callback above: the user will
    // invoke PerfettoDsStopDone later. If c_args.stopper is not nullptr, we
    // need to invoke it.
    if (c_args.stopper) {
      PerfettoDsStopDone(c_args.stopper);
    }
  }

  ~ShlibDataSource() override {
    if (type_.on_destroy_cb) {
      type_.on_destroy_cb(&type_, type_.cb_user_arg, inst_ctx_);
    }
  }

  void OnFlush(const FlushArgs& args) override {
    if (type_.on_flush_cb) {
      type_.on_flush_cb(
          &type_, args.internal_instance_index, type_.cb_user_arg, inst_ctx_,
          const_cast<PerfettoDsOnFlushArgs*>(
              reinterpret_cast<const PerfettoDsOnFlushArgs*>(&args)));
    }
  }

  const PerfettoDsImpl& type() const { return type_; }

  void* inst_ctx() const { return inst_ctx_; }

 private:
  PerfettoDsImpl& type_;
  void* inst_ctx_ = nullptr;
};

struct DataSourceTraits {
  static DataSourceThreadLocalState* GetDataSourceTLS(
      perfetto::internal::DataSourceStaticState* static_state,
      perfetto::internal::TracingTLS* root_tls) {
    auto* ds_tls = &root_tls->data_sources_tls[static_state->index];
    // ds_tls->static_state can be:
    // * nullptr
    // * equal to static_state
    // * equal to the static state of a different data source, in tests (when
    //   ResetForTesting() has been used)
    // In any case, there's no need to do anything, the caller will reinitialize
    // static_state.
    return ds_tls;
  }
};

struct TracePointTraits {
  using TracePointData = DataSourceType*;
  static std::atomic<uint32_t>* GetActiveInstances(TracePointData s) {
    return s->valid_instances();
  }
};

DataSourceInstanceThreadLocalState::ObjectWithDeleter CreateShlibTls(
    DataSourceInstanceThreadLocalState* tls_inst,
    uint32_t inst_idx,
    void* ctx) {
  auto* ds_impl = reinterpret_cast<PerfettoDsImpl*>(ctx);

  void* custom_state = ds_impl->on_create_tls_cb(
      ds_impl, inst_idx, reinterpret_cast<PerfettoDsTracerImpl*>(tls_inst),
      ds_impl->cb_user_arg);
  return DataSourceInstanceThreadLocalState::ObjectWithDeleter(
      custom_state, ds_impl->on_delete_tls_cb);
}

DataSourceInstanceThreadLocalState::ObjectWithDeleter
CreateShlibIncrementalState(DataSourceInstanceThreadLocalState* tls_inst,
                            uint32_t inst_idx,
                            void* ctx) {
  auto* ds_impl = reinterpret_cast<PerfettoDsImpl*>(ctx);

  void* custom_state = ds_impl->on_create_incr_cb(
      ds_impl, inst_idx, reinterpret_cast<PerfettoDsTracerImpl*>(tls_inst),
      ds_impl->cb_user_arg);
  return DataSourceInstanceThreadLocalState::ObjectWithDeleter(
      custom_state, ds_impl->on_delete_incr_cb);
}

}  // namespace

// Exposed through data_source_abi.h
std::atomic<bool> perfetto_atomic_false{false};

struct PerfettoDsImpl* PerfettoDsImplCreate() {
  return new PerfettoDsImpl();
}

void PerfettoDsSetOnSetupCallback(struct PerfettoDsImpl* ds_impl,
                                  PerfettoDsOnSetupCb cb) {
  PERFETTO_CHECK(!ds_impl->IsRegistered());
  ds_impl->on_setup_cb = cb;
}

void PerfettoDsSetOnStartCallback(struct PerfettoDsImpl* ds_impl,
                                  PerfettoDsOnStartCb cb) {
  PERFETTO_CHECK(!ds_impl->IsRegistered());
  ds_impl->on_start_cb = cb;
}

void PerfettoDsSetOnStopCallback(struct PerfettoDsImpl* ds_impl,
                                 PerfettoDsOnStopCb cb) {
  PERFETTO_CHECK(!ds_impl->IsRegistered());
  ds_impl->on_stop_cb = cb;
}

void PerfettoDsSetOnDestroyCallback(struct PerfettoDsImpl* ds_impl,
                                    PerfettoDsOnDestroyCb cb) {
  PERFETTO_CHECK(!ds_impl->IsRegistered());
  ds_impl->on_destroy_cb = cb;
}

void PerfettoDsSetOnFlushCallback(struct PerfettoDsImpl* ds_impl,
                                  PerfettoDsOnFlushCb cb) {
  PERFETTO_CHECK(!ds_impl->IsRegistered());
  ds_impl->on_flush_cb = cb;
}

void PerfettoDsSetOnCreateTls(struct PerfettoDsImpl* ds_impl,
                              PerfettoDsOnCreateCustomState cb) {
  PERFETTO_CHECK(!ds_impl->IsRegistered());
  ds_impl->on_create_tls_cb = cb;
}

void PerfettoDsSetOnDeleteTls(struct PerfettoDsImpl* ds_impl,
                              PerfettoDsOnDeleteCustomState cb) {
  PERFETTO_CHECK(!ds_impl->IsRegistered());
  ds_impl->on_delete_tls_cb = cb;
}

void PerfettoDsSetOnCreateIncr(struct PerfettoDsImpl* ds_impl,
                               PerfettoDsOnCreateCustomState cb) {
  PERFETTO_CHECK(!ds_impl->IsRegistered());
  ds_impl->on_create_incr_cb = cb;
}

void PerfettoDsSetOnDeleteIncr(struct PerfettoDsImpl* ds_impl,
                               PerfettoDsOnDeleteCustomState cb) {
  PERFETTO_CHECK(!ds_impl->IsRegistered());
  ds_impl->on_delete_incr_cb = cb;
}

void PerfettoDsSetCbUserArg(struct PerfettoDsImpl* ds_impl, void* user_arg) {
  PERFETTO_CHECK(!ds_impl->IsRegistered());
  ds_impl->cb_user_arg = user_arg;
}

bool PerfettoDsSetBufferExhaustedPolicy(struct PerfettoDsImpl* ds_impl,
                                        uint32_t policy) {
  if (ds_impl->IsRegistered()) {
    return false;
  }

  switch (policy) {
    case PERFETTO_DS_BUFFER_EXHAUSTED_POLICY_DROP:
      ds_impl->buffer_exhausted_policy = perfetto::BufferExhaustedPolicy::kDrop;
      return true;
    case PERFETTO_DS_BUFFER_EXHAUSTED_POLICY_STALL_AND_ABORT:
      ds_impl->buffer_exhausted_policy =
          perfetto::BufferExhaustedPolicy::kStall;
      return true;
    case PERFETTO_DS_BUFFER_EXHAUSTED_POLICY_STALL_AND_DROP:
      ds_impl->buffer_exhausted_policy =
          perfetto::BufferExhaustedPolicy::kStallThenDrop;
      return true;
  }
  return false;
}

bool PerfettoDsSetBufferExhaustedPolicyConfigurable(
    struct PerfettoDsImpl* ds_impl,
    bool configurable) {
  if (ds_impl->IsRegistered()) {
    return false;
  }

  ds_impl->buffer_exhausted_policy_configurable = configurable;

  return true;
}

bool PerfettoDsImplRegister(struct PerfettoDsImpl* ds_impl,
                            PERFETTO_ATOMIC(bool) * *enabled_ptr,
                            const void* descriptor,
                            size_t descriptor_size) {
  std::unique_ptr<PerfettoDsImpl> data_source_type(ds_impl);

  perfetto::DataSourceDescriptor dsd;
  dsd.ParseFromArray(descriptor, descriptor_size);

  auto factory = [ds_impl]() {
    return std::unique_ptr<perfetto::DataSourceBase>(
        new ShlibDataSource(ds_impl));
  };

  DataSourceType::CreateCustomTlsFn create_custom_tls_fn = nullptr;
  DataSourceType::CreateIncrementalStateFn create_incremental_state_fn =
      nullptr;
  void* cb_ctx = nullptr;
  if (data_source_type->on_create_incr_cb &&
      data_source_type->on_delete_incr_cb) {
    create_incremental_state_fn = CreateShlibIncrementalState;
    cb_ctx = data_source_type.get();
  }
  if (data_source_type->on_create_tls_cb &&
      data_source_type->on_delete_tls_cb) {
    create_custom_tls_fn = CreateShlibTls;
    cb_ctx = data_source_type.get();
  }

  perfetto::internal::DataSourceParams params;
  params.default_buffer_exhausted_policy = ds_impl->buffer_exhausted_policy;
  params.buffer_exhausted_policy_configurable =
      ds_impl->buffer_exhausted_policy_configurable;
  params.supports_multiple_instances = true;
  params.requires_callbacks_under_lock = false;
  params.default_buffer_exhausted_policy =
      data_source_type->buffer_exhausted_policy;
  bool success = data_source_type->cpp_type.Register(
      dsd, factory, params, data_source_type->on_flush_cb == nullptr,
      create_custom_tls_fn, create_incremental_state_fn, cb_ctx);
  if (!success) {
    return false;
  }
  *enabled_ptr = &data_source_type->enabled;
  perfetto::base::ignore_result(data_source_type.release());
  return true;
}

void PerfettoDsImplUpdateDescriptor(struct PerfettoDsImpl* ds_impl,
                                    const void* descriptor,
                                    size_t descriptor_size) {
  perfetto::DataSourceDescriptor dsd;
  dsd.ParseFromArray(descriptor, descriptor_size);

  ds_impl->cpp_type.UpdateDescriptor(dsd);
}

PerfettoDsAsyncStopper* PerfettoDsOnStopArgsPostpone(
    PerfettoDsOnStopArgs* args) {
  PerfettoDsAsyncStopper* stopper = args->stopper;
  args->stopper = nullptr;
  return stopper;
}

void PerfettoDsStopDone(PerfettoDsAsyncStopper* stopper) {
  stopper->FinishStop();
  delete stopper;
}

PerfettoDsAsyncFlusher* PerfettoDsOnFlushArgsPostpone(
    PerfettoDsOnFlushArgs* args) {
  auto* cb = new std::function<void()>();
  *cb = reinterpret_cast<const ShlibDataSource::FlushArgs*>(args)
            ->HandleFlushAsynchronously();
  return reinterpret_cast<PerfettoDsAsyncFlusher*>(cb);
}

void PerfettoDsFlushDone(PerfettoDsAsyncFlusher* stopper) {
  auto* cb = reinterpret_cast<std::function<void()>*>(stopper);
  (*cb)();
  delete cb;
}

void* PerfettoDsImplGetInstanceLocked(struct PerfettoDsImpl* ds_impl,
                                      PerfettoDsInstanceIndex idx) {
  auto* internal_state = ds_impl->cpp_type.static_state()->TryGet(idx);
  if (!internal_state) {
    return nullptr;
  }
  std::unique_lock<std::recursive_mutex> lock(internal_state->lock);
  auto* data_source =
      static_cast<ShlibDataSource*>(internal_state->data_source.get());
  if (!data_source || &data_source->type() != ds_impl) {
    // The data source instance has been destroyed and recreated as a different
    // type while we where tracing.
    return nullptr;
  }
  void* inst_ctx = data_source->inst_ctx();
  if (inst_ctx != nullptr) {
    lock.release();
  }
  return inst_ctx;
}

void PerfettoDsImplReleaseInstanceLocked(struct PerfettoDsImpl* ds_impl,
                                         PerfettoDsInstanceIndex idx) {
  // The `valid_instances` bitmap might have changed since the lock has been
  // taken, but the instance must still be alive (we were holding the lock on
  // it).
  auto* internal_state = ds_impl->cpp_type.static_state()->GetUnsafe(idx);
  internal_state->lock.unlock();
}

void* PerfettoDsImplGetCustomTls(struct PerfettoDsImpl*,
                                 struct PerfettoDsTracerImpl* tracer,
                                 PerfettoDsInstanceIndex) {
  auto* tls_inst =
      reinterpret_cast<DataSourceInstanceThreadLocalState*>(tracer);

  PERFETTO_DCHECK(tls_inst->data_source_custom_tls);
  return tls_inst->data_source_custom_tls.get();
}

void* PerfettoDsImplGetIncrementalState(struct PerfettoDsImpl* ds_impl,
                                        struct PerfettoDsTracerImpl* tracer,
                                        PerfettoDsInstanceIndex idx) {
  auto* tls_inst =
      reinterpret_cast<DataSourceInstanceThreadLocalState*>(tracer);

  return ds_impl->cpp_type.GetIncrementalState(tls_inst, idx);
}

struct PerfettoDsImplTracerIterator PerfettoDsImplTraceIterateBegin(
    struct PerfettoDsImpl* ds_impl) {
  DataSourceThreadLocalState** tls =
      &g_tls_cache[ds_impl->cpp_type.static_state()->index];

  struct PerfettoDsImplTracerIterator ret = {0, nullptr, nullptr};
  uint32_t cached_instances =
      ds_impl->cpp_type.valid_instances()->load(std::memory_order_relaxed);
  if (!cached_instances) {
    return ret;
  }
  bool res =
      ds_impl->cpp_type.TracePrologue<DataSourceTraits, TracePointTraits>(
          tls, &cached_instances, &ds_impl->cpp_type);
  if (!res) {
    return ret;
  }
  DataSourceType::InstancesIterator it =
      ds_impl->cpp_type.BeginIteration<TracePointTraits>(cached_instances, *tls,
                                                         &ds_impl->cpp_type);
  ret.inst_id = it.i;
  (*tls)->root_tls->cached_instances = it.cached_instances;
  ret.tracer = reinterpret_cast<struct PerfettoDsTracerImpl*>(it.instance);
  if (!ret.tracer) {
    ds_impl->cpp_type.TraceEpilogue(*tls);
  }

  ret.tls = reinterpret_cast<struct PerfettoDsTlsImpl*>(*tls);
  return ret;
}

void PerfettoDsImplTraceIterateNext(
    struct PerfettoDsImpl* ds_impl,
    struct PerfettoDsImplTracerIterator* iterator) {
  auto* tls = reinterpret_cast<DataSourceThreadLocalState*>(iterator->tls);

  DataSourceType::InstancesIterator it;
  it.i = iterator->inst_id;
  it.cached_instances = tls->root_tls->cached_instances;
  it.instance =
      reinterpret_cast<DataSourceInstanceThreadLocalState*>(iterator->tracer);

  ds_impl->cpp_type.NextIteration<TracePointTraits>(&it, tls,
                                                    &ds_impl->cpp_type);

  iterator->inst_id = it.i;
  tls->root_tls->cached_instances = it.cached_instances;
  iterator->tracer =
      reinterpret_cast<struct PerfettoDsTracerImpl*>(it.instance);

  if (!iterator->tracer) {
    ds_impl->cpp_type.TraceEpilogue(tls);
  }
}

void PerfettoDsImplTraceIterateBreak(
    struct PerfettoDsImpl* ds_impl,
    struct PerfettoDsImplTracerIterator* iterator) {
  auto* tls = reinterpret_cast<DataSourceThreadLocalState*>(iterator->tls);

  ds_impl->cpp_type.TraceEpilogue(tls);
}

struct PerfettoStreamWriter PerfettoDsTracerImplPacketBegin(
    struct PerfettoDsTracerImpl* tracer) {
  auto* tls_inst =
      reinterpret_cast<DataSourceInstanceThreadLocalState*>(tracer);

  auto message_handle = tls_inst->trace_writer->NewTracePacket();
  struct PerfettoStreamWriter ret;
  protozero::ScatteredStreamWriter* sw = message_handle.TakeStreamWriter();
  ret.impl = reinterpret_cast<PerfettoStreamWriterImpl*>(sw);
  perfetto::UpdateStreamWriter(*sw, &ret);
  return ret;
}

void PerfettoDsTracerImplPacketEnd(struct PerfettoDsTracerImpl* tracer,
                                   struct PerfettoStreamWriter* w) {
  auto* tls_inst =
      reinterpret_cast<DataSourceInstanceThreadLocalState*>(tracer);
  auto* sw = reinterpret_cast<protozero::ScatteredStreamWriter*>(w->impl);

  sw->set_write_ptr(w->write_ptr);
  tls_inst->trace_writer->FinishTracePacket();
}

void PerfettoDsTracerImplFlush(struct PerfettoDsTracerImpl* tracer,
                               PerfettoDsTracerOnFlushCb cb,
                               void* user_arg) {
  auto* tls_inst =
      reinterpret_cast<DataSourceInstanceThreadLocalState*>(tracer);

  std::function<void()> fn;
  if (cb != nullptr) {
    fn = [user_arg, cb]() { cb(user_arg); };
  }
  tls_inst->trace_writer->Flush(fn);
}
