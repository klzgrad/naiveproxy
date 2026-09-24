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

#include "src/trace_processor/trace_summary/summary.h"

#include <cctype>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/base/test/status_matchers.h"
#include "src/trace_processor/trace_summary/trace_summary.descriptor.h"
#include "src/trace_processor/util/descriptors.h"
#include "test/gtest_and_gmock.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
#include <zlib.h>
#endif

namespace perfetto::trace_processor::summary {
namespace {

using ::testing::HasSubstr;

MATCHER_P(EqualsIgnoringWhitespace, param, "equals ignoring whitespace") {
  auto RemoveAllWhitespace = [](const std::string& input) {
    std::string result;
    result.reserve(input.length());
    std::copy_if(input.begin(), input.end(), std::back_inserter(result),
                 [](char c) { return !std::isspace(c); });
    return result;
  };
  return RemoveAllWhitespace(arg) == RemoveAllWhitespace(param);
}

MATCHER_P(HasSubstrIgnoringWhitespace,
          param,
          "has substring ignoring whitespace") {
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

  base::StatusOr<std::vector<uint8_t>> RunSummarizeBinary(
      const std::string& spec_str,
      const TraceSummaryOutputSpec& output_spec) {
    TraceSummarySpecBytes spec;
    spec.ptr = reinterpret_cast<const uint8_t*>(spec_str.data());
    spec.size = spec_str.size();
    spec.format = TraceSummarySpecBytes::Format::kTextProto;

    std::vector<uint8_t> output;
    base::Status status =
        Summarize(tp_.get(), pool_, {}, {spec}, &output, output_spec);
    if (!status.ok()) {
      return status;
    }
    return output;
  }
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

TEST_F(TraceSummaryTest, TemplateSpecWithInnerQueryId) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      id_prefix: "my_metric"
      value_columns: "value"
      query {
        inner_query_id: "shared_query"
      }
    }
    query {
      id: "shared_query"
      sql {
        sql: "SELECT 1.0 as value"
        column_names: "value"
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"my_metric_value\""));
}

TEST_F(TraceSummaryTest, TemplateSpecWithInnerQueryIdAndGroupBy) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      id_prefix: "my_metric"
      dimensions_specs { name: "category" type: STRING }
      value_columns: "total_value"
      query {
        inner_query_id: "shared_query"
      }
    }
    query {
      id: "shared_query"
      sql {
        sql: "SELECT 'cat1' as category, 1.0 as value UNION ALL SELECT 'cat1' as category, 2.0 as value UNION ALL SELECT 'cat2' as category, 3.0 as value"
        column_names: "category"
        column_names: "value"
      }
      group_by {
        column_names: "category"
        aggregates {
          column_name: "value"
          op: SUM
          result_column_name: "total_value"
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"my_metric_total_value\""));
}

TEST_F(TraceSummaryTest, TemplateSpecWithInnerQueryIdTableSource) {
  // This test uses a table source in the shared query (like the slice table)
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      id_prefix: "my_metric"
      dimensions_specs { name: "name" type: STRING }
      value_columns: "count"
      query {
        inner_query_id: "shared_query"
      }
    }
    query {
      id: "shared_query"
      table {
        table_name: "slice"
      }
      group_by {
        column_names: "name"
        aggregates {
          column_name: "id"
          op: COUNT
          result_column_name: "count"
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"my_metric_count\""));
}

