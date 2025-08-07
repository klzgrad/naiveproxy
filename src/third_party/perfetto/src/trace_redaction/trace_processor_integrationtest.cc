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

#include "perfetto/trace_processor/trace_processor.h"
#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "test/gtest_and_gmock.h"

namespace perfetto::trace_redaction {
namespace {
constexpr auto kTrace = "test/data/trace-redaction-api-capture.pftrace";

constexpr auto kPackageName = "com.prefabulated.touchlatency";
constexpr auto kPackageUid = 10020;
}  // namespace

class AfterRedactionIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    SetSourceTrace(kTrace);

    trace_processor::Config tp_config;
    trace_processor_ =
        trace_processor::TraceProcessor::CreateInstance(tp_config);

    TraceRedactor::Config tr_config;
    auto trace_redactor = TraceRedactor::CreateInstance(tr_config);

    Context context;
    context.package_name = kPackageName;

    ASSERT_OK(Redact(*trace_redactor, &context));

    auto raw = LoadRedacted();
    ASSERT_OK(raw);

    auto read_buffer = std::make_unique<uint8_t[]>(raw->size());
    memcpy(read_buffer.get(), raw->data(), raw->size());

    ASSERT_OK(trace_processor_->Parse(std::move(read_buffer), raw->size()));
    ASSERT_OK(trace_processor_->NotifyEndOfFile());
  }

  std::unique_ptr<trace_processor::TraceProcessor> trace_processor_;
};

// After redaction, the only package remaining in the package list should be the
// target package.
TEST_F(AfterRedactionIntegrationTest, FindsCorrectUid) {
  auto rows = trace_processor_->ExecuteQuery(
      "SELECT uid FROM package_list ORDER BY uid");

  ASSERT_TRUE(rows.Next());
  ASSERT_EQ(rows.Get(0).AsLong(), kPackageUid);

  ASSERT_FALSE(rows.Next());
  ASSERT_OK(rows.Status());
}

TEST_F(AfterRedactionIntegrationTest, CreatesThreadForEachCPU) {
  // There's a main thread, but it is not used (it's just there to create a
  // thread group). Exclude it so we get N threads instead of N+1.

  // This should yield a collection of size 1.
  std::string synth_process =
      "SELECT upid FROM process WHERE name='Other-Processes'";

  auto threads = trace_processor_->ExecuteQuery(
      "SELECT COUNT(tid) FROM thread WHERE upid IN (" + synth_process +
      ") AND NOT is_main_thread");

  auto cpus = trace_processor_->ExecuteQuery(
      "SELECT COUNT(DISTINCT cpu) FROM cpu_counter_track");

  ASSERT_TRUE(threads.Next());
  ASSERT_TRUE(cpus.Next());

  auto thread_count = threads.Get(0).AsLong();
  ASSERT_NE(thread_count, 0);

  auto cpu_count = threads.Get(0).AsLong();
  ASSERT_NE(cpu_count, 0);

  ASSERT_EQ(thread_count, cpu_count);

  ASSERT_FALSE(threads.Next());
  ASSERT_FALSE(cpus.Next());

  ASSERT_OK(threads.Status());
  ASSERT_OK(cpus.Status());
}

TEST_F(AfterRedactionIntegrationTest, ReducesProcesses) {
  auto processes = trace_processor_->ExecuteQuery(
      "SELECT pid, name FROM process ORDER BY pid");

  // PID      NAME
  // ======================================================
  // 0        NULL
  // 1        NULL
  // 863      NULL  <--- Zygote
  // 4524     com.prefabulated.touchlatency
  // 4194305  Other-Processes

  ASSERT_TRUE(processes.Next());
  ASSERT_EQ(processes.Get(0).AsLong(), 0);
  ASSERT_TRUE(processes.Get(1).is_null());

  ASSERT_TRUE(processes.Next());
  ASSERT_EQ(processes.Get(0).AsLong(), 1);
  ASSERT_TRUE(processes.Get(1).is_null());

  // Zygote
  ASSERT_TRUE(processes.Next());
  ASSERT_EQ(processes.Get(0).AsLong(), 863);
  ASSERT_TRUE(processes.Get(1).is_null());

  ASSERT_TRUE(processes.Next());
  ASSERT_EQ(processes.Get(0).AsLong(), 4524);
  ASSERT_STREQ(processes.Get(1).AsString(), kPackageName);

  ASSERT_TRUE(processes.Next());
  ASSERT_EQ(processes.Get(0).AsLong(), 4194305);
  ASSERT_STREQ(processes.Get(1).AsString(), "Other-Processes");
}

// Tests comparing the trace before and after redaction.
class BeforeAndAfterAfterIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    SetSourceTrace(kTrace);

    trace_processor::Config config;

    auto raw_before = LoadOriginal();
    ASSERT_OK(raw_before);
    trace_processor_before_ = CreateTraceProcessor(raw_before.value());

    TraceRedactor::Config tr_config;
    auto trace_redactor = TraceRedactor::CreateInstance(tr_config);

    Context context;
    context.package_name = kPackageName;

    Redact(*trace_redactor, &context);

    auto raw_after = LoadRedacted();
    ASSERT_OK(raw_after);
    trace_processor_after_ = CreateTraceProcessor(raw_after.value());
  }

  static std::unique_ptr<trace_processor::TraceProcessor> CreateTraceProcessor(
      std::string_view raw) {
    auto read_buffer = std::make_unique<uint8_t[]>(raw.size());
    memcpy(read_buffer.get(), raw.data(), raw.size());

    trace_processor::Config config;
    auto trace_processor =
        trace_processor::TraceProcessor::CreateInstance(config);

    auto parsed = trace_processor->Parse(std::move(read_buffer), raw.size());

    if (!parsed.ok()) {
      return nullptr;
    }
    if (auto status = trace_processor->NotifyEndOfFile(); !status.ok()) {
      return nullptr;
    }
    return trace_processor;
  }

  std::unique_ptr<trace_processor::TraceProcessor> trace_processor_before_;
  std::unique_ptr<trace_processor::TraceProcessor> trace_processor_after_;
};

