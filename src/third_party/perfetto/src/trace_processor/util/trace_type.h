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

#ifndef SRC_TRACE_PROCESSOR_UTIL_TRACE_TYPE_H_
#define SRC_TRACE_PROCESSOR_UTIL_TRACE_TYPE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"

namespace perfetto::trace_processor {

class ChunkedTraceReader;
class TraceProcessorContext;

// The minimum sort ordering a trace type requires from the TraceSorter.
enum class TraceSortPolicy {
  kFullSort,
  kConfigDriven,
  kNone,
};

// The clock domain a trace type's native timestamps are expressed in.
enum class TraceClockPolicy {
  kNone,
  kMonotonic,
  kBoottime,
  kRealtime,
  kTraceFile,
};

// The passive metadata describing a trace type: its name, clock/sort policy and
// archive ordering. Every trace type - builtin or plugin-contributed - has one.
// Detection and reader creation are behaviour, not metadata, and live on
// TraceImporter (for plugins) or in GuessTraceType / TraceReaderRegistry (for
// builtins).
struct TraceTypeDescriptor {
  std::string name;
  bool is_container = false;
  TraceSortPolicy sort_policy = TraceSortPolicy::kFullSort;
  TraceClockPolicy clock_policy = TraceClockPolicy::kNone;
  // Whether the format's source clock (clock_policy) is recorded as the file's
  // default clock and claimed as the global trace-time clock. Only proto, which
  // manages its own clock via ClockSnapshot, sets these false.
  bool sets_default_clock = true;
  bool claims_global_clock = true;
  int archive_priority = 2;
  // Lower runs first among plugin importers; ties keep registration order.
  int detection_priority = 0;
  // Behavioural policy; defaults suit an ordinary single-trace format.
  //   proto, systrace: treat pid 0 as the idle process.
  bool pid_zero_is_idle = false;
  //   false: this format cannot appear nested inside a container (ninja).
  bool supports_nesting = true;
  //   false: does not fork a per-trace context (containers + the manifest,
  //   which produce no timeline of their own).
  bool forks_context = true;
  //   The perfetto_manifest sidecar: a config file rather than a trace. It must
  //   be the first file in the input, and gates manifest-specific handling.
  bool is_manifest = false;
};

// Compile-time identity tag for a trace importer class (mirrors PluginTag).
template <typename T>
struct TraceImporterTag {
  static constexpr char kTag = 0;
  static constexpr const void* Id() { return &kTag; }
};

// Opaque, comparable identity for a trace type. Like plugin identity it is a
// per-importer-class compile-time tag: compare handles and test validity, but
// never dereference. Resolve to metadata/readers via TraceImporterRegistry.
class TraceImporterId {
 public:
  constexpr TraceImporterId() = default;

  constexpr bool operator==(TraceImporterId o) const { return tag_ == o.tag_; }
  constexpr bool operator!=(TraceImporterId o) const { return tag_ != o.tag_; }
  constexpr explicit operator bool() const { return tag_ != nullptr; }

  struct Hasher {
    size_t operator()(TraceImporterId id) const {
      return std::hash<const void*>()(id.tag_);
    }
  };

 private:
  friend class TraceImporterRegistry;
  template <typename>
  friend class TraceImporter;

  constexpr explicit TraceImporterId(const void* tag) : tag_(tag) {}

  const void* tag_ = nullptr;
};

// Pure-virtual interface for trace importers (mirrors PluginBase). Concrete
// importers derive from TraceImporter<Self> below, which supplies identity and
// descriptor storage; a metadata-only type (one detected structurally
// elsewhere) implements Sniff() as false and CreateReader() as an error.
//
// Detection and reader creation are deliberately separate calls: archives sniff
// every member up front to order them, then create readers later in that order,
// with the (possibly forked) parsing context.
class TraceImporterBase {
 public:
  virtual ~TraceImporterBase();

  // The compile-time identity of this importer's class.
  virtual TraceImporterId id() const = 0;

  // The static metadata for this trace type.
  virtual const TraceTypeDescriptor& descriptor() const = 0;

  // Returns true if `data` (the first bytes of a file) is this format.
  virtual bool Sniff(const uint8_t* data, size_t size) const = 0;

  // Creates the reader for this format. `file_id` is the trace_file_table id of
  // the file being read.
  virtual base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateReader(
      TraceProcessorContext* context,
      uint32_t file_id) const = 0;
};

// CRTP subclass supplying compile-time identity and descriptor storage (mirrors
// Plugin<Self>). Concrete importers subclass this and implement Sniff() and
// CreateReader().
template <typename Self>
class TraceImporter : public TraceImporterBase {
 public:
  static constexpr TraceImporterId kId =
      TraceImporterId(TraceImporterTag<Self>::Id());
  TraceImporterId id() const final { return kId; }
  const TraceTypeDescriptor& descriptor() const final { return descriptor_; }

 protected:
  explicit TraceImporter(TraceTypeDescriptor descriptor)
      : descriptor_(std::move(descriptor)) {}

 private:
  TraceTypeDescriptor descriptor_;
};

// The registry of trace importers: the single home for per-type identity,
// metadata, detection and reader creation. Importers are keyed by their opaque
// compile-time identity; builtins and plugins are registered the same way.
class TraceImporterRegistry {
 public:
  // Adds an importer, keyed by its identity, and returns that id.
  TraceImporterId Register(std::unique_ptr<TraceImporterBase> importer);

  // Returns the descriptor for `id`: the registered importer's descriptor if
  // present, else a shared "unknown" descriptor so callers never see nullptr.
  const TraceTypeDescriptor* Find(TraceImporterId id) const;

  // Returns the importer for `id`, or nullptr if none is registered.
  const TraceImporterBase* FindImporter(TraceImporterId id) const;

  // Runs every importer's Sniff() in detection_priority order (lowest first,
  // and priorities are globally unique) and returns the matching id, or an
  // invalid id if none match.
  TraceImporterId Guess(const uint8_t* data, size_t size) const;

  // Per-type metadata helpers.
  const char* ToString(TraceImporterId id) const;
  bool IsContainer(TraceImporterId id) const;

 private:
  base::FlatHashMap<TraceImporterId,
                    std::unique_ptr<TraceImporterBase>,
                    TraceImporterId::Hasher>
      importers_;
};

// The number of leading bytes an importer's Sniff() may inspect. Detection
// must decide from at most this prefix.
constexpr size_t kGuessTraceMaxLookahead = 128;
// The formats read_trace distinguishes, detected from magic bytes only, so
// tools that merely decompress or pass through a trace need not build the
// importer registry or link any tokenizer. kGzip/kZstd map to the matching
// util::CompressionType.
enum class CompressedTraceType { kGzip, kZstd, kProto, kOther };
CompressedTraceType SniffCompressedTraceType(const uint8_t* data, size_t size);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_UTIL_TRACE_TYPE_H_
