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

#include "perfetto/base/build_config.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/read_trace.h"

#include "src/base/test/utils.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)
#include <zstd.h>
#endif

namespace perfetto {
namespace trace_processor {
namespace {

base::ScopedFstream OpenTestTrace(const std::string& path) {
  std::string full_path = base::GetTestDataPath(path);
  EXPECT_TRUE(base::FileExists(full_path)) << full_path;
  return base::OpenFstream(full_path, "r");
}

std::vector<uint8_t> ReadAllData(const base::ScopedFstream& f) {
  std::vector<uint8_t> raw_trace;
  while (!feof(*f)) {
    uint8_t buf[4096];
    auto rsize =
        fread(reinterpret_cast<char*>(buf), 1, base::ArraySize(buf), *f);
    raw_trace.insert(raw_trace.end(), buf, buf + rsize);
  }
  return raw_trace;
}

bool ZlibSupported() {
#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
  return true;
#else
  return false;
#endif
}

class ReadTraceIntegrationTest : public testing::Test {
  void SetUp() override {
    if (!ZlibSupported()) {
      GTEST_SKIP() << "Gzip not enabled";
    }
  }
};

TEST_F(ReadTraceIntegrationTest, CompressedTrace) {
  base::ScopedFstream f = OpenTestTrace("test/data/compressed.pb");
  std::vector<uint8_t> raw_trace = ReadAllData(f);

  std::vector<uint8_t> decompressed;
  decompressed.reserve(raw_trace.size());

  base::Status status = trace_processor::DecompressTrace(
      raw_trace.data(), raw_trace.size(), &decompressed);
  ASSERT_TRUE(status.ok());

  protos::pbzero::Trace::Decoder decoder(decompressed.data(),
                                         decompressed.size());
  uint32_t packet_count = 0;
  for (auto it = decoder.packet(); it; ++it) {
    protos::pbzero::TracePacket::Decoder packet(*it);
    ASSERT_FALSE(packet.has_compressed_packets());
    ++packet_count;
  }
  ASSERT_EQ(packet_count, 2412u);
}

TEST_F(ReadTraceIntegrationTest, NonProtobufShouldNotDecompress) {
  base::ScopedFstream f = OpenTestTrace("test/data/unsorted_trace.json");
  std::vector<uint8_t> raw_trace = ReadAllData(f);

  std::vector<uint8_t> decompressed;
  base::Status status = trace_processor::DecompressTrace(
      raw_trace.data(), raw_trace.size(), &decompressed);
  ASSERT_FALSE(status.ok());
}

TEST_F(ReadTraceIntegrationTest, OuterGzipDecompressTrace) {
  base::ScopedFstream f =
      OpenTestTrace("test/data/example_android_trace_30s.pb.gz");
  std::vector<uint8_t> raw_compressed_trace = ReadAllData(f);

  std::vector<uint8_t> decompressed;
  base::Status status = trace_processor::DecompressTrace(
      raw_compressed_trace.data(), raw_compressed_trace.size(), &decompressed);
  ASSERT_TRUE(status.ok());

  base::ScopedFstream u =
      OpenTestTrace("test/data/example_android_trace_30s.pb");
  std::vector<uint8_t> raw_trace = ReadAllData(u);

  ASSERT_EQ(decompressed.size(), raw_trace.size());
  ASSERT_EQ(decompressed, raw_trace);
}

TEST_F(ReadTraceIntegrationTest, DoubleGzipDecompressTrace) {
  base::ScopedFstream f = OpenTestTrace("test/data/compressed.pb.gz");
  std::vector<uint8_t> raw_compressed_trace = ReadAllData(f);

  std::vector<uint8_t> decompressed;
  base::Status status = trace_processor::DecompressTrace(
      raw_compressed_trace.data(), raw_compressed_trace.size(), &decompressed);
  ASSERT_TRUE(status.ok()) << status.message();

  protos::pbzero::Trace::Decoder decoder(decompressed.data(),
                                         decompressed.size());
  uint32_t packet_count = 0;
  for (auto it = decoder.packet(); it; ++it) {
    protos::pbzero::TracePacket::Decoder packet(*it);
    ASSERT_FALSE(packet.has_compressed_packets());
    ++packet_count;
  }
  ASSERT_EQ(packet_count, 2412u);
}

#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)
// End-to-end check of the zstd decode path: wrap a zstd-compressed packet
// stream in TracePacket.zstd_compressed_packets (as the tracing service emits)
// and assert DecompressTrace recovers the original packets.
TEST_F(ReadTraceIntegrationTest, ZstdCompressedPackets) {
  // The framed packet stream is itself a valid Trace proto.
  constexpr uint64_t kTimestamps[] = {1000, 2000, 3000};
  protozero::HeapBuffered<protos::pbzero::Trace> inner;
  for (uint64_t ts : kTimestamps) {
    inner->add_packet()->set_timestamp(ts);
  }
  std::vector<uint8_t> inner_bytes = inner.SerializeAsArray();

  size_t bound = ZSTD_compressBound(inner_bytes.size());
  std::vector<uint8_t> frame(bound);
  size_t frame_size = ZSTD_compress(frame.data(), bound, inner_bytes.data(),
                                    inner_bytes.size(), /*level=*/3);
  ASSERT_FALSE(ZSTD_isError(frame_size));
  frame.resize(frame_size);

  protozero::HeapBuffered<protos::pbzero::Trace> outer;
  outer->add_packet()->set_zstd_compressed_packets(frame.data(), frame.size());
  std::vector<uint8_t> outer_bytes = outer.SerializeAsArray();

  std::vector<uint8_t> decompressed;
  base::Status status = trace_processor::DecompressTrace(
      outer_bytes.data(), outer_bytes.size(), &decompressed);
  ASSERT_TRUE(status.ok()) << status.message();

  protos::pbzero::Trace::Decoder decoder(decompressed.data(),
                                         decompressed.size());
  std::vector<uint64_t> got;
  for (auto it = decoder.packet(); it; ++it) {
    protos::pbzero::TracePacket::Decoder packet(*it);
    ASSERT_FALSE(packet.has_compressed_packets());
    got.push_back(packet.timestamp());
  }
  EXPECT_THAT(got, testing::ElementsAre(1000u, 2000u, 3000u));
}

// A corrupt zstd frame in zstd_compressed_packets must surface as an error, not
// a silent partial/empty decode.
TEST_F(ReadTraceIntegrationTest, ZstdCorruptCompressedPacketsFails) {
  const uint8_t kGarbage[] = {0x28, 0xb5, 0x2f, 0xfd, 0x01, 0x02, 0x03, 0x04};
  protozero::HeapBuffered<protos::pbzero::Trace> outer;
  outer->add_packet()->set_zstd_compressed_packets(kGarbage, sizeof(kGarbage));
  std::vector<uint8_t> outer_bytes = outer.SerializeAsArray();

  std::vector<uint8_t> decompressed;
  base::Status status = trace_processor::DecompressTrace(
      outer_bytes.data(), outer_bytes.size(), &decompressed);
  ASSERT_FALSE(status.ok());
}
#endif  // PERFETTO_BUILDFLAG(PERFETTO_ZSTD)

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
