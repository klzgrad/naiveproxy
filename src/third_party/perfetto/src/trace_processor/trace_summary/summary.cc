/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/protozero/text_to_proto/text_to_proto.h"
#include "src/trace_processor/perfetto_sql/generator/structured_query_generator.h"
#include "src/trace_processor/trace_summary/trace_summary.descriptor.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/protozero_to_text.h"

#include "protos/perfetto/trace_summary/file.pbzero.h"
#include "protos/perfetto/trace_summary/v2_metric.pbzero.h"

namespace perfetto::trace_processor::summary {

namespace {

struct Metric {
  std::string id;
  std::string query;
  protozero::ConstBytes spec;
};

using perfetto_sql::generator::StructuredQueryGenerator;

base::Status ExpandMetricTemplates(
    const std::vector<protos::pbzero::TraceSummarySpec::Decoder>& spec_decoders,
    std::vector<std::vector<uint8_t>>& synthetic_protos) {
  protozero::HeapBuffered<protos::pbzero::TraceMetricV2Spec> expanded;
  for (const auto& spec : spec_decoders) {
    for (auto it = spec.metric_template_spec(); it; ++it) {
      protos::pbzero::TraceMetricV2TemplateSpec::Decoder tmpl(*it);
      std::string id_prefix = tmpl.id_prefix().ToStdString();
      if (id_prefix.empty()) {
        return base::ErrStatus(
            "Metric template with empty id_prefix field: this is not allowed");
      }
      for (auto vc_it = tmpl.value_columns(); vc_it; ++vc_it) {
        expanded.Reset();

        protozero::ConstChars value_column = *vc_it;
        expanded->set_id(id_prefix + "_" + value_column.ToStdString());
        expanded->set_value(value_column);
        for (auto dim = tmpl.dimensions(); dim; ++dim) {
          protozero::ConstChars dim_str = *dim;
          expanded->add_dimensions(dim_str.data, dim_str.size);
        }
        for (auto dim = tmpl.dimensions_specs(); dim; ++dim) {
          protozero::ConstBytes dim_spec = *dim;
          expanded->add_dimensions_specs()->AppendRawProtoBytes(dim_spec.data,
                                                                dim_spec.size);
        }
        if (tmpl.has_query()) {
          expanded->set_query()->AppendRawProtoBytes(tmpl.query().data,
                                                     tmpl.query().size);
        }
        if (!tmpl.disable_auto_bundling()) {
          expanded->set_bundle_id(id_prefix);
        }
        expanded->set_dimension_uniqueness(
            static_cast<protos::pbzero::TraceMetricV2Spec::DimensionUniqueness>(
                tmpl.dimension_uniqueness()));
        synthetic_protos.push_back(expanded.SerializeAsArray());
      }
    }
  }
  return base::OkStatus();
}

base::Status WriteMetadata(TraceProcessor* processor,
                           const std::string& metadata_sql,
                           protos::pbzero::TraceSummary* summary) {
  auto it = processor->ExecuteQuery(metadata_sql);
  RETURN_IF_ERROR(it.Status());

  uint32_t col_count = it.ColumnCount();

  // This can happen if there is no metadata. Just early return in that case.
  if (col_count == 0) {
    return base::OkStatus();
  }

  // Otherwise we expect a strict schema of (key, value).
  if (col_count != 2 || it.GetColumnName(0) != "key" ||
      it.GetColumnName(1) != "value") {
    return base::ErrStatus(
        "Metadata query did not match schema of (key, value)");
  }
  while (it.Next()) {
    auto key = it.Get(0);
    if (key.type != SqlValue::kString) {
      return base::ErrStatus(
          "Key column in metadata query was not of type string");
    }
    // Silently ignore any null values.
    auto value = it.Get(1);
    if (value.is_null()) {
      continue;
    }
    if (value.type != SqlValue::kString) {
      return base::ErrStatus(
          "Key column in metadata query was not of type string or null");
    }
    auto* metadata = summary->add_metadata();
    metadata->set_key(key.AsString());
    metadata->set_value(value.AsString());
  }
  return it.Status();
}

struct Dimension {
  std::string name;
  protos::pbzero::TraceMetricV2Spec::DimensionType type;

