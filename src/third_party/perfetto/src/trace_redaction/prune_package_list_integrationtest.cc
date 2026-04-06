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

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/status.h"
#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/find_package_uid.h"
#include "src/trace_redaction/prune_package_list.h"
#include "src/trace_redaction/trace_redaction_framework.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "src/trace_redaction/trace_redactor.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/android/packages_list.gen.h"
#include "protos/perfetto/trace/android/packages_list.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.gen.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_redaction {

class PrunePackageListIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    context_.package_name = kSomePackageName;

    trace_redactor_.emplace_collect<FindPackageUid>();
    trace_redactor_.emplace_transform<PrunePackageList>();
  }

  std::vector<protos::gen::PackagesList::PackageInfo> GetPackageInfo(
      const protos::pbzero::Trace::Decoder& trace) const {
    std::vector<protos::gen::PackagesList::PackageInfo> packages;

    for (auto packet_it = trace.packet(); packet_it; ++packet_it) {
      protos::pbzero::TracePacket::Decoder packet(*packet_it);

      if (!packet.has_packages_list()) {
        continue;
      }

      protos::pbzero::PackagesList::Decoder list(packet.packages_list());

      for (auto info = list.packages(); info; ++info) {
        auto& item = packages.emplace_back();
        item.ParseFromArray(info->data(), info->size());
      }
    }

    return packages;
  }

  std::vector<std::string> GetPackageNames(
      const protos::pbzero::Trace::Decoder& trace) const {
    std::vector<std::string> names;

    for (const auto& package : GetPackageInfo(trace)) {
      if (package.has_name()) {
        names.push_back(package.name());
      }
    }

    return names;
  }

  Context context_;
  TraceRedactor trace_redactor_;
};

// It is possible for two packages_list to appear in the trace. The
// find_package_uid will stop after the first one is found. Package uids are
// appear as n * 1,000,000 where n is some integer. It is also possible for two
// packages_list to contain copies of each other - for example
// "com.Unity.com.unity.multiplayer.samples.coop" appears in both packages_list.
TEST_F(PrunePackageListIntegrationTest, FindsPackageAndFiltersPackageList) {
  auto result = Redact(trace_redactor_, &context_);
  ASSERT_OK(result) << result.message();

  auto after_raw_trace = LoadRedacted();
  ASSERT_OK(after_raw_trace) << after_raw_trace.status().message();

  ASSERT_TRUE(context_.package_uid.has_value());
  ASSERT_EQ(*context_.package_uid, kSomePackageUid);

  protos::pbzero::Trace::Decoder redacted_trace(after_raw_trace.value());
  auto packages = GetPackageInfo(redacted_trace);

  ASSERT_EQ(packages.size(), 2u);

  for (const auto& package : packages) {
    ASSERT_TRUE(package.has_name());
    ASSERT_EQ(package.name(), kSomePackageName);

    ASSERT_TRUE(package.has_uid());
    ASSERT_EQ(NormalizeUid(package.uid()), kSomePackageUid);
  }
}

// It is possible for multiple packages to share a uid. The names will appears
// across multiple package lists. The only time the package name appears is in
// the package list, so there is no way to differentiate these packages (only
// the uid is used later), so each entry should remain.
TEST_F(PrunePackageListIntegrationTest, RetainsAllInstancesOfUid) {
  context_.package_name = "com.google.android.networkstack.tethering";

  auto result = Redact(trace_redactor_, &context_);
  ASSERT_OK(result) << result.message();

  auto after_raw_trace = LoadRedacted();
  ASSERT_OK(after_raw_trace) << after_raw_trace.status().message();

  protos::pbzero::Trace::Decoder redacted_trace(after_raw_trace.value());
  auto package_names = GetPackageNames(redacted_trace);

  std::vector<std::string> expected_package_names = {
      "com.google.android.cellbroadcastservice",
      "com.google.android.cellbroadcastservice",
      "com.google.android.networkstack",
      "com.google.android.networkstack",
      "com.google.android.networkstack.permissionconfig",
      "com.google.android.networkstack.permissionconfig",
      "com.google.android.networkstack.tethering",
      "com.google.android.networkstack.tethering",
  };

  // Sort to make compare possible.
  std::sort(expected_package_names.begin(), expected_package_names.end());
  std::sort(package_names.begin(), package_names.end());

  ASSERT_TRUE(std::equal(package_names.begin(), package_names.end(),
                         expected_package_names.begin(),
                         expected_package_names.end()));
}

}  // namespace perfetto::trace_redaction
