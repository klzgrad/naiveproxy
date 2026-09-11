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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_TRACE_EXPORT_TRACE_EXPORT_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_TRACE_EXPORT_TRACE_EXPORT_H_

#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/core/plugin/registration.h"

namespace perfetto::trace_processor {
class StringPool;
}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::trace_export {

// Registers the importer used only by the version-coupled kPerfetto format.
void RegisterPlugin();

// Streams the contents of Trace Processor as defined by |format|. kArrowTar is
// a cross-version-compatible tar of self-contained Arrow files for external
// consumers and cannot be loaded back into Trace Processor. kPerfetto adds the
// internal manifest that lets a fresh instance from the same version reload
// those tables; loading it in a different version may work but is not
// guaranteed.
base::Status WriteExport(const std::vector<PluginDataframe>& dataframes,
                         const StringPool& pool,
                         TraceProcessor::ExportFormat format,
                         TraceProcessor::ExportOutput* output);

}  // namespace perfetto::trace_processor::trace_export

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_TRACE_EXPORT_TRACE_EXPORT_H_
