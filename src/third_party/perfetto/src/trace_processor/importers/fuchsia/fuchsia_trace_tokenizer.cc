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

#include "src/trace_processor/importers/fuchsia/fuchsia_trace_tokenizer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/importers/fuchsia/fuchsia_record.h"
#include "src/trace_processor/importers/fuchsia/fuchsia_trace_parser.h"
#include "src/trace_processor/importers/fuchsia/fuchsia_trace_utils.h"
#include "src/trace_processor/importers/proto/proto_trace_reader.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

namespace {

using fuchsia_trace_utils::ArgValue;

// Record types
constexpr uint32_t kMetadata = 0;
constexpr uint32_t kInitialization = 1;
constexpr uint32_t kString = 2;
constexpr uint32_t kThread = 3;
constexpr uint32_t kEvent = 4;
constexpr uint32_t kBlob = 5;
constexpr uint32_t kKernelObject = 7;
constexpr uint32_t kSchedulerEvent = 8;

constexpr uint32_t kSchedulerEventLegacyContextSwitch = 0;
constexpr uint32_t kSchedulerEventContextSwitch = 1;
constexpr uint32_t kSchedulerEventThreadWakeup = 2;

// Metadata types
constexpr uint32_t kProviderInfo = 1;
constexpr uint32_t kProviderSection = 2;
constexpr uint32_t kProviderEvent = 3;

// Zircon object types
constexpr uint32_t kZxObjTypeProcess = 1;
constexpr uint32_t kZxObjTypeThread = 2;

}  // namespace

FuchsiaTraceTokenizer::FuchsiaTraceTokenizer(TraceProcessorContext* context)
    : context_(context),
      proto_trace_reader_(context),
      process_id_(context->storage->InternString("process")) {
  auto parser = std::make_unique<FuchsiaTraceParser>(context);
  parser_ = parser.get();
  stream_ = context->sorter->CreateStream(std::move(parser));
  RegisterProvider(0, "");
}

FuchsiaTraceTokenizer::~FuchsiaTraceTokenizer() = default;

