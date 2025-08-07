/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/instruments/instruments_xml_tokenizer.h"

#include <cctype>
#include <map>

#include <expat.h>
#include <stdint.h>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/fnv1a.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/instruments/row.h"
#include "src/trace_processor/importers/instruments/row_data_tracker.h"
#include "src/trace_processor/sorter/trace_sorter.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_TP_INSTRUMENTS)
#error \
    "This file should not be built when enable_perfetto_trace_processor_mac_instruments=false"
#endif

namespace perfetto::trace_processor::instruments_importer {

namespace {

std::string MakeTrimmed(const char* chars, int len) {
  while (len > 0 && std::isspace(*chars)) {
    chars++;
    len--;
  }
  while (len > 0 && std::isspace(chars[len - 1])) {
    len--;
  }
  return std::string(chars, static_cast<size_t>(len));
}

}  // namespace

// The Instruments XML tokenizer reads instruments traces exported with:
//
//   xctrace export --input /path/to/profile.trace --xpath
//     '//trace-toc/run/data/table[@schema="os-signpost and
//        @category="PointsOfInterest"] |
//      //trace-toc/run/data/table[@schema="cpu-profile"]'
//
// This exports two tables:
//   1. Points of interest signposts
//   2. CPU profile
// You can also use time-profile instead of cpu-profile if needed.
//
// The first is used for clock synchronization -- perfetto emits signpost events
// during tracing which allow synchronization of the xctrace clock (relative to
// start of profiling) with the perfetto boottime clock. The second contains
// the samples themselves.
//
// The expected format of the rows in the clock sync table is:
//
//     <row>
//       <event-time>1234</event-time>
//       <subsystem>dev.perfetto.clock_sync</subsystem>
//       <os-log-metadata>
//         <uint64>5678</uint64>
//       </os-log-metadata>
//     </row>
//
// There may be other rows with other data (from other subsystems), and
// additional data in the row (such as thread data and other metadata) -- this
// can be safely ignored.
//
// The expected format of the rows in the time sample table is:
//
//     <row>
//       <sample-time>1234</sample-time>
//       <thread fmt="Thread name">
//         <tid>1</tid>
//         <process fmt="Process name">
//           <pid>1<pid>
//         </process>
//       </thread>
//       <core>0</core>
//       <backtrace>
//         <frame addr="0x120001234">
//           <binary
//             name="MyBinary" UUID="01234567-89ABC-CDEF-0123-456789ABCDEF"
//             load-addr="0x120000000" path="/path/to/MyBinary.app/MyBinary" />
//         </frame>
//         ... more frames ...
//     </row>
//
// Here we do not expect other rows with other data -- every row should have a
// backtrace, and we use the presence of a backtrace to distinguish time samples
// and clock sync eventst. However, there can be additional data in the row
// (such as other metadata) -- this can be safely ignored.
//
// In addition, the XML format annotates elements with ids, to later reuse the
// same data by id without needing to repeat its contents. For example, you
// might have thread data for a sample:
//
//     <thread id="11" fmt="My Thread"><tid id="12">10</tid>...</thread>
//
// and subsequent samples on that thread will simply have
//
//     <thread ref="11" />
//
// This means that most elements have to have their pertinent data cached by id,
// including any data store in child elements (which themselves also have to
// be cached by id, like the <tid> in the example above).
//
// This importer reads the XML data using a streaming XML parser, which means
// it has to maintain some parsing state (such as the current stack of tags, or
// the current element for which we are reading data).
class InstrumentsXmlTokenizer::Impl {
 public:
  explicit Impl(TraceProcessorContext* context)
      : context_(context), data_(RowDataTracker::GetOrCreate(context_)) {
    parser_ = XML_ParserCreate(nullptr);
    XML_SetElementHandler(parser_, ElementStart, ElementEnd);
    XML_SetCharacterDataHandler(parser_, CharacterData);
    XML_SetUserData(parser_, this);

    const char* subsystem = "dev.perfetto.instruments_clock";
    clock_ = static_cast<ClockTracker::ClockId>(
        PerfettoFnv1a(subsystem, strlen(subsystem)) | 0x80000000);

    // Use the above clock if we can, in case there is no other trace and
    // no clock sync events.
    context_->clock_tracker->SetTraceTimeClock(clock_);
  }
  ~Impl() { XML_ParserFree(parser_); }

