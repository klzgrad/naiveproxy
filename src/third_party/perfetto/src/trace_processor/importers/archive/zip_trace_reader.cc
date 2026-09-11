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

#include "src/trace_processor/importers/archive/zip_trace_reader.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/forwarding_trace_parser.h"
#include "src/trace_processor/importers/android_bugreport/android_bugreport_reader.h"
#include "src/trace_processor/importers/archive/archive_entry.h"
#include "src/trace_processor/importers/common/builtin_trace_importers.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/decompressor.h"
#include "src/trace_processor/util/trace_type.h"
#include "src/trace_processor/util/zip_reader.h"

namespace perfetto::trace_processor {

ZipTraceReader::ZipTraceReader(TraceProcessorContext* context)
    : context_(context) {}
ZipTraceReader::~ZipTraceReader() = default;

base::Status ZipTraceReader::Parse(TraceBlobView blob) {
  return zip_reader_.Parse(std::move(blob));
}

base::Status ZipTraceReader::OnPushDataToSorter() {
  if (!parsers_.empty()) {
    return base::OkStatus();
  }

  std::vector<util::ZipFile> files = zip_reader_.TakeFiles();

  // Android bug reports are ZIP files and its files do not get handled
  // separately.
  if (AndroidBugreportReader::IsAndroidBugReport(files)) {
    // TODO(lalitm): this is a bit of a hack to workaround the fact that we
    // don't have access to the zip file id here.
    auto bugreport_file = context_->trace_file_tracker->AddFile("");
    auto* context = context_->ForkContextForTrace(bugreport_file, 0);
    android_bugreport_reader_ =
        std::make_unique<AndroidBugreportReader>(context);
    return android_bugreport_reader_->Parse(std::move(files));
  }

  // TODO(carlscab): There is a lot of unnecessary copying going on here.
  // ZipTraceReader can directly parse the ZIP file and given that we know the
  // decompressed size we could directly decompress into TraceBlob chunks and
  // send them to the tokenizer.
  std::vector<uint8_t> buffer;
  std::map<ArchiveEntry, File> ordered_files;
  for (size_t i = 0; i < files.size(); ++i) {
    util::ZipFile& zip_file = files[i];
    // Ignore hidden files/directories (e.g. macOS "__MACOSX/._" resource forks
    // and ".DS_Store"). They are never traces and would otherwise fail the
    // whole archive with an "unknown trace type" error.
    if (IsHiddenArchivePath(zip_file.name())) {
      continue;
    }
    auto id = context_->trace_file_tracker->AddFile(zip_file.name());
    context_->trace_file_tracker->SetSize(id, zip_file.compressed_size());
    RETURN_IF_ERROR(files[i].Decompress(&buffer));
    TraceBlobView data(TraceBlob::CopyFrom(buffer.data(), buffer.size()));
    const auto& importers = *context_->trace_importer_registry;
    TraceImporterId type = importers.Guess(data.data(), data.size());
    ArchiveEntry entry{zip_file.name(), i, type,
                       ArchiveEntry::ComputePriority(type, importers)};
    ordered_files.emplace(entry, File{id, std::move(data)});
  }

  for (auto& file : ordered_files) {
    auto chunk_reader =
        std::make_unique<ForwardingTraceParser>(context_, file.second.id);
    auto& parser = *chunk_reader;
    parsers_.push_back(std::move(chunk_reader));

    RETURN_IF_ERROR(parser.Parse(std::move(file.second.data)));
    RETURN_IF_ERROR(parser.OnPushDataToSorter());
    // Make sure the ForwardingTraceParser determined the same trace type as we
    // did.
    PERFETTO_CHECK(parser.trace_type() == file.first.trace_type);
  }

  return base::OkStatus();
}

void ZipTraceReader::OnEventsFullyExtracted() {
  for (auto it = parsers_.rbegin(); it != parsers_.rend(); ++it) {
    (*it)->OnEventsFullyExtracted();
  }
}

namespace {

// ZIP archive.
class ZipImporter : public TraceImporter<ZipImporter> {
 public:
  ZipImporter() : TraceImporter(MakeDescriptor()) {}
  ~ZipImporter() override;

  bool Sniff(const uint8_t* data, size_t size) const override {
    static constexpr char kMagic[] = {'P', 'K', '\x03', '\x04'};
    return size >= sizeof(kMagic) && memcmp(data, kMagic, sizeof(kMagic)) == 0;
  }

  base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateReader(
      TraceProcessorContext* context,
      uint32_t) const override {
    if (!util::IsGzipSupported()) {
      return base::ErrStatus(
          "Cannot open compressed trace. zlib not enabled in the build config");
    }
    return std::unique_ptr<ChunkedTraceReader>(
        std::make_unique<ZipTraceReader>(context));
  }

 private:
  static TraceTypeDescriptor MakeDescriptor() {
    TraceTypeDescriptor d;
    d.name = "zip";
    d.is_container = true;
    d.archive_priority = 1;
    d.forks_context = false;
    d.detection_priority = 50;
    return d;
  }
};

ZipImporter::~ZipImporter() = default;

}  // namespace

std::unique_ptr<TraceImporterBase> CreateZipImporter() {
  return std::make_unique<ZipImporter>();
}

}  // namespace perfetto::trace_processor