base::Status FuchsiaTraceTokenizer::Parse(TraceBlobView blob) {
  size_t size = blob.size();

  // The relevant internal state is |leftover_bytes_|. Each call to Parse should
  // maintain the following properties, unless a fatal error occurs in which
  // case it should return false and no assumptions should be made about the
  // resulting internal state:
  //
  // 1) Every byte passed to |Parse| has either been passed to |ParseRecord| or
  // is present in |leftover_bytes_|, but not both.
  // 2) |leftover_bytes_| does not contain a complete record.
  //
  // Parse is responsible for creating the "full" |TraceBlobView|s, which own
  // the underlying data. Generally, there will be one such view. However, if
  // there is a record that started in an earlier call, then a new buffer is
  // created here to make the bytes in that record contiguous.
  //
  // Because some of the bytes in |data| might belong to the record starting in
  // |leftover_bytes_|, we track the offset at which the following record will
  // start.
  size_t byte_offset = 0;

  // Look for a record starting with the leftover bytes.
  if (leftover_bytes_.size() + size < 8) {
    // Even with the new bytes, we can't even read the header of the next
    // record, so just add the new bytes to |leftover_bytes_| and return.
    leftover_bytes_.insert(leftover_bytes_.end(), blob.data() + byte_offset,
                           blob.data() + size);
    return base::OkStatus();
  }
  if (!leftover_bytes_.empty()) {
    // There is a record starting from leftover bytes.
    if (leftover_bytes_.size() < 8) {
      // Header was previously incomplete, but we have enough now.
      // Copy bytes into |leftover_bytes_| so that the whole header is present,
      // and update |byte_offset| and |size| accordingly.
      size_t needed_bytes = 8 - leftover_bytes_.size();
      leftover_bytes_.insert(leftover_bytes_.end(), blob.data() + byte_offset,
                             blob.data() + needed_bytes);
      byte_offset += needed_bytes;
      size -= needed_bytes;
    }
    // Read the record length from the header.
    uint64_t header =
        *reinterpret_cast<const uint64_t*>(leftover_bytes_.data());
    uint32_t record_len_words =
        fuchsia_trace_utils::ReadField<uint32_t>(header, 4, 15);
    uint32_t record_len_bytes = record_len_words * sizeof(uint64_t);

    // From property (2) above, leftover_bytes_ must have had less than a full
    // record to start with. We padded leftover_bytes_ out to read the header,
    // so it may now be a full record (in the case that the record consists of
    // only the header word), but it still cannot have any extra bytes.
    PERFETTO_DCHECK(leftover_bytes_.size() <= record_len_bytes);
    size_t missing_bytes = record_len_bytes - leftover_bytes_.size();

    if (missing_bytes <= size) {
      // We have enough bytes to complete the partial record. Create a new
      // buffer for that record.
      TraceBlob buf = TraceBlob::Allocate(record_len_bytes);
      memcpy(buf.data(), leftover_bytes_.data(), leftover_bytes_.size());
      memcpy(buf.data() + leftover_bytes_.size(), blob.data() + byte_offset,
             missing_bytes);
      byte_offset += missing_bytes;
      size -= missing_bytes;
      leftover_bytes_.clear();
      ParseRecord(TraceBlobView(std::move(buf)));
    } else {
      // There are not enough bytes for the full record. Add all the bytes we
      // have to leftover_bytes_ and wait for more.
      leftover_bytes_.insert(leftover_bytes_.end(), blob.data() + byte_offset,
                             blob.data() + byte_offset + size);
      return base::OkStatus();
    }
  }

  TraceBlobView full_view = blob.slice_off(byte_offset, size);

  // |record_offset| is a number of bytes past |byte_offset| where the record
  // under consideration starts. As a result, it must always be in the range [0,
  // size-8]. Any larger offset means we don't have enough bytes for the header.
  size_t record_offset = 0;
  while (record_offset + 8 <= size) {
    uint64_t header =
        *reinterpret_cast<const uint64_t*>(full_view.data() + record_offset);
    uint32_t record_len_bytes =
        fuchsia_trace_utils::ReadField<uint32_t>(header, 4, 15) *
        sizeof(uint64_t);
    if (record_len_bytes == 0)
      return base::ErrStatus("Unexpected record of size 0");

    if (record_offset + record_len_bytes > size)
      break;

    TraceBlobView record = full_view.slice_off(record_offset, record_len_bytes);
    ParseRecord(std::move(record));

    record_offset += record_len_bytes;
  }

  leftover_bytes_.insert(leftover_bytes_.end(),
                         full_view.data() + record_offset,
                         full_view.data() + size);

  TraceBlob perfetto_blob =
      TraceBlob::CopyFrom(proto_trace_data_.data(), proto_trace_data_.size());
  proto_trace_data_.clear();

  return proto_trace_reader_.Parse(TraceBlobView(std::move(perfetto_blob)));
}