  base::Status Parse(TraceBlobView view) {
    const char* data = reinterpret_cast<const char*>(view.data());
    size_t length = view.length();
    while (length > 0) {
      // Handle the data in chunks of at most 32MB. Don't use std::min, to
      // be robust against length > 2GB.
      static constexpr int kMaxChunkSize = 32 * 1024 * 1024;
      int chunk_size = kMaxChunkSize;
      if (length < kMaxChunkSize) {
        chunk_size = static_cast<int>(length);
      }
      void* buffer;
      // Allocate an XML parser buffer -- libexpat insists on reading
      // from a buffer it owns, rather than a user provided one.
      while (!(buffer = XML_GetBuffer(parser_, chunk_size))) {
        // Be robust against XML_GetBuffer failing to allocate the chunk size,
        // and retry with smaller chunk sizes.
        chunk_size /= 2;
        if (chunk_size < 1024) {
          return base::ErrStatus(
              "XML parse error at line %lu: failed to allocate buffer\n",
              XML_GetCurrentLineNumber(parser_));
        }
      }
      // Copy the data into libexpat's buffer, and parse it.
      memcpy(buffer, data, static_cast<size_t>(chunk_size));
      length -= static_cast<size_t>(chunk_size);
      data += chunk_size;
      if (!XML_ParseBuffer(parser_, chunk_size, false)) {
        return base::ErrStatus("XML parse error at line %lu: %s\n",
                               XML_GetCurrentLineNumber(parser_),
                               XML_ErrorString(XML_GetErrorCode(parser_)));
      }
    }
    return base::OkStatus();
  }

  base::Status End() {
    if (!XML_Parse(parser_, nullptr, 0, true)) {
      return base::ErrStatus("XML parse error at end, line %lu: %s\n",
                             XML_GetCurrentLineNumber(parser_),
                             XML_ErrorString(XML_GetErrorCode(parser_)));
    }
    return base::OkStatus();
  }

 private:
  static void ElementStart(void* data, const char* el, const char** attr) {
    reinterpret_cast<Impl*>(data)->ElementStart(el, attr);
  }
  static void ElementEnd(void* data, const char* el) {
    reinterpret_cast<Impl*>(data)->ElementEnd(el);
  }
  static void CharacterData(void* data, const char* chars, int len) {
    reinterpret_cast<Impl*>(data)->CharacterData(chars, len);
  }

