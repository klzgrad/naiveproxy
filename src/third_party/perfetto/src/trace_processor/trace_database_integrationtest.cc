/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/temp_file.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/status.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "protos/perfetto/common/descriptor.pbzero.h"
#include "protos/perfetto/trace/test_extensions.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"
#include "protos/perfetto/trace_processor/trace_processor.pbzero.h"
#include "src/trace_processor/local_file_system.h"
#include "src/trace_processor/rpc/rpc.h"

#include "src/base/test/status_matchers.h"
#include "src/base/test/utils.h"
#include "test/gtest_and_gmock.h"

namespace perfetto::trace_processor {
namespace {

using base::gtest_matchers::IsError;
using testing::Eq;
using testing::HasSubstr;

constexpr size_t kMaxChunkSize = 4ul * 1024 * 1024;

class FailingFile final : public io::File {
 public:
  base::Status ReadAt(uint64_t, void*, size_t, size_t*) override {
    return base::ErrStatus("injected read failure");
  }

  base::Status WriteAt(uint64_t, const void*, size_t) override {
    return base::ErrStatus("injected write failure");
  }

  base::Status Truncate(uint64_t) override {
    return base::ErrStatus("injected truncate failure");
  }

  base::Status GetSize(uint64_t*) override {
    return base::ErrStatus("injected size failure");
  }

  base::Status Flush() override {
    return base::ErrStatus("injected flush failure");
  }
};

class FailingWriteFileSystem final : public io::FileSystem {
 public:
  base::Status OpenFile(const std::string&,
                        const io::FileOpenOptions&,
                        std::unique_ptr<io::File>* file) override {
    file->reset(new FailingFile());
    return base::OkStatus();
  }

  base::Status DeleteFile(const std::string&) override {
    return base::ErrStatus("injected delete failure");
  }

  base::Status FileExists(const std::string&, bool*) override {
    return base::ErrStatus("injected exists failure");
  }
};

class CountingFile final : public io::File {
 public:
  explicit CountingFile(size_t* write_count) : write_count_(write_count) {}

  base::Status ReadAt(uint64_t, void*, size_t, size_t*) override {
    return base::ErrStatus("not supported");
  }

  base::Status WriteAt(uint64_t, const void*, size_t) override {
    ++*write_count_;
    return base::OkStatus();
  }

  base::Status Truncate(uint64_t) override {
    return base::ErrStatus("not supported");
  }

  base::Status GetSize(uint64_t*) override {
    return base::ErrStatus("not supported");
  }

  base::Status Flush() override { return base::ErrStatus("not supported"); }

 private:
  size_t* write_count_;
};

class CountingFileSystem final : public io::FileSystem {
 public:
  base::Status OpenFile(const std::string&,
                        const io::FileOpenOptions&,
                        std::unique_ptr<io::File>* file) override {
    file->reset(new CountingFile(&write_count_));
    return base::OkStatus();
  }

  base::Status DeleteFile(const std::string&) override {
    return base::ErrStatus("not supported");
  }

  base::Status FileExists(const std::string&, bool*) override {
    return base::ErrStatus("not supported");
  }

  size_t write_count() const { return write_count_; }

 private:
  size_t write_count_ = 0;
};

class FileSystemPlatform final : public TraceProcessor::PlatformInterface {
 public:
  explicit FileSystemPlatform(io::FileSystem* file_system)
      : file_system_(file_system) {}

  io::FileSystem* GetFileSystem() override { return file_system_; }

 private:
  io::FileSystem* file_system_;
};

TEST(TraceProcessorCustomConfigTest, SkipInternalMetricsMatchingMountPath) {
  auto config = Config();
  config.skip_builtin_metric_paths = {"android/"};
  auto processor = TraceProcessor::CreateInstance(config);
  ASSERT_OK(processor->NotifyEndOfFile());

  // Check that android metrics have not been loaded.
  auto it = processor->ExecuteQuery(
      "select count(*) from trace_metrics "
      "where name = 'android_cpu';");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(0).long_value, 0);

  // Check that other metrics have been loaded.
  it = processor->ExecuteQuery(
      "select count(*) from trace_metrics "
      "where name = 'trace_metadata';");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(0).long_value, 1);
}

TEST(TraceProcessorCustomConfigTest, EmptyStringSkipsAllMetrics) {
  auto config = Config();
  config.skip_builtin_metric_paths = {""};
  auto processor = TraceProcessor::CreateInstance(config);
  ASSERT_OK(processor->NotifyEndOfFile());

  // Check that other metrics have been loaded.
  auto it = processor->ExecuteQuery(
      "select count(*) from trace_metrics "
      "where name = 'trace_metadata';");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(0).long_value, 0);
}

TEST(TraceProcessorCustomConfigTest, HandlesMalformedMountPath) {
  auto config = Config();
  config.skip_builtin_metric_paths = {"androi"};
  auto processor = TraceProcessor::CreateInstance(config);
  ASSERT_OK(processor->NotifyEndOfFile());

  // Check that android metrics have been loaded.
  auto it = processor->ExecuteQuery(
      "select count(*) from trace_metrics "
      "where name = 'android_cpu';");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(0).long_value, 1);
}

TEST(TraceProcessorCustomConfigTest, ExportJsonRequiresEnabledConfig) {
  FailingWriteFileSystem file_system;
  FileSystemPlatform platform(&file_system);
  auto processor = TraceProcessor::CreateInstance(Config(), &platform);
  ASSERT_OK(processor->NotifyEndOfFile());
  auto it = processor->ExecuteQuery("SELECT EXPORT_JSON('file')");
  EXPECT_FALSE(it.Next());
  EXPECT_THAT(it.Status(), IsError());
  EXPECT_THAT(it.Status().message(), HasSubstr("File I/O is disabled"));
}

TEST(TraceProcessorCustomConfigTest, ExportJsonRequiresPlatformFileSystem) {
  Config config;
  config.enable_sql_file_access = true;
  auto processor = TraceProcessor::CreateInstance(config);
  ASSERT_OK(processor->NotifyEndOfFile());
  auto it = processor->ExecuteQuery("SELECT EXPORT_JSON('file')");
  EXPECT_FALSE(it.Next());
  EXPECT_THAT(it.Status(), IsError());
  EXPECT_THAT(it.Status().message(), HasSubstr("File I/O is disabled"));
}

TEST(TraceProcessorCustomConfigTest, ExportJsonRejectsIntegerFileDescriptor) {
  auto file_system = io::CreateLocalFileSystem();
  FileSystemPlatform platform(file_system);
  Config config;
  config.enable_sql_file_access = true;
  auto processor = TraceProcessor::CreateInstance(config, &platform);
  ASSERT_OK(processor->NotifyEndOfFile());
  auto it = processor->ExecuteQuery("SELECT EXPORT_JSON(1)");
  EXPECT_FALSE(it.Next());
  EXPECT_THAT(it.Status(), IsError());
  EXPECT_THAT(it.Status().message(),
              HasSubstr("argument must be a filename string"));
}

