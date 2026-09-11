/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_EXTENSION_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_EXTENSION_PARSER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/importers/proto/typed_proto_field.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

struct TrackEventExtensionParserContext;

// A single out-of-tree extension field (`extensions 1000 to 9999`) of a
// TrackEvent, handed to plugins. The plugin decodes it with the generated field
// metadata of the extension it owns, e.g.
//   field.Cast<FrameworksBaseTrackEvent::kProcessStart>()  // -> ConstBytes
using TrackEventExtensionField = TypedProtoField;

// Base class for plugins that handle TrackEvent extension fields. This is the
// TrackEvent-extension analogue of ProtoImporterModule: a plugin registers (via
// RegisterTrackEventExtension) the extension field ids it owns, and the
// matching On*() callback is invoked after the core counter/slice/state row has
// been inserted, so the plugin receives its Id and can populate its own side
// tables.
//
// Each extension field id is owned by exactly one plugin. Only modern
// counter/slice/state events are dispatched, never legacy ones.
class TrackEventExtensionParser {
 public:
  // kHandled means the parser skips the default flattening of the event into
  // the args table; kIgnored leaves it untouched.
  enum class Result { kHandled, kIgnored };

  explicit TrackEventExtensionParser(TrackEventExtensionParserContext* context);
  virtual ~TrackEventExtensionParser();

  // Default to kIgnored so a plugin only overrides the kinds it handles.
  virtual Result OnTrackEventCounterExtension(
      const TrackEventExtensionField& field,
      CounterId id);
  virtual Result OnTrackEventSliceExtension(
      const TrackEventExtensionField& field,
      SliceId id);
  virtual Result OnTrackEventStateExtension(
      const TrackEventExtensionField& field,
      StateId id);

 protected:
  // Registers this plugin as the owner of |field_id| (CHECKs if already owned).
  void RegisterTrackEventExtension(uint32_t field_id);

  TrackEventExtensionParserContext* context_;
};

// Per-trace registry of TrackEvent extension parsers, mirroring
// ProtoImporterModuleContext: |parsers| owns them and |parsers_by_field| maps
// each registered extension field id to its owner.
struct TrackEventExtensionParserContext {
  std::vector<std::unique_ptr<TrackEventExtensionParser>> parsers;
  base::FlatHashMap<uint32_t, TrackEventExtensionParser*> parsers_by_field;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_EXTENSION_PARSER_H_