// Most record types are read and recorded in |TraceStorage| here directly.
// Event records are sorted by timestamp before processing, so instead of
// recording them in |TraceStorage| they are given to |TraceSorter|. In order to
// facilitate the parsing after sorting, a small view of the provider's string
// and thread tables is passed alongside the record. See |FuchsiaProviderView|.
void FuchsiaTraceTokenizer::ParseRecord(TraceBlobView tbv) {
  TraceStorage* storage = context_->storage.get();
  ProcessTracker* procs = context_->process_tracker.get();

  fuchsia_trace_utils::RecordCursor cursor(tbv.data(), tbv.length());
  uint64_t header;
  if (!cursor.ReadUint64(&header)) {
    storage->IncrementStats(stats::fuchsia_record_read_error);
    return;
  }

  auto record_type = fuchsia_trace_utils::ReadField<uint32_t>(header, 0, 3);

  // All non-metadata events require current_provider_ to be set.
  if (record_type != kMetadata && current_provider_ == nullptr) {
    storage->IncrementStats(stats::fuchsia_invalid_event);
    return;
  }

  // Adapters for FuchsiaTraceParser::ParseArgs.
  const auto intern_string = [this](base::StringView string) {
    return context_->storage->InternString(string);
  };
  const auto get_string = [this](uint16_t index) {
    StringId id = current_provider_->GetString(index);
    if (id == StringId::Null()) {
      context_->storage->IncrementStats(stats::fuchsia_invalid_string_ref);
    }
    return id;
  };

  const auto insert_args = [this](uint32_t n_args,
                                  fuchsia_trace_utils::RecordCursor& cursor,
                                  FuchsiaRecord& record) {
    for (uint32_t i = 0; i < n_args; i++) {
      const size_t arg_base = cursor.WordIndex();
      uint64_t arg_header;
      if (!cursor.ReadUint64(&arg_header)) {
        context_->storage->IncrementStats(stats::fuchsia_record_read_error);
        return false;
      }
      auto arg_type =
          fuchsia_trace_utils::ReadField<uint32_t>(arg_header, 0, 3);
      auto arg_size_words =
          fuchsia_trace_utils::ReadField<uint32_t>(arg_header, 4, 15);
      auto arg_name_ref =
          fuchsia_trace_utils::ReadField<uint32_t>(arg_header, 16, 31);

      if (fuchsia_trace_utils::IsInlineString(arg_name_ref)) {
        // Skip over inline string
        if (!cursor.ReadInlineString(arg_name_ref, nullptr)) {
          context_->storage->IncrementStats(stats::fuchsia_record_read_error);
          return false;
        }
      } else {
        StringId id = current_provider_->GetString(arg_name_ref);
        if (id == StringId::Null()) {
          context_->storage->IncrementStats(stats::fuchsia_invalid_string_ref);
          return false;
        }
        record.InsertString(arg_name_ref, id);
      }

      if (arg_type == ArgValue::ArgType::kString) {
        auto arg_value_ref =
            fuchsia_trace_utils::ReadField<uint32_t>(arg_header, 32, 47);
        if (fuchsia_trace_utils::IsInlineString(arg_value_ref)) {
          // Skip over inline string
          if (!cursor.ReadInlineString(arg_value_ref, nullptr)) {
            context_->storage->IncrementStats(stats::fuchsia_record_read_error);
            return false;
          }
        } else {
          StringId id = current_provider_->GetString(arg_value_ref);
          if (id == StringId::Null()) {
            context_->storage->IncrementStats(
                stats::fuchsia_invalid_string_ref);
            return false;
          }
          record.InsertString(arg_value_ref, id);
        }
      }
      cursor.SetWordIndex(arg_base + arg_size_words);
    }

    return true;
  };

  switch (record_type) {
    case kMetadata: {
      auto metadata_type =
          fuchsia_trace_utils::ReadField<uint32_t>(header, 16, 19);
      switch (metadata_type) {
        case kProviderInfo: {
          auto provider_id =
              fuchsia_trace_utils::ReadField<uint32_t>(header, 20, 51);
          auto name_len =
              fuchsia_trace_utils::ReadField<uint32_t>(header, 52, 59);
          base::StringView name_view;
          if (!cursor.ReadInlineString(name_len, &name_view)) {
            storage->IncrementStats(stats::fuchsia_record_read_error);
            return;
          }
          RegisterProvider(provider_id, name_view.ToStdString());
          break;
        }
        case kProviderSection: {
          auto provider_id =
              fuchsia_trace_utils::ReadField<uint32_t>(header, 20, 51);
          current_provider_ = providers_[provider_id].get();
          break;
        }
        case kProviderEvent: {
          // TODO(bhamrick): Handle buffer fill events
          PERFETTO_DLOG(
              "Ignoring provider event. Events may have been dropped");
          break;
        }
      }
      break;
    }
    case kInitialization: {
      if (!cursor.ReadUint64(&current_provider_->ticks_per_second)) {
        storage->IncrementStats(stats::fuchsia_record_read_error);
        return;
      }
      break;
    }
    case kString: {
      auto index = fuchsia_trace_utils::ReadField<uint32_t>(header, 16, 30);
      if (index != 0) {
        auto len = fuchsia_trace_utils::ReadField<uint32_t>(header, 32, 46);
        base::StringView s;
        if (!cursor.ReadInlineString(len, &s)) {
          storage->IncrementStats(stats::fuchsia_record_read_error);
          return;
        }
        StringId id = storage->InternString(s);

        current_provider_->string_table[index] = id;
      }
      break;
    }
    case kThread: {
      auto index = fuchsia_trace_utils::ReadField<uint32_t>(header, 16, 23);
      if (index != 0) {
        FuchsiaThreadInfo tinfo;
        if (!cursor.ReadInlineThread(&tinfo)) {
          storage->IncrementStats(stats::fuchsia_record_read_error);
          return;
        }

        current_provider_->thread_table[index] = tinfo;
      }
      break;
    }
    case kEvent: {
      auto thread_ref =
          fuchsia_trace_utils::ReadField<uint32_t>(header, 24, 31);
      auto cat_ref = fuchsia_trace_utils::ReadField<uint32_t>(header, 32, 47);
      auto name_ref = fuchsia_trace_utils::ReadField<uint32_t>(header, 48, 63);

      // Build the FuchsiaRecord for the event, i.e. extract the thread
      // information if not inline, and any non-inline strings (name, category
      // for now, arg names and string values in the future).
      FuchsiaRecord record(std::move(tbv));
      record.set_ticks_per_second(current_provider_->ticks_per_second);

      uint64_t ticks;
      if (!cursor.ReadUint64(&ticks)) {
        storage->IncrementStats(stats::fuchsia_record_read_error);
        return;
      }
      int64_t ts = fuchsia_trace_utils::TicksToNs(
          ticks, current_provider_->ticks_per_second);
      if (ts < 0) {
        storage->IncrementStats(stats::fuchsia_timestamp_overflow);
        return;
      }

      if (fuchsia_trace_utils::IsInlineThread(thread_ref)) {
        // Skip over inline thread
        if (!cursor.ReadInlineThread(nullptr)) {
          storage->IncrementStats(stats::fuchsia_record_read_error);
          return;
        }
      } else {
        record.InsertThread(thread_ref,
                            current_provider_->GetThread(thread_ref));
      }

      if (fuchsia_trace_utils::IsInlineString(cat_ref)) {
        // Skip over inline string
        if (!cursor.ReadInlineString(cat_ref, nullptr)) {
          storage->IncrementStats(stats::fuchsia_record_read_error);
          return;
        }
      } else {
        StringId id = current_provider_->GetString(cat_ref);
        if (id == StringId::Null()) {
          storage->IncrementStats(stats::fuchsia_invalid_string_ref);
          return;
        }
        record.InsertString(cat_ref, id);
      }

      if (fuchsia_trace_utils::IsInlineString(name_ref)) {
        // Skip over inline string
        if (!cursor.ReadInlineString(name_ref, nullptr)) {
          storage->IncrementStats(stats::fuchsia_record_read_error);
          return;
        }
      } else {
        StringId id = current_provider_->GetString(name_ref);
        if (id == StringId::Null()) {
          storage->IncrementStats(stats::fuchsia_invalid_string_ref);
          return;
        }
        record.InsertString(name_ref, id);
      }

      auto n_args = fuchsia_trace_utils::ReadField<uint32_t>(header, 20, 23);
      if (!insert_args(n_args, cursor, record)) {
        return;
      }
      stream_->Push(ts, std::move(record));
      break;
    }
    case kBlob: {
      constexpr uint32_t kPerfettoBlob = 3;
      auto blob_type = fuchsia_trace_utils::ReadField<uint32_t>(header, 48, 55);
      if (blob_type == kPerfettoBlob) {
        FuchsiaRecord record(std::move(tbv));
        auto blob_size =
            fuchsia_trace_utils::ReadField<uint32_t>(header, 32, 46);
        auto name_ref =
            fuchsia_trace_utils::ReadField<uint32_t>(header, 16, 31);

        // We don't need the name, but we still need to parse it in case it is
        // inline
        if (fuchsia_trace_utils::IsInlineString(name_ref)) {
          base::StringView name_view;
          if (!cursor.ReadInlineString(name_ref, &name_view)) {
            storage->IncrementStats(stats::fuchsia_record_read_error);
            return;
          }
        }

        // Append the Blob into the embedded perfetto bytes -- we'll parse them
        // all after the main pass is done.
        if (!cursor.ReadBlob(blob_size, proto_trace_data_)) {
          storage->IncrementStats(stats::fuchsia_record_read_error);
          return;
        }
      }
      break;
    }
    case kKernelObject: {
      auto obj_type = fuchsia_trace_utils::ReadField<uint32_t>(header, 16, 23);
      auto name_ref = fuchsia_trace_utils::ReadField<uint32_t>(header, 24, 39);

      uint64_t obj_id;
      if (!cursor.ReadUint64(&obj_id)) {
        storage->IncrementStats(stats::fuchsia_record_read_error);
        return;
      }

      StringId name = StringId::Null();
      if (fuchsia_trace_utils::IsInlineString(name_ref)) {
        base::StringView name_view;
        if (!cursor.ReadInlineString(name_ref, &name_view)) {
          storage->IncrementStats(stats::fuchsia_record_read_error);
          return;
        }
        name = storage->InternString(name_view);
      } else {
        name = current_provider_->GetString(name_ref);
        if (name == StringId::Null()) {
          storage->IncrementStats(stats::fuchsia_invalid_string_ref);
          return;
        }
      }

      switch (obj_type) {
        case kZxObjTypeProcess: {
          // Note: Fuchsia pid/tids are 64 bits but Perfetto's tables only
          // support 32 bits. This is usually not an issue except for
          // artificial koids which have the 2^63 bit set. This is used for
          // things such as virtual threads.
          UniquePid upid = context_->process_tracker->GetOrCreateProcess(
              static_cast<uint32_t>(obj_id));
          procs->SetProcessMetadata(upid,
                                    base::StringView(storage->GetString(name)),
                                    base::StringView());
          break;
        }
        case kZxObjTypeThread: {
          auto n_args =
              fuchsia_trace_utils::ReadField<uint32_t>(header, 40, 43);

          auto maybe_args = FuchsiaTraceParser::ParseArgs(
              cursor, n_args, intern_string, get_string);
          if (!maybe_args.has_value()) {
            storage->IncrementStats(stats::fuchsia_record_read_error);
            return;
          }

          uint64_t pid = 0;
          for (const auto arg : *maybe_args) {
            if (arg.name == process_id_) {
              if (arg.value.Type() != ArgValue::ArgType::kKoid) {
                storage->IncrementStats(stats::fuchsia_invalid_event_arg_type);
                return;
              }
              pid = arg.value.Koid();
            }
          }

          // TODO(lalitm): this is a gross hack we're adding to unblock a crash
          // (b/383877212). This should be refactored properly out into a
          // tracker (which is the pattern for handling this sort of thing
          // in the rest of TP) but that is a bunch of boilerplate.
          // TODO: DNS: this is not correct.
          auto& thread = parser_->GetThread(obj_id);
          thread.info.pid = pid;

          UniqueTid utid = procs->UpdateThread(static_cast<uint32_t>(obj_id),
                                               static_cast<uint32_t>(pid));
          auto& tt = *storage->mutable_thread_table();
          tt[utid].set_name(name);
          break;
        }
        default: {
          PERFETTO_DLOG("Skipping Kernel Object record with type %d", obj_type);
          break;
        }
      }
      break;
    }
    case kSchedulerEvent: {
      // Context switch records come in order, so they do not need to go through
      // TraceSorter.
      auto event_type =
          fuchsia_trace_utils::ReadField<uint32_t>(header, 60, 63);
      switch (event_type) {
        case kSchedulerEventLegacyContextSwitch: {
          auto outgoing_thread_ref =
              fuchsia_trace_utils::ReadField<uint32_t>(header, 28, 35);
          auto incoming_thread_ref =
              fuchsia_trace_utils::ReadField<uint32_t>(header, 36, 43);

          FuchsiaRecord record(std::move(tbv));
          record.set_ticks_per_second(current_provider_->ticks_per_second);

          int64_t ts;
          if (!cursor.ReadTimestamp(current_provider_->ticks_per_second, &ts)) {
            storage->IncrementStats(stats::fuchsia_record_read_error);
            return;
          }
          if (ts == -1) {
            storage->IncrementStats(stats::fuchsia_timestamp_overflow);
            return;
          }

          if (fuchsia_trace_utils::IsInlineThread(outgoing_thread_ref)) {
            // Skip over inline thread
            if (!cursor.ReadInlineThread(nullptr)) {
              storage->IncrementStats(stats::fuchsia_record_read_error);
              return;
            }
          } else {
            record.InsertThread(
                outgoing_thread_ref,
                current_provider_->GetThread(outgoing_thread_ref));
          }

          if (fuchsia_trace_utils::IsInlineThread(incoming_thread_ref)) {
            // Skip over inline thread
            if (!cursor.ReadInlineThread(nullptr)) {
              storage->IncrementStats(stats::fuchsia_record_read_error);
              return;
            }
          } else {
            record.InsertThread(
                incoming_thread_ref,
                current_provider_->GetThread(incoming_thread_ref));
          }
          stream_->Push(ts, std::move(record));
          break;
        }
        case kSchedulerEventContextSwitch: {
          FuchsiaRecord record(std::move(tbv));
          record.set_ticks_per_second(current_provider_->ticks_per_second);

          int64_t ts;
          if (!cursor.ReadTimestamp(current_provider_->ticks_per_second, &ts)) {
            storage->IncrementStats(stats::fuchsia_record_read_error);
            return;
          }
          if (ts < 0) {
            storage->IncrementStats(stats::fuchsia_timestamp_overflow);
            return;
          }

          // Skip outgoing tid.
          if (!cursor.ReadUint64(nullptr)) {
            storage->IncrementStats(stats::fuchsia_record_read_error);
            return;
          }

          // Skip incoming tid.
          if (!cursor.ReadUint64(nullptr)) {
            storage->IncrementStats(stats::fuchsia_record_read_error);
            return;
          }

          const auto n_args =
              fuchsia_trace_utils::ReadField<uint32_t>(header, 16, 19);
          if (!insert_args(n_args, cursor, record)) {
            return;
          }
          stream_->Push(ts, std::move(record));
          break;
        }
        case kSchedulerEventThreadWakeup: {
          FuchsiaRecord record(std::move(tbv));
          record.set_ticks_per_second(current_provider_->ticks_per_second);

          int64_t ts;
          if (!cursor.ReadTimestamp(current_provider_->ticks_per_second, &ts)) {
            storage->IncrementStats(stats::fuchsia_record_read_error);
            return;
          }
          if (ts < 0) {
            storage->IncrementStats(stats::fuchsia_timestamp_overflow);
            return;
          }

          // Skip waking tid.
          if (!cursor.ReadUint64(nullptr)) {
            storage->IncrementStats(stats::fuchsia_record_read_error);
            return;
          }

          const auto n_args =
              fuchsia_trace_utils::ReadField<uint32_t>(header, 16, 19);
          if (!insert_args(n_args, cursor, record)) {
            return;
          }
          stream_->Push(ts, std::move(record));
          break;
        }
        default:
          PERFETTO_DLOG("Skipping unknown scheduler event type %d", event_type);
          break;
      }

      break;
    }
    default: {
      PERFETTO_DLOG("Skipping record of unknown type %d", record_type);
      break;
    }
  }
}

void FuchsiaTraceTokenizer::RegisterProvider(uint32_t provider_id,
                                             std::string name) {
  std::unique_ptr<ProviderInfo> provider(new ProviderInfo());
  provider->name = std::move(name);
  current_provider_ = provider.get();
  providers_[provider_id] = std::move(provider);
}

base::Status FuchsiaTraceTokenizer::NotifyEndOfFile() {
  RETURN_IF_ERROR(proto_trace_reader_.NotifyEndOfFile());
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