  void ElementStart(const char* el, const char** attrs) {
    tag_stack_.emplace_back(el);
    std::string_view tag_name = tag_stack_.back();

    if (tag_name == "row") {
      current_row_ = Row{};
    } else if (tag_name == "thread") {
      MaybeCachedRef<ThreadId> thread_lookup =
          GetOrInsertByRef(attrs, thread_ref_to_thread_);
      if (thread_lookup.is_new) {
        auto new_thread = data_.NewThread();
        thread_lookup.ref = new_thread.id;

        for (int i = 2; attrs[i]; i += 2) {
          std::string key(attrs[i]);
          if (key == "fmt") {
            new_thread.ptr->fmt = InternString(attrs[i + 1]);
          }
        }

        current_new_thread_ = new_thread.id;
      }
      current_row_.thread = thread_lookup.ref;
    } else if (tag_name == "process") {
      MaybeCachedRef<ProcessId> process_lookup =
          GetOrInsertByRef(attrs, process_ref_to_process_);
      if (process_lookup.is_new) {
        // Can only be processing a new process when processing a new thread.
        PERFETTO_DCHECK(current_new_thread_ != kNullId);
        auto new_process = data_.NewProcess();
        process_lookup.ref = new_process.id;

        for (int i = 2; attrs[i]; i += 2) {
          std::string key(attrs[i]);
          if (key == "fmt") {
            new_process.ptr->fmt = InternString(attrs[i + 1]);
          }
        }

        current_new_process_ = new_process.id;
      }
      if (current_new_thread_) {
        data_.GetThread(current_new_thread_)->process = process_lookup.ref;
      }
    } else if (tag_name == "core") {
      MaybeCachedRef<uint32_t> core_id_lookup =
          GetOrInsertByRef(attrs, core_ref_to_core_);
      if (core_id_lookup.is_new) {
        current_new_core_id_ = &core_id_lookup.ref;
      } else {
        current_row_.core_id = core_id_lookup.ref;
      }
    } else if (tag_name == "sample-time" || tag_name == "event-time") {
      // Share time lookup logic between sample times and event times, including
      // updating the current row's sample time for both.
      MaybeCachedRef<int64_t> time_lookup =
          GetOrInsertByRef(attrs, sample_time_ref_to_time_);
      if (time_lookup.is_new) {
        current_time_ref_ = &time_lookup.ref;
      } else {
        current_row_.timestamp_ = time_lookup.ref;
      }
    } else if (tag_name == "subsystem") {
      MaybeCachedRef<std::string> subsystem_lookup =
          GetOrInsertByRef(attrs, subsystem_ref_to_subsystem_);
      current_subsystem_ref_ = &subsystem_lookup.ref;
    } else if (tag_name == "uint64") {
      // The only uint64 we care about is the one for the clock sync, which is
      // expected to contain exactly one uint64 value -- we'll
      // map all uint64 to a single value and check against the subsystem
      // when the row is closed.
      MaybeCachedRef<uint64_t> uint64_lookup =
          GetOrInsertByRef(attrs, os_log_metadata_or_uint64_ref_to_uint64_);
      if (uint64_lookup.is_new) {
        current_uint64_ref_ = &uint64_lookup.ref;
      } else {
        if (current_os_log_metadata_uint64_ref_) {
          // Update the os-log-metadata's uint64 value with this uint64 value.
          *current_os_log_metadata_uint64_ref_ = uint64_lookup.ref;
        }
      }
    } else if (tag_name == "os-log-metadata") {
      // The only os-log-metadata we care about is the one with the single
      // uint64 clock sync value, so also map this to uint64 values with its own
      // id.
      MaybeCachedRef<uint64_t> uint64_lookup =
          GetOrInsertByRef(attrs, os_log_metadata_or_uint64_ref_to_uint64_);
      current_os_log_metadata_uint64_ref_ = &uint64_lookup.ref;
    } else if (tag_name == "backtrace") {
      MaybeCachedRef<BacktraceId> backtrace_lookup =
          GetOrInsertByRef(attrs, backtrace_ref_to_backtrace_);
      if (backtrace_lookup.is_new) {
        backtrace_lookup.ref = data_.NewBacktrace().id;
      }
      current_row_.backtrace = backtrace_lookup.ref;
    } else if (tag_name == "frame") {
      MaybeCachedRef<BacktraceFrameId> frame_lookup =
          GetOrInsertByRef(attrs, frame_ref_to_frame_);
      if (frame_lookup.is_new) {
        IdPtr<Frame> new_frame = data_.NewFrame();
        frame_lookup.ref = new_frame.id;
        for (int i = 2; attrs[i]; i += 2) {
          std::string key(attrs[i]);
          if (key == "addr") {
            new_frame.ptr->addr = strtoll(attrs[i + 1], nullptr, 16);
          } else if (key == "name") {
            new_frame.ptr->name = std::string(attrs[i + 1]);
          }
        }
        current_new_frame_ = new_frame.id;
      }
      data_.GetBacktrace(current_row_.backtrace)
          ->frames.push_back(frame_lookup.ref);
    } else if (tag_name == "binary") {
      // Can only be processing a binary when processing a new frame.
      PERFETTO_DCHECK(current_new_frame_ != kNullId);

      MaybeCachedRef<BinaryId> binary_lookup =
          GetOrInsertByRef(attrs, binary_ref_to_binary_);
      if (binary_lookup.is_new) {
        auto new_binary = data_.NewBinary();
        binary_lookup.ref = new_binary.id;
        for (int i = 2; attrs[i]; i += 2) {
          std::string key(attrs[i]);
          if (key == "path") {
            new_binary.ptr->path = std::string(attrs[i + 1]);
          } else if (key == "UUID") {
            new_binary.ptr->uuid =
                BuildId::FromHex(base::StringView(attrs[i + 1]));
          } else if (key == "load-addr") {
            new_binary.ptr->load_addr = strtoll(attrs[i + 1], nullptr, 16);
          }
        }
        new_binary.ptr->max_addr = new_binary.ptr->load_addr;
      }
      PERFETTO_DCHECK(data_.GetFrame(current_new_frame_)->binary == kNullId);
      data_.GetFrame(current_new_frame_)->binary = binary_lookup.ref;
    }
  }