TEST(TraceProcessorCustomConfigTest, ExportJsonUsesConfiguredFileSystem) {
  base::TempDir dir = base::TempDir::Create();
  std::string path = dir.path() + "/trace.json";
  ASSERT_EQ(path.find('\''), std::string::npos);

  auto file_system = io::CreateLocalFileSystem();
  FileSystemPlatform platform(file_system);
  Config config;
  config.enable_sql_file_access = true;
  auto processor = TraceProcessor::CreateInstance(config, &platform);
  ASSERT_OK(processor->NotifyEndOfFile());
  auto it = processor->ExecuteQuery("SELECT EXPORT_JSON('" + path + "')");
  ASSERT_TRUE(it.Next());
  EXPECT_OK(it.Status());

  std::string contents;
  ASSERT_TRUE(base::ReadFile(path, &contents));
  EXPECT_THAT(contents, HasSubstr("\"traceEvents\""));
  ASSERT_TRUE(base::Unlink(path.c_str()));
}

TEST(TraceProcessorCustomConfigTest,
     RpcImplicitResetPreservesSqlFileAccessGrant) {
  base::TempDir dir = base::TempDir::Create();
  auto file_system = io::CreateLocalFileSystem();
  FileSystemPlatform platform(file_system);
  Config config;
  config.enable_sql_file_access = true;
  auto processor = TraceProcessor::CreateInstance(config, &platform);
  Rpc rpc(std::move(processor), false, config, &platform, {});

  auto write_file = [&rpc](const std::string& path) {
    auto it = rpc.trace_processor()->ExecuteQuery(
        "SELECT __intrinsic_file_write('" + path + "', X'00')");
    ASSERT_TRUE(it.Next());
    EXPECT_OK(it.Status());
  };

  std::string first_path = dir.path() + "/before_reset";
  write_file(first_path);
  ASSERT_TRUE(base::Unlink(first_path.c_str()));

  ASSERT_OK(rpc.NotifyEndOfFile());
  ASSERT_OK(rpc.Parse(nullptr, 0));

  std::string second_path = dir.path() + "/after_reset";
  write_file(second_path);
  ASSERT_TRUE(base::Unlink(second_path.c_str()));
}

TEST(TraceProcessorCustomConfigTest, ExportJsonPropagatesWriteFailure) {
  FailingWriteFileSystem file_system;
  FileSystemPlatform platform(&file_system);
  Config config;
  config.enable_sql_file_access = true;
  auto processor = TraceProcessor::CreateInstance(config, &platform);
  ASSERT_OK(processor->NotifyEndOfFile());
  auto it = processor->ExecuteQuery("SELECT EXPORT_JSON('file')");
  EXPECT_FALSE(it.Next());
  EXPECT_THAT(it.Status(), IsError());
  EXPECT_THAT(it.Status().message(), HasSubstr("injected write failure"));
}

TEST(TraceProcessorCustomConfigTest, ExportJsonBuffersWrites) {
  CountingFileSystem file_system;
  FileSystemPlatform platform(&file_system);
  Config config;
  config.enable_sql_file_access = true;
  auto processor = TraceProcessor::CreateInstance(config, &platform);
  ASSERT_OK(processor->NotifyEndOfFile());
  auto it = processor->ExecuteQuery("SELECT EXPORT_JSON('file')");
  ASSERT_TRUE(it.Next());
  EXPECT_OK(it.Status());
  EXPECT_EQ(file_system.write_count(), 1u);
}

TEST(TraceProcessorCustomConfigTest,
     IntrinsicFileWriteRequiresConfiguredFileSystem) {
  auto processor = TraceProcessor::CreateInstance(Config());
  ASSERT_OK(processor->NotifyEndOfFile());
  auto it =
      processor->ExecuteQuery("SELECT __intrinsic_file_write('file', X'00')");
  EXPECT_FALSE(it.Next());
  EXPECT_THAT(it.Status(), IsError());
  EXPECT_THAT(it.Status().message(), HasSubstr("File I/O is disabled"));
}

TEST(TraceProcessorCustomConfigTest,
     IntrinsicFileWriteUsesConfiguredFileSystem) {
  base::TempDir dir = base::TempDir::Create();
  std::string path = dir.path() + "/file_write";
  ASSERT_EQ(path.find('\''), std::string::npos);

  auto file_system = io::CreateLocalFileSystem();
  FileSystemPlatform platform(file_system);
  Config config;
  config.enable_sql_file_access = true;
  auto processor = TraceProcessor::CreateInstance(config, &platform);
  ASSERT_OK(processor->NotifyEndOfFile());
  auto it = processor->ExecuteQuery("SELECT __intrinsic_file_write('" + path +
                                    "', X'000102FF')");
  ASSERT_TRUE(it.Next());
  EXPECT_EQ(it.Get(0).AsLong(), 4);
  EXPECT_OK(it.Status());

  std::string contents;
  ASSERT_TRUE(base::ReadFile(path, &contents));
  EXPECT_EQ(contents, std::string("\0\1\2\xff", 4));
  ASSERT_TRUE(base::Unlink(path.c_str()));
}

namespace {
base::Status ParseTraceString(TraceProcessor* processor, const std::string& s) {
  std::unique_ptr<uint8_t[]> buf(new uint8_t[s.size()]);
  memcpy(buf.get(), s.data(), s.size());
  auto status = processor->Parse(std::move(buf), s.size());
  if (!status.ok()) {
    return status;
  }
  return processor->NotifyEndOfFile();
}

// 'A' [3, 6.776] and 'B' [5, 7] partially overlap; 'B' cannot nest under 'A'.
constexpr char kOverlappingCompleteEventsJson[] =
    R"({"traceEvents":[
      {"ph":"X","cat":"k","name":"A","pid":0,"tid":7,"ts":3,"dur":3.776},
      {"ph":"X","cat":"k","name":"B","pid":0,"tid":7,"ts":5,"dur":2}
    ]})";

int64_t QueryLong(TraceProcessor* processor, const std::string& query) {
  auto it = processor->ExecuteQuery(query);
  PERFETTO_CHECK(it.Next());
  return it.Get(0).AsLong();
}
}  // namespace

TEST(TraceProcessorCustomConfigTest,
     OverlappingJsonEventsSpilledToOverflowTrack) {
  auto processor = TraceProcessor::CreateInstance(Config());
  ASSERT_OK(ParseTraceString(processor.get(), kOverlappingCompleteEventsJson));

  // Both events are kept: 'B' spills onto a separate overflow track instead of
  // being dropped.
  EXPECT_EQ(QueryLong(processor.get(), "select count(*) from slice"), 2);
  EXPECT_EQ(QueryLong(processor.get(),
                      "select count(*) from slice join track "
                      "on slice.track_id = track.id "
                      "where track.type = 'thread_overlapping_slice'"),
            1);

  // The spill is flagged to the user, but nothing is dropped.
  EXPECT_EQ(QueryLong(processor.get(),
                      "select value from stats where name = "
                      "'slice_spill_overlapping_complete_event'"),
            1);
  EXPECT_EQ(QueryLong(processor.get(),
                      "select value from stats where name = "
                      "'slice_drop_overlapping_complete_event'"),
            0);
}

