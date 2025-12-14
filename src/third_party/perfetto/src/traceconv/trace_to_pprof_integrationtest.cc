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

#include "test/gtest_and_gmock.h"

#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/base/test/utils.h"
#include "src/traceconv/pprof_reader.h"
#include "src/traceconv/trace_to_profile.h"

namespace perfetto {
namespace {

using testing::Contains;

pprof::PprofProfileReader ConvertTraceToPprof(
    const std::string& input_file_name) {
  const std::string trace_file = base::GetTestDataPath(input_file_name);
  std::ifstream file_istream;
  file_istream.open(trace_file, std::ios_base::in | std::ios_base::binary);
  PERFETTO_CHECK(file_istream.is_open());

  std::stringstream ss;
  std::ostream os(ss.rdbuf());
  trace_to_text::TraceToJavaHeapProfile(&file_istream, &os, /*pid=*/0,
                                        /*timestamps=*/{},
                                        /*annotate_frames=*/false);

  auto conv_stdout = base::SplitString(ss.str(), " ");
  PERFETTO_CHECK(!conv_stdout.empty());
  std::string out_dirname = base::TrimWhitespace(conv_stdout.back());
  std::vector<std::string> filenames;
  base::ListFilesRecursive(out_dirname, filenames);
  // assumption: all test inputs contain exactly one profile
  PERFETTO_CHECK(filenames.size() == 1);
  std::string profile_path = out_dirname + "/" + filenames[0];

  // read in the profile contents and then clean up the temp files
  pprof::PprofProfileReader pprof_reader(profile_path);
  unlink(profile_path.c_str());
  PERFETTO_CHECK(base::Rmdir(out_dirname));
  return pprof_reader;
}

std::vector<std::vector<std::string>> get_samples_function_names(
    const pprof::PprofProfileReader& pprof,
    const std::string& last_function_name) {
  const auto samples = pprof.get_samples(last_function_name);
  std::vector<std::vector<std::string>> samples_function_names;
  for (const auto& sample : samples) {
    samples_function_names.push_back(pprof.get_sample_function_names(sample));
  }
  return samples_function_names;
}

class TraceToPprofTest : public ::testing::Test {
 public:
  pprof::PprofProfileReader* pprof = nullptr;

  void SetUp() override {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    GTEST_SKIP() << "do not run traceconv tests on Android target";
#endif
  }

  void TearDown() override { delete pprof; }
};

TEST_F(TraceToPprofTest, SummaryValues) {
  const auto pprof = ConvertTraceToPprof("test/data/heap_graph/heap_graph.pb");

  EXPECT_EQ(pprof.get_samples_value_sum("Foo", "Total allocation count"), 1);
  EXPECT_EQ(pprof.get_samples_value_sum("Foo", "Total allocation size"), 32);
  EXPECT_EQ(pprof.get_samples("Foo").size(), 1U);
  EXPECT_EQ(pprof.get_sample_count(), 3U);

  const std::vector<std::string> expected_function_names = {
      "Foo", "FactoryProducerDelegateImplActor [ROOT_JAVA_FRAME]"};
  EXPECT_THAT(get_samples_function_names(pprof, "Foo"),
              Contains(expected_function_names));
}

TEST_F(TraceToPprofTest, TreeLocationFunctionNames) {
  const auto pprof =
      ConvertTraceToPprof("test/data/heap_graph/heap_graph_branching.pb");

  EXPECT_THAT(get_samples_function_names(pprof, "LeftChild0"),
              Contains(std::vector<std::string>{"LeftChild0",
                                                "RootNode [ROOT_JAVA_FRAME]"}));
  EXPECT_THAT(get_samples_function_names(pprof, "LeftChild1"),
              Contains(std::vector<std::string>{"LeftChild1", "LeftChild0",
                                                "RootNode [ROOT_JAVA_FRAME]"}));
  EXPECT_THAT(get_samples_function_names(pprof, "RightChild0"),
              Contains(std::vector<std::string>{"RightChild0",
                                                "RootNode [ROOT_JAVA_FRAME]"}));
  EXPECT_THAT(get_samples_function_names(pprof, "RightChild1"),
              Contains(std::vector<std::string>{"RightChild1", "RightChild0",
                                                "RootNode [ROOT_JAVA_FRAME]"}));
}

TEST_F(TraceToPprofTest, HugeSizes) {
  const auto pprof =
      ConvertTraceToPprof("test/data/heap_graph/heap_graph_huge_size.pb");
  EXPECT_EQ(pprof.get_samples_value_sum("dev.perfetto.BigStuff",
                                        "Total allocation size"),
            3000000000);
}

class TraceToPprofRealTraceTest : public ::testing::Test {
 public:
  void SetUp() override {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
    GTEST_SKIP() << "do not run traceconv tests on Android target";
#endif
#if defined(LEAK_SANITIZER)
    GTEST_SKIP() << "trace is too big to be tested in sanitizer builds";
#endif
  }
};

TEST_F(TraceToPprofRealTraceTest, AllocationCountForClass) {
  const auto pprof =
      ConvertTraceToPprof("test/data/system-server-heap-graph-new.pftrace");

  EXPECT_EQ(pprof.get_samples_value_sum(
                "android.content.pm.parsing.component.ParsedActivity",
                "Total allocation count"),
            5108);
  EXPECT_EQ(pprof.get_samples_value_sum(
                "android.content.pm.parsing.component.ParsedActivity",
                "Total allocation size"),
            817280);
  EXPECT_EQ(
      pprof.get_samples("android.content.pm.parsing.component.ParsedActivity")
          .size(),
      5U);
  EXPECT_EQ(pprof.get_sample_count(), 83256U);

  const std::vector<std::string> expected_function_names = {
      "android.content.pm.parsing.component.ParsedActivity",
      "java.lang.Object[]",
      "java.util.ArrayList",
      "com.android.server.pm.parsing.pkg.PackageImpl",
      "com.android.server.pm.PackageSetting",
      "java.lang.Object[]",
      "android.util.ArrayMap",
      "com.android.server.pm.Settings",
      "com.android.server.pm.PackageManagerService [ROOT_JNI_GLOBAL]"};

  EXPECT_THAT(get_samples_function_names(
                  pprof, "android.content.pm.parsing.component.ParsedActivity"),
              Contains(expected_function_names));
}

}  // namespace
}  // namespace perfetto
