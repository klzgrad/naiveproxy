// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef BASE_TRACE_EVENT_TRACE_EVENT_IMPL_H_
#define BASE_TRACE_EVENT_TRACE_EVENT_IMPL_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/atomicops.h"
#include "base/base_export.h"
#include "base/callback.h"
#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_local.h"
#include "base/trace_event/trace_event_memory_overhead.h"
#include "build/build_config.h"

namespace base {
namespace trace_event {

typedef base::Callback<bool(const char* arg_name)> ArgumentNameFilterPredicate;

typedef base::Callback<bool(const char* category_group_name,
                            const char* event_name,
                            ArgumentNameFilterPredicate*)>
    ArgumentFilterPredicate;

// For any argument of type TRACE_VALUE_TYPE_CONVERTABLE the provided
// class must implement this interface.
class BASE_EXPORT ConvertableToTraceFormat {
 public:
  ConvertableToTraceFormat() {}
  virtual ~ConvertableToTraceFormat() {}

  // Append the class info to the provided |out| string. The appended
  // data must be a valid JSON object. Strings must be properly quoted, and
  // escaped. There is no processing applied to the content after it is
  // appended.
  virtual void AppendAsTraceFormat(std::string* out) const = 0;

  virtual void EstimateTraceMemoryOverhead(TraceEventMemoryOverhead* overhead);

  std::string ToString() const {
    std::string result;
    AppendAsTraceFormat(&result);
    return result;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ConvertableToTraceFormat);
};

const int kTraceMaxNumArgs = 2;

struct TraceEventHandle {
  uint32_t chunk_seq;
  // These numbers of bits must be kept consistent with
  // TraceBufferChunk::kMaxTrunkIndex and
  // TraceBufferChunk::kTraceBufferChunkSize (in trace_buffer.h).
  unsigned chunk_index : 26;
  unsigned event_index : 6;
};

class BASE_EXPORT TraceEvent {
 public:
  union TraceValue {
    bool as_bool;
    unsigned long long as_uint;
    long long as_int;
    double as_double;
    const void* as_pointer;
    const char* as_string;
  };

  TraceEvent();
  ~TraceEvent();

  void MoveFrom(std::unique_ptr<TraceEvent> other);

  void Initialize(int thread_id,
                  TimeTicks timestamp,
                  ThreadTicks thread_timestamp,
                  char phase,
                  const unsigned char* category_group_enabled,
                  const char* name,
                  const char* scope,
                  unsigned long long id,
                  unsigned long long bind_id,
                  int num_args,
                  const char* const* arg_names,
                  const unsigned char* arg_types,
                  const unsigned long long* arg_values,
                  std::unique_ptr<ConvertableToTraceFormat>* convertable_values,
                  unsigned int flags);

  void Reset();

  void UpdateDuration(const TimeTicks& now, const ThreadTicks& thread_now);

  void EstimateTraceMemoryOverhead(TraceEventMemoryOverhead* overhead);

  // Serialize event data to JSON
  void AppendAsJSON(
      std::string* out,
      const ArgumentFilterPredicate& argument_filter_predicate) const;
  void AppendPrettyPrinted(std::ostringstream* out) const;

  static void AppendValueAsJSON(unsigned char type,
                                TraceValue value,
                                std::string* out);

  TimeTicks timestamp() const { return timestamp_; }
  ThreadTicks thread_timestamp() const { return thread_timestamp_; }
  char phase() const { return phase_; }
  int thread_id() const { return thread_id_; }
  TimeDelta duration() const { return duration_; }
  TimeDelta thread_duration() const { return thread_duration_; }
  const char* scope() const { return scope_; }
  unsigned long long id() const { return id_; }
  unsigned int flags() const { return flags_; }

  // Exposed for unittesting:

  const std::string* parameter_copy_storage() const {
    return parameter_copy_storage_.get();
  }

  const unsigned char* category_group_enabled() const {
    return category_group_enabled_;
  }

  const char* name() const { return name_; }

#if defined(OS_ANDROID)
  void SendToATrace();
#endif

 private:
  // Note: these are ordered by size (largest first) for optimal packing.
  TimeTicks timestamp_;
  ThreadTicks thread_timestamp_;
  TimeDelta duration_;
  TimeDelta thread_duration_;
  // scope_ and id_ can be used to store phase-specific data.
  const char* scope_;
  unsigned long long id_;
  TraceValue arg_values_[kTraceMaxNumArgs];
  const char* arg_names_[kTraceMaxNumArgs];
  std::unique_ptr<ConvertableToTraceFormat>
      convertable_values_[kTraceMaxNumArgs];
  const unsigned char* category_group_enabled_;
  const char* name_;
  std::unique_ptr<std::string> parameter_copy_storage_;
  // Depending on TRACE_EVENT_FLAG_HAS_PROCESS_ID the event will have either:
  //  tid: thread_id_, pid: current_process_id (default case).
  //  tid: -1, pid: process_id_ (when flags_ & TRACE_EVENT_FLAG_HAS_PROCESS_ID).
  union {
    int thread_id_;
    int process_id_;
  };
  unsigned int flags_;
  unsigned long long bind_id_;
  unsigned char arg_types_[kTraceMaxNumArgs];
  char phase_;

  DISALLOW_COPY_AND_ASSIGN(TraceEvent);
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_TRACE_EVENT_IMPL_H_
