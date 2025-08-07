/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/base/test/status_matchers.h"
#include "src/trace_processor/trace_summary/summary.h"

#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/trace_summary/trace_summary.descriptor.h"
#include "src/trace_processor/util/descriptors.h"
#include "test/gtest_and_gmock.h"

namespace perfetto::trace_processor::summary {
namespace {

using ::testing::HasSubstr;

MATCHER_P(EqualsIgnoringWhitespace, param, "") {
  auto RemoveAllWhitespace = [](const std::string& input) {
    std::string result;
    result.reserve(input.length());
    std::copy_if(input.begin(), input.end(), std::back_inserter(result),
                 [](char c) { return !std::isspace(c); });
    return result;
  };
  return RemoveAllWhitespace(arg) == RemoveAllWhitespace(param);
}

MATCHER_P(HasSubstrIgnoringWhitespace, param, "") {
  auto RemoveAllWhitespace = [](const std::string& input) {
    std::string result;
    result.reserve(input.length());
    std::copy_if(input.begin(), input.end(), std::back_inserter(result),
                 [](char c) { return !std::isspace(c); });
    return result;
  };
  return RemoveAllWhitespace(arg).find(RemoveAllWhitespace(param)) !=
         std::string::npos;
}

class TraceSummaryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tp_ = TraceProcessor::CreateInstance(Config{});
    tp_->NotifyEndOfFile();
    pool_.AddFromFileDescriptorSet(kTraceSummaryDescriptor.data(),
                                   kTraceSummaryDescriptor.size());
  }

  base::StatusOr<std::string> RunSummarize(const std::string& spec_str) {
    TraceSummarySpecBytes spec;
    spec.ptr = reinterpret_cast<const uint8_t*>(spec_str.data());
    spec.size = spec_str.size();
    spec.format = TraceSummarySpecBytes::Format::kTextProto;

    std::vector<uint8_t> output;
    TraceSummaryOutputSpec output_spec;
    output_spec.format = TraceSummaryOutputSpec::Format::kTextProto;

    base::Status status =
        Summarize(tp_.get(), pool_, {}, {spec}, &output, output_spec);
    if (!status.ok()) {
      return status;
    }
    return std::string(output.begin(), output.end());
  }

  std::unique_ptr<TraceProcessor> tp_;
  DescriptorPool pool_;
};

TEST_F(TraceSummaryTest, DuplicateDimensionsErrorIfUnique) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_spec {
      id: "my_metric"
      value: "value"
      dimensions: "dim"
      query {
        sql {
          sql: "SELECT 'a' as dim, 1.0 as value UNION ALL SELECT 'a' as dim, 2.0 as value"
          column_names: "dim"
          column_names: "value"
        }
      }
      dimension_uniqueness: UNIQUE
    }
  )");
  ASSERT_FALSE(status_or_output.ok());
  EXPECT_THAT(
      status_or_output.status().message(),
      HasSubstr("Duplicate dimensions found for metric bundle 'my_metric'"));
}

TEST_F(TraceSummaryTest, DuplicateDimensionsNoErrorIfNotUnique) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_spec {
      id: "my_metric"
      value: "value"
      dimensions: "dim"
      query {
        sql {
          sql: "SELECT 'a' as dim, 1.0 as value UNION ALL SELECT 'a' as dim, 2.0 as value"
          column_names: "dim"
          column_names: "value"
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok());
}

TEST_F(TraceSummaryTest, SingleTemplateSpec) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      id_prefix: "my_metric"
      value_columns: "value"
      query {
        sql {
          sql: "SELECT 1.0 as value"
          column_names: "value"
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok());
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"my_metric_value\""));
}

TEST_F(TraceSummaryTest, MultiValueColumnTemplateSpec) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      id_prefix: "my_metric"
      value_columns: "value_a"
      value_columns: "value_b"
      query {
        sql {
          sql: "SELECT 1.0 as value_a, 2.0 as value_b"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok());
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"my_metric_value_a\""));
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"my_metric_value_b\""));
}

