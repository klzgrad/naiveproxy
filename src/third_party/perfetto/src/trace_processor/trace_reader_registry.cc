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

#include "src/trace_processor/trace_reader_registry.h"

#include <memory>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/gzip_utils.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor {
namespace {
const char kNoZlibErr[] =
    "Cannot open compressed trace. zlib not enabled in the build config";

bool RequiresZlibSupport(TraceType type) {
  switch (type) {
    case kGzipTraceType:
    case kAndroidBugreportTraceType:
    case kCtraceTraceType:
    case kZipFile:
      return true;

    case kNinjaLogTraceType:
    case kSystraceTraceType:
    case kPerfDataTraceType:
    case kPprofTraceType:
    case kInstrumentsXmlTraceType:
    case kUnknownTraceType:
    case kJsonTraceType:
    case kFuchsiaTraceType:
    case kProtoTraceType:
    case kSymbolsTraceType:
    case kAndroidLogcatTraceType:
    case kAndroidDumpstateTraceType:
    case kGeckoTraceType:
    case kArtMethodTraceType:
    case kArtHprofTraceType:
    case kPerfTextTraceType:
    case kSimpleperfProtoTraceType:
    case kTarTraceType:
      return false;
  }
  PERFETTO_FATAL("For GCC");
}
}  // namespace

void TraceReaderRegistry::RegisterFactory(TraceType trace_type,
                                          Factory factory) {
  PERFETTO_CHECK(factories_.Insert(trace_type, std::move(factory)).second);
}

base::StatusOr<std::unique_ptr<ChunkedTraceReader>>
TraceReaderRegistry::CreateTraceReader(TraceType type,
                                       TraceProcessorContext* context) {
  if (auto* it = factories_.Find(type); it) {
    return (*it)(context);
  }

  if (RequiresZlibSupport(type) && !util::IsGzipSupported()) {
    return base::ErrStatus("%s support is disabled. %s",
                           TraceTypeToString(type), kNoZlibErr);
  }

  return base::ErrStatus("%s support is disabled", TraceTypeToString(type));
}

}  // namespace perfetto::trace_processor