  void ElementEnd(const char* el) {
    PERFETTO_DCHECK(el == tag_stack_.back());
    std::string tag_name = std::move(tag_stack_.back());
    tag_stack_.pop_back();

    if (tag_name == "row") {
      if (current_row_.backtrace) {
        // Rows with backtraces are assumed to be time samples.
        base::StatusOr<int64_t> trace_ts =
            ToTraceTimestamp(current_row_.timestamp_);
        if (!trace_ts.ok()) {
          PERFETTO_DLOG("Skipping timestamp %" PRId64 ", no clock snapshot yet",
                        current_row_.timestamp_);
        } else {
          context_->sorter->PushInstrumentsRow(*trace_ts,
                                               std::move(current_row_));
        }
      } else if (current_subsystem_ref_ != nullptr) {
        // Rows without backtraces are assumed to be signpost events -- filter
        // these for `dev.perfetto.clock_sync` events.
        if (*current_subsystem_ref_ == "dev.perfetto.clock_sync") {
          PERFETTO_DCHECK(current_os_log_metadata_uint64_ref_ != nullptr);
          uint64_t clock_sync_timestamp = *current_os_log_metadata_uint64_ref_;
          if (latest_clock_sync_timestamp_ > clock_sync_timestamp) {
            PERFETTO_DLOG("Skipping timestamp %" PRId64
                          ", non-monotonic sync detected",
                          current_row_.timestamp_);
          } else {
            latest_clock_sync_timestamp_ = clock_sync_timestamp;
            auto status = context_->clock_tracker->AddSnapshot(
                {{clock_, current_row_.timestamp_},
                 {protos::pbzero::ClockSnapshot::Clock::BOOTTIME,
                  static_cast<int64_t>(latest_clock_sync_timestamp_)}});
            if (!status.ok()) {
              PERFETTO_FATAL("Error adding clock snapshot: %s",
                             status.status().c_message());
            }
          }
        }
        current_subsystem_ref_ = nullptr;
        current_os_log_metadata_uint64_ref_ = nullptr;
        current_uint64_ref_ = nullptr;
      }
    } else if (current_new_frame_ != kNullId && tag_name == "frame") {
      Frame* frame = data_.GetFrame(current_new_frame_);
      if (frame->binary) {
        Binary* binary = data_.GetBinary(frame->binary);
        // We don't know what the binary's mapping end is, but we know that the
        // current frame is inside of it, so use that.
        PERFETTO_DCHECK(frame->addr > binary->load_addr);
        if (frame->addr > binary->max_addr) {
          binary->max_addr = frame->addr;
        }
      }
      current_new_frame_ = kNullId;
    } else if (current_new_thread_ != kNullId && tag_name == "thread") {
      current_new_thread_ = kNullId;
    } else if (current_new_process_ != kNullId && tag_name == "process") {
      current_new_process_ = kNullId;
    } else if (current_new_core_id_ != nullptr && tag_name == "core") {
      current_new_core_id_ = nullptr;
    }
  }

  void CharacterData(const char* chars, int len) {
    std::string_view tag_name = tag_stack_.back();
    if (current_time_ref_ != nullptr &&
        (tag_name == "sample-time" || tag_name == "event-time")) {
      std::string s = MakeTrimmed(chars, len);
      current_row_.timestamp_ = *current_time_ref_ = stoll(s);
      current_time_ref_ = nullptr;
    } else if (current_new_thread_ != kNullId && tag_name == "tid") {
      std::string s = MakeTrimmed(chars, len);
      data_.GetThread(current_new_thread_)->tid = stoi(s);
    } else if (current_new_process_ != kNullId && tag_name == "pid") {
      std::string s = MakeTrimmed(chars, len);
      data_.GetProcess(current_new_process_)->pid = stoi(s);
    } else if (current_new_core_id_ != nullptr && tag_name == "core") {
      std::string s = MakeTrimmed(chars, len);
      *current_new_core_id_ = static_cast<uint32_t>(stoul(s));
    } else if (current_subsystem_ref_ != nullptr && tag_name == "subsystem") {
      std::string s = MakeTrimmed(chars, len);
      *current_subsystem_ref_ = s;
    } else if (current_uint64_ref_ != nullptr &&
               current_os_log_metadata_uint64_ref_ != nullptr &&
               tag_name == "uint64") {
      std::string s = MakeTrimmed(chars, len);
      *current_os_log_metadata_uint64_ref_ = *current_uint64_ref_ = stoull(s);
    }
  }