TEST_F(BeforeAndAfterAfterIntegrationTest, KeepsAllTargetPackageThreads) {
  std::string package_name = kPackageName;

  // This should yield a collection of one.
  std::string packages =
      "SELECT uid FROM package_list WHERE package_name='" + package_name + "'";

  // This should yield a collection of one.
  std::string processes =
      "SELECT upid FROM process WHERE uid IN (" + packages + ")";

  // This should yield a collect of N where N is some non-zero integer.
  const std::string tid_query =
      "SELECT tid FROM thread WHERE upid IN (" + processes + ") ORDER BY tid";

  auto it_before = trace_processor_before_->ExecuteQuery(tid_query);
  auto it_after = trace_processor_after_->ExecuteQuery(tid_query);

  ASSERT_TRUE(it_before.Next());

  do {
    ASSERT_TRUE(it_after.Next());
    ASSERT_EQ(it_before.Get(0).AsLong(), it_after.Get(0).AsLong());
  } while (it_before.Next());

  ASSERT_FALSE(it_after.Next());

  ASSERT_OK(it_before.Status());
  ASSERT_OK(it_after.Status());
}

// There are two Zygotes on Android ('zygote', 'zygote64'). Modern device should
// have both, so we assume both are present in the unredacted trace. During
// redaction, all zygote information will be lost during the merge stage.
// However, since the target process references the zygote (ppid) a "ghost"
// process will appear in the process table.
class RedactedZygoteIntegrationTest
    : public BeforeAndAfterAfterIntegrationTest {
 protected:
  void SetUp() {
    BeforeAndAfterAfterIntegrationTest::SetUp();

    auto it_before = trace_processor_before_->ExecuteQuery(
        "SELECT pid FROM process WHERE name IN ('zygote', 'zygote64')");

    ASSERT_TRUE(it_before.Next());
    zygotes_[0] = it_before.Get(0).AsLong();

    ASSERT_TRUE(it_before.Next());
    zygotes_[1] = it_before.Get(0).AsLong();

    ASSERT_FALSE(it_before.Next());
    ASSERT_OK(it_before.Status());
  }

  // Creates a SQL statement that can be used AFTER a "WHERE" clause to test if
  // the process is a zygote processes. The caller is responsible for the
  // prefix (e.g. WHERE, AND, OR, etc.).
  std::string IsZygote() const {
    auto p = std::to_string(zygotes_.at(0));
    auto q = std::to_string(zygotes_.at(1));

    return "pid=" + p + " OR pid=" + q;
  }

  std::array<int64_t, 2> zygotes_;
};

TEST_F(RedactedZygoteIntegrationTest, KeepsOneZygote) {
  auto count = trace_processor_after_->ExecuteQuery(
      "SELECT COUNT(pid) FROM process WHERE " + IsZygote());

  ASSERT_TRUE(count.Next());
  ASSERT_EQ(count.Get(0).AsLong(), 1);
  ASSERT_FALSE(count.Next());
  ASSERT_OK(count.Status());
}

TEST_F(RedactedZygoteIntegrationTest, RemovesName) {
  auto names = trace_processor_after_->ExecuteQuery(
      "SELECT name FROM process WHERE " + IsZygote());

  ASSERT_TRUE(names.Next());
  ASSERT_TRUE(names.Get(0).is_null());
  ASSERT_FALSE(names.Next());
  ASSERT_OK(names.Status());
}

// After redaction, the only application left should be the target package.
// While an application can have multiple processes, there should one top level
// process that was forked by the zygote.
//
// WARNING: This test relies on an assumption: there is only be one instance of
// the application running. We know this assumption to be faulty as multiple
// profiles allow for multiple instances of the same package to be running.
// In redaction, we treat them all as a single instance. The test trace does not
// use multiple profiles, so this assumption hold for this trace.
TEST_F(RedactedZygoteIntegrationTest, OnlyReferencedByTargetPackage) {
  // To avoid collisions, trace processor quickly moves away from volatile
  // values like tid and pid to use globally stable values like upid and utid.
  // Because of this, we can't check if a process's parent is the zygote, we
  // need to convert the pid to a upid first.
  auto upids = "SELECT upid FROM process WHERE " + IsZygote();

  auto ppids = trace_processor_after_->ExecuteQuery(
      "SELECT COUNT(pid) FROM process WHERE parent_upid IN (" + upids + ")");

  ASSERT_TRUE(ppids.Next());
  ASSERT_EQ(ppids.Get(0).AsLong(), 1);
  ASSERT_FALSE(ppids.Next());
  ASSERT_OK(ppids.Status());
}

}  // namespace perfetto::trace_redaction