TEST(TraceProcessorCustomConfigTest, ExtraParsingDescriptors) {
  Config config;
  std::string data;
  ASSERT_TRUE(base::ReadFile(
      base::GetGenDataPath(
          "protos/perfetto/trace/test_extensions_slim.descriptor"),
      &data));
  config.extra_parsing_descriptors.push_back(std::move(data));
  auto processor = TraceProcessor::CreateInstance(std::move(config));

  protozero::HeapBuffered<protos::pbzero::Trace> trace;

  auto* packet = trace->add_packet();
  packet->set_trusted_packet_sequence_id(1);
  packet->set_timestamp(1000);
  auto* te = packet->set_track_event();
  te->add_categories("cat");
  te->set_name("name");
  te->set_type(protos::pbzero::TrackEvent::TYPE_INSTANT);
  auto* ext = static_cast<protos::pbzero::TestExtension*>(te);
  ext->set_string_extension_for_testing("testing");

  trace->Finalize();
  auto [buffer, size] = trace.SerializeAsUniquePtr();

  ASSERT_OK(processor->Parse(
      TraceBlobView(TraceBlob::TakeOwnership(std::move(buffer), size))));
  ASSERT_OK(processor->NotifyEndOfFile());

  auto it = processor->ExecuteQuery(
      "SELECT COUNT(*) FROM args WHERE key = 'string_extension_for_testing'");
  ASSERT_TRUE(it.Next());
  EXPECT_THAT(it.Get(0).AsLong(), Eq(1));

  it = processor->ExecuteQuery(
      "SELECT value FROM stats WHERE name = 'unknown_extension_fields'");
  ASSERT_TRUE(it.Next());
  EXPECT_THAT(it.Get(0).AsLong(), Eq(0));
}

class TraceProcessorIntegrationTest : public ::testing::Test {
 public:
  TraceProcessorIntegrationTest()
      : processor_(TraceProcessor::CreateInstance(Config())) {}

 protected:
  base::Status LoadTrace(const char* name,
                         size_t min_chunk_size = 512,
                         size_t max_chunk_size = kMaxChunkSize) {
    EXPECT_LE(min_chunk_size, max_chunk_size);
    base::ScopedFstream f = base::OpenFstream(
        base::GetTestDataPath(std::string("test/data/") + name),
        base::kFopenReadFlag);
    std::minstd_rand0 rnd_engine(0);
    std::uniform_int_distribution<size_t> dist(min_chunk_size, max_chunk_size);
    while (!feof(*f)) {
      size_t chunk_size = dist(rnd_engine);
      std::unique_ptr<uint8_t[]> buf(new uint8_t[chunk_size]);
      auto rsize = fread(reinterpret_cast<char*>(buf.get()), 1, chunk_size, *f);
      auto status = processor_->Parse(std::move(buf), rsize);
      if (!status.ok())
        return status;
    }
    return NotifyEndOfFile();
  }
  base::Status NotifyEndOfFile() { return processor_->NotifyEndOfFile(); }

  Iterator Query(const std::string& query) {
    return processor_->ExecuteQuery(query);
  }

  TraceProcessor* Processor() { return processor_.get(); }

  size_t RestoreInitialTables() { return processor_->RestoreInitialTables(); }

 private:
  std::unique_ptr<TraceProcessor> processor_;
};

TEST_F(TraceProcessorIntegrationTest, AndroidSchedAndPs) {
  ASSERT_TRUE(LoadTrace("android_sched_and_ps.pb").ok());
  auto it = Query(
      "select count(*), max(ts) - min(ts) from sched "
      "where dur != 0 and utid != 0");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(0).long_value, 139793);
  ASSERT_EQ(it.Get(1).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(1).long_value, 19684308497);
  ASSERT_FALSE(it.Next());
}

TEST_F(TraceProcessorIntegrationTest, TraceBounds) {
  ASSERT_TRUE(LoadTrace("android_sched_and_ps.pb").ok());
  auto it = Query("select start_ts, end_ts from trace_bounds");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(0).long_value, 81473009948313);
  ASSERT_EQ(it.Get(1).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(1).long_value, 81492700784311);
  ASSERT_FALSE(it.Next());
}

// Tests that the duration of the last slice is accounted in the computation
// of the trace boundaries. Linux ftraces tend to hide this problem because
// after the last sched_switch there's always a "wake" event which causes the
// raw table to fix the bounds.
TEST_F(TraceProcessorIntegrationTest, TraceBoundsUserspaceOnly) {
  ASSERT_TRUE(LoadTrace("sfgate.json").ok());
  auto it = Query("select start_ts, end_ts from trace_bounds");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(0).long_value, 2213649212614000);
  ASSERT_EQ(it.Get(1).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(1).long_value, 2213689745140000);
  ASSERT_FALSE(it.Next());
}

TEST_F(TraceProcessorIntegrationTest, Hash) {
  auto it = Query("select HASH()");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).long_value, static_cast<int64_t>(0xcbf29ce484222325));

  it = Query("select HASH('test')");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).long_value, static_cast<int64_t>(0xf9e6e6ef197c2b25));

  it = Query("select HASH('test', 1)");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).long_value, static_cast<int64_t>(0xa9cb070fdc15f7a4));
}

#if !PERFETTO_BUILDFLAG(PERFETTO_LLVM_DEMANGLE) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#define MAYBE_Demangle DISABLED_Demangle
#else
#define MAYBE_Demangle Demangle
#endif
TEST_F(TraceProcessorIntegrationTest, MAYBE_Demangle) {
  auto it = Query("select DEMANGLE('_Znwm')");
  ASSERT_TRUE(it.Next());
  EXPECT_STRCASEEQ(it.Get(0).string_value, "operator new(unsigned long)");

  it = Query("select DEMANGLE('_ZN3art6Thread14CreateCallbackEPv')");
  ASSERT_TRUE(it.Next());
  EXPECT_STRCASEEQ(it.Get(0).string_value,
                   "art::Thread::CreateCallback(void*)");

  it = Query("select DEMANGLE('test')");
  ASSERT_TRUE(it.Next());
  EXPECT_TRUE(it.Get(0).is_null());
}

#if !PERFETTO_BUILDFLAG(PERFETTO_LLVM_DEMANGLE)
#define MAYBE_DemangleRust DISABLED_DemangleRust
#else
#define MAYBE_DemangleRust DemangleRust
#endif
TEST_F(TraceProcessorIntegrationTest, MAYBE_DemangleRust) {
  auto it = Query(
      "select DEMANGLE("
      "'_RNvNvMs0_NtNtNtCsg1Z12QU66Yk_3std3sys4unix6threadNtB7_"
      "6Thread3new12thread_start')");
  ASSERT_TRUE(it.Next());
  EXPECT_STRCASEEQ(it.Get(0).string_value,
                   "<std::sys::unix::thread::Thread>::new::thread_start");

  it = Query("select DEMANGLE('_RNvCsdV139EorvfX_14keystore2_main4main')");
  ASSERT_TRUE(it.Next());
  ASSERT_STRCASEEQ(it.Get(0).string_value, "keystore2_main::main");

  it = Query("select DEMANGLE('_R')");
  ASSERT_TRUE(it.Next());
  ASSERT_TRUE(it.Get(0).is_null());
}

TEST_F(TraceProcessorIntegrationTest, Sfgate) {
  ASSERT_TRUE(LoadTrace("sfgate.json", strlen("{\"traceEvents\":[")).ok());
  auto it = Query(
      "select count(*), max(ts) - min(ts) "
      "from slice s inner join thread_track t "
      "on s.track_id = t.id where utid != 0");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(0).long_value, 43357);
  ASSERT_EQ(it.Get(1).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(1).long_value, 40532506000);
  ASSERT_FALSE(it.Next());
}