TEST_F(TraceSummaryTest, MultiTemplateSpec) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      id_prefix: "my_metric_a"
      value_columns: "value"
      query {
        sql {
          sql: "SELECT 1.0 as value"
          column_names: "value"
        }
      }
    }
    metric_template_spec {
      id_prefix: "my_metric_b"
      value_columns: "value"
      query {
        sql {
          sql: "SELECT 1.0 as value"
          column_names: "value"
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok());
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"my_metric_a_value\""));
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"my_metric_b_value\""));
}

TEST_F(TraceSummaryTest, EmptyIdPrefixTemplateSpec) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      value_columns: "value"
      query {
        sql {
          sql: "SELECT 1.0 as value"
          column_names: "value"
        }
      }
    }
  )");
  ASSERT_FALSE(status_or_output.ok());
  EXPECT_THAT(status_or_output.status().message(),
              HasSubstr("Metric template with empty id_prefix field"));
}

TEST_F(TraceSummaryTest, DuplicateMetricIdFromTemplate) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_spec {
      id: "my_metric_value"
      value: "value"
      query {
        sql {
          sql: "SELECT 1.0 as value"
          column_names: "value"
        }
      }
    }
    metric_template_spec {
      id_prefix: "my_metric"
      value_columns: "value"
      query {
        sql {
          sql: "SELECT 1.0 as value"
          column_names: "value"
        }
      }
    }
  )");
  ASSERT_FALSE(status_or_output.ok());
  EXPECT_THAT(status_or_output.status().message(),
              HasSubstr("Duplicate definitions for metric 'my_metric_value'"));
}

