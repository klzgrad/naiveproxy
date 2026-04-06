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

#ifndef SRC_TRACE_PROCESSOR_SHELL_METRICS_H_
#define SRC_TRACE_PROCESSOR_SHELL_METRICS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_processor.h"

namespace perfetto::trace_processor {

enum MetricV1OutputFormat {
  kBinaryProto,
  kTextProto,
  kJson,
  kNone,
};

struct MetricNameAndPath {
  std::string name;
  std::optional<std::string> no_ext_path;
};

class MetricExtension {
 public:
  void SetDiskPath(std::string path);
  void SetVirtualPath(std::string path);

  const std::string& disk_path() const { return disk_path_; }
  const std::string& virtual_path() const { return virtual_path_; }

 private:
  std::string disk_path_;
  std::string virtual_path_;

  static void AddTrailingSlashIfNeeded(std::string& path);
};

std::string BaseName(std::string metric_path);

base::Status RegisterMetric(TraceProcessor* trace_processor,
                            const std::string& register_metric);

base::Status ParseToFileDescriptorProto(
    const std::string& filename,
    google::protobuf::FileDescriptorProto* file_desc);

base::Status ExtendMetricsProto(TraceProcessor* trace_processor,
                                const std::string& extend_metrics_proto,
                                google::protobuf::DescriptorPool* pool);

base::Status RunMetrics(TraceProcessor* trace_processor,
                        const std::vector<MetricNameAndPath>& metrics,
                        MetricV1OutputFormat format);

base::Status ParseSingleMetricExtensionPath(bool dev,
                                            const std::string& raw_extension,
                                            MetricExtension& parsed_extension);

base::Status CheckForDuplicateMetricExtension(
    const std::vector<MetricExtension>& metric_extensions);

base::Status ParseMetricExtensionPaths(
    bool dev,
    const std::vector<std::string>& raw_metric_extensions,
    std::vector<MetricExtension>& metric_extensions);

void ExtendPoolWithBinaryDescriptor(
    google::protobuf::DescriptorPool& pool,
    const void* data,
    int size,
    const std::vector<std::string>& skip_prefixes);

base::Status LoadMetricExtensionProtos(TraceProcessor* trace_processor,
                                       const std::string& proto_root,
                                       const std::string& mount_path,
                                       google::protobuf::DescriptorPool& pool);

base::Status LoadMetricExtensionSql(TraceProcessor* trace_processor,
                                    const std::string& sql_root,
                                    const std::string& mount_path);

base::Status LoadMetricExtension(TraceProcessor* trace_processor,
                                 const MetricExtension& extension,
                                 google::protobuf::DescriptorPool& pool);

base::Status PopulateDescriptorPool(
    google::protobuf::DescriptorPool& pool,
    const std::vector<MetricExtension>& metric_extensions);

base::Status LoadMetrics(TraceProcessor* trace_processor,
                         const std::string& raw_metric_names,
                         google::protobuf::DescriptorPool& pool,
                         std::vector<MetricNameAndPath>& name_and_path);

base::Status LoadMetricsAndExtensionsSql(
    TraceProcessor* trace_processor,
    const std::vector<MetricNameAndPath>& metrics,
    const std::vector<MetricExtension>& extensions);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_SHELL_METRICS_H_