TEST_F(TraceProcessorIntegrationTest, UnsortedTrace) {
  ASSERT_TRUE(
      LoadTrace("unsorted_trace.json", strlen("{\"traceEvents\":[")).ok());
  auto it = Query("select ts, depth from slice order by ts");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(0).long_value, 50000);
  ASSERT_EQ(it.Get(1).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(1).long_value, 0);
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(0).long_value, 100000);
  ASSERT_EQ(it.Get(1).type, SqlValue::kLong);
  ASSERT_EQ(it.Get(1).long_value, 1);
  ASSERT_FALSE(it.Next());
}

TEST_F(TraceProcessorIntegrationTest, SerializeMetricDescriptors) {
  std::vector<uint8_t> desc_set_bytes = Processor()->GetMetricDescriptors();
  protos::pbzero::DescriptorSet::Decoder desc_set(desc_set_bytes.data(),
                                                  desc_set_bytes.size());

  ASSERT_TRUE(desc_set.has_descriptors());
  int trace_metrics_count = 0;
  for (auto desc = desc_set.descriptors(); desc; ++desc) {
    protos::pbzero::DescriptorProto::Decoder proto_desc(*desc);
    if (proto_desc.name().ToStdString() == ".perfetto.protos.TraceMetrics") {
      ASSERT_TRUE(proto_desc.has_field());
      trace_metrics_count++;
    }
  }

  // There should be exactly one definition of TraceMetrics. This can be not
  // true if we're not deduping descriptors properly.
  ASSERT_EQ(trace_metrics_count, 1);
}

TEST_F(TraceProcessorIntegrationTest, ComputeMetricsFormattedExtension) {
  ASSERT_OK(NotifyEndOfFile());

  std::string metric_output;
  base::Status status = Processor()->ComputeMetricText(
      std::vector<std::string>{"test_chrome_metric"},
      TraceProcessor::MetricResultFormat::kProtoText, &metric_output);
  ASSERT_TRUE(status.ok());
  // Extension fields are output as [fully.qualified.name].
  ASSERT_EQ(metric_output,
            "[perfetto.protos.test_chrome_metric] {\n"
            "  test_value: 1\n"
            "}");
}

TEST_F(TraceProcessorIntegrationTest, ComputeMetricsFormattedNoExtension) {
  ASSERT_OK(NotifyEndOfFile());

  std::string metric_output;
  ASSERT_OK(Processor()->ComputeMetricText(
      std::vector<std::string>{"trace_metadata"},
      TraceProcessor::MetricResultFormat::kProtoText, &metric_output));
  // Check that metric result starts with trace_metadata field. Since this is
  // not an extension field, the field name is not fully qualified.
  ASSERT_TRUE(metric_output.rfind("trace_metadata {") == 0);
}

// TODO(hjd): Add trace to test_data.
TEST_F(TraceProcessorIntegrationTest, DISABLED_AndroidBuildTrace) {
  ASSERT_TRUE(LoadTrace("android_build_trace.json", strlen("[\n{")).ok());
}

TEST_F(TraceProcessorIntegrationTest, DISABLED_Clusterfuzz14357) {
  ASSERT_FALSE(LoadTrace("clusterfuzz_14357", 4096).ok());
}

TEST_F(TraceProcessorIntegrationTest, Clusterfuzz14730) {
  ASSERT_TRUE(LoadTrace("clusterfuzz_14730", 4096).ok());
}

TEST_F(TraceProcessorIntegrationTest, Clusterfuzz14753) {
  ASSERT_TRUE(LoadTrace("clusterfuzz_14753", 4096).ok());
}

TEST_F(TraceProcessorIntegrationTest, Clusterfuzz14762) {
  ASSERT_TRUE(LoadTrace("clusterfuzz_14762", 4096ul * 1024).ok());
  auto it = Query("select sum(value) from stats where severity = 'error';");
  ASSERT_TRUE(it.Next());
  ASSERT_GT(it.Get(0).long_value, 0);
}

TEST_F(TraceProcessorIntegrationTest, Clusterfuzz14767) {
  ASSERT_TRUE(LoadTrace("clusterfuzz_14767", 4096ul * 1024).ok());
  auto it = Query("select sum(value) from stats where severity = 'error';");
  ASSERT_TRUE(it.Next());
  ASSERT_GT(it.Get(0).long_value, 0);
}

TEST_F(TraceProcessorIntegrationTest, Clusterfuzz14799) {
  ASSERT_TRUE(LoadTrace("clusterfuzz_14799", 4096ul * 1024).ok());
  auto it = Query("select sum(value) from stats where severity = 'error';");
  ASSERT_TRUE(it.Next());
  ASSERT_GT(it.Get(0).long_value, 0);
}

TEST_F(TraceProcessorIntegrationTest, Clusterfuzz15252) {
  ASSERT_TRUE(LoadTrace("clusterfuzz_15252", 4096).ok());
}

TEST_F(TraceProcessorIntegrationTest, Clusterfuzz17805) {
  // This trace is garbage but is detected as a systrace. However, it should
  // still parse successfully as we try to be graceful with encountering random
  // data in systrace as they can have arbitrary print events from the kernel.
  ASSERT_TRUE(LoadTrace("clusterfuzz_17805", 4096).ok());
}

// Failing on DCHECKs during import because the traces aren't really valid.
#if PERFETTO_DCHECK_IS_ON()
#define MAYBE_Clusterfuzz20215 DISABLED_Clusterfuzz20215
#define MAYBE_Clusterfuzz20292 DISABLED_Clusterfuzz20292
#define MAYBE_Clusterfuzz21178 DISABLED_Clusterfuzz21178
#define MAYBE_Clusterfuzz23053 DISABLED_Clusterfuzz23053
#define MAYBE_Clusterfuzz28338 DISABLED_Clusterfuzz28338
#define MAYBE_Clusterfuzz28766 DISABLED_Clusterfuzz28766
#else  // PERFETTO_DCHECK_IS_ON()
#define MAYBE_Clusterfuzz20215 Clusterfuzz20215
#define MAYBE_Clusterfuzz20292 Clusterfuzz20292
#define MAYBE_Clusterfuzz21178 Clusterfuzz21178
#define MAYBE_Clusterfuzz23053 Clusterfuzz23053
#define MAYBE_Clusterfuzz28338 Clusterfuzz28338
#define MAYBE_Clusterfuzz28766 Clusterfuzz28766
#endif  // PERFETTO_DCHECK_IS_ON()

TEST_F(TraceProcessorIntegrationTest, MAYBE_Clusterfuzz20215) {
  ASSERT_TRUE(LoadTrace("clusterfuzz_20215", 4096).ok());
}

TEST_F(TraceProcessorIntegrationTest, MAYBE_Clusterfuzz20292) {
  ASSERT_FALSE(LoadTrace("clusterfuzz_20292", 4096).ok());
}

TEST_F(TraceProcessorIntegrationTest, MAYBE_Clusterfuzz21178) {
  ASSERT_TRUE(LoadTrace("clusterfuzz_21178", 4096).ok());
}

