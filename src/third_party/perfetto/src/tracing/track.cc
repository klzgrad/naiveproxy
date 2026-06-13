/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "perfetto/tracing/track.h"

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/fnv_hash.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/thread_utils.h"
#include "perfetto/ext/base/uuid.h"
#include "perfetto/tracing/internal/track_event_data_source.h"
#include "perfetto/tracing/internal/track_event_internal.h"
#include "protos/perfetto/trace/track_event/counter_descriptor.gen.h"
#include "protos/perfetto/trace/track_event/process_descriptor.gen.h"
#include "protos/perfetto/trace/track_event/process_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.gen.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"

namespace perfetto {

// static
uint64_t Track::process_uuid;

protos::gen::TrackDescriptor Track::Serialize() const {
  protos::gen::TrackDescriptor desc;
  desc.set_uuid(uuid);
  if (parent_uuid)
    desc.set_parent_uuid(parent_uuid);
  return desc;
}

void Track::Serialize(protos::pbzero::TrackDescriptor* desc) const {
  auto bytes = Serialize().SerializeAsString();
  desc->AppendRawProtoBytes(bytes.data(), bytes.size());
}

// static
Track Track::ThreadScoped(const void* ptr, Track parent) {
  if (parent.uuid == 0)
    return Track::FromPointer(ptr, ThreadTrack::Current());
  return Track::FromPointer(ptr, parent);
}

protos::gen::TrackDescriptor ProcessTrack::Serialize() const {
  auto desc = Track::Serialize();
  auto pd = desc.mutable_process();
  pd->set_pid(static_cast<int32_t>(pid));
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  std::string cmdline;
  if (base::ReadFile("/proc/self/cmdline", &cmdline)) {
    // Since cmdline is a zero-terminated list of arguments, this ends up
    // writing just the first element, i.e., the process name, into the process
    // name field.
    pd->set_process_name(cmdline.c_str());
    base::StringSplitter splitter(std::move(cmdline), '\0');
    while (splitter.Next()) {
      pd->add_cmdline(
          std::string(splitter.cur_token(), splitter.cur_token_size()));
    }
  }
  // TODO(skyostil): Record command line on Windows, FreeBSD and Mac.
#endif
  return desc;
}

void ProcessTrack::Serialize(protos::pbzero::TrackDescriptor* desc) const {
  auto bytes = Serialize().SerializeAsString();
  desc->AppendRawProtoBytes(bytes.data(), bytes.size());
}

protos::gen::TrackDescriptor ThreadTrack::Serialize() const {
  auto desc = Track::Serialize();
  auto td = desc.mutable_thread();
  td->set_pid(static_cast<int32_t>(pid));
  td->set_tid(static_cast<int32_t>(tid));
  if (disallow_merging_with_system_tracks) {
    desc.set_disallow_merging_with_system_tracks(true);
  }
  std::string thread_name;
  if (base::GetThreadName(thread_name))
    td->set_thread_name(thread_name);
  return desc;
}

// static
ThreadTrack ThreadTrack::Current() {
  return ThreadTrack(
      internal::TracingMuxer::Get()->GetCurrentThreadId(),
      internal::TrackEventInternal::GetDisallowMergingWithSystemTracks());
}

// static
ThreadTrack ThreadTrack::ForThread(base::PlatformThreadId tid_) {
  return ThreadTrack(
      tid_, internal::TrackEventInternal::GetDisallowMergingWithSystemTracks());
}

void ThreadTrack::Serialize(protos::pbzero::TrackDescriptor* desc) const {
  auto bytes = Serialize().SerializeAsString();
  desc->AppendRawProtoBytes(bytes.data(), bytes.size());
}

protos::gen::TrackDescriptor NamedTrack::Serialize() const {
  auto desc = Track::Serialize();
  if (static_name_) {
    desc.set_static_name(static_name_.value);
  } else {
    desc.set_name(dynamic_name_.value);
  }
  if (sibling_merge_behavior_ != perfetto::protos::gen::TrackDescriptor::
                                     SIBLING_MERGE_BEHAVIOR_UNSPECIFIED) {
    desc.set_sibling_merge_behavior(sibling_merge_behavior_);
  }
  if (sibling_merge_key_) {
    desc.set_sibling_merge_key(sibling_merge_key_);
  } else if (sibling_merge_key_int_.has_value()) {
    desc.set_sibling_merge_key_int(*sibling_merge_key_int_);
  }
  return desc;
}

void NamedTrack::Serialize(protos::pbzero::TrackDescriptor* desc) const {
  auto bytes = Serialize().SerializeAsString();
  desc->AppendRawProtoBytes(bytes.data(), bytes.size());
}

protos::gen::TrackDescriptor CounterTrack::Serialize() const {
  auto desc = Track::Serialize();
  auto* counter = desc.mutable_counter();
  if (static_name_) {
    desc.set_static_name(static_name_.value);
  } else {
    desc.set_name(dynamic_name_.value);
  }

  if (category_)
    counter->add_categories(category_);
  if (unit_ != perfetto::protos::pbzero::CounterDescriptor::UNIT_UNSPECIFIED)
    counter->set_unit(static_cast<protos::gen::CounterDescriptor_Unit>(unit_));
  {
    // if |type| is set, we don't want to emit |unit_name|. Trace processor
    // infers the track name from the type in that case.
    if (type_ !=
        perfetto::protos::gen::CounterDescriptor::COUNTER_UNSPECIFIED) {
      counter->set_type(type_);
    } else if (unit_name_) {
      counter->set_unit_name(unit_name_);
    }
  }
  if (unit_multiplier_ != 1)
    counter->set_unit_multiplier(unit_multiplier_);
  if (is_incremental_)
    counter->set_is_incremental(is_incremental_);
  return desc;
}

void CounterTrack::Serialize(protos::pbzero::TrackDescriptor* desc) const {
  auto bytes = Serialize().SerializeAsString();
  desc->AppendRawProtoBytes(bytes.data(), bytes.size());
}

namespace internal {
namespace {

uint64_t GetProcessStartTime() {
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  std::string stat;
  if (!base::ReadFile("/proc/self/stat", &stat))
    return 0u;
  // The stat file is a single line split into space-separated fields as "pid
  // (comm) state ppid ...". However because the command name can contain any
  // characters (including parentheses and spaces), we need to skip past it
  // before parsing the rest of the fields. To do that, we look for the last
  // instance of ") " (parentheses followed by space) and parse forward from
  // that point.
  size_t comm_end = stat.rfind(") ");
  if (comm_end == std::string::npos)
    return 0u;
  stat = stat.substr(comm_end + strlen(") "));
  base::StringSplitter splitter(stat, ' ');
  for (size_t skip = 0; skip < 20; skip++) {
    if (!splitter.Next())
      return 0u;
  }
  return base::CStringToUInt64(splitter.cur_token()).value_or(0u);
#else
  return 0;
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
}

}  // namespace

// static
TrackRegistry* TrackRegistry::instance_;

TrackRegistry::TrackRegistry() = default;
TrackRegistry::~TrackRegistry() = default;

// static
void TrackRegistry::InitializeInstance(std::optional<uint64_t> process_uuid) {
  if (instance_)
    return;
  instance_ = new TrackRegistry();
  if (process_uuid) {
    Track::process_uuid = *process_uuid;
  } else {
    Track::process_uuid = ComputeProcessUuid();
  }
}

// static
uint64_t TrackRegistry::ComputeProcessUuid() {
  // Use the process start time + pid as the unique identifier for this process.
  // This ensures that if there are two independent copies of the Perfetto SDK
  // in the same process (e.g., one in the app and another in a system
  // framework), events emitted by each will be consistently interleaved on
  // common thread and process tracks.
  uint64_t random;
  if (uint64_t start_time = GetProcessStartTime()) {
    random = start_time;
  } else {
    // Fall back to a randomly generated identifier.
    static uint64_t random_once = static_cast<uint64_t>(base::Uuidv4().lsb());
    random = random_once;
  }
  return base::FnvHasher::Combine(random, Platform::GetCurrentProcessId());
}

void TrackRegistry::ResetForTesting() {
  instance_->tracks_.clear();
}

void TrackRegistry::UpdateTrack(Track track,
                                const std::string& serialized_desc) {
  std::lock_guard<std::mutex> lock(mutex_);
  tracks_[track.uuid] = {serialized_desc, track.parent_uuid};
}

void TrackRegistry::EraseTrack(Track track) {
  std::lock_guard<std::mutex> lock(mutex_);
  tracks_.erase(track.uuid);
}

// static
void TrackRegistry::WriteTrackDescriptor(
    const SerializedTrackDescriptor& desc,
    protozero::MessageHandle<protos::pbzero::TracePacket> packet) {
  packet->AppendString(
      perfetto::protos::pbzero::TracePacket::kTrackDescriptorFieldNumber, desc);
}

}  // namespace internal
}  // namespace perfetto
