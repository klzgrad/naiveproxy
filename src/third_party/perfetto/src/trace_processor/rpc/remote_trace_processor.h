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

#ifndef SRC_TRACE_PROCESSOR_RPC_REMOTE_TRACE_PROCESSOR_H_
#define SRC_TRACE_PROCESSOR_RPC_REMOTE_TRACE_PROCESSOR_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/protozero/proto_ring_buffer.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/summarizer.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/rpc/rpc_transport.h"

namespace perfetto::trace_processor {

// A TraceProcessor that executes every method over the wire against a warm
// `server unix` (or, in future, http) session instead of a local engine: each
// method marshals to the matching TraceProcessorRpc verb and unmarshals the
// response. Methods with no RPC equivalent (RegisterMetric, ExtendMetricsProto,
// RegisterFileContent) return an error. Being a TraceProcessor, every existing
// consumer works against a remote session unchanged.
class RemoteTraceProcessor : public TraceProcessor {
 public:
  // Connects to |addr|, which is resolved as in `--remote`: an absolute path or
  // *.sock is a socket path; a bare name is a session name resolved to the
  // convention socket path; host:port/scheme:// is a WebSocket to an http
  // server.
  static base::StatusOr<std::unique_ptr<RemoteTraceProcessor>> Connect(
      const std::string& addr);

  ~RemoteTraceProcessor() override;

  // TraceProcessorStorage:
  base::Status Parse(TraceBlobView) override;
  void Flush() override;
  base::Status NotifyEndOfFile() override;

  // TraceProcessor:
  Iterator ExecuteQuery(const std::string& sql) override;
  std::optional<Iterator> ExecuteNextStatement(const std::string& sql,
                                               uint32_t* offset) override;
  base::Status RegisterSqlPackage(SqlPackage) override;
  base::Status Summarize(const TraceSummaryComputationSpec& computation,
                         const std::vector<TraceSummarySpecBytes>& specs,
                         std::vector<uint8_t>* output,
                         const TraceSummaryOutputSpec& output_spec) override;
  void EnableMetatrace(MetatraceConfig config) override;
  base::Status DisableAndReadMetatrace(
      std::vector<uint8_t>* trace_proto) override;
  std::string GetCurrentTraceName() override;
  void SetCurrentTraceName(const std::string&) override;
  base::Status RegisterFileContent(const std::string& path,
                                   TraceBlob content) override;
  void InterruptQuery() override;
  size_t RestoreInitialTables() override;
  base::Status RegisterMetric(const std::string& path,
                              const std::string& sql) override;
  base::Status ExtendMetricsProto(const uint8_t* data, size_t size) override;
  base::Status ExtendMetricsProto(
      const uint8_t* data,
      size_t size,
      const std::vector<std::string>& skip_prefixes) override;
  base::Status ComputeMetric(const std::vector<std::string>& metric_names,
                             std::vector<uint8_t>* metrics_proto) override;
  base::Status ComputeMetricText(const std::vector<std::string>& metric_names,
                                 MetricResultFormat format,
                                 std::string* metrics_string) override;
  std::vector<uint8_t> GetMetricDescriptors() override;
  base::Status Export(ExportFormat format, ExportOutput* output) override;
  base::Status CreateSummarizer(std::unique_ptr<Summarizer>* out) override;

 private:
  friend class RemoteSummarizer;
  friend class RemoteIteratorImpl;

  explicit RemoteTraceProcessor(std::unique_ptr<RpcTransport> transport);

  // Sends |framed| (a fully-serialized TraceProcessorRpcStream) over the
  // transport, blocking until all bytes are written.
  base::Status SendStream(const std::vector<uint8_t>& framed);
  // Reads exactly one TraceProcessorRpc message into |out| (the serialized
  // message bytes, stream framing already stripped), blocking.
  base::Status ReadResponse(std::vector<uint8_t>* out);

  // Shared shape for non-streaming methods: build the request with |populate|,
  // send it, read the one response, check the envelope, then let |handle|
  // decode the method-specific result.
  template <typename Fn, typename Handle>
  base::Status RoundTrip(uint32_t method, Fn populate, Handle handle);

  std::unique_ptr<RpcTransport> transport_;
  protozero::ProtoRingBuffer rxbuf_;
  int next_summarizer_id_ = 0;

  // True while a query stream still has unread batches on the shared socket.
  // SendStream CHECKs against it (a concurrent request would interleave reads);
  // RemoteIteratorImpl clears it once drained, in Next() or its destructor.
  bool stream_in_flight_ = false;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_RPC_REMOTE_TRACE_PROCESSOR_H_