  [[maybe_unused]] bool operator==(const Dimension& other) const {
    return std::tie(name, type) == std::tie(other.name, other.type);
  }
  [[maybe_unused]] bool operator!=(const Dimension& other) const {
    return !(*this == other);
  }
};

base::StatusOr<std::vector<Dimension>> GetDimensions(
    const protos::pbzero::TraceMetricV2Spec::Decoder& spec_decoder) {
  if (spec_decoder.has_dimensions_specs() && spec_decoder.has_dimensions()) {
    return base::ErrStatus(
        "Both dimensions and dimension_specs defined for metric '%s'. Only "
        "one is allowed",
        spec_decoder.id().ToStdString().c_str());
  }
  std::vector<Dimension> dimensions;
  if (spec_decoder.dimensions_specs()) {
    for (auto dim = spec_decoder.dimensions_specs(); dim; ++dim) {
      protos::pbzero::TraceMetricV2Spec::DimensionSpec::Decoder dim_spec(*dim);
      std::string_view dim_name = dim_spec.name().ToStdStringView();
      auto dimension_type =
          static_cast<protos::pbzero::TraceMetricV2Spec::DimensionType>(
              dim_spec.type());
      if (dimension_type ==
          protos::pbzero::TraceMetricV2Spec::DIMENSION_TYPE_UNSPECIFIED) {
        return base::ErrStatus(
            "Dimension '%.*s' in metric '%s' has unspecified type",
            int(dim_name.size()), dim_name.data(),
            spec_decoder.id().ToStdString().c_str());
      }
      dimensions.push_back({dim_spec.name().ToStdString(), dimension_type});
    }
  } else {
    for (auto dim = spec_decoder.dimensions(); dim; ++dim) {
      dimensions.push_back(
          {dim->as_std_string(),
           protos::pbzero::TraceMetricV2Spec::DIMENSION_TYPE_UNSPECIFIED});
    }
  }
  return dimensions;
}

struct DimensionWithIndex : Dimension {
  uint32_t index;
};

base::StatusOr<std::vector<DimensionWithIndex>> GetDimensionsWithIndex(
    const protos::pbzero::TraceMetricV2Spec::Decoder& spec_decoder,
    perfetto::trace_processor::Iterator& it) {
  ASSIGN_OR_RETURN(auto dimensions, GetDimensions(spec_decoder));
  std::vector<DimensionWithIndex> output;
  uint32_t col_count = it.ColumnCount();
  for (const auto& dim : dimensions) {
    std::optional<uint32_t> dim_index;
    for (uint32_t i = 0; i < col_count; ++i) {
      if (it.GetColumnName(i) == dim.name) {
        dim_index = i;
        break;
      }
    }
    if (!dim_index) {
      return base::ErrStatus(
          "Dimensions column '%s' not found in the query result for metric "
          "'%s'",
          dim.name.c_str(), spec_decoder.id().ToStdString().c_str());
    }
    output.push_back({dim, *dim_index});
  }
  return output;
}

base::Status WriteDimension(
    const DimensionWithIndex& dim_with_index,
    const std::string& metric_or_bundle_name,
    Iterator& query_it,
    protos::pbzero::TraceMetricV2Bundle::Row::Dimension* dimension,
    base::Hasher* hasher) {
  const auto& dimension_value = query_it.Get(dim_with_index.index);
  hasher->Update(dimension_value.type);
  if (dimension_value.is_null()) {
    // Accept null value for all dimension types.
    dimension->set_null_value();
    return base::OkStatus();
  }
  switch (dim_with_index.type) {
    case protos::pbzero::TraceMetricV2Spec::STRING: {
      if (dimension_value.type != SqlValue::kString) {
        return base::ErrStatus(
            "Expected string for dimension '%s' in metric or bundle '%s', got "
            "%d",
            dim_with_index.name.c_str(), metric_or_bundle_name.c_str(),
            dimension_value.type);
      }
      const char* dimension_str = dimension_value.string_value;
      hasher->Update(dimension_str);
      dimension->set_string_value(dimension_str);
      break;
    }
    case protos::pbzero::TraceMetricV2Spec::INT64: {
      if (dimension_value.type != SqlValue::kLong) {
        return base::ErrStatus(
            "Expected int64 for dimension '%s' in metric or bundle '%s', got "
            "%d",
            dim_with_index.name.c_str(), metric_or_bundle_name.c_str(),
            dimension_value.type);
      }
      int64_t dim_value = dimension_value.long_value;
      hasher->Update(dim_value);
      dimension->set_int64_value(dim_value);
      break;
    }
    case protos::pbzero::TraceMetricV2Spec::DOUBLE: {
      if (dimension_value.type != SqlValue::kDouble) {
        return base::ErrStatus(
            "Expected double for dimension '%s' in metric or bundle '%s', got "
            "%d",
            dim_with_index.name.c_str(), metric_or_bundle_name.c_str(),
            dimension_value.type);
      }
      double dim_value = dimension_value.AsDouble();
      hasher->Update(dim_value);
      dimension->set_double_value(dim_value);
      break;
    }
    case protos::pbzero::TraceMetricV2Spec::DIMENSION_TYPE_UNSPECIFIED:
      if (dimension_value.type == SqlValue::kLong) {
        int64_t dim_value = dimension_value.long_value;
        hasher->Update(dim_value);
        dimension->set_int64_value(dim_value);
      } else if (dimension_value.type == SqlValue::kDouble) {
        double dim_value = dimension_value.AsDouble();
        hasher->Update(dim_value);
        dimension->set_double_value(dim_value);
      } else if (dimension_value.type == SqlValue::kString) {
        const char* dimension_str = dimension_value.string_value;
        hasher->Update(dimension_str);
        dimension->set_string_value(dimension_str);
      } else if (dimension_value.type == SqlValue::kBytes) {
        return base::ErrStatus(
            "Received bytes for dimension '%s' in metric or bundle '%s': this "
            "is not supported",
            dim_with_index.name.c_str(), metric_or_bundle_name.c_str());
      } else {
        PERFETTO_FATAL("Null dimension should have been handled above");
      }
      break;
  }
  return base::OkStatus();
}

base::Status VerifyBundleHasConsistentSpecs(
    const std::string& bundle_id,
    const std::vector<const Metric*>& metrics) {
  if (metrics.empty()) {
    return base::ErrStatus("Empty metric bundle %s: this is not allowed",
                           bundle_id.c_str());
  }
  if (metrics.size() == 1) {
    return base::OkStatus();
  }
  const Metric* first = metrics.front();
  protos::pbzero::TraceMetricV2Spec::Decoder first_spec(first->spec);
  ASSIGN_OR_RETURN(auto first_dims, GetDimensions(first_spec));
  for (const Metric* metric : metrics) {
    protos::pbzero::TraceMetricV2Spec::Decoder spec(metric->spec);
    if (spec.bundle_id().ToStdStringView() !=
        first_spec.bundle_id().ToStdStringView()) {
      return base::ErrStatus(
          "Metric '%s' in bundle '%s' has different bundle_id than the "
          "first metric '%s': this is not allowed",
          metric->id.c_str(), bundle_id.c_str(), first->id.c_str());
    }
    if (spec.dimension_uniqueness() != first_spec.dimension_uniqueness()) {
      return base::ErrStatus(
          "Metric '%s' in bundle '%s' has different dimension_uniqueness than "
          "the first metric '%s': this is not allowed",
          metric->id.c_str(), bundle_id.c_str(), first->id.c_str());
    }
    ASSIGN_OR_RETURN(auto dims, GetDimensions(spec));
    if (dims != first_dims) {
      return base::ErrStatus(
          "Metric '%s' in bundle '%s' has different dimensions than the first "
          "metric '%s': this is not allowed",
          metric->id.c_str(), bundle_id.c_str(), first->id.c_str());
    }
    if (first->query != metric->query) {
      return base::ErrStatus(
          "Metric '%s' in bundle '%s' has different query than the first "
          "metric '%s': this is not allowed",
          metric->id.c_str(), bundle_id.c_str(), first->id.c_str());
    }
  }
  return base::OkStatus();
}

base::Status CreateQueriesAndComputeMetrics(
    TraceProcessor* processor,
    const std::vector<Metric>& metrics,
    protos::pbzero::TraceSummary* summary) {
  base::FlatHashMap<std::string, std::vector<const Metric*>> metrics_by_bundle;
  for (const Metric& m : metrics) {
    protos::pbzero::TraceMetricV2Spec::Decoder spec_decoder(m.spec);
    std::string bundle_id = spec_decoder.bundle_id().ToStdString();
    if (bundle_id.empty()) {
      bundle_id = m.id;
    }
    metrics_by_bundle[bundle_id].push_back(&m);
  }
  for (auto it = metrics_by_bundle.GetIterator(); it; ++it) {
    const auto& value = it.value();
    RETURN_IF_ERROR(VerifyBundleHasConsistentSpecs(it.key(), value));

    const std::string& bundle_id = it.key();
    auto* bundle = summary->add_metric_bundles();
    for (const Metric* metric : value) {
      bundle->add_specs()->AppendRawProtoBytes(metric->spec.data,
                                               metric->spec.size);
    }

    const Metric* first = value.front();
    protos::pbzero::TraceMetricV2Spec::Decoder first_spec(first->spec);

    auto query_it = processor->ExecuteQuery(first->query);
    ASSIGN_OR_RETURN(std::vector<DimensionWithIndex> dimensions_with_index,
                     GetDimensionsWithIndex(first_spec, query_it));

    std::vector<uint32_t> value_indices;
    for (const auto* metric : value) {
      protos::pbzero::TraceMetricV2Spec::Decoder spec(metric->spec);
      std::string value_column_name = spec.value().ToStdString();
      std::optional<uint32_t> value_index;
      for (uint32_t i = 0; i < query_it.ColumnCount(); ++i) {
        if (query_it.GetColumnName(i) == value_column_name) {
          value_index = i;
          break;
        }
      }
      if (!value_index) {
        return base::ErrStatus(
            "Column '%s' not found in the query result for metric '%s'",
            value_column_name.c_str(), spec.id().ToStdString().c_str());
      }
      value_indices.push_back(*value_index);
    }
    bool is_unique_dimensions =
        first_spec.dimension_uniqueness() ==
        protos::pbzero::TraceMetricV2Spec::DimensionUniqueness::UNIQUE;
    base::FlatHashMap<uint64_t, bool> seen_dimensions;
    while (query_it.Next()) {
      bool all_null = true;
      for (size_t i = 0; i < value.size(); ++i) {
        const auto& metric_value_column = query_it.Get(value_indices[i]);
        if (!metric_value_column.is_null()) {
          all_null = false;
          break;
        }
      }
      // If all values are null, we skip writing the row.
      if (all_null) {
        continue;
      }
      auto* row = bundle->add_row();
      base::Hasher hasher;
      for (const auto& dim : dimensions_with_index) {
        RETURN_IF_ERROR(WriteDimension(dim, bundle_id, query_it,
                                       row->add_dimension(), &hasher));
      }
      uint64_t hash = hasher.digest();
      if (is_unique_dimensions && !seen_dimensions.Insert(hash, true).second) {
        return base::ErrStatus(
            "Duplicate dimensions found for metric bundle '%s': this is not "
            "allowed",
            bundle_id.c_str());
      }

      for (size_t i = 0; i < value.size(); ++i) {
        const auto& metric_value_column = query_it.Get(value_indices[i]);
        auto* row_value = row->add_values();
        if (metric_value_column.is_null()) {
          row_value->set_null_value();
          continue;
        }
        switch (metric_value_column.type) {
          case SqlValue::kLong:
            row_value->set_double_value(
                static_cast<double>(metric_value_column.long_value));
            break;
          case SqlValue::kDouble:
            row_value->set_double_value(metric_value_column.double_value);
            break;
          case SqlValue::kNull:
            PERFETTO_FATAL("Null value should have been skipped");
          case SqlValue::kString:
          case SqlValue::kBytes:
            return base::ErrStatus(
                "Received string/bytes for value column in metric '%s': this "
                "is not supported",
                it.value()[i]->id.c_str());
        }
      }
    }
    RETURN_IF_ERROR(query_it.Status());
  }
  return base::OkStatus();
}

base::Status CreateQueriesAndComputeMetrics(
    TraceProcessor* processor,
    const DescriptorPool& pool,
    const std::vector<StructuredQueryGenerator::Query>& queries,
    const std::vector<Metric>& metrics,
    const std::optional<std::string>& metadata_sql,
    std::vector<uint8_t>* output,
    const TraceSummaryOutputSpec& output_spec) {
  for (const auto& query : queries) {
    auto it = processor->ExecuteQuery("CREATE PERFETTO TABLE " +
                                      query.table_name + " AS " + query.sql);
    PERFETTO_CHECK(!it.Next());
    if (!it.Status().ok()) {
      return base::ErrStatus("Error while executing shared query %s: %s",
                             query.id.c_str(), it.Status().c_message());
    }
  }
  protozero::HeapBuffered<protos::pbzero::TraceSummary> summary;
  RETURN_IF_ERROR(
      CreateQueriesAndComputeMetrics(processor, metrics, summary.get()));
  if (metadata_sql) {
    RETURN_IF_ERROR(WriteMetadata(processor, *metadata_sql, summary.get()));
  }
  switch (output_spec.format) {
    case TraceSummaryOutputSpec::Format::kBinaryProto:
      *output = summary.SerializeAsArray();
      break;
    case TraceSummaryOutputSpec::Format::kTextProto:
      std::vector<uint8_t> proto = summary.SerializeAsArray();
      std::string out = protozero_to_text::ProtozeroToText(
          pool, ".perfetto.protos.TraceSummary",
          protozero::ConstBytes{proto.data(), proto.size()});
      *output = std::vector<uint8_t>(out.begin(), out.end());
      break;
  }
  return base::OkStatus();
}

}  // namespace

base::Status Summarize(TraceProcessor* processor,
                       const DescriptorPool& pool,
                       const TraceSummaryComputationSpec& computation,
                       const std::vector<TraceSummarySpecBytes>& specs,
                       std::vector<uint8_t>* output,
                       const TraceSummaryOutputSpec& output_spec) {
  std::vector<std::vector<uint8_t>> synthetic_protos;
  std::vector<protos::pbzero::TraceSummarySpec::Decoder> spec_decoders;
  for (uint32_t i = 0; i < specs.size(); ++i) {
    switch (specs[i].format) {
      case TraceSummarySpecBytes::Format::kBinaryProto:
        spec_decoders.emplace_back(specs[i].ptr, specs[i].size);
        break;
      case TraceSummarySpecBytes::Format::kTextProto:
        synthetic_protos.emplace_back();
        ASSIGN_OR_RETURN(
            synthetic_protos.back(),
            protozero::TextToProto(
                kTraceSummaryDescriptor.data(), kTraceSummaryDescriptor.size(),
                ".perfetto.protos.TraceSummarySpec", "-",
                std::string_view(reinterpret_cast<const char*>(specs[i].ptr),
                                 specs[i].size)));
        spec_decoders.emplace_back(synthetic_protos.back().data(),
                                   synthetic_protos.back().size());
        break;
    }
  }

  perfetto_sql::generator::StructuredQueryGenerator generator;
  for (const auto& spec : spec_decoders) {
    for (auto it = spec.query(); it; ++it) {
      RETURN_IF_ERROR(generator.AddQuery(it->data(), it->size()));
    }
  }

  std::vector<protos::pbzero::TraceMetricV2Spec::Decoder> metric_decoders;
  for (const auto& spec : spec_decoders) {
    for (auto it = spec.metric_spec(); it; ++it) {
      metric_decoders.emplace_back(*it);
    }
  }

  std::vector<std::vector<uint8_t>> expanded_metrics;
  RETURN_IF_ERROR(ExpandMetricTemplates(spec_decoders, expanded_metrics));
  for (const auto& expanded : expanded_metrics) {
    metric_decoders.emplace_back(expanded.data(), expanded.size());
  }

  // If `v2_metric_ids` is an empty vector, we will not compute any metrics.
  std::vector<std::string> metric_ids;
  if (computation.v2_metric_ids) {
    metric_ids = std::move(*computation.v2_metric_ids);
  } else {
    // If `v2_metric_ids` is not specified, we will compute all metrics
    // specified in the summary specs.
    for (const auto& spec : metric_decoders) {
      metric_ids.push_back(spec.id().ToStdString());
    }
  }

  base::FlatHashMap<std::string, size_t> queries_per_metric;
  std::vector<Metric> metrics;
  for (const auto& id : metric_ids) {
    if (base::CaseInsensitiveEqual(id, "all")) {
      return base::ErrStatus(
          "Metric has id 'all' which is not allowed as this is a reserved "
          "name. Please use a different id for your metric");
    }
    queries_per_metric.Insert(id, metrics.size());
    metrics.emplace_back(Metric{id, {}, {}});
  }
  for (const auto& m : metric_decoders) {
    std::string id = m.id().ToStdString();
    if (id.empty()) {
      return base::ErrStatus("Metric with empty id field: this is not allowed");
    }
    // Only compute metrics which were populated in the map (i.e. the ones
    // which were specified in the `computation.v2_metric_ids` field).
    size_t* idx = queries_per_metric.Find(id);
    if (!idx) {
      continue;
    }
    Metric* metric = &metrics[*idx];
    if (!metric->query.empty()) {
      return base::ErrStatus(
          "Duplicate definitions for metric '%s' received: this is not "
          "allowed",
          id.c_str());
    }
    base::StatusOr<std::string> query_or =
        generator.Generate(m.query().data, m.query().size);
    if (!query_or.ok()) {
      return base::ErrStatus("Unable to build query for metric '%s': %s",
                             id.c_str(), query_or.status().c_message());
    }
    metric->query = *query_or;
    metric->spec = protozero::ConstBytes{
        m.begin(),
        static_cast<size_t>(m.end() - m.begin()),
    };
  }

  std::optional<std::string> metadata_sql;
  if (computation.metadata_query_id) {
    ASSIGN_OR_RETURN(metadata_sql,
                     generator.GenerateById(*computation.metadata_query_id));
  }

  for (const auto& preamble : generator.ComputePreambles()) {
    auto it = processor->ExecuteQuery(preamble);
    if (it.Next()) {
      return base::ErrStatus(
          "Preamble query returned results. Preambles must not return. Only "
          "the last statement of the `sql` field can return results.");
    }
    PERFETTO_CHECK(!it.Next());
    RETURN_IF_ERROR(it.Status());
  }

  for (const auto& module : generator.ComputeReferencedModules()) {
    auto it = processor->ExecuteQuery("INCLUDE PERFETTO MODULE " + module);
    PERFETTO_CHECK(!it.Next());
    RETURN_IF_ERROR(it.Status());
  }

  auto queries = generator.referenced_queries();
  base::Status status = CreateQueriesAndComputeMetrics(
      processor, pool, queries, metrics, metadata_sql, output, output_spec);

  // Make sure to cleanup all the queries.
  for (const auto& query : queries) {
    auto it =
        processor->ExecuteQuery("DROP TABLE IF EXISTS " + query.table_name);
    PERFETTO_CHECK(!it.Next());
    PERFETTO_CHECK(it.Status().ok());
  }
  return status;
}

}  // namespace perfetto::trace_processor::summary
