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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_VIDEO_FRAME_IMPORTER_VIDEO_FRAME_MODULE_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_VIDEO_FRAME_IMPORTER_VIDEO_FRAME_MODULE_H_

#include <cstdint>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/plugins/video_frame_importer/tables_py.h"
#include "src/trace_processor/storage/trace_storage.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Parses video_frame into the plugin-owned AndroidVideoFramesTable (with the
// encoded payload held zero-copy in `au_data`, exposed via
// __INTRINSIC_VIDEO_FRAME_AU_DATA), and video_frame_error into per-reason
// kIndexed stats keyed by display_id. The table and au_data vector are owned
// by VideoFrameImporter and passed in.
class VideoFrameModule : public ProtoImporterModule {
 public:
  VideoFrameModule(ProtoImporterModuleContext* module_context,
                   TraceProcessorContext* context,
                   tables::AndroidVideoFramesTable* table,
                   std::vector<TraceBlobView>* au_data);
  ~VideoFrameModule() override;

  // Re-stamps an access-unit packet with its presentation time during
  // tokenization so the table timestamp goes through the sorter (see the .cc).
  ModuleResult TokenizePacket(const TokenizePacketArgs& args) override;

  void ParseField(const ParseFieldArgs& args) override;

  // Parse-time per-stream byte cap, a backstop against traces recorded
  // without a producer cap. Matches the producer's 256 MB default.
  static constexpr int64_t kDefaultMaxStreamSizeBytes = 256ll * 1024 * 1024;
  void SetMaxStreamSizeBytesForTesting(int64_t bytes) {
    max_stream_size_bytes_ = bytes;
  }

 private:
  void ParseVideoFrame(protozero::ConstBytes bytes,
                       int64_t ts,
                       const TracePacketData& data);
  void ParseVideoFrameError(protozero::ConstBytes bytes, int64_t ts);

  struct StreamInfo {
    // display_name and codec_string arrive on the codec_config packet and
    // are propagated to every frame of the stream.
    StringId display_name = kNullStringId;
    StringId codec_string = kNullStringId;
    int64_t emitted_bytes = 0;
    bool size_cap_hit = false;
  };
  TraceProcessorContext* const context_;
  tables::AndroidVideoFramesTable* const table_;
  std::vector<TraceBlobView>* const au_data_;
  int64_t max_stream_size_bytes_ = kDefaultMaxStreamSizeBytes;
  base::FlatHashMap<uint32_t, StreamInfo> stream_info_by_id_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_VIDEO_FRAME_IMPORTER_VIDEO_FRAME_MODULE_H_