TEST_F(TraceSummaryTest, GroupedBasic) {
  base::StatusOr<std::string> status_or_output = RunSummarize(
      R"(
    metric_spec {
      id: "metric_a"
      value: "value_a"
      bundle_id: "group"
      query {
        sql {
          sql: "SELECT 1.0 as value_a, 2.0 as value_b"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
    metric_spec {
      id: "metric_b"
      value: "value_b"
      bundle_id: "group"
      query {
        sql {
          sql: "SELECT 1.0 as value_a, 2.0 as value_b"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();
  EXPECT_THAT(*status_or_output, EqualsIgnoringWhitespace(R"(
    metric_bundles {
      specs {
        id: "metric_a"
        value: "value_a"
        bundle_id: "group"
        query {
          sql {
            sql: "SELECT 1.0 as value_a, 2.0 as value_b"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
      }
      specs {
        id: "metric_b"
        value: "value_b"
        bundle_id: "group"
        query {
          sql {
            sql: "SELECT 1.0 as value_a, 2.0 as value_b"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
      }
      row {
        values { double_value: 1.000000 }
        values { double_value: 2.000000 }
      }
    }
  )"));
}

TEST_F(TraceSummaryTest, GroupedTemplateGroupingOrder) {
  base::StatusOr<std::string> status_or_output = RunSummarize(
      R"(
    metric_template_spec {
      id_prefix: "my_metric"
      value_columns: "value_a"
      value_columns: "value_b"
      query {
        sql {
          sql: "SELECT 1.0 as value_a, 2.0 as value_b"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok());
  EXPECT_THAT(*status_or_output, EqualsIgnoringWhitespace(R"(
    metric_bundles {
      specs {
        id: "my_metric_value_a"
        value: "value_a"
        query {
          sql {
            sql: "SELECT 1.0 as value_a, 2.0 as value_b"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
        bundle_id: "my_metric"
        dimension_uniqueness: DIMENSION_UNIQUENESS_UNSPECIFIED
      }
      specs {
        id: "my_metric_value_b"
        value: "value_b"
        query {
          sql {
            sql: "SELECT 1.0 as value_a, 2.0 as value_b"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
        bundle_id: "my_metric"
        dimension_uniqueness: DIMENSION_UNIQUENESS_UNSPECIFIED
      }
      row {
        values { double_value: 1.000000 }
        values { double_value: 2.000000 }
      }
    }
  )"));
}

TEST_F(TraceSummaryTest, GroupedDifferentDimensionsError) {
  base::StatusOr<std::string> status_or_output = RunSummarize(
      R"(
    metric_spec {
      id: "metric_a"
      value: "value"
      dimensions: "dim_a"
      bundle_id: "group"
      query {
        sql {
          sql: "SELECT 1.0 as value, 'a' as dim_a, 'b' as dim_b"
          column_names: "value"
          column_names: "dim_a"
          column_names: "dim_b"
        }
      }
    }
    metric_spec {
      id: "metric_b"
      value: "value"
      dimensions: "dim_b"
      bundle_id: "group"
      query {
        sql {
          sql: "SELECT 1.0 as value, 'a' as dim_a, 'b' as dim_b"
          column_names: "value"
          column_names: "dim_a"
          column_names: "dim_b"
        }
      }
    }
  )");
  ASSERT_FALSE(status_or_output.ok());
  EXPECT_THAT(status_or_output.status().message(),
              HasSubstr("has different dimensions than the first metric"));
}

TEST_F(TraceSummaryTest, GroupedMultipleGroups) {
  base::StatusOr<std::string> status_or_output = RunSummarize(
      R"(
    metric_spec {
      id: "metric_a"
      value: "value"
      bundle_id: "group_a"
      query { sql { sql: "SELECT 1.0 as value" column_names: "value" } }
    }
    metric_spec {
      id: "metric_b"
      value: "value"
      bundle_id: "group_b"
      query { sql { sql: "SELECT 2.0 as value" column_names: "value" } }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();
  EXPECT_THAT(*status_or_output, HasSubstrIgnoringWhitespace(R"(
    metric_bundles {
      specs {
        id: "metric_a"
        value: "value"
        bundle_id: "group_a"
        query { sql { sql: "SELECT 1.0 as value" column_names: "value" } }
      }
      row { values { double_value: 1.000000 } }
    }
  )"));
  EXPECT_THAT(*status_or_output, HasSubstrIgnoringWhitespace(R"(
    metric_bundles {
      specs {
        id: "metric_b"
        value: "value"
        bundle_id: "group_b"
        query { sql { sql: "SELECT 2.0 as value" column_names: "value" } }
      }
      row { values { double_value: 2.000000 } }
    }
  )"));
}

TEST_F(TraceSummaryTest, GroupedNullValues) {
  base::StatusOr<std::string> status_or_output = RunSummarize(
      R"(
    metric_spec {
      id: "my_metric"
      value: "value"
      dimensions: "dim"
      bundle_id: "group"
      query {
        sql {
          sql: "SELECT NULL as dim, NULL as value"
          column_names: "dim"
          column_names: "value"
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok());
  EXPECT_THAT(*status_or_output, EqualsIgnoringWhitespace(R"(
    metric_bundles {
      specs {
        id: "my_metric"
        value: "value"
        dimensions: "dim"
        bundle_id: "group"
        query {
          sql {
            sql: "SELECT NULL as dim, NULL as value"
            column_names: "dim"
            column_names: "value"
          }
        }
      }
    }
  )"));
}

TEST_F(TraceSummaryTest, GroupedMixedGrouping) {
  base::StatusOr<std::string> status_or_output = RunSummarize(
      R"(
    metric_spec {
      id: "metric_a"
      value: "value"
      bundle_id: "group"
      query { sql { sql: "SELECT 1.0 as value" column_names: "value" } }
    }
    metric_spec {
      id: "metric_b"
      value: "value"
      query { sql { sql: "SELECT 2.0 as value" column_names: "value" } }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();
  EXPECT_THAT(*status_or_output, HasSubstrIgnoringWhitespace(R"(
    metric_bundles {
      specs {
        id: "metric_a"
        value: "value"
        bundle_id: "group"
        query { sql { sql: "SELECT 1.0 as value" column_names: "value" } }
      }
      row { values { double_value: 1.000000 } }
    }
  )"));
  EXPECT_THAT(*status_or_output, HasSubstrIgnoringWhitespace(R"(
    metric_bundles {
      specs {
        id: "metric_b"
        value: "value"
        query { sql { sql: "SELECT 2.0 as value" column_names: "value" } }
      }
      row { values { double_value: 2.000000 } }
    }
  )"));
}

TEST_F(TraceSummaryTest, GroupedQueryMismatchError) {
  base::StatusOr<std::string> status_or_output = RunSummarize(
      R"(
    metric_spec {
      id: "metric_a"
      value: "value"
      bundle_id: "group"
      query { sql { sql: "SELECT 1.0 as value" column_names: "value" } }
    }
    metric_spec {
      id: "metric_b"
      value: "value"
      bundle_id: "group"
      query { sql { sql: "SELECT 2.0 as value" column_names: "value" } }
    }
  )");
  ASSERT_FALSE(status_or_output.ok());
  EXPECT_THAT(status_or_output.status().message(),
              HasSubstr("has different query than the first metric"));
}

TEST_F(TraceSummaryTest, GroupedDimensionUniquenessMismatchError) {
  base::StatusOr<std::string> status_or_output = RunSummarize(
      R"(
    metric_spec {
      id: "metric_a"
      value: "value"
      bundle_id: "group"
      dimension_uniqueness: UNIQUE
      query { sql { sql: "SELECT 1.0 as value" column_names: "value" } }
    }
    metric_spec {
      id: "metric_b"
      value: "value"
      bundle_id: "group"
      query { sql { sql: "SELECT 1.0 as value" column_names: "value" } }
    }
  )");
  ASSERT_FALSE(status_or_output.ok());
  EXPECT_THAT(
      status_or_output.status().message(),
      HasSubstr("has different dimension_uniqueness than the first metric"));
}

TEST_F(TraceSummaryTest, GroupedEmptyGroupId) {
  base::StatusOr<std::string> status_or_output = RunSummarize(
      R"(
    metric_spec {
      id: "metric_a"
      value: "value"
      bundle_id: ""
      query { sql { sql: "SELECT 1.0 as value" column_names: "value" } }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();
  EXPECT_THAT(*status_or_output, EqualsIgnoringWhitespace(R"(
    metric_bundles {
      specs {
        id: "metric_a"
        value: "value"
        bundle_id: ""
        query { sql { sql: "SELECT 1.0 as value" column_names: "value" } }
      }
      row { values { double_value: 1.000000 } }
    }
  )"));
}

TEST_F(TraceSummaryTest, GroupedTemplateDisabledGrouping) {
  ASSERT_OK_AND_ASSIGN(auto output, RunSummarize(
                                        R"(
    metric_template_spec {
      id_prefix: "my_metric"
      value_columns: "value_a"
      value_columns: "value_b"
      disable_auto_bundling: true
      query {
        sql {
          sql: "SELECT 1.0 as value_a, 2.0 as value_b"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
  )"));
  EXPECT_THAT(output, HasSubstrIgnoringWhitespace(R"(
    metric_bundles {
      specs {
        id: "my_metric_value_a"
        value: "value_a"
        query {
          sql {
            sql: "SELECT 1.0 as value_a, 2.0 as value_b"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
        dimension_uniqueness: DIMENSION_UNIQUENESS_UNSPECIFIED
      }
      row {
        values { double_value: 1.000000 }
      }
    }
  )"));
  EXPECT_THAT(output, HasSubstrIgnoringWhitespace(R"(
    metric_bundles {
      specs {
        id: "my_metric_value_b"
        value: "value_b"
        query {
          sql {
            sql: "SELECT 1.0 as value_a, 2.0 as value_b"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
        dimension_uniqueness: DIMENSION_UNIQUENESS_UNSPECIFIED
      }
      row {
        values { double_value: 2.000000 }
      }
    }
  )"));
}
TEST_F(TraceSummaryTest, GroupedAllNullValuesAreSkipped) {
  ASSERT_OK_AND_ASSIGN(auto output, RunSummarize(
                                        R"(
    metric_spec {
      id: "metric_a"
      value: "value_a"
      dimensions: "dim"
      bundle_id: "group"
      query {
        sql {
          sql: "SELECT 'not_null' as dim, 1.0 as value_a, 2.0 as value_b UNION ALL SELECT 'all_null' as dim, NULL as value_a, NULL as value_b"
          column_names: "dim"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
    metric_spec {
      id: "metric_b"
      value: "value_b"
      dimensions: "dim"
      bundle_id: "group"
      query {
        sql {
          sql: "SELECT 'not_null' as dim, 1.0 as value_a, 2.0 as value_b UNION ALL SELECT 'all_null' as dim, NULL as value_a, NULL as value_b"
          column_names: "dim"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
  )"));
  EXPECT_THAT(output, EqualsIgnoringWhitespace(R"-(
    metric_bundles {
      specs {
        id: "metric_a"
        value: "value_a"
        dimensions: "dim"
        bundle_id: "group"
        query {
          sql {
            sql: "SELECT \'not_null\' as dim, 1.0 as value_a, 2.0 as value_b UNION ALL SELECT \'all_null\' as dim, NULL as value_a, NULL as value_b"
            column_names: "dim"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
      }
      specs {
        id: "metric_b"
        value: "value_b"
        dimensions: "dim"
        bundle_id: "group"
        query {
          sql {
            sql: "SELECT \'not_null\' as dim, 1.0 as value_a, 2.0 as value_b UNION ALL SELECT \'all_null\' as dim, NULL as value_a, NULL as value_b"
            column_names: "dim"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
      }
      row {
        dimension { string_value: "not_null" }
        values { double_value: 1.000000 }
        values { double_value: 2.000000 }
      }
    }
  )-"));
}

TEST_F(TraceSummaryTest, GroupedOneNullValueIsNotSkipped) {
  ASSERT_OK_AND_ASSIGN(auto output, RunSummarize(
                                        R"(
    metric_spec {
      id: "metric_a"
      value: "value_a"
      dimensions: "dim"
      bundle_id: "group"
      query {
        sql {
          sql: "SELECT 'one_null' as dim, 1.0 as value_a, NULL as value_b"
          column_names: "dim"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
    metric_spec {
      id: "metric_b"
      value: "value_b"
      dimensions: "dim"
      bundle_id: "group"
      query {
        sql {
          sql: "SELECT 'one_null' as dim, 1.0 as value_a, NULL as value_b"
          column_names: "dim"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
  )"));
  EXPECT_THAT(output, EqualsIgnoringWhitespace(R"-(
    metric_bundles {
      specs {
        id: "metric_a"
        value: "value_a"
        dimensions: "dim"
        bundle_id: "group"
        query {
          sql {
            sql: "SELECT \'one_null\' as dim, 1.0 as value_a, NULL as value_b"
            column_names: "dim"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
      }
      specs {
        id: "metric_b"
        value: "value_b"
        dimensions: "dim"
        bundle_id: "group"
        query {
          sql {
            sql: "SELECT \'one_null\' as dim, 1.0 as value_a, NULL as value_b"
            column_names: "dim"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
      }
      row {
        dimension { string_value: "one_null" }
        values { double_value: 1.000000 }
        values { null_value {} }
      }
    }
  )-"));
}

TEST_F(TraceSummaryTest, GroupedSingleNullValueIsSkipped) {
  ASSERT_OK_AND_ASSIGN(auto output, RunSummarize(
                                        R"(
    metric_spec {
      id: "metric_a"
      value: "value_a"
      dimensions: "dim"
      bundle_id: "group"
      query {
        sql {
          sql: "SELECT 'one_null' as dim, NULL as value_a"
          column_names: "dim"
          column_names: "value_a"
        }
      }
    }
  )"));
  EXPECT_THAT(output, EqualsIgnoringWhitespace(R"-(
    metric_bundles {
      specs {
        id: "metric_a"
        value: "value_a"
        dimensions: "dim"
        bundle_id: "group"
        query {
          sql {
            sql: "SELECT \'one_null\' as dim, NULL as value_a"
            column_names: "dim"
            column_names: "value_a"
          }
        }
      }
    }
  )-"));
}

}  // namespace
}  // namespace perfetto::trace_processor::summary
