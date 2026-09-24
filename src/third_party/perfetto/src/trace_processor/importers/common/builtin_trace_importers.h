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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_BUILTIN_TRACE_IMPORTERS_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_BUILTIN_TRACE_IMPORTERS_H_

#include <memory>

// Factory functions for the builtin trace-type importers. Each is defined in
// its tokenizer's .cc so the importer lives next to the format it reads; they
// are declared together here so the registration sites need a single include.
namespace perfetto::trace_processor {

class TraceImporterBase;

std::unique_ptr<TraceImporterBase> CreateFuchsiaImporter();
std::unique_ptr<TraceImporterBase> CreatePerfDataImporter();
std::unique_ptr<TraceImporterBase> CreateSimpleperfProtoImporter();
std::unique_ptr<TraceImporterBase> CreateZipImporter();
std::unique_ptr<TraceImporterBase> CreateGzipImporter();
std::unique_ptr<TraceImporterBase> CreateZstdImporter();
std::unique_ptr<TraceImporterBase> CreateCtraceImporter();
std::unique_ptr<TraceImporterBase> CreateArtHprofImporter();
std::unique_ptr<TraceImporterBase> CreateTarImporter();
std::unique_ptr<TraceImporterBase> CreateArtMethodImporter();
std::unique_ptr<TraceImporterBase> CreateArtMethodV2Importer();
std::unique_ptr<TraceImporterBase> CreateGeckoImporter();
std::unique_ptr<TraceImporterBase> CreateJsonImporter();
std::unique_ptr<TraceImporterBase> CreateSystraceImporter();
std::unique_ptr<TraceImporterBase> CreateInstrumentsXmlImporter();
std::unique_ptr<TraceImporterBase> CreateAndroidLogcatImporter();
std::unique_ptr<TraceImporterBase> CreateAndroidDumpstateImporter();
std::unique_ptr<TraceImporterBase> CreateCollapsedStackImporter();
std::unique_ptr<TraceImporterBase> CreatePprofImporter();
std::unique_ptr<TraceImporterBase> CreatePrimesImporter();
std::unique_ptr<TraceImporterBase> CreateProtoImporter();
std::unique_ptr<TraceImporterBase> CreateSymbolsImporter();

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_BUILTIN_TRACE_IMPORTERS_H_
