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

#ifndef INCLUDE_PERFETTO_TRACE_PROCESSOR_READ_TRACE_H_
#define INCLUDE_PERFETTO_TRACE_PROCESSOR_READ_TRACE_H_

#include <cstdint>
#include <functional>
#include <vector>

#include "perfetto/base/export.h"
#include "perfetto/trace_processor/status.h"

namespace perfetto {
namespace trace_processor {

class TraceProcessor;

// Optional behaviours for ReadTrace(). Defaults are chosen so that the
// behaviour is identical to historical callers; new options must be opt-in.
struct ReadTraceArgs {
  // When true, a `filename` that looks like an "http://" or "https://" URL is
  // fetched by shelling out to `curl` and streamed into the TraceProcessor,
  // instead of being treated as a local file path.
  //
  // This does NOT cover Perfetto UI share links; see allow_perfetto_ui_links.
  //
  // Defaults to false: by default every `filename` is treated as a local path,
  // so existing callers are unaffected.
  bool allow_http = false;

  // When true, a `filename` that is a Perfetto UI share link
  // (e.g. https://ui.perfetto.dev/#!/?s=<hash>) is resolved to the underlying
  // trace URL and that trace is fetched and streamed into the TraceProcessor.
  //
  // This is independent of allow_http: enabling it authorises the share-link
  // resolution flow (which fetches from storage.googleapis.com) even if
  // allow_http is false.
  //
  // Defaults to false.
  bool allow_perfetto_ui_links = false;

  // When loading a trace from a URL or share link, cache the downloaded bytes
  // on local disk (~/.cache/perfetto/tp-http-traces, or the platform
  // equivalent). Subsequent loads of the same URL issue a conditional request
  // (If-Modified-Since) and reuse the cached bytes if the server reports the
  // resource is unchanged, so a stale cache is never served for a mutable URL.
  // Only has an effect alongside allow_http / allow_perfetto_ui_links.
  //
  // Defaults to false.
  bool cache_downloads = false;
};

base::Status PERFETTO_EXPORT_COMPONENT ReadTrace(
    TraceProcessor* tp,
    const char* filename,
    const std::function<void(uint64_t parsed_size)>& progress_callback =
        [](uint64_t) {},
    bool call_notify_end_of_file = true,
    const ReadTraceArgs& args = {});

// Decompresses a gzip/zstd trace, or a proto trace containing
// compressed_packets, into `output` as a plain proto trace.
base::Status PERFETTO_EXPORT_COMPONENT
DecompressTrace(const uint8_t* data, size_t size, std::vector<uint8_t>* output);

}  // namespace trace_processor
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACE_PROCESSOR_READ_TRACE_H_
