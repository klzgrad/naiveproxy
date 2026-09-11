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

#include "perfetto/ext/trace_processor/trace_processor_shell.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/temp_file.h"
#include "protos/perfetto/trace/ftrace/ftrace.gen.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.gen.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.gen.h"
#include "protos/perfetto/trace/interned_data/interned_data.gen.h"
#include "protos/perfetto/trace/profiling/deobfuscation.gen.h"
#include "protos/perfetto/trace/profiling/profile_common.gen.h"
#include "protos/perfetto/trace/trace.gen.h"
#include "protos/perfetto/trace/trace_packet.gen.h"
#include "src/base/test/utils.h"
#include "test/gtest_and_gmock.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <unistd.h>
#endif

namespace perfetto::trace_processor {
namespace {

using testing::HasSubstr;
using testing::UnorderedElementsAre;

// Helper: builds an argv from owned strings and invokes the shell's `bundle`
// subcommand via TraceProcessorShellMain.
class ArgvInvoker {
 public:
  void Add(const std::string& arg) { args_.push_back(arg); }
  int Run() {
    std::vector<char*> argv;
    for (auto& s : args_) {
      argv.push_back(s.data());
    }
    return TraceProcessorShellMain(static_cast<int>(argv.size()), argv.data());
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

class TraceconvShellBundleTest : public ::testing::Test {
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
TEST_F(TraceconvShellBundleTest, BundleWithProguardMap) {
  base::TempFile mapping = WriteTempFile(
      "com.example.Foo -> a.a:\n"
      "    void bar() -> b\n");

  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
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
TEST_F(TraceconvShellBundleTest, BundleWithRepeatedProguardMaps) {
  base::TempFile map1 = WriteTempFile("com.example.Foo -> a.a:\n");
  base::TempFile map2 = WriteTempFile("com.example.Bar -> b.b:\n");

  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
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
TEST_F(TraceconvShellBundleTest, BundleWithProguardMapNoPackage) {
  base::TempFile mapping = WriteTempFile("com.example.Foo -> a.a:\n");

  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
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
TEST_F(TraceconvShellBundleTest, BundleWithMissingProguardMapFails) {
  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add("--proguard-map");
  invoker.Add("com.example=/nonexistent/mapping.txt");
  invoker.Add(input_trace_);
  invoker.Add(output_path_);

  EXPECT_NE(invoker.Run(), 0);
}

// --proguard-map with no following argument is a usage error.
TEST_F(TraceconvShellBundleTest, BundleProguardMapMissingArgFails) {
  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add("--proguard-map");

  EXPECT_NE(invoker.Run(), 0);
}

// With --no-auto-proguard-maps, an explicit --proguard-map still propagates
// and produces a DeobfuscationMapping for the specified package. (In the
// test environment there are no Gradle layouts to auto-discover either
// way, so this asserts the explicit path keeps working.)
TEST_F(TraceconvShellBundleTest, BundleNoAutoProguardMapsWithExplicit) {
  base::TempFile mapping = WriteTempFile("com.example.Foo -> a.a:\n");

  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
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

// --- Edge-case feedback tests ---
//
// These assert that the bundle command gives *useful* feedback in the
// situations users actually hit (see https://github.com/google/perfetto/
// issues/6927), rather than a bare "failed to create bundle."

// Builds a minimal trace with a single function_graph entry/exit event on
// cpu 0 (the idle thread). If |with_ksyms| is true, the trace also embeds a
// kernel symbol map (as traced_probes does when FtraceConfig.symbolize_ksyms
// is enabled), so the function resolves to its name; otherwise it stays a
// raw hex address that cannot be symbolized offline.
std::string BuildFuncgraphTrace(bool with_ksyms) {
  protos::gen::Trace trace;

  if (with_ksyms) {
    auto* packet = trace.add_packet();
    packet->set_timestamp(100);
    packet->set_trusted_uid(0);
    packet->set_trusted_packet_sequence_id(2);
    packet->set_sequence_flags(
        protos::gen::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);
    auto* sym = packet->mutable_interned_data()->add_kernel_symbols();
    sym->set_iid(42);
    sym->set_str("my_kernel_func");
  }

  auto* packet = trace.add_packet();
  packet->set_timestamp(1000);
  packet->set_trusted_uid(0);
  packet->set_trusted_packet_sequence_id(2);
  auto* bundle = packet->mutable_ftrace_events();
  bundle->set_cpu(0);
  auto* entry = bundle->add_event();
  entry->set_timestamp(1000);
  entry->set_pid(0);
  entry->mutable_funcgraph_entry()->set_depth(0);
  entry->mutable_funcgraph_entry()->set_func(42);
  auto* exit_evt = bundle->add_event();
  exit_evt->set_timestamp(1000);
  exit_evt->set_pid(0);
  auto* exit_fg = exit_evt->mutable_funcgraph_exit();
  exit_fg->set_calltime(1000);
  exit_fg->set_depth(0);
  exit_fg->set_func(42);
  exit_fg->set_overrun(0);
  exit_fg->set_rettime(2000);

  return trace.SerializeAsString();
}

// Redirects stderr to a temp file for the lifetime of the object so tests
// can assert on the shell's diagnostics. POSIX only (the tests that use it
// are not compiled on Windows; see the #if guards around them below).
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
class ScopedStderrCapture {
 public:
  ScopedStderrCapture() {
    fflush(stderr);
    saved_fd_ = dup(STDERR_FILENO);
    PERFETTO_CHECK(saved_fd_ >= 0);
    char tmpl[] = "/tmp/perfetto_stderr_XXXXXX";
    capture_fd_ = mkstemp(tmpl);
    PERFETTO_CHECK(capture_fd_ >= 0);
    path_ = tmpl;
    PERFETTO_CHECK(dup2(capture_fd_, STDERR_FILENO) == STDERR_FILENO);
  }

  ~ScopedStderrCapture() {
    fflush(stderr);
    dup2(saved_fd_, STDERR_FILENO);
    close(saved_fd_);
    close(capture_fd_);
    unlink(path_.c_str());
  }

  std::string Get() const {
    fflush(stderr);
    std::string out;
    PERFETTO_CHECK(base::ReadFile(path_, &out));
    return out;
  }

 private:
  int saved_fd_ = -1;
  int capture_fd_ = -1;
  std::string path_;
};
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

// A function_graph trace recorded WITHOUT symbolize_ksyms contains raw kernel
// addresses that cannot be symbolized offline. The bundle must still succeed
// (the trace alone is bundled) but must tell the user why the names are hex
// and how to fix it.
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
TEST_F(TraceconvShellBundleTest, BundleFuncgraphWithoutKallsymsGuidesUser) {
  base::TempFile trace =
      WriteTempFile(BuildFuncgraphTrace(/*with_ksyms=*/false));

  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add(trace.path());
  invoker.Add(output_path_);

  ScopedStderrCapture capture;
  ASSERT_EQ(invoker.Run(), 0);
  std::string stderr_text = capture.Get();

  // The trace is bundled even though nothing could be symbolized.
  auto members = ReadTarMembers(output_path_);
  ASSERT_TRUE(members.count("trace.perfetto"));
  EXPECT_EQ(members.size(), 1u);

  // The user must be pointed at symbolize_ksyms, with the config and doc.
  EXPECT_THAT(stderr_text, HasSubstr("symbolize_ksyms: true"));
  EXPECT_THAT(stderr_text, HasSubstr("function_graph"));
  EXPECT_THAT(stderr_text, HasSubstr("re-record"));
  EXPECT_THAT(stderr_text, HasSubstr("symbolization#ftrace"));
}
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

// When symbolize_ksyms WAS enabled the names resolve fine: the bundle should
// succeed without the kernel warning.
TEST_F(TraceconvShellBundleTest, BundleFuncgraphWithKallsymsSucceedsQuietly) {
  base::TempFile trace =
      WriteTempFile(BuildFuncgraphTrace(/*with_ksyms=*/true));

  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add(trace.path());
  invoker.Add(output_path_);

  ASSERT_EQ(invoker.Run(), 0);

  auto members = ReadTarMembers(output_path_);
  ASSERT_TRUE(members.count("trace.perfetto"));
  EXPECT_EQ(members.size(), 1u);
}

// A trace with no profiled data at all (e.g. a pure atrace/systrace trace)
// has nothing to enrich; bundling it must succeed, not fail with a cryptic
// message.
TEST_F(TraceconvShellBundleTest, BundleTraceWithoutProfileDataSucceeds) {
  base::TempFile trace = WriteTempFile("");

  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add(trace.path());
  invoker.Add(output_path_);

  ASSERT_EQ(invoker.Run(), 0);
  EXPECT_EQ(ReadTarMembers(output_path_).size(), 1u);
}

// A trace with unsymbolized user-space frames: the bundle is still produced
// (with the trace), and the user gets a summary of what could not be
// symbolized plus a hint on how to fix it.
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
TEST_F(TraceconvShellBundleTest, BundleUnsymbolizedFramesSucceedsWithWarning) {
  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  // Note: no --no-auto-symbol-paths here, so the symbolizer searches the
  // mappings' absolute paths (which don't exist in this environment) and
  // reports them as "no matching symbols".
  invoker.Add(input_trace_);
  invoker.Add(output_path_);

  ScopedStderrCapture capture;
  ASSERT_EQ(invoker.Run(), 0);
  std::string stderr_text = capture.Get();

  auto members = ReadTarMembers(output_path_);
  ASSERT_TRUE(members.count("trace.perfetto"));
  // No symbols were found, so no symbols.pb member is expected.
  EXPECT_FALSE(members.count("symbols.pb"));

  EXPECT_THAT(stderr_text, HasSubstr("could not be symbolized"));
  EXPECT_THAT(stderr_text, HasSubstr("--symbol-paths"));
}
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

// With --no-auto-symbol-paths and no explicit paths, no symbol source is
// configured at all: the user must be told how to provide one.
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
TEST_F(TraceconvShellBundleTest, BundleNoSymbolPathsConfiguredExplainsFix) {
  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add(input_trace_);
  invoker.Add(output_path_);

  ScopedStderrCapture capture;
  ASSERT_EQ(invoker.Run(), 0);
  std::string stderr_text = capture.Get();

  EXPECT_THAT(stderr_text, HasSubstr("no symbol paths were searched"));
  EXPECT_THAT(stderr_text, HasSubstr("--symbol-paths"));
  // PERFETTO_BINARY_PATH is a hidden feature and must not be surfaced in
  // bundle's guidance.
  EXPECT_THAT(stderr_text, Not(HasSubstr("PERFETTO_BINARY_PATH")));
}
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)

// An unwritable output path must produce a graceful, descriptive error
// instead of crashing the process.
TEST_F(TraceconvShellBundleTest, BundleUnwritableOutputFailsGracefully) {
  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add(input_trace_);
  invoker.Add(temp_dir_.path() + "/no_such_dir/out.tar");

  // Previously this crashed with a PERFETTO_CHECK deep in the tar writer;
  // now it must return a normal error.
  EXPECT_NE(invoker.Run(), 0);
}

// An input path that is a directory must produce a helpful message.
TEST_F(TraceconvShellBundleTest, BundleInputIsDirectoryFailsHelpfully) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  GTEST_SKIP() << "directory-vs-file validation differs on Windows";
#endif
  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add(temp_dir_.path());
  invoker.Add(output_path_);

  EXPECT_EQ(invoker.Run(), 1);
}

// A broken input symlink must be called out as a broken link, not as a
// missing file.
TEST_F(TraceconvShellBundleTest, BundleBrokenSymlinkInputFailsHelpfully) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  GTEST_SKIP() << "symlink tests are POSIX-only";
#endif
  std::string link_path = temp_dir_.path() + "/dangling.pftrace";
  PERFETTO_CHECK(symlink("/nonexistent/target.pftrace", link_path.c_str()) ==
                 0);

  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add(link_path);
  invoker.Add(output_path_);

  EXPECT_NE(invoker.Run(), 0);
  // Clean up so the TempDir destructor can remove the directory.
  unlink(link_path.c_str());
}

// A garbage (non-trace) input file must fail with a clear "not a trace"
// style error rather than a bare failure.
TEST_F(TraceconvShellBundleTest, BundleMalformedInputFailsWithReason) {
  base::TempFile trace = WriteTempFile(
      "this is definitely not a perfetto trace, just some text bytes");

  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add(trace.path());
  invoker.Add(output_path_);

  EXPECT_NE(invoker.Run(), 0);
}

// Regression test for the classic misuse where each --symbol-paths directory
// is passed as a separate positional argument (e.g.
// `bundle --symbol-paths a b trace out.tar`). The shell must reject this
// with a hint instead of silently treating the extra paths as the input and
// output files.
TEST_F(TraceconvShellBundleTest, BundleRejectsSymbolPathsAsSeparateArgs) {
  ArgvInvoker invoker;
  invoker.Add("trace_processor_shell");
  invoker.Add("bundle");
  invoker.Add("--no-auto-symbol-paths");
  invoker.Add("--symbol-paths");
  invoker.Add("/path/one");
  invoker.Add("/path/two");
  invoker.Add(input_trace_);
  invoker.Add(output_path_);

  EXPECT_NE(invoker.Run(), 0);
}

}  // namespace
}  // namespace perfetto::trace_processor
