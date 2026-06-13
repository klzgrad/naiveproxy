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

#include "perfetto/ext/traceconv/traceconv.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/temp_file.h"
#include "protos/perfetto/trace/profiling/deobfuscation.gen.h"
#include "protos/perfetto/trace/trace.gen.h"
#include "protos/perfetto/trace/trace_packet.gen.h"
#include "src/base/test/utils.h"
#include "test/gtest_and_gmock.h"

namespace perfetto::traceconv {
namespace {

using testing::UnorderedElementsAre;

// Helper: builds an argv from owned strings and invokes TraceconvMain.
class ArgvInvoker {
 public:
  void Add(const std::string& arg) { args_.push_back(arg); }
  int Run() {
    std::vector<char*> argv;
    for (auto& s : args_) {
      argv.push_back(s.data());
    }
    return TraceconvMain(static_cast<int>(argv.size()), argv.data());
  }

 private:
  std::vector<std::string> args_;
};

base::TempFile WriteTempFile(const std::string& content) {
  auto f = base::TempFile::Create();
  PERFETTO_CHECK(base::WriteAll(f.fd(), content.data(), content.size()) ==
                 static_cast<ssize_t>(content.size()));
  return f;
}

// Parses a USTAR archive into a map of filename -> file content.
// USTAR layout: each file is a 512-byte header (name at offset 0, size as
// octal ASCII at offset 124) followed by the content padded to 512 bytes;
// the archive ends with at least two zero-filled blocks.
std::map<std::string, std::string> ReadTarMembers(const std::string& path) {
  std::string bytes;
  PERFETTO_CHECK(base::ReadFile(path, &bytes));
  std::map<std::string, std::string> out;
  size_t pos = 0;
  while (pos + 512 <= bytes.size()) {
    const char* header = bytes.data() + pos;
    // Zero-name header -> end-of-archive marker.
    if (header[0] == '\0') {
      break;
    }
    std::string name(header, strnlen(header, 100));
    // Size is null/space terminated octal ASCII at offset 124, up to 11 digits.
    char size_buf[13] = {};
    memcpy(size_buf, header + 124, 12);
    size_t size = static_cast<size_t>(strtoul(size_buf, nullptr, 8));
    pos += 512;
    PERFETTO_CHECK(pos + size <= bytes.size());
    out.emplace(std::move(name), bytes.substr(pos, size));
    // Content is padded to a 512-byte boundary.
    pos += ((size + 511) / 512) * 512;
  }
  return out;
}

class TraceconvBundleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    input_trace_ = base::GetTestDataPath(
        "test/data/heapprofd_standalone_client_example-trace");
    output_path_ = temp_dir_.path() + "/bundle.tar";
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    GTEST_SKIP() << "do not run traceconv tests on Android target";
#endif
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    GTEST_SKIP() << "TarWriter is not supported on Windows";
#endif
  }

  void TearDown() override { remove(output_path_.c_str()); }

  // Collects every package_name found in a deobfuscation.pb proto stream.
  static std::set<std::string> PackageNames(const std::string& deob_bytes) {
    protos::gen::Trace trace;
    PERFETTO_CHECK(trace.ParseFromString(deob_bytes));
    std::set<std::string> names;
    for (const auto& pkt : trace.packet()) {
      if (pkt.has_deobfuscation_mapping()) {
        names.insert(pkt.deobfuscation_mapping().package_name());
      }
    }
    return names;
  }

  base::TempDir temp_dir_ = base::TempDir::Create();
  std::string input_trace_;
  std::string output_path_;
};

// The bundle should contain the unmodified input trace plus a parseable
// deobfuscation.pb whose `deobfuscation_mapping` reflects the supplied
// mapping.txt (package, class, method names).
TEST_F(TraceconvBundleTest, BundleWithProguardMap) {
  base::TempFile mapping = WriteTempFile(
      "com.example.Foo -> a.a:\n"
      "    void bar() -> b\n");

  ArgvInvoker invoker;
  invoker.Add("traceconv");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add("--proguard-map");
  invoker.Add("com.example=" + mapping.path());
  invoker.Add(input_trace_);
  invoker.Add(output_path_);

  ASSERT_EQ(invoker.Run(), 0);

  auto members = ReadTarMembers(output_path_);
  ASSERT_EQ(members.size(), 2u);

  // trace.perfetto must be a byte-for-byte copy of the input trace.
  std::string input_bytes;
  ASSERT_TRUE(base::ReadFile(input_trace_, &input_bytes));
  auto trace_it = members.find("trace.perfetto");
  ASSERT_NE(trace_it, members.end());
  EXPECT_EQ(trace_it->second, input_bytes);

  // deobfuscation.pb parses as a Trace proto containing one
  // DeobfuscationMapping for package "com.example" with our class + method.
  auto deob_it = members.find("deobfuscation.pb");
  ASSERT_NE(deob_it, members.end());
  protos::gen::Trace deob;
  ASSERT_TRUE(deob.ParseFromString(deob_it->second));
  ASSERT_EQ(deob.packet().size(), 1u);
  ASSERT_TRUE(deob.packet()[0].has_deobfuscation_mapping());
  const auto& dm = deob.packet()[0].deobfuscation_mapping();
  EXPECT_EQ(dm.package_name(), "com.example");
  ASSERT_EQ(dm.obfuscated_classes().size(), 1u);
  const auto& cls = dm.obfuscated_classes()[0];
  EXPECT_EQ(cls.obfuscated_name(), "a.a");
  EXPECT_EQ(cls.deobfuscated_name(), "com.example.Foo");
  ASSERT_EQ(cls.obfuscated_methods().size(), 1u);
  EXPECT_EQ(cls.obfuscated_methods()[0].obfuscated_name(), "b");
}

