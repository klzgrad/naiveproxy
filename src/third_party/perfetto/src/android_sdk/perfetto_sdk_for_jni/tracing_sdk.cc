/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/android_sdk/perfetto_sdk_for_jni/tracing_sdk.h"

#include <sys/types.h>

#include <cstdarg>
#include <mutex>

#include "perfetto/public/abi/producer_abi.h"
#include "perfetto/public/producer.h"
#include "perfetto/public/te_macros.h"
#include "perfetto/public/track_event.h"

namespace perfetto {
namespace sdk_for_jni {
void register_perfetto(bool backend_in_process) {
  static std::once_flag registration;
  std::call_once(registration, [backend_in_process]() {
    struct PerfettoProducerInitArgs args = PERFETTO_PRODUCER_INIT_ARGS_INIT();
    args.backends = backend_in_process ? PERFETTO_BACKEND_IN_PROCESS
                                       : PERFETTO_BACKEND_SYSTEM;
    args.shmem_size_hint_kb = 1024;
    PerfettoProducerInit(args);
    PerfettoTeInit();
  });
}

void trace_event(int type,
                 const PerfettoTeCategory* perfettoTeCategory,
                 const char* name,
                 Extra* extra) {
  bool enabled = PERFETTO_UNLIKELY(PERFETTO_ATOMIC_LOAD_EXPLICIT(
      perfettoTeCategory->enabled, PERFETTO_MEMORY_ORDER_RELAXED));
  if (enabled) {
    extra->push_extra(nullptr);
    PerfettoTeHlEmitImpl(perfettoTeCategory->impl, type,
                         type == PERFETTO_TE_TYPE_COUNTER ? nullptr : name,
                         extra->get());
    extra->clear_extras();
  }
}

uint64_t get_process_track_uuid() {
  return PerfettoTeProcessTrackUuid();
}

uint64_t get_thread_track_uuid(pid_t tid) {
  // Cating a signed pid_t to unsigned
  return PerfettoTeProcessTrackUuid() ^ PERFETTO_STATIC_CAST(uint64_t, tid);
}

Extra::Extra() {}

void Extra::push_extra(PerfettoTeHlExtra* ptr) {
  extras_.push_back(ptr);
}

void Extra::pop_extra() {
  extras_.pop_back();
}

void Extra::clear_extras() {
  extras_.clear();
}

void Extra::delete_extra(Extra* ptr) {
  delete ptr;
}

Category::Category(const std::string& name) : Category(name, {}) {}

Category::Category(const std::string& name,
                   const std::vector<std::string>& tags)
    : category_({&perfetto_atomic_false, {}, {}, 0}), name_(name), tags_(tags) {
  for (const auto& tag : tags_) {
    tags_data_.push_back(tag.data());
  }
}

Category::~Category() {
  unregister_category();
}

void Category::register_category() {
  if (category_.impl)
    return;

  category_.desc = {name_.c_str(), name_.c_str(), tags_data_.data(),
                    tags_data_.size()};

  PerfettoTeCategoryRegister(&category_);
  PerfettoTePublishCategories();
}

void Category::unregister_category() {
  if (!category_.impl)
    return;

  PerfettoTeCategoryUnregister(&category_);
  PerfettoTePublishCategories();
}

bool Category::is_category_enabled() {
  return PERFETTO_UNLIKELY(PERFETTO_ATOMIC_LOAD_EXPLICIT(
      (category_).enabled, PERFETTO_MEMORY_ORDER_RELAXED));
}

void Category::delete_category(Category* ptr) {
  delete ptr;
}

Flow::Flow() : flow_{} {}

void Flow::set_process_flow(uint64_t id) {
  flow_.header.type = PERFETTO_TE_HL_EXTRA_TYPE_FLOW;
  PerfettoTeFlow ret = PerfettoTeProcessScopedFlow(id);
  flow_.id = ret.id;
}

void Flow::set_process_terminating_flow(uint64_t id) {
  flow_.header.type = PERFETTO_TE_HL_EXTRA_TYPE_TERMINATING_FLOW;
  PerfettoTeFlow ret = PerfettoTeProcessScopedFlow(id);
  flow_.id = ret.id;
}

void Flow::delete_flow(Flow* ptr) {
  delete ptr;
}

NamedTrack::NamedTrack(uint64_t id,
                       uint64_t parent_uuid,
                       const std::string& name)
    : name_(name),
      track_{{PERFETTO_TE_HL_EXTRA_TYPE_NAMED_TRACK},
             name_.data(),
             id,
             parent_uuid} {}

void NamedTrack::delete_track(NamedTrack* ptr) {
  delete ptr;
}

RegisteredTrack::RegisteredTrack(uint64_t id,
                                 uint64_t parent_uuid,
                                 const std::string& name,
                                 bool is_counter)
    : registered_track_{},
      track_{{PERFETTO_TE_HL_EXTRA_TYPE_REGISTERED_TRACK},
             &(registered_track_.impl)},
      name_(name),
      id_(id),
      parent_uuid_(parent_uuid),
      is_counter_(is_counter) {
  register_track();
}

RegisteredTrack::~RegisteredTrack() {
  unregister_track();
}

void RegisteredTrack::register_track() {
  if (registered_track_.impl.descriptor)
    return;

  if (is_counter_) {
    PerfettoTeCounterTrackRegister(&registered_track_, name_.data(),
                                   parent_uuid_);
  } else {
    PerfettoTeNamedTrackRegister(&registered_track_, name_.data(), id_,
                                 parent_uuid_);
  }
}

void RegisteredTrack::unregister_track() {
  if (!registered_track_.impl.descriptor)
    return;
  PerfettoTeRegisteredTrackUnregister(&registered_track_);
}

void RegisteredTrack::delete_track(RegisteredTrack* ptr) {
  delete ptr;
}

Proto::Proto() : proto_({{PERFETTO_TE_HL_EXTRA_TYPE_PROTO_FIELDS}, nullptr}) {}

void Proto::add_field(PerfettoTeHlProtoField* ptr) {
  if (!fields_.empty()) {
    fields_.pop_back();
  }

  fields_.push_back(ptr);
  fields_.push_back(nullptr);
  proto_.fields = fields_.data();
}

void Proto::clear_fields() {
  fields_.clear();
  proto_.fields = nullptr;
}

void Proto::delete_proto(Proto* ptr) {
  delete ptr;
}

ProtoFieldNested::ProtoFieldNested()
    : field_({{PERFETTO_TE_HL_PROTO_TYPE_NESTED, 0}, nullptr}) {}

void ProtoFieldNested::add_field(PerfettoTeHlProtoField* ptr) {
  if (!fields_.empty()) {
    fields_.pop_back();
  }

  fields_.push_back(ptr);
  fields_.push_back(nullptr);
  field_.fields = fields_.data();
}

void ProtoFieldNested::set_id(uint32_t id) {
  fields_.clear();
  field_.header.id = id;
  field_.fields = nullptr;
}

void ProtoFieldNested::delete_field(ProtoFieldNested* ptr) {
  delete ptr;
}

Session::Session(bool is_backend_in_process, void* buf, size_t len) {
  session_ = PerfettoTracingSessionCreate(is_backend_in_process
                                              ? PERFETTO_BACKEND_IN_PROCESS
                                              : PERFETTO_BACKEND_SYSTEM);

  PerfettoTracingSessionSetup(session_, buf, len);

  PerfettoTracingSessionStartBlocking(session_);
}

Session::~Session() {
  PerfettoTracingSessionStopBlocking(session_);
  PerfettoTracingSessionDestroy(session_);
}

bool Session::FlushBlocking(uint32_t timeout_ms) {
  return PerfettoTracingSessionFlushBlocking(session_, timeout_ms);
}

void Session::StopBlocking() {
  PerfettoTracingSessionStopBlocking(session_);
}

std::vector<uint8_t> Session::ReadBlocking() {
  std::vector<uint8_t> data;
  PerfettoTracingSessionReadTraceBlocking(
      session_,
      [](struct PerfettoTracingSessionImpl*, const void* trace_data,
         size_t size, bool, void* user_arg) {
        auto& dst = *static_cast<std::vector<uint8_t>*>(user_arg);
        auto* src = static_cast<const uint8_t*>(trace_data);
        dst.insert(dst.end(), src, src + size);
      },
      &data);
  return data;
}

void Session::delete_session(Session* ptr) {
  delete ptr;
}

void activate_trigger(const char* name, uint32_t ttl_ms) {
  const char* names[] = {name, nullptr};
  PerfettoProducerActivateTriggers(names, ttl_ms);
}
}  // namespace sdk_for_jni
}  // namespace perfetto
