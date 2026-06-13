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

#include "src/trace_processor/shell/metrics.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <google/protobuf/compiler/parser.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/io/tokenizer.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/metrics/all_chrome_metrics.descriptor.h"
#include "src/trace_processor/metrics/all_webview_metrics.descriptor.h"
#include "src/trace_processor/metrics/metrics.descriptor.h"

namespace perfetto::trace_processor {

namespace {

class ErrorPrinter : public google::protobuf::io::ErrorCollector {
#if GOOGLE_PROTOBUF_VERSION >= 4022000
  void RecordError(int line, int col, absl::string_view msg) override {
    PERFETTO_ELOG("%d:%d: %.*s", line, col, static_cast<int>(msg.size()),
                  msg.data());
  }
  void RecordWarning(int line, int col, absl::string_view msg) override {
    PERFETTO_ILOG("%d:%d: %.*s", line, col, static_cast<int>(msg.size()),
                  msg.data());
  }
#else
  void AddError(int line, int col, const std::string& msg) override {
    PERFETTO_ELOG("%d:%d: %s", line, col, msg.c_str());
  }
  void AddWarning(int line, int col, const std::string& msg) override {
    PERFETTO_ILOG("%d:%d: %s", line, col, msg.c_str());
  }
#endif
};

}  // namespace

void MetricExtension::SetDiskPath(std::string path) {
  AddTrailingSlashIfNeeded(path);
  disk_path_ = std::move(path);
}

void MetricExtension::SetVirtualPath(std::string path) {
  AddTrailingSlashIfNeeded(path);
  virtual_path_ = std::move(path);
}

void MetricExtension::AddTrailingSlashIfNeeded(std::string& path) {
  if (path.length() > 0 && path[path.length() - 1] != '/') {
    path.push_back('/');
  }
}

std::string BaseName(std::string metric_path) {
  std::replace(metric_path.begin(), metric_path.end(), '\\', '/');
  auto slash_idx = metric_path.rfind('/');
  return slash_idx == std::string::npos ? metric_path
                                        : metric_path.substr(slash_idx + 1);
}

base::Status RegisterMetric(TraceProcessor* trace_processor,
                            const std::string& register_metric) {
  std::string sql;
  base::ReadFile(register_metric, &sql);

  std::string path = "shell/" + BaseName(register_metric);
  return trace_processor->RegisterMetric(path, sql);
}

base::Status ParseToFileDescriptorProto(
    const std::string& filename,
    google::protobuf::FileDescriptorProto* file_desc) {
  base::ScopedFile file(base::OpenFile(filename, O_RDONLY));
  if (file.get() == -1) {
    return base::ErrStatus("Failed to open proto file %s", filename.c_str());
  }

  google::protobuf::io::FileInputStream stream(file.get());
  ErrorPrinter printer;
  google::protobuf::io::Tokenizer tokenizer(&stream, &printer);

  google::protobuf::compiler::Parser parser;
  parser.Parse(&tokenizer, file_desc);
  return base::OkStatus();
}

base::Status ExtendMetricsProto(TraceProcessor* trace_processor,
                                const std::string& extend_metrics_proto,
                                google::protobuf::DescriptorPool* pool) {
  google::protobuf::FileDescriptorSet desc_set;
  auto* file_desc = desc_set.add_file();
  RETURN_IF_ERROR(ParseToFileDescriptorProto(extend_metrics_proto, file_desc));

  file_desc->set_name(BaseName(extend_metrics_proto));
  pool->BuildFile(*file_desc);

  std::vector<uint8_t> metric_proto;
  metric_proto.resize(desc_set.ByteSizeLong());
  desc_set.SerializeToArray(metric_proto.data(),
                            static_cast<int>(metric_proto.size()));

  return trace_processor->ExtendMetricsProto(metric_proto.data(),
                                             metric_proto.size());
}

base::Status RunMetrics(TraceProcessor* trace_processor,
                        const std::vector<MetricNameAndPath>& metrics,
                        MetricV1OutputFormat format) {
  std::vector<std::string> metric_names(metrics.size());
  for (size_t i = 0; i < metrics.size(); ++i) {
    metric_names[i] = metrics[i].name;
  }

  switch (format) {
    case MetricV1OutputFormat::kBinaryProto: {
      std::vector<uint8_t> metric_result;
      RETURN_IF_ERROR(
          trace_processor->ComputeMetric(metric_names, &metric_result));
      fwrite(metric_result.data(), sizeof(uint8_t), metric_result.size(),
             stdout);
      break;
    }
    case MetricV1OutputFormat::kJson: {
      std::string out;
      RETURN_IF_ERROR(trace_processor->ComputeMetricText(
          metric_names, TraceProcessor::MetricResultFormat::kJson, &out));
      out += '\n';
      fwrite(out.c_str(), sizeof(char), out.size(), stdout);
      break;
    }
    case MetricV1OutputFormat::kTextProto: {
      std::string out;
      RETURN_IF_ERROR(trace_processor->ComputeMetricText(
          metric_names, TraceProcessor::MetricResultFormat::kProtoText, &out));
      out += '\n';
      fwrite(out.c_str(), sizeof(char), out.size(), stdout);
      break;
    }
    case MetricV1OutputFormat::kNone:
      break;
  }

  return base::OkStatus();
}

base::Status ParseSingleMetricExtensionPath(bool dev,
                                            const std::string& raw_extension,
                                            MetricExtension& parsed_extension) {
  // We cannot easily use ':' as a path separator because windows paths can have
  // ':' in them (e.g. C:\foo\bar).
  std::vector<std::string> parts = base::SplitString(raw_extension, "@");
  if (parts.size() != 2 || parts[0].length() == 0 || parts[1].length() == 0) {
    return base::ErrStatus(
        "--metric-extension-dir must be of format disk_path@virtual_path");
  }

  parsed_extension.SetDiskPath(std::move(parts[0]));
  parsed_extension.SetVirtualPath(std::move(parts[1]));

  if (parsed_extension.virtual_path() == "/") {
    if (!dev) {
      return base::ErrStatus(
          "Local development features must be enabled (using the "
          "--dev flag) to override built-in metrics");
    }
    parsed_extension.SetVirtualPath("");
  }

  if (parsed_extension.virtual_path() == "shell/") {
    return base::Status(
        "Cannot have 'shell/' as metric extension virtual path.");
  }
  return base::OkStatus();
}

base::Status CheckForDuplicateMetricExtension(
    const std::vector<MetricExtension>& metric_extensions) {
  std::unordered_set<std::string> disk_paths;
  std::unordered_set<std::string> virtual_paths;
  for (const auto& extension : metric_extensions) {
    auto ret = disk_paths.insert(extension.disk_path());
    if (!ret.second) {
      return base::ErrStatus(
          "Another metric extension is already using disk path %s",
          extension.disk_path().c_str());
    }
    ret = virtual_paths.insert(extension.virtual_path());
    if (!ret.second) {
      return base::ErrStatus(
          "Another metric extension is already using virtual path %s",
          extension.virtual_path().c_str());
    }
  }
  return base::OkStatus();
}

base::Status ParseMetricExtensionPaths(
    bool dev,
    const std::vector<std::string>& raw_metric_extensions,
    std::vector<MetricExtension>& metric_extensions) {
  for (const auto& raw_extension : raw_metric_extensions) {
    metric_extensions.push_back({});
    RETURN_IF_ERROR(ParseSingleMetricExtensionPath(dev, raw_extension,
                                                   metric_extensions.back()));
  }
  return CheckForDuplicateMetricExtension(metric_extensions);
}

void ExtendPoolWithBinaryDescriptor(
    google::protobuf::DescriptorPool& pool,
    const void* data,
    int size,
    const std::vector<std::string>& skip_prefixes) {
  google::protobuf::FileDescriptorSet desc_set;
  PERFETTO_CHECK(desc_set.ParseFromArray(data, size));
  for (const auto& file_desc : desc_set.file()) {
    if (base::StartsWithAny(file_desc.name(), skip_prefixes))
      continue;
    pool.BuildFile(file_desc);
  }
}

base::Status LoadMetricExtensionProtos(TraceProcessor* trace_processor,
                                       const std::string& proto_root,
                                       const std::string& mount_path,
                                       google::protobuf::DescriptorPool& pool) {
  if (!base::FileExists(proto_root)) {
    return base::ErrStatus(
        "Directory %s does not exist. Metric extension directory must contain "
        "a 'sql/' and 'protos/' subdirectory.",
        proto_root.c_str());
  }
  std::vector<std::string> proto_files;
  RETURN_IF_ERROR(base::ListFilesRecursive(proto_root, proto_files));

  google::protobuf::FileDescriptorSet parsed_protos;
  for (const auto& file_path : proto_files) {
    if (base::GetFileExtension(file_path) != ".proto")
      continue;
    auto* file_desc = parsed_protos.add_file();
    ParseToFileDescriptorProto(proto_root + file_path, file_desc);
    file_desc->set_name(mount_path + file_path);
  }

  std::vector<uint8_t> serialized_filedescset;
  serialized_filedescset.resize(parsed_protos.ByteSizeLong());
  parsed_protos.SerializeToArray(
      serialized_filedescset.data(),
      static_cast<int>(serialized_filedescset.size()));

  // Extend the pool for any subsequent reflection-based operations
  // (e.g. output json)
  ExtendPoolWithBinaryDescriptor(
      pool, serialized_filedescset.data(),
      static_cast<int>(serialized_filedescset.size()), {});
  return trace_processor->ExtendMetricsProto(serialized_filedescset.data(),
                                             serialized_filedescset.size());
}

base::Status LoadMetricExtensionSql(TraceProcessor* trace_processor,
                                    const std::string& sql_root,
                                    const std::string& mount_path) {
  if (!base::FileExists(sql_root)) {
    return base::ErrStatus(
        "Directory %s does not exist. Metric extension directory must contain "
        "a 'sql/' and 'protos/' subdirectory.",
        sql_root.c_str());
  }

  std::vector<std::string> sql_files;
  RETURN_IF_ERROR(base::ListFilesRecursive(sql_root, sql_files));
  for (const auto& file_path : sql_files) {
    if (base::GetFileExtension(file_path) != ".sql")
      continue;
    std::string file_contents;
    if (!base::ReadFile(sql_root + file_path, &file_contents)) {
      return base::ErrStatus("Cannot read file %s", file_path.c_str());
    }
    RETURN_IF_ERROR(
        trace_processor->RegisterMetric(mount_path + file_path, file_contents));
  }
  return base::OkStatus();
}

base::Status LoadMetricExtension(TraceProcessor* trace_processor,
                                 const MetricExtension& extension,
                                 google::protobuf::DescriptorPool& pool) {
  const std::string& disk_path = extension.disk_path();
  const std::string& virtual_path = extension.virtual_path();

  if (!base::FileExists(disk_path)) {
    return base::ErrStatus("Metric extension directory %s does not exist",
                           disk_path.c_str());
  }

  // Note: Proto files must be loaded first, because we determine whether an SQL
  // file is a metric or not by checking if the name matches a field of the root
  // TraceMetrics proto.
  RETURN_IF_ERROR(
      LoadMetricExtensionProtos(trace_processor, disk_path + "protos/",
                                kMetricProtoRoot + virtual_path, pool));
  RETURN_IF_ERROR(LoadMetricExtensionSql(trace_processor, disk_path + "sql/",
                                         virtual_path));

  return base::OkStatus();
}

base::Status PopulateDescriptorPool(
    google::protobuf::DescriptorPool& pool,
    const std::vector<MetricExtension>& metric_extensions) {
  // TODO(b/182165266): There is code duplication here with trace_processor_impl
  // SetupMetrics. This will be removed when we switch the output formatter to
  // use internal DescriptorPool.
  std::vector<std::string> skip_prefixes;
  skip_prefixes.reserve(metric_extensions.size());
  for (const auto& ext : metric_extensions) {
    skip_prefixes.push_back(kMetricProtoRoot + ext.virtual_path());
  }
  ExtendPoolWithBinaryDescriptor(pool, kMetricsDescriptor.data(),
                                 kMetricsDescriptor.size(), skip_prefixes);
  ExtendPoolWithBinaryDescriptor(pool, kAllChromeMetricsDescriptor.data(),
                                 kAllChromeMetricsDescriptor.size(),
                                 skip_prefixes);
  ExtendPoolWithBinaryDescriptor(pool, kAllWebviewMetricsDescriptor.data(),
                                 kAllWebviewMetricsDescriptor.size(),
                                 skip_prefixes);
  return base::OkStatus();
}

base::Status LoadMetrics(TraceProcessor* trace_processor,
                         const std::string& raw_metric_names,
                         google::protobuf::DescriptorPool& pool,
                         std::vector<MetricNameAndPath>& name_and_path) {
  std::vector<std::string> split;
  for (base::StringSplitter ss(raw_metric_names, ','); ss.Next();) {
    split.emplace_back(ss.cur_token());
  }

  // For all metrics which are files, register them and extend the metrics
  // proto.
  for (const std::string& metric_or_path : split) {
    // If there is no extension, we assume it is a builtin metric.
    auto ext_idx = metric_or_path.rfind('.');
    if (ext_idx == std::string::npos) {
      name_and_path.emplace_back(
          MetricNameAndPath{metric_or_path, std::nullopt});
      continue;
    }

    std::string no_ext_path = metric_or_path.substr(0, ext_idx);

    // The proto must be extended before registering the metric.
    base::Status status =
        ExtendMetricsProto(trace_processor, no_ext_path + ".proto", &pool);
    if (!status.ok()) {
      return base::ErrStatus("Unable to extend metrics proto %s: %s",
                             metric_or_path.c_str(), status.c_message());
    }

    status = RegisterMetric(trace_processor, no_ext_path + ".sql");
    if (!status.ok()) {
      return base::ErrStatus("Unable to register metric %s: %s",
                             metric_or_path.c_str(), status.c_message());
    }
    name_and_path.emplace_back(
        MetricNameAndPath{BaseName(no_ext_path), no_ext_path});
  }
  return base::OkStatus();
}

base::Status LoadMetricsAndExtensionsSql(
    TraceProcessor* trace_processor,
    const std::vector<MetricNameAndPath>& metrics,
    const std::vector<MetricExtension>& extensions) {
  for (const MetricExtension& extension : extensions) {
    const std::string& disk_path = extension.disk_path();
    const std::string& virtual_path = extension.virtual_path();

    RETURN_IF_ERROR(LoadMetricExtensionSql(trace_processor, disk_path + "sql/",
                                           virtual_path));
  }

  for (const MetricNameAndPath& metric : metrics) {
    // Ignore builtin metrics.
    if (!metric.no_ext_path.has_value())
      continue;
    RETURN_IF_ERROR(
        RegisterMetric(trace_processor, metric.no_ext_path.value() + ".sql"));
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