// Repeating --proguard-map should produce one DeobfuscationMapping per input
// map, each tagged with the right package name.
TEST_F(TraceconvBundleTest, BundleWithRepeatedProguardMaps) {
  base::TempFile map1 = WriteTempFile("com.example.Foo -> a.a:\n");
  base::TempFile map2 = WriteTempFile("com.example.Bar -> b.b:\n");

  ArgvInvoker invoker;
  invoker.Add("traceconv");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add("--proguard-map");
  invoker.Add("com.example.one=" + map1.path());
  invoker.Add("--proguard-map");
  invoker.Add("com.example.two=" + map2.path());
  invoker.Add(input_trace_);
  invoker.Add(output_path_);

  ASSERT_EQ(invoker.Run(), 0);

  auto members = ReadTarMembers(output_path_);
  ASSERT_TRUE(members.count("deobfuscation.pb"));
  EXPECT_THAT(PackageNames(members["deobfuscation.pb"]),
              UnorderedElementsAre("com.example.one", "com.example.two"));
}

// --proguard-map without `pkg=` is accepted; the package name ends up empty
// in the emitted mapping but the class mapping is still present.
TEST_F(TraceconvBundleTest, BundleWithProguardMapNoPackage) {
  base::TempFile mapping = WriteTempFile("com.example.Foo -> a.a:\n");

  ArgvInvoker invoker;
  invoker.Add("traceconv");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add("--proguard-map");
  invoker.Add(mapping.path());
  invoker.Add(input_trace_);
  invoker.Add(output_path_);

  ASSERT_EQ(invoker.Run(), 0);

  auto members = ReadTarMembers(output_path_);
  ASSERT_TRUE(members.count("deobfuscation.pb"));
  protos::gen::Trace deob;
  ASSERT_TRUE(deob.ParseFromString(members["deobfuscation.pb"]));
  ASSERT_EQ(deob.packet().size(), 1u);
  const auto& dm = deob.packet()[0].deobfuscation_mapping();
  EXPECT_EQ(dm.package_name(), "");
  ASSERT_EQ(dm.obfuscated_classes().size(), 1u);
  EXPECT_EQ(dm.obfuscated_classes()[0].deobfuscated_name(), "com.example.Foo");
}

// Explicit --proguard-map pointing at a missing file must fail the command.
TEST_F(TraceconvBundleTest, BundleWithMissingProguardMapFails) {
  ArgvInvoker invoker;
  invoker.Add("traceconv");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add("--proguard-map");
  invoker.Add("com.example=/nonexistent/mapping.txt");
  invoker.Add(input_trace_);
  invoker.Add(output_path_);

  EXPECT_NE(invoker.Run(), 0);
}

// --proguard-map with no following argument is a usage error.
TEST_F(TraceconvBundleTest, BundleProguardMapMissingArgFails) {
  ArgvInvoker invoker;
  invoker.Add("traceconv");
  invoker.Add("bundle");
  invoker.Add("--proguard-map");

  EXPECT_NE(invoker.Run(), 0);
}

// With --no-auto-proguard-maps, an explicit --proguard-map still propagates
// and produces a DeobfuscationMapping for the specified package. (In the
// test environment there are no Gradle layouts to auto-discover either
// way, so this asserts the explicit path keeps working.)
TEST_F(TraceconvBundleTest, BundleNoAutoProguardMapsWithExplicit) {
  base::TempFile mapping = WriteTempFile("com.example.Foo -> a.a:\n");

  ArgvInvoker invoker;
  invoker.Add("traceconv");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add("--no-auto-proguard-maps");
  invoker.Add("--proguard-map");
  invoker.Add("com.example=" + mapping.path());
  invoker.Add(input_trace_);
  invoker.Add(output_path_);

  ASSERT_EQ(invoker.Run(), 0);

  auto members = ReadTarMembers(output_path_);
  ASSERT_TRUE(members.count("deobfuscation.pb"));
  EXPECT_THAT(PackageNames(members["deobfuscation.pb"]),
              UnorderedElementsAre("com.example"));
}

}  // namespace
}  // namespace perfetto::traceconv