TEST_F(TraceProcessorIntegrationTest, MAYBE_Clusterfuzz23053) {
  ASSERT_FALSE(LoadTrace("clusterfuzz_23053", 4096).ok());
}

TEST_F(TraceProcessorIntegrationTest, MAYBE_Clusterfuzz28338) {
  ASSERT_TRUE(LoadTrace("clusterfuzz_28338", 4096).ok());
}

TEST_F(TraceProcessorIntegrationTest, MAYBE_Clusterfuzz28766) {
  ASSERT_TRUE(LoadTrace("clusterfuzz_28766", 4096).ok());
}

TEST_F(TraceProcessorIntegrationTest, RestoreInitialTablesInvariant) {
  ASSERT_OK(NotifyEndOfFile());
  uint64_t first_restore = RestoreInitialTables();
  ASSERT_EQ(RestoreInitialTables(), first_restore);
}

TEST_F(TraceProcessorIntegrationTest, RestoreInitialTablesPerfettoSql) {
  ASSERT_OK(NotifyEndOfFile());
  RestoreInitialTables();

  for (int repeat = 0; repeat < 3; repeat++) {
    ASSERT_EQ(RestoreInitialTables(), 0u);

    // 1. Perfetto table
    {
      auto it = Query("CREATE PERFETTO TABLE obj1 AS SELECT 1 AS col;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    // 2. Perfetto view
    {
      auto it = Query("CREATE PERFETTO VIEW obj2 AS SELECT * FROM stats;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    // 3. Runtime function
    {
      auto it =
          Query("CREATE PERFETTO FUNCTION obj3() RETURNS INT AS SELECT 1;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    // 4. Runtime table function
    {
      auto it = Query(
          "CREATE PERFETTO FUNCTION obj4() RETURNS TABLE(col INT) AS SELECT 1 "
          "AS col;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    // 5. Macro
    {
      auto it = Query("CREATE PERFETTO MACRO obj5(a Expr) returns Expr AS $a;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    {
      auto it = Query("obj5!(SELECT 1);");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    ASSERT_EQ(RestoreInitialTables(), 5u);
  }
}

TEST_F(TraceProcessorIntegrationTest, RestoreInitialTablesStandardSqlite) {
  ASSERT_OK(NotifyEndOfFile());
  RestoreInitialTables();

  for (int repeat = 0; repeat < 3; repeat++) {
    ASSERT_EQ(RestoreInitialTables(), 0u);
    {
      auto it = Query("CREATE TABLE obj1(unused text);");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    {
      auto it = Query("CREATE TEMPORARY TABLE obj2(unused text);");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    // Add a view
    {
      auto it = Query("CREATE VIEW obj3 AS SELECT * FROM stats;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    ASSERT_EQ(RestoreInitialTables(), 3u);
  }
}

TEST_F(TraceProcessorIntegrationTest, RestoreInitialTablesModules) {
  ASSERT_OK(NotifyEndOfFile());
  RestoreInitialTables();

  for (int repeat = 0; repeat < 3; repeat++) {
    ASSERT_EQ(RestoreInitialTables(), 0u);
    {
      auto it = Query("INCLUDE PERFETTO MODULE time.conversion;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    {
      auto it = Query("SELECT trace_start();");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    RestoreInitialTables();
  }
}

TEST_F(TraceProcessorIntegrationTest, RestoreInitialTablesSpanJoin) {
  ASSERT_OK(NotifyEndOfFile());
  RestoreInitialTables();

  for (int repeat = 0; repeat < 3; repeat++) {
    ASSERT_EQ(RestoreInitialTables(), 0u);
    {
      auto it = Query(
          "CREATE TABLE t1(ts BIGINT, dur BIGINT, PRIMARY KEY (ts, dur)) "
          "WITHOUT ROWID;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    {
      auto it = Query(
          "CREATE TABLE t2(ts BIGINT, dur BIGINT, PRIMARY KEY (ts, dur)) "
          "WITHOUT ROWID;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    {
      auto it = Query("INSERT INTO t2(ts, dur) VALUES(1, 2), (5, 0), (1, 1);");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    {
      auto it = Query("CREATE VIRTUAL TABLE sp USING span_join(t1, t2);;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    {
      auto it = Query("SELECT ts, dur FROM sp;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    ASSERT_EQ(RestoreInitialTables(), 3u);
  }
}

TEST_F(TraceProcessorIntegrationTest, RestoreInitialTablesWithClause) {
  ASSERT_OK(NotifyEndOfFile());
  RestoreInitialTables();

  for (int repeat = 0; repeat < 3; repeat++) {
    ASSERT_EQ(RestoreInitialTables(), 0u);
    {
      auto it = Query(
          "CREATE PERFETTO TABLE foo AS WITH bar AS (SELECT * FROM slice) "
          "SELECT ts FROM bar;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    ASSERT_EQ(RestoreInitialTables(), 1u);
  }
}

TEST_F(TraceProcessorIntegrationTest, RestoreInitialTablesIndex) {
  ASSERT_OK(NotifyEndOfFile());
  RestoreInitialTables();

  for (int repeat = 0; repeat < 3; repeat++) {
    ASSERT_EQ(RestoreInitialTables(), 0u);
    {
      auto it = Query("CREATE TABLE foo AS SELECT * FROM slice;");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    {
      auto it = Query("CREATE INDEX ind ON foo (ts, track_id);");
      it.Next();
      ASSERT_TRUE(it.Status().ok());
    }
    ASSERT_EQ(RestoreInitialTables(), 2u);
  }
}

TEST_F(TraceProcessorIntegrationTest, RestoreInitialTablesTraceBounds) {
  ASSERT_TRUE(LoadTrace("android_sched_and_ps.pb").ok());
  {
    auto it = Query("SELECT * from trace_bounds;");
    it.Next();
    ASSERT_TRUE(it.Status().ok());
    ASSERT_EQ(it.Get(0).AsLong(), 81473009948313l);
  }

  ASSERT_EQ(RestoreInitialTables(), 0u);
  {
    auto it = Query("SELECT * from trace_bounds;");
    it.Next();
    ASSERT_TRUE(it.Status().ok());
    ASSERT_EQ(it.Get(0).AsLong(), 81473009948313l);
  }
}

TEST_F(TraceProcessorIntegrationTest, RestoreInitialTablesDependents) {
  ASSERT_OK(NotifyEndOfFile());
  {
    auto it = Query("create perfetto table foo as select 1 as x");
    ASSERT_FALSE(it.Next());
    ASSERT_TRUE(it.Status().ok());

    it = Query("create perfetto function f() returns INT as select * from foo");
    ASSERT_FALSE(it.Next());
    ASSERT_TRUE(it.Status().ok());

    it = Query("SELECT f()");
    ASSERT_TRUE(it.Next());
    ASSERT_FALSE(it.Next());
    ASSERT_TRUE(it.Status().ok());
  }

  ASSERT_EQ(RestoreInitialTables(), 2u);
}

TEST_F(TraceProcessorIntegrationTest, RestoreDependentFunction) {
  ASSERT_OK(NotifyEndOfFile());
  {
    auto it =
        Query("create perfetto function foo0() returns INT as select 1 as x");
    ASSERT_FALSE(it.Next());
    ASSERT_TRUE(it.Status().ok());
  }
  for (int i = 1; i < 100; ++i) {
    base::StackString<1024> sql(
        "create perfetto function foo%d() returns INT as select foo%d()", i,
        i - 1);
    auto it = Query(sql.c_str());
    ASSERT_FALSE(it.Next());
    ASSERT_TRUE(it.Status().ok()) << it.Status().c_message();
  }

  ASSERT_EQ(RestoreInitialTables(), 100u);
}

TEST_F(TraceProcessorIntegrationTest, RestoreDependentTableFunction) {
  ASSERT_OK(NotifyEndOfFile());
  {
    auto it = Query(
        "create perfetto function foo0() returns TABLE(x INT) "
        " as select 1 as x");
    ASSERT_FALSE(it.Next());
    ASSERT_TRUE(it.Status().ok());
  }
  for (int i = 1; i < 100; ++i) {
    base::StackString<1024> sql(
        "create perfetto function foo%d() returns TABLE(x INT) "
        " as select * from foo%d()",
        i, i - 1);
    auto it = Query(sql.c_str());
    ASSERT_FALSE(it.Next());
    ASSERT_TRUE(it.Status().ok()) << it.Status().c_message();
  }

  ASSERT_EQ(RestoreInitialTables(), 100u);
}

// This test checks that a ninja trace is tokenized properly even if read in
// small chunks of 1KB each. The values used in the test have been cross-checked
// with opening the same trace with ninjatracing + chrome://tracing.
TEST_F(TraceProcessorIntegrationTest, NinjaLog) {
  ASSERT_TRUE(LoadTrace("ninja_log", 1024).ok());
  auto it = Query("select count(*) from process where name glob 'Build';");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).long_value, 1);

  it = Query(
      "select count(*) from thread left join process using(upid) where "
      "thread.name like 'Worker%' and process.pid=1");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).long_value, 28);

  it = Query(
      "create view slices_1st_build as select slices.* from slices left "
      "join thread_track on(slices.track_id == thread_track.id) left join "
      "thread using(utid) left join process using(upid) where pid=1");
  it.Next();
  ASSERT_TRUE(it.Status().ok());

  it = Query("select (max(ts) - min(ts)) / 1000000 from slices_1st_build");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).long_value, 44697);

  it = Query("select name from slices_1st_build order by ts desc limit 1");
  ASSERT_TRUE(it.Next());
  ASSERT_STREQ(it.Get(0).string_value, "trace_processor_shell");

  it = Query("select sum(dur) / 1000000 from slices_1st_build");
  ASSERT_TRUE(it.Next());
  ASSERT_EQ(it.Get(0).long_value, 837192);
}

/*
 * This trace does not have a uuid. The uuid will be generated from the first
 * 4096 bytes, which will be read in one chunk.
 */
TEST_F(TraceProcessorIntegrationTest, TraceWithoutUuidReadInOneChunk) {
  ASSERT_TRUE(LoadTrace("example_android_trace_30s.pb", kMaxChunkSize).ok());
  auto it = Query("select str_value from metadata where name = 'trace_uuid'");
  ASSERT_TRUE(it.Next());
  EXPECT_STREQ(it.Get(0).string_value, "00000000-0000-0000-8906-ebb53e1d0738");
}

/*
 * This trace does not have a uuid. The uuid will be generated from the first
 * 4096 bytes, which will be read in multiple chunks.
 */
TEST_F(TraceProcessorIntegrationTest, TraceWithoutUuidReadInMultipleChunks) {
  ASSERT_TRUE(LoadTrace("example_android_trace_30s.pb", 512, 2048).ok());
  auto it = Query("select str_value from metadata where name = 'trace_uuid'");
  ASSERT_TRUE(it.Next());
  EXPECT_STREQ(it.Get(0).string_value, "00000000-0000-0000-8906-ebb53e1d0738");
}

/*
 * This trace has a uuid. It will not be overridden by the hash of the first
 * 4096 bytes.
 */
TEST_F(TraceProcessorIntegrationTest, TraceWithUuidReadInParts) {
  ASSERT_TRUE(LoadTrace("trace_with_uuid.pftrace", 512, 2048).ok());
  auto it = Query("select str_value from metadata where name = 'trace_uuid'");
  ASSERT_TRUE(it.Next());
  EXPECT_STREQ(it.Get(0).string_value, "123e4567-e89b-12d3-a456-426655443322");
}

TEST_F(TraceProcessorIntegrationTest, ErrorMessageExecuteQuery) {
  ASSERT_OK(NotifyEndOfFile());
  auto it = Query("select t from slice");
  ASSERT_FALSE(it.Next());
  ASSERT_FALSE(it.Status().ok());

  ASSERT_THAT(it.Status().message(),
              testing::Eq(R"(Traceback (most recent call last):
  File "stdin" line 1 col 8
    select t from slice
           ^
no such column: t)"));
}

TEST_F(TraceProcessorIntegrationTest, ErrorMessageMetricFile) {
  ASSERT_OK(NotifyEndOfFile());
  ASSERT_TRUE(
      Processor()->RegisterMetric("foo/bar.sql", "select t from slice").ok());

  auto it = Query("select RUN_METRIC('foo/bar.sql');");
  ASSERT_FALSE(it.Next());
  ASSERT_FALSE(it.Status().ok());

  ASSERT_EQ(it.Status().message(),
            R"(Traceback (most recent call last):
  File "stdin" line 1 col 1
    select RUN_METRIC('foo/bar.sql')
    ^
  Metric file "foo/bar.sql" line 1 col 8
    select t from slice
           ^
no such column: t)");
}

TEST_F(TraceProcessorIntegrationTest, ErrorMessageModule) {
  ASSERT_OK(NotifyEndOfFile());
  SqlPackage module;
  module.name = "foo";
  module.modules.push_back(std::make_pair("foo.bar", "select t from slice"));

  ASSERT_TRUE(Processor()->RegisterSqlPackage(module).ok());

  auto it = Query("include perfetto module foo.bar;");
  ASSERT_FALSE(it.Next());
  ASSERT_FALSE(it.Status().ok());

  ASSERT_EQ(it.Status().message(),
            R"(Traceback (most recent call last):
  File "stdin" line 1 col 1
    include perfetto module foo.bar
    ^
  Module include "foo.bar" line 1 col 8
    select t from slice
           ^
no such column: t)");
}

TEST_F(TraceProcessorIntegrationTest, FunctionRegistrationError) {
  auto it =
      Query("create perfetto function f() returns INT as select * from foo");
  ASSERT_FALSE(it.Next());
  ASSERT_FALSE(it.Status().ok());

  it = Query("SELECT foo()");
  ASSERT_FALSE(it.Next());
  ASSERT_FALSE(it.Status().ok());

  it = Query("create perfetto function f() returns INT as select 1");
  ASSERT_FALSE(it.Next());
  ASSERT_TRUE(it.Status().ok());
}

TEST_F(TraceProcessorIntegrationTest, CreateTableDuplicateNames) {
  auto it = Query(
      "create perfetto table foo select 1 as duplicate_a, 2 as duplicate_a, 3 "
      "as duplicate_b, 4 as duplicate_b");
  ASSERT_FALSE(it.Next());
  ASSERT_FALSE(it.Status().ok());
  ASSERT_THAT(it.Status().message(), HasSubstr("duplicate_a"));
  ASSERT_THAT(it.Status().message(), HasSubstr("duplicate_b"));
}

TEST_F(TraceProcessorIntegrationTest, InvalidTrace) {
  constexpr char kBadData[] = "\0\0\0\0";
  EXPECT_FALSE(Processor()
                   ->Parse(TraceBlobView(
                       TraceBlob::CopyFrom(kBadData, sizeof(kBadData))))
                   .ok());
  NotifyEndOfFile();
}

TEST_F(TraceProcessorIntegrationTest, NoNotifyEndOfFileCalled) {
  constexpr char kProtoData[] = "\x0a";
  EXPECT_TRUE(Processor()
                  ->Parse(TraceBlobView(
                      TraceBlob::CopyFrom(kProtoData, sizeof(kProtoData))))
                  .ok());
}

TEST_F(TraceProcessorIntegrationTest, PackagePrefixClash_ExistingIsPrefix) {
  ASSERT_OK(NotifyEndOfFile());

  // Register package "foo"
  SqlPackage pkg1;
  pkg1.name = "foo";
  pkg1.modules.push_back({"foo.mod1", "SELECT 1"});
  ASSERT_OK(Processor()->RegisterSqlPackage(pkg1));

  // Registering "foo.bar" should fail (existing "foo" is prefix of "foo.bar")
  SqlPackage pkg2;
  pkg2.name = "foo.bar";
  pkg2.modules.push_back({"foo.bar.mod2", "SELECT 2"});
  auto status = Processor()->RegisterSqlPackage(pkg2);
  ASSERT_FALSE(status.ok());
  ASSERT_THAT(status.message(), HasSubstr("clashes"));
}

TEST_F(TraceProcessorIntegrationTest, PackagePrefixClash_NewIsPrefix) {
  ASSERT_OK(NotifyEndOfFile());

  // Register package "foo.bar"
  SqlPackage pkg1;
  pkg1.name = "foo.bar";
  pkg1.modules.push_back({"foo.bar.mod1", "SELECT 1"});
  ASSERT_OK(Processor()->RegisterSqlPackage(pkg1));

  // Registering "foo" should fail (new "foo" is prefix of existing "foo.bar")
  SqlPackage pkg2;
  pkg2.name = "foo";
  pkg2.modules.push_back({"foo.mod2", "SELECT 2"});
  auto status = Processor()->RegisterSqlPackage(pkg2);
  ASSERT_FALSE(status.ok());
  ASSERT_THAT(status.message(), HasSubstr("clashes"));
}

TEST_F(TraceProcessorIntegrationTest, StdlibDocsObjectsDottedPackage) {
  ASSERT_OK(NotifyEndOfFile());

  // A package whose name itself contains dots, owning a module beneath it.
  // Package ownership must come directly from the registry rather than from
  // splitting the module key on the first dot.
  SqlPackage pkg;
  pkg.name = "dev.perfetto.test";
  pkg.modules.push_back({"dev.perfetto.test.common", "SELECT 1"});
  ASSERT_OK(Processor()->RegisterSqlPackage(pkg));

  auto result = Query(
      "SELECT COUNT(*) FROM __intrinsic_stdlib_objects "
      "WHERE package = 'dev.perfetto.test' "
      "AND module = 'dev.perfetto.test.common' "
      "AND object_type = 'MODULE'");
  ASSERT_TRUE(result.Next());
  ASSERT_EQ(result.Get(0).AsLong(), 1);
  ASSERT_OK(result.Status());
}

TEST_F(TraceProcessorIntegrationTest, PackageSameNameOverride) {
  ASSERT_OK(NotifyEndOfFile());

  // Register package "foo"
  SqlPackage pkg1;
  pkg1.name = "foo";
  pkg1.modules.push_back({"foo.mod1", "SELECT 1"});
  ASSERT_OK(Processor()->RegisterSqlPackage(pkg1));

  // Re-registering "foo" without override should fail
  SqlPackage pkg2;
  pkg2.name = "foo";
  pkg2.modules.push_back({"foo.mod2", "SELECT 2"});
  ASSERT_FALSE(Processor()->RegisterSqlPackage(pkg2).ok());

  // Re-registering "foo" with override should succeed
  pkg2.allow_override = true;
  ASSERT_OK(Processor()->RegisterSqlPackage(pkg2));
}

class StringExportOutput : public TraceProcessor::ExportOutput {
 public:
  base::Status Write(const void* data, size_t size) override {
    bytes.append(static_cast<const char*>(data), size);
    return base::OkStatus();
  }

  std::string bytes;
};

std::string ExportToString(TraceProcessor* tp,
                           TraceProcessor::ExportFormat format) {
  StringExportOutput output;
  EXPECT_OK(tp->Export(format, &output));
  return std::move(output.bytes);
}

base::Status ParsePerfettoExport(TraceProcessor* tp, const std::string& bytes) {
  constexpr size_t kChunk = 4096;
  for (size_t i = 0; i < bytes.size(); i += kChunk) {
    size_t len = std::min(kChunk, bytes.size() - i);
    auto buf = std::make_unique<uint8_t[]>(len);
    memcpy(buf.get(), bytes.data() + i, len);
    auto status = tp->Parse(std::move(buf), len);
    if (!status.ok()) {
      return status;
    }
  }
  return tp->NotifyEndOfFile();
}

int64_t QuerySingleInt(TraceProcessor* tp, const std::string& sql) {
  auto it = tp->ExecuteQuery(sql);
  EXPECT_TRUE(it.Next()) << sql;
  int64_t value = it.Get(0).long_value;
  EXPECT_FALSE(it.Next());
  EXPECT_OK(it.Status());
  return value;
}

std::string QuerySingleString(TraceProcessor* tp, const std::string& sql) {
  auto it = tp->ExecuteQuery(sql);
  EXPECT_TRUE(it.Next()) << sql;
  std::string value = it.Get(0).string_value;
  EXPECT_FALSE(it.Next());
  EXPECT_OK(it.Status());
  return value;
}

struct TarMember {
  std::string name;
  size_t data_offset;
  size_t size;
};

std::vector<TarMember> ReadTarMembers(const std::string& tar) {
  std::vector<TarMember> members;
  for (size_t offset = 0; offset + 512 <= tar.size();) {
    if (tar[offset] == '\0') {
      break;
    }
    std::string name(tar.data() + offset, strnlen(tar.data() + offset, 100));
    size_t size = 0;
    for (size_t i = 0; i < 11; ++i) {
      char c = tar[offset + 124 + i];
      if (c < '0' || c > '7') {
        break;
      }
      size = size * 8 + static_cast<size_t>(c - '0');
    }
    size_t data_offset = offset + 512;
    if (data_offset + size > tar.size()) {
      return {};
    }
    members.push_back({std::move(name), data_offset, size});
    offset = data_offset + ((size + 511) / 512) * 512;
  }
  return members;
}

TEST_F(TraceProcessorIntegrationTest, ExportSqliteRequiresFilePath) {
  StringExportOutput output;
  EXPECT_THAT(
      Processor()->Export(TraceProcessor::ExportFormat::kSqlite, &output),
      IsError());
}

TEST_F(TraceProcessorIntegrationTest, ExportPerfetto) {
  ASSERT_OK(LoadTrace("example_android_trace_30s.pb"));

  std::string tar =
      ExportToString(Processor(), TraceProcessor::ExportFormat::kPerfetto);

  // The archive must start with the manifest entry: a tar file begins with
  // the first entry's 100-byte name field.
  ASSERT_GT(tar.size(), 1024u);
  ASSERT_EQ(std::string(tar.data(), 22), "perfetto_manifest.json");
  ASSERT_EQ(std::string(tar.data() + 257, 5), "ustar");

  std::vector<TarMember> members = ReadTarMembers(tar);
  ASSERT_GT(members.size(), 2u);
  EXPECT_EQ(members[0].name, "perfetto_manifest.json");
  for (size_t i = 1; i < members.size(); ++i) {
    const TarMember& member = members[i];
    EXPECT_TRUE(base::EndsWith(member.name, ".arrow")) << member.name;
    ASSERT_GE(member.size, 6u);
    EXPECT_EQ(tar.substr(member.data_offset, 6), "ARROW1") << member.name;
  }
}

TEST_F(TraceProcessorIntegrationTest, ExportArrowTar) {
  ASSERT_OK(LoadTrace("example_android_trace_30s.pb"));
  auto runtime_table = Processor()->ExecuteQuery(
      "CREATE PERFETTO TABLE runtime_export_test AS "
      "SELECT 1 AS value");
  EXPECT_FALSE(runtime_table.Next());
  ASSERT_OK(runtime_table.Status());

  std::string tar =
      ExportToString(Processor(), TraceProcessor::ExportFormat::kArrowTar);
  std::vector<TarMember> members = ReadTarMembers(tar);
  ASSERT_FALSE(members.empty());
  for (const TarMember& member : members) {
    EXPECT_TRUE(base::EndsWith(member.name, ".arrow")) << member.name;
    ASSERT_GE(member.size, 6u);
    EXPECT_EQ(tar.substr(member.data_offset, 6), "ARROW1") << member.name;
    EXPECT_NE(member.name, "runtime_export_test.arrow");
  }
  EXPECT_EQ(tar.find("perfetto_manifest.json"), std::string::npos);

  auto reimport = TraceProcessor::CreateInstance(Config());
  EXPECT_THAT(ParsePerfettoExport(reimport.get(), tar), IsError());
}

TEST_F(TraceProcessorIntegrationTest, PerfettoExportRoundTrip) {
  ASSERT_OK(LoadTrace("example_android_trace_30s.pb"));

  int64_t thread_count =
      QuerySingleInt(Processor(), "SELECT count() FROM thread");
  int64_t slice_count =
      QuerySingleInt(Processor(), "SELECT count() FROM slice");
  int64_t slice_dur_sum =
      QuerySingleInt(Processor(), "SELECT sum(dur) FROM slice");
  std::string first_thread_name = QuerySingleString(
      Processor(),
      "SELECT name FROM thread WHERE name IS NOT NULL ORDER BY name LIMIT 1");
  ASSERT_GT(thread_count, 0);
  ASSERT_GT(slice_count, 0);

  std::string tar =
      ExportToString(Processor(), TraceProcessor::ExportFormat::kPerfetto);

  auto reimport = TraceProcessor::CreateInstance(Config());
  ASSERT_OK(ParsePerfettoExport(reimport.get(), tar));

  EXPECT_EQ(QuerySingleInt(reimport.get(), "SELECT count() FROM thread"),
            thread_count);
  EXPECT_EQ(QuerySingleInt(reimport.get(), "SELECT count() FROM slice"),
            slice_count);
  EXPECT_EQ(QuerySingleInt(reimport.get(), "SELECT sum(dur) FROM slice"),
            slice_dur_sum);
  EXPECT_EQ(QuerySingleString(
                reimport.get(),
                "SELECT name FROM thread WHERE name IS NOT NULL ORDER BY name "
                "LIMIT 1"),
            first_thread_name);
}

TEST_F(TraceProcessorIntegrationTest, PerfettoExportSchemaMismatchRejected) {
  ASSERT_OK(LoadTrace("example_android_trace_30s.pb"));
  std::string tar =
      ExportToString(Processor(), TraceProcessor::ExportFormat::kPerfetto);

  // Corrupt the manifest: flip a column type. Same-length replacement keeps
  // the tar structure intact.
  size_t pos = tar.find("\"type\":\"int64\"");
  ASSERT_NE(pos, std::string::npos);
  tar.replace(pos, 14, "\"type\":\"int32\"");

  auto reimport = TraceProcessor::CreateInstance(Config());
  auto status = ParsePerfettoExport(reimport.get(), tar);
  EXPECT_THAT(status, IsError());
  EXPECT_NE(status.message().find("schema mismatch"), std::string::npos)
      << status.message();
}

TEST_F(TraceProcessorIntegrationTest, PerfettoExportVersionMismatchRejected) {
  ASSERT_OK(LoadTrace("example_android_trace_30s.pb"));
  std::string tar =
      ExportToString(Processor(), TraceProcessor::ExportFormat::kPerfetto);

  size_t pos = tar.find("\"format\":1");
  ASSERT_NE(pos, std::string::npos);
  tar.replace(pos, 10, "\"format\":9");

  auto reimport = TraceProcessor::CreateInstance(Config());
  auto status = ParsePerfettoExport(reimport.get(), tar);
  EXPECT_THAT(status, IsError());
  EXPECT_NE(status.message().find("format"), std::string::npos)
      << status.message();
}

TEST_F(TraceProcessorIntegrationTest, PerfettoExportRowCountMismatchRejected) {
  ASSERT_OK(LoadTrace("example_android_trace_30s.pb"));
  std::string tar =
      ExportToString(Processor(), TraceProcessor::ExportFormat::kPerfetto);

  size_t pos = tar.find("\"row_count\":");
  ASSERT_NE(pos, std::string::npos);
  char& digit = tar[pos + 12];
  ASSERT_TRUE(digit >= '0' && digit <= '9');
  digit = digit == '9' ? '8' : static_cast<char>(digit + 1);

  auto reimport = TraceProcessor::CreateInstance(Config());
  EXPECT_THAT(ParsePerfettoExport(reimport.get(), tar), IsError());
}

TEST_F(TraceProcessorIntegrationTest, MultiLevelPackageInclude) {
  ASSERT_OK(NotifyEndOfFile());

  // Register a multi-level package "foo.bar" with module "foo.bar.baz"
  SqlPackage pkg;
  pkg.name = "foo.bar";
  pkg.modules.push_back(
      {"foo.bar.baz", "CREATE PERFETTO TABLE test_tbl AS SELECT 42 AS val"});
  ASSERT_OK(Processor()->RegisterSqlPackage(pkg));

  // INCLUDE should find the module via prefix matching
  auto it = Query("INCLUDE PERFETTO MODULE foo.bar.baz");
  ASSERT_FALSE(it.Next());
  ASSERT_OK(it.Status()) << it.Status().message();

  auto it2 = Query("SELECT val FROM test_tbl");
  ASSERT_TRUE(it2.Next()) << it2.Status().message();
  ASSERT_EQ(it2.Get(0).long_value, 42);
  ASSERT_FALSE(it2.Next());
  ASSERT_OK(it2.Status());
}

}  // namespace
}  // namespace perfetto::trace_processor