  base::StatusOr<int64_t> ToTraceTimestamp(int64_t time) {
    base::StatusOr<int64_t> trace_ts =
        context_->clock_tracker->ToTraceTime(clock_, time);

    if (PERFETTO_LIKELY(trace_ts.ok())) {
      latest_timestamp_ = std::max(latest_timestamp_, *trace_ts);
    }

    return trace_ts;
  }

  StringId InternString(base::StringView string_view) {
    return context_->storage->InternString(string_view);
  }
  StringId InternString(const char* string) {
    return InternString(base::StringView(string));
  }
  StringId InternString(const char* data, size_t len) {
    return InternString(base::StringView(data, len));
  }

  template <typename Value>
  struct MaybeCachedRef {
    Value& ref;
    bool is_new;
  };
  // Implement the element caching mechanism. Either insert an element by its
  // id attribute into the given map, or look up the element in the cache by its
  // ref attribute. The returned value is a reference into the map, to allow
  // in-place modification.
  template <typename Value>
  MaybeCachedRef<Value> GetOrInsertByRef(const char** attrs,
                                         std::map<unsigned long, Value>& map) {
    PERFETTO_DCHECK(attrs[0] != nullptr);
    PERFETTO_DCHECK(attrs[1] != nullptr);
    const char* key = attrs[0];
    // The id or ref attribute has to be the first attribute on the element.
    PERFETTO_DCHECK(strcmp(key, "ref") == 0 || strcmp(key, "id") == 0);
    unsigned long id = strtoul(attrs[1], nullptr, 10);
    // If the first attribute key is `id`, then this is a new entry in the
    // cache -- otherwise, for lookup by ref, it should already exist.
    bool is_new = strcmp(key, "id") == 0;
    PERFETTO_DCHECK(is_new == (map.find(id) == map.end()));
    return {map[id], is_new};
  }

  TraceProcessorContext* context_;
  RowDataTracker& data_;

  XML_Parser parser_;
  std::vector<std::string> tag_stack_;
  int64_t latest_timestamp_;

  // These maps store the cached element data. These currently have to be
  // std::map, because they require pointer stability under insertion,
  // as the various `current_foo_` pointers below point directly into the map
  // data.
  //
  // TODO(leszeks): Relax this pointer stability requirement, and use
  // base::FlatHashMap.
  // TODO(leszeks): Consider merging these into a single map from ID to
  // a variant (or similar).
  std::map<unsigned long, ThreadId> thread_ref_to_thread_;
  std::map<unsigned long, ProcessId> process_ref_to_process_;
  std::map<unsigned long, uint32_t> core_ref_to_core_;
  std::map<unsigned long, int64_t> sample_time_ref_to_time_;
  std::map<unsigned long, BinaryId> binary_ref_to_binary_;
  std::map<unsigned long, BacktraceFrameId> frame_ref_to_frame_;
  std::map<unsigned long, BacktraceId> backtrace_ref_to_backtrace_;
  std::map<unsigned long, std::string> subsystem_ref_to_subsystem_;
  std::map<unsigned long, uint64_t> os_log_metadata_or_uint64_ref_to_uint64_;

  Row current_row_;
  int64_t* current_time_ref_ = nullptr;
  ThreadId current_new_thread_ = kNullId;
  ProcessId current_new_process_ = kNullId;
  uint32_t* current_new_core_id_ = nullptr;
  BacktraceFrameId current_new_frame_ = kNullId;

  ClockTracker::ClockId clock_;
  std::string* current_subsystem_ref_ = nullptr;
  uint64_t* current_os_log_metadata_uint64_ref_ = nullptr;
  uint64_t* current_uint64_ref_ = nullptr;
  uint64_t latest_clock_sync_timestamp_ = 0;
};

InstrumentsXmlTokenizer::InstrumentsXmlTokenizer(TraceProcessorContext* context)
    : impl_(new Impl(context)) {}
InstrumentsXmlTokenizer::~InstrumentsXmlTokenizer() {
  delete impl_;
}

base::Status InstrumentsXmlTokenizer::Parse(TraceBlobView view) {
  return impl_->Parse(std::move(view));
}

[[nodiscard]] base::Status InstrumentsXmlTokenizer::NotifyEndOfFile() {
  return impl_->End();
}

}  // namespace perfetto::trace_processor::instruments_importer