// Test complex template spec with multiple inner_query_id references including
// one in interned_dimension_specs. This mimics real-world heap graph metrics.
TEST_F(TraceSummaryTest, TemplateSpecWithInternedDimensionsAndInnerQueryId) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      id_prefix: "test_aggregation"
      dimensions_specs {
        name: "track_id"
        type: INT64
      }
      dimensions_specs {
        name: "name"
        type: STRING
      }
      value_columns: "total_dur"
      value_columns: "slice_count"
      interned_dimension_specs {
        key_column_spec {
          name: "track_id"
          type: INT64
        }
        data_column_specs {
          name: "track_name"
          type: STRING
        }
        query {
          inner_query_id: "track_metadata_query"
        }
      }
      query {
        inner_query_id: "slice_aggregation_query"
      }
    }
    query {
      id: "slice_aggregation_query"
      table {
        table_name: "slice"
      }
      group_by {
        column_names: "track_id"
        column_names: "name"
        aggregates {
          column_name: "dur"
          op: SUM
          result_column_name: "total_dur"
        }
        aggregates {
          column_name: "id"
          op: COUNT
          result_column_name: "slice_count"
        }
      }
    }
    query {
      id: "track_metadata_query"
      table {
        table_name: "track"
        column_names: "id"
        column_names: "name"
      }
      select_columns {
        column_name_or_expression: "id"
        alias: "track_id"
      }
      select_columns {
        column_name_or_expression: "name"
        alias: "track_name"
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();
  EXPECT_THAT(*status_or_output,
              HasSubstr("id: \"test_aggregation_total_dur\""));
  EXPECT_THAT(*status_or_output,
              HasSubstr("id: \"test_aggregation_slice_count\""));
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
      bundle_id: "group"
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
      bundle_id: "my_metric"
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
      bundle_id: "group_a"
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
      bundle_id: "group_b"
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
      bundle_id: "group"
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
      bundle_id: "group"
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
      bundle_id: "metric_b"
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
      bundle_id: "metric_a"
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
      bundle_id: "my_metric_value_a"
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
      }
      row {
        values { double_value: 1.000000 }
      }
    }
  )"));
  EXPECT_THAT(output, HasSubstrIgnoringWhitespace(R"(
    metric_bundles {
      bundle_id: "my_metric_value_b"
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
      bundle_id: "group"
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
      bundle_id: "group"
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
      bundle_id: "group"
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

TEST_F(TraceSummaryTest, TemplateSpecWithUnitAndPolarity) {
  ASSERT_OK_AND_ASSIGN(auto output, RunSummarize(R"(
    metric_template_spec {
      id_prefix: "my_metric"
      value_column_specs: {
        name: "value_a"
        unit: BYTES
        polarity: LOWER_IS_BETTER
      }
      value_column_specs: {
        name: "value_b"
        custom_unit: "widgets"
        polarity: HIGHER_IS_BETTER
      }
      query {
        sql {
          sql: "SELECT 1.0 as value_a, 2.0 as value_b"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
  )"));
  EXPECT_THAT(output, EqualsIgnoringWhitespace(R"-(
    metric_bundles {
      bundle_id: "my_metric"
      specs {
        id: "my_metric_value_a"
        value: "value_a"
        unit: BYTES
        polarity: LOWER_IS_BETTER
        query {
          sql {
            sql: "SELECT 1.0 as value_a, 2.0 as value_b"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
        bundle_id: "my_metric"
      }
      specs {
        id: "my_metric_value_b"
        value: "value_b"
        custom_unit: "widgets"
        polarity: HIGHER_IS_BETTER
        query {
          sql {
            sql: "SELECT 1.0 as value_a, 2.0 as value_b"
            column_names: "value_a"
            column_names: "value_b"
          }
        }
        bundle_id: "my_metric"
      }
      row {
        values { double_value: 1.000000 }
        values { double_value: 2.000000 }
      }
    }
  )-"));
}

TEST_F(TraceSummaryTest, InternedDimensionBundleUnusedKeysDropped) {
  ASSERT_OK_AND_ASSIGN(auto output, RunSummarize(R"(
    metric_template_spec {
      id_prefix: "my_metric"
      value_columns: "dur"
      dimensions_specs { name: "dim_1" type: INT64 }
      dimensions_specs { name: "dim_2" type: INT64 }
      query {
        sql {
          sql: "SELECT 123 as dim_1, 123 as dim_2, 750.0 as dur UNION ALL SELECT 456 as dim_1, 789 as dim_2, 850.0 as dur"
          column_names: "dim_1"
          column_names: "dim_2"
          column_names: "dur"
        }
      }
      interned_dimension_specs {
        key_column_spec { name: "dim_1" type: INT64 }
        data_column_specs { name: "version_1" type: INT64 }
        query {
          sql {
            sql: "SELECT 123 as dim_1, 100 as version_1 UNION ALL SELECT 456 as dim_1, 200 as version_1 UNION ALL SELECT 789 as dim_1, 300 as version_1"
          }
        }
      }
      interned_dimension_specs {
        key_column_spec { name: "dim_2" type: INT64 }
        data_column_specs { name: "version_2" type: INT64 }
        query {
          sql {
            sql: "SELECT 123 as dim_2, 1000 as version_2 UNION ALL SELECT 456 as dim_2, 2000 as version_2 UNION ALL SELECT 789 as dim_2, 3000 as version_2"
          }
        }
      }
    }
  )"));
  EXPECT_THAT(output, EqualsIgnoringWhitespace(R"-(
    metric_bundles {
      bundle_id: "my_metric"
      specs {
        id: "my_metric_dur"
        value: "dur"
        dimensions_specs {
          name: "dim_1"
          type: INT64
        }
        dimensions_specs {
          name: "dim_2"
          type: INT64
        }
        query {
          sql {
            sql: "SELECT 123 as dim_1, 123 as dim_2, 750.0 as dur UNION ALL SELECT 456 as dim_1, 789 as dim_2, 850.0 as dur"
            column_names: "dim_1"
            column_names: "dim_2"
            column_names: "dur"
          }
        }
        bundle_id: "my_metric"
        interned_dimension_specs {
          key_column_spec {
            name: "dim_1"
            type: INT64
          }
          data_column_specs {
            name: "version_1"
            type: INT64
          }
          query {
            sql {
              sql: "SELECT 123 as dim_1, 100 as version_1 UNION ALL SELECT 456 as dim_1, 200 as version_1 UNION ALL SELECT 789 as dim_1, 300 as version_1"
            }
          }
        }
        interned_dimension_specs {
          key_column_spec {
            name: "dim_2"
            type: INT64
          }
          data_column_specs {
            name: "version_2"
            type: INT64
          }
          query {
            sql {
              sql: "SELECT 123 as dim_2, 1000 as version_2 UNION ALL SELECT 456 as dim_2, 2000 as version_2 UNION ALL SELECT 789 as dim_2, 3000 as version_2"
            }
          }
        }
      }
      row {
        dimension { int64_value: 123 }
        dimension { int64_value: 123 }
        values { double_value: 750.000000 }
      }
      row {
        dimension { int64_value: 456 }
        dimension { int64_value: 789 }
        values { double_value: 850.000000 }
      }
      interned_dimension_bundles {
        interned_dimension_rows {
          key_dimension_value { int64_value: 123 }
          interned_dimension_values { int64_value: 100 }
        }
        interned_dimension_rows {
          key_dimension_value { int64_value: 456 }
          interned_dimension_values { int64_value: 200 }
        }
      }
      interned_dimension_bundles {
        interned_dimension_rows {
          key_dimension_value { int64_value: 123 }
          interned_dimension_values { int64_value: 1000 }
        }
        interned_dimension_rows {
          key_dimension_value { int64_value: 789 }
          interned_dimension_values { int64_value: 3000 }
        }
      }
    }
  )-"));
}

TEST_F(TraceSummaryTest, TemplateSpecWithValueColumnsAndSpecsError) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      id_prefix: "my_metric"
      value_columns: "value_a"
      value_column_specs: {
        name: "value_b"
      }
      query {
        sql {
          sql: "SELECT 1.0 as value_a, 2.0 as value_b"
          column_names: "value_a"
          column_names: "value_b"
        }
      }
    }
  )");
  ASSERT_FALSE(status_or_output.ok());
  EXPECT_THAT(status_or_output.status().message(),
              HasSubstr("Metric template has both value_columns and "
                        "value_column_specs defined"));
}

TEST_F(TraceSummaryTest, InternedDimensionBundleBasic) {
  ASSERT_OK_AND_ASSIGN(auto output, RunSummarize(R"(
    metric_template_spec {
      id_prefix: "my_metric"
      value_columns: "dur"
      value_columns: "count"
      dimensions_specs { name: "dim" type: STRING }
      query {
        sql {
          sql: "SELECT 'a' as dim, 750.0 as dur, 3.0 as count UNION ALL SELECT 'b' as dim, 425.0 as dur, 4.0 as count"
          column_names: "dim"
          column_names: "dur"
          column_names: "count"
        }
      }
      interned_dimension_specs {
        key_column_spec { name: "dim" type: STRING }
        data_column_specs { name: "version" type: DOUBLE }
        data_column_specs { name: "is_kernel" type: BOOLEAN }
        query {
          sql {
            sql: "SELECT 'a' as dim, 1.0 as version, false as is_kernel UNION ALL SELECT 'b' as dim, 2.0 as version, true as is_kernel"
          }
        }
      }
    }
  )"));
  EXPECT_THAT(output, EqualsIgnoringWhitespace(R"-(
    metric_bundles {
      bundle_id: "my_metric"
      specs {
        id: "my_metric_dur"
        value: "dur"
        dimensions_specs {
          name: "dim"
          type: STRING
        }
        query {
          sql {
            sql: "SELECT \'a\' as dim, 750.0 as dur, 3.0 as count UNION ALL SELECT \'b\' as dim, 425.0 as dur, 4.0 as count"
            column_names: "dim"
            column_names: "dur"
            column_names: "count"
          }
        }
        bundle_id: "my_metric"
        interned_dimension_specs {
          key_column_spec {
            name: "dim"
            type: STRING
          }
          data_column_specs {
            name: "version"
            type: DOUBLE
          }
          data_column_specs { 
            name: "is_kernel" 
            type: BOOLEAN 
          }
          query {
            sql {
               sql: "SELECT \'a\' as dim, 1.0 as version, false as is_kernel UNION ALL SELECT \'b\' as dim, 2.0 as version, true as is_kernel"
            }
          }
        }
      }
      specs {
        id: "my_metric_count"
        value: "count"
        dimensions_specs {
          name: "dim"
          type: STRING
        }
        query {
          sql {
            sql: "SELECT \'a\' as dim, 750.0 as dur, 3.0 as count UNION ALL SELECT \'b\' as dim, 425.0 as dur, 4.0 as count"
            column_names: "dim"
            column_names: "dur"
            column_names: "count"
          }
        }
        bundle_id: "my_metric"
        interned_dimension_specs {
          key_column_spec {
            name: "dim"
            type: STRING
          }
          data_column_specs {
            name: "version"
            type: DOUBLE
          }
          data_column_specs { 
            name: "is_kernel" 
            type: BOOLEAN 
          }
          query {
            sql {
               sql: "SELECT \'a\' as dim, 1.0 as version, false as is_kernel UNION ALL SELECT \'b\' as dim, 2.0 as version, true as is_kernel"
            }
          }
        }
      }
      row {
        dimension {
          string_value: "a"
        }
        values {
          double_value: 750.000000
        }
        values {
          double_value: 3.000000
        }
      }
      row {
        dimension {
          string_value: "b"
        }
        values {
          double_value: 425.000000
        }
        values {
          double_value: 4.000000
        }
      }
      interned_dimension_bundles {
        interned_dimension_rows {
          key_dimension_value {
            string_value: "a"
          }
          interned_dimension_values {
            double_value: 1.000000
          }
          interned_dimension_values {
            bool_value: false
          }
        }
        interned_dimension_rows {
          key_dimension_value {
            string_value: "b"
          }
          interned_dimension_values {
            double_value: 2.000000
          }
          interned_dimension_values {
            bool_value: true
          }
        }
      }
    }
  )-"));
}

TEST_F(TraceSummaryTest, InternedDimensionBundleKeyColumnNotInDimensions) {
  auto status = RunSummarize(R"(
    metric_spec {
      id: "my_metric"
      value: "value"
      dimensions_specs { name: "dim" type: STRING }
      query {
        sql {
          sql: "SELECT 'a' as dim, 1.0 as value"
          column_names: "dim"
          column_names: "value"
        }
      }
      interned_dimension_specs {
        key_column_spec { name: "other" type: STRING }
        query { sql { sql: "SELECT 'a' as other" } }
      }
    }
  )");
  ASSERT_FALSE(status.ok());
  EXPECT_THAT(
      status.status().message(),
      HasSubstr("Key column 'other' in interned dimension bundle not found in "
                "metric dimensions"));
}

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
TEST_F(TraceSummaryTest, OutputIsCompressed) {
  TraceSummaryOutputSpec uncompressed_spec;
  uncompressed_spec.format = TraceSummaryOutputSpec::Format::kBinaryProto;
  uncompressed_spec.compression = TraceSummaryOutputSpec::Compression::kNone;

  const char* kSpec = R"(
    metric_spec {
      id: "my_metric"
      value: "value"
      query {
        sql {
          sql: "SELECT 1.0 as value"
          column_names: "value"
        }
      }
    }
  )";
  ASSERT_OK_AND_ASSIGN(auto uncompressed_output,
                       RunSummarizeBinary(kSpec, uncompressed_spec));

  TraceSummaryOutputSpec compressed_spec;
  compressed_spec.format = TraceSummaryOutputSpec::Format::kBinaryProto;
  compressed_spec.compression = TraceSummaryOutputSpec::Compression::kZlib;
  ASSERT_OK_AND_ASSIGN(auto compressed_output,
                       RunSummarizeBinary(kSpec, compressed_spec));

  ASSERT_GT(uncompressed_output.size(), 0u);
  ASSERT_GT(compressed_output.size(), 0u);
  ASSERT_LT(compressed_output.size(), uncompressed_output.size());

  std::vector<uint8_t> decompressed_output(uncompressed_output.size());
  uLongf decompressed_size = static_cast<uLongf>(decompressed_output.size());
  int res = uncompress(decompressed_output.data(), &decompressed_size,
                       compressed_output.data(),
                       static_cast<uLongf>(compressed_output.size()));
  ASSERT_EQ(res, Z_OK);
  decompressed_output.resize(decompressed_size);

  ASSERT_EQ(decompressed_output, uncompressed_output);
}
#else
TEST_F(TraceSummaryTest, OutputCompressionFailsWhenZlibDisabled) {
  TraceSummaryOutputSpec compressed_spec;
  compressed_spec.format = TraceSummaryOutputSpec::Format::kBinaryProto;
  compressed_spec.compression = TraceSummaryOutputSpec::Compression::kZlib;

  const char* kSpec = R"(
    metric_spec {
      id: "my_metric"
      value: "value"
      query {
        sql {
          sql: "SELECT 1.0 as value"
          column_names: "value"
        }
      }
    }
  )";
  base::StatusOr<std::vector<uint8_t>> status_or_output =
      RunSummarizeBinary(kSpec, compressed_spec);

  // Zlib compression is not supported on this platform, but was requested, so
  // the function should fail.
  ASSERT_FALSE(status_or_output.ok());
  EXPECT_THAT(
      status_or_output.status().message(),
      HasSubstr(
          "Zlib compression requested but is not supported on this platform."));
}
#endif

// Test that sql.column_names works correctly with group_by transformations.
// The column_names field describes what the SQL returns before transformations,
// but group_by changes the output schema to group keys + aggregates.
TEST_F(TraceSummaryTest, SqlColumnNamesWithGroupBy) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      id_prefix: "test_metric"
      dimensions: "process_name"
      value_columns: "min_val"
      value_columns: "max_val"
      value_columns: "avg_val"
      query {
        sql {
          column_names: "process_name"
          column_names: "metric_val"
          column_names: "dur"
          sql: "
            SELECT 'systemui' as process_name, 100 as metric_val, 1000 as dur
            UNION ALL
            SELECT 'systemui' as process_name, 200 as metric_val, 2000 as dur
            UNION ALL
            SELECT 'launcher' as process_name, 150 as metric_val, 1500 as dur
          "
        }
        group_by {
          column_names: "process_name"
          aggregates {
            column_name: "metric_val"
            op: MIN
            result_column_name: "min_val"
          }
          aggregates {
            column_name: "metric_val"
            op: MAX
            result_column_name: "max_val"
          }
          aggregates {
            column_name: "metric_val"
            op: MEAN
            result_column_name: "avg_val"
          }
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();

  // Verify we get metrics for both processes
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"test_metric_min_val\""));
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"test_metric_max_val\""));
  EXPECT_THAT(*status_or_output, HasSubstr("id: \"test_metric_avg_val\""));

  // Verify dimension values
  EXPECT_THAT(*status_or_output, HasSubstr("string_value: \"systemui\""));
  EXPECT_THAT(*status_or_output, HasSubstr("string_value: \"launcher\""));

  // Verify aggregated values for systemui (min=100, max=200, avg=150)
  EXPECT_THAT(*status_or_output, HasSubstr("double_value: 100.000000"));
  EXPECT_THAT(*status_or_output, HasSubstr("double_value: 200.000000"));
  EXPECT_THAT(*status_or_output, HasSubstr("double_value: 150.000000"));
}

// Test that sql.column_names works correctly with select_columns
// transformations.
TEST_F(TraceSummaryTest, SqlColumnNamesWithSelectColumns) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_spec {
      id: "test_metric"
      value: "renamed_value"
      query {
        sql {
          column_names: "id"
          column_names: "name"
          column_names: "value"
          sql: "SELECT 1 as id, 'foo' as name, 42.0 as value"
        }
        select_columns {
          column_name_or_expression: "value"
          alias: "renamed_value"
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();

  EXPECT_THAT(*status_or_output, HasSubstr("id: \"test_metric\""));
  EXPECT_THAT(*status_or_output, HasSubstr("double_value: 42"));
}

// Test that sql.column_names validation still works when there are no
// transformations (only filters, which don't change the schema).
TEST_F(TraceSummaryTest, SqlColumnNamesWithFiltersOnly) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_spec {
      id: "test_metric"
      value: "value"
      dimensions: "name"
      query {
        sql {
          column_names: "name"
          column_names: "value"
          sql: "
            SELECT 'a' as name, 10.0 as value
            UNION ALL
            SELECT 'b' as name, 20.0 as value
            UNION ALL
            SELECT 'c' as name, 30.0 as value
          "
        }
        filters {
          column_name: "value"
          op: GREATER_THAN
          double_rhs: 15.0
        }
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();

  EXPECT_THAT(*status_or_output, HasSubstr("id: \"test_metric\""));
  // Should only have b and c (values > 15)
  EXPECT_THAT(*status_or_output, HasSubstr("string_value: \"b\""));
  EXPECT_THAT(*status_or_output, HasSubstr("string_value: \"c\""));
  EXPECT_THAT(*status_or_output, HasSubstr("double_value: 20.000000"));
  EXPECT_THAT(*status_or_output, HasSubstr("double_value: 30.000000"));
}

// Test the exact heap_graph_class_aggregation spec with inner_query_id
// references in both the main query and interned_dimension_specs.
TEST_F(TraceSummaryTest, HeapGraphClassAggregationSpec) {
  base::StatusOr<std::string> status_or_output = RunSummarize(R"(
    metric_template_spec {
      id_prefix: "heap_graph_class_aggregation"
      dimensions_specs {
        name: "upid"
        type: INT64
      }
      dimensions_specs {
        name: "type_name"
        type: STRING
      }
      dimensions_specs {
        name: "is_libcore_or_array"
        type: BOOLEAN
      }
      value_columns: "total_size_bytes"
      value_columns: "total_native_size_bytes"
      value_columns: "total_dominated_obj_count"
      value_columns: "total_dominated_size_bytes"
      value_columns: "total_reachable_obj_count"
      interned_dimension_specs {
        key_column_spec {
          name: "upid"
          type: INT64
        }
        data_column_specs {
          name: "pid"
          type: INT64
        }
        data_column_specs {
          name: "process_name"
          type: STRING
        }
        data_column_specs {
          name: "uid"
          type: INT64
        }
        data_column_specs {
          name: "user_id"
          type: INT64
        }
        data_column_specs {
          name: "package_name"
          type: STRING
        }
        data_column_specs {
          name: "version_code"
          type: INT64
        }
        data_column_specs {
          name: "debuggable"
          type: BOOLEAN
        }
        data_column_specs {
          name: "is_kernel_task"
          type: BOOLEAN
        }
        query {
          inner_query_id: "process_metadata_query"
        }
      }
      query {
        inner_query_id: "heap_graph_class_aggregation_outer_query"
      }
    }
    query {
      id: "heap_graph_class_aggregation_outer_query"
      table {
        table_name: "android_heap_graph_class_aggregation"
      }
      referenced_modules: "android.memory.heap_graph.heap_graph_class_aggregation"
      group_by {
        column_names: "upid"
        column_names: "type_name"
        column_names: "is_libcore_or_array"
        aggregates {
          column_name: "size_bytes"
          op: SUM
          result_column_name: "total_size_bytes"
        }
        aggregates {
          column_name: "native_size_bytes"
          op: SUM
          result_column_name: "total_native_size_bytes"
        }
        aggregates {
          column_name: "dominated_obj_count"
          op: SUM
          result_column_name: "total_dominated_obj_count"
        }
        aggregates {
          column_name: "dominated_size_bytes"
          op: SUM
          result_column_name: "total_dominated_size_bytes"
        }
        aggregates {
          column_name: "reachable_obj_count"
          op: SUM
          result_column_name: "total_reachable_obj_count"
        }
      }
    }
    query {
      id: "process_metadata_query"
      referenced_modules: "android.process_metadata"
      table {
        table_name: "android_process_metadata"
      }
    }
  )");
  ASSERT_TRUE(status_or_output.ok()) << status_or_output.status().message();

  // Verify all the expected metric IDs are generated
  EXPECT_THAT(
      *status_or_output,
      HasSubstr("id: \"heap_graph_class_aggregation_total_size_bytes\""));
  EXPECT_THAT(
      *status_or_output,
      HasSubstr(
          "id: \"heap_graph_class_aggregation_total_native_size_bytes\""));
  EXPECT_THAT(
      *status_or_output,
      HasSubstr(
          "id: \"heap_graph_class_aggregation_total_dominated_obj_count\""));
  EXPECT_THAT(
      *status_or_output,
      HasSubstr(
          "id: \"heap_graph_class_aggregation_total_dominated_size_bytes\""));
  EXPECT_THAT(
      *status_or_output,
      HasSubstr(
          "id: \"heap_graph_class_aggregation_total_reachable_obj_count\""));
}

}  // namespace
}  // namespace perfetto::trace_processor::summary
