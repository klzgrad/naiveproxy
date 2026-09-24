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

#include "src/trace_processor/rpc/remote_trace_processor.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/iterator.h"
#include "perfetto/trace_processor/metatrace_config.h"
#include "src/trace_processor/iterator_impl.h"
#include "src/trace_processor/rpc/query_result_deserializer.h"
#include "src/trace_processor/rpc/rpc_transport.h"

#include "protos/perfetto/trace_processor/metatrace_categories.pbzero.h"
#include "protos/perfetto/trace_processor/trace_processor.pbzero.h"
#include "protos/perfetto/trace_summary/file.pbzero.h"

namespace perfetto::trace_processor {
namespace {

using RpcProto = protos::pbzero::TraceProcessorRpc;

// Builds a single-message TraceProcessorRpcStream; |populate| sets the args.
// seq stays 0 so the server skips its cross-connection ordering check (rpc.cc).
template <typename Fn>
std::vector<uint8_t> BuildStream(RpcProto::TraceProcessorMethod method,
                                 Fn populate) {
  protozero::HeapBuffered<protos::pbzero::TraceProcessorRpcStream> stream;
  RpcProto* rpc = stream->add_msg();
  rpc->set_request(method);
  populate(rpc);
  return stream.SerializeAsArray();
}

// Checks the envelope of a response message for transport-level failures.
base::Status CheckRpcEnvelope(const RpcProto::Decoder& d) {
  if (d.has_fatal_error())
    return base::ErrStatus("session: %s",
                           d.fatal_error().ToStdString().c_str());
  if (d.has_invalid_request())
    return base::ErrStatus("session does not support this request");
  return base::OkStatus();
}

// Maps a result proto's `error` field to a Status.
template <typename Decoder>
base::Status ResultError(const Decoder& result) {
  if (result.has_error() && result.error().size > 0)
    return base::ErrStatus("%s", result.error().ToStdString().c_str());
  return base::OkStatus();
}

}  // namespace

// Streams a TPM_QUERY_STREAMING or TPM_STATEMENT_STREAMING response, pulling
// one message off the socket per refill. Lazy reads let the server's blocking
// Send() apply backpressure instead of buffering the whole result.
class RemoteIteratorImpl : public IteratorImpl {
 public:
  // Which streaming endpoint the response comes from: kStatement responses
  // wrap each QueryResult in a StatementResult carrying the cursor metadata.
  enum class Mode { kQuery, kStatement };

  // |rtp| must outlive the iterator; the request is already sent.
  explicit RemoteIteratorImpl(RemoteTraceProcessor* rtp,
                              Mode mode = Mode::kQuery)
      : rtp_(rtp), mode_(mode) {}
  // Fails immediately with |status|; nothing was sent (rtp_ stays null).
  explicit RemoteIteratorImpl(base::Status status)
      : status_(std::move(status)), stream_done_(true) {}
  ~RemoteIteratorImpl() override;  // Out-of-line: anchors the vtable.

  // kStatement only: eagerly reads the first response message, which carries
  // the statement-cursor metadata (tail_offset, statement_executed). Any
  // rows it contains stay queued for Next(). Returns the iterator's status;
  // on failure the iterator carries the same error.
  base::Status PrimeStatementStream() {
    PERFETTO_DCHECK(mode_ == Mode::kStatement);
    if (base::Status s = ReadNextMessage(); !s.ok())
      status_ = std::move(s);
    return status_;
  }
  uint32_t tail_offset() const { return tail_offset_; }
  bool statement_executed() const { return statement_executed_; }

  bool Next() override {
    if (!status_.ok())
      return false;
    // A message may carry only metadata or zero rows, so refill until we have a
    // row or hit end-of-stream.
    while (pending_.empty() && !stream_done_) {
      if (base::Status s = ReadNextMessage(); !s.ok()) {
        status_ = std::move(s);
        return false;
      }
      if (!status_.ok())  // A query error reported inside the result.
        return false;
    }
    if (pending_.empty())
      return false;
    current_ = std::move(pending_.front());
    pending_.pop_front();
    return true;
  }

  SqlValue Get(uint32_t col) const override {
    return current_[col].ToSqlValue();
  }

  std::string GetColumnName(uint32_t col) const override {
    const auto& names = deser_.column_names();
    return col < names.size() ? names[col] : "";
  }

  base::Status Status() const override { return status_; }
  uint32_t ColumnCount() const override {
    return static_cast<uint32_t>(deser_.column_names().size());
  }
  uint32_t StatementCount() const override { return deser_.statement_count(); }
  uint32_t StatementCountWithOutput() const override {
    return deser_.statement_with_output_count();
  }
  std::string LastStatementSql() override {
    return deser_.last_statement_sql();
  }

 private:
  using Cell = QueryResultDeserializer::Cell;

  // Reads one response message and appends its completed rows to |pending_|.
  // On failure the stream is marked done: after a transport failure or an
  // envelope error (fatal_error, or invalid_request from a server that
  // doesn't support the method) the server sends nothing further for this
  // request, so the destructor's drain loop must not block waiting for
  // messages that will never arrive.
  base::Status ReadNextMessage() {
    base::Status s = ReadNextMessageImpl();
    if (!s.ok())
      stream_done_ = true;
    return s;
  }

  base::Status ReadNextMessageImpl() {
    RETURN_IF_ERROR(rtp_->ReadResponse(&msg_buf_));
    RpcProto::Decoder d(msg_buf_.data(), msg_buf_.size());
    RETURN_IF_ERROR(CheckRpcEnvelope(d));

    protozero::ConstBytes qr;
    if (mode_ == Mode::kStatement) {
      protos::pbzero::StatementResult::Decoder stmt_res(d.statement_result());
      if (stmt_res.has_statement_executed()) {
        statement_executed_ = stmt_res.statement_executed();
        tail_offset_ = stmt_res.tail_offset();
      }
      qr = stmt_res.result();
    } else {
      qr = d.query_result();
    }
    std::vector<Cell> cells;
    RETURN_IF_ERROR(deser_.AddMessage(qr.data, qr.size, &cells));

    // A row can span batch/message boundaries; accumulate until `ncols` wide.
    const size_t ncols = deser_.column_names().size();
    for (auto& cell : cells) {
      cur_row_.push_back(std::move(cell));
      if (ncols > 0 && cur_row_.size() == ncols) {
        pending_.push_back(std::move(cur_row_));
        cur_row_.clear();
      }
    }
    if (!deser_.error().empty())
      status_ = base::ErrStatus("%s", deser_.error().c_str());
    if (deser_.eof()) {
      stream_done_ = true;
      // Socket is back at a message boundary; safe for the next request.
      rtp_->stream_in_flight_ = false;
    }
    return base::OkStatus();
  }

  RemoteTraceProcessor* rtp_ = nullptr;
  Mode mode_ = Mode::kQuery;
  QueryResultDeserializer deser_;
  std::vector<uint8_t> msg_buf_;           // Current response bytes.
  std::vector<Cell> cur_row_;              // Partial row spanning messages.
  std::deque<std::vector<Cell>> pending_;  // Completed rows awaiting Next().
  std::vector<Cell> current_;              // The row Get() reads from.
  base::Status status_;
  bool stream_done_ = false;
  // kStatement only: cursor metadata from the first response message.
  uint32_t tail_offset_ = 0;
  bool statement_executed_ = false;
};

RemoteIteratorImpl::~RemoteIteratorImpl() {
  // If the consumer stopped early (e.g. the pager's 'q'), drain the unread
  // batches so the socket is left at a message boundary. Bounded: the server
  // always ends the stream with is_last_batch. rtp_ is null for the error
  // iterator (nothing was sent).
  while (rtp_ && !stream_done_) {
    pending_.clear();  // Discarding the result; don't accumulate rows.
    cur_row_.clear();
    if (base::Status s = ReadNextMessage(); !s.ok())
      break;
  }
  // No consumer remains: clear the flag so the next request isn't blocked by
  // the CHECK (it will surface any dead-socket error itself).
  if (rtp_)
    rtp_->stream_in_flight_ = false;
}

namespace {

// Builds an Iterator that fails immediately with |status|.
Iterator ErrorIterator(base::Status status) {
  return Iterator(std::make_unique<RemoteIteratorImpl>(std::move(status)));
}

}  // namespace

// A Summarizer mirrored on the server: created via TPM_CREATE_SUMMARIZER,
// UpdateSpec/Query forwarded, destroyed on dtor.
class RemoteSummarizer : public Summarizer {
 public:
  RemoteSummarizer(RemoteTraceProcessor* rtp, std::string id)
      : rtp_(rtp), id_(std::move(id)) {}
  ~RemoteSummarizer() override;

  base::Status UpdateSpec(const uint8_t* spec_data,
                          size_t spec_size,
                          SummarizerUpdateSpecResult* result) override;
  base::Status Query(const std::string& query_id,
                     SummarizerQueryResult* result) override;

 private:
  RemoteTraceProcessor* rtp_;
  std::string id_;
};

base::Status RemoteTraceProcessor::Export(ExportFormat, ExportOutput*) {
  return base::ErrStatus("Export is not supported over remote connections");
}

base::StatusOr<std::unique_ptr<RemoteTraceProcessor>>
RemoteTraceProcessor::Connect(const std::string& addr) {
  ASSIGN_OR_RETURN(std::unique_ptr<RpcTransport> transport,
                   ConnectRpcTransport(addr));
  return std::unique_ptr<RemoteTraceProcessor>(
      new RemoteTraceProcessor(std::move(transport)));
}

RemoteTraceProcessor::RemoteTraceProcessor(
    std::unique_ptr<RpcTransport> transport)
    : transport_(std::move(transport)) {}

RemoteTraceProcessor::~RemoteTraceProcessor() = default;

base::Status RemoteTraceProcessor::SendStream(
    const std::vector<uint8_t>& framed) {
  // A live query stream owns the transport; a concurrent request would
  // interleave with its unread batches. That's a caller bug, so fail fast.
  PERFETTO_CHECK(!stream_in_flight_);
  return transport_->Send(framed.data(), framed.size());
}

base::Status RemoteTraceProcessor::ReadResponse(std::vector<uint8_t>* out) {
  // Hoisted out of the loop: ProtoRingBuffer's fastpath can return a message
  // pointing into the last buffer passed to Append(), so it must stay alive.
  uint8_t buf[4096];
  for (;;) {
    auto msg = rxbuf_.ReadMessage();
    if (msg.fatal_framing_error)
      return base::ErrStatus("RPC framing error from session");
    if (msg.valid()) {
      out->assign(msg.start, msg.start + msg.len);
      return base::OkStatus();
    }
    ASSIGN_OR_RETURN(size_t n, transport_->Recv(buf, sizeof(buf)));
    if (n == 0)
      return base::ErrStatus("Session closed the connection");
    rxbuf_.Append(buf, n);
  }
}

template <typename Fn, typename Handle>
base::Status RemoteTraceProcessor::RoundTrip(uint32_t method,
                                             Fn populate,
                                             Handle handle) {
  RETURN_IF_ERROR(SendStream(BuildStream(
      static_cast<RpcProto::TraceProcessorMethod>(method), populate)));
  std::vector<uint8_t> resp;
  RETURN_IF_ERROR(ReadResponse(&resp));
  RpcProto::Decoder d(resp.data(), resp.size());
  RETURN_IF_ERROR(CheckRpcEnvelope(d));
  return handle(d);
}

Iterator RemoteTraceProcessor::ExecuteQuery(const std::string& sql) {
  auto req = BuildStream(RpcProto::TPM_QUERY_STREAMING, [&](RpcProto* rpc) {
    rpc->set_query_args()->set_sql_query(sql);
  });
  if (base::Status s = SendStream(req); !s.ok())
    return ErrorIterator(s);
  stream_in_flight_ = true;
  return Iterator(std::make_unique<RemoteIteratorImpl>(this));
}

std::optional<Iterator> RemoteTraceProcessor::ExecuteNextStatement(
    const std::string& sql,
    uint32_t* offset) {
  auto req = BuildStream(RpcProto::TPM_STATEMENT_STREAMING, [&](RpcProto* rpc) {
    auto* args = rpc->set_statement_args();
    args->set_sql(sql);
    args->set_start_offset(*offset);
  });
  if (base::Status s = SendStream(req); !s.ok())
    return ErrorIterator(std::move(s));
  stream_in_flight_ = true;
  auto impl = std::make_unique<RemoteIteratorImpl>(
      this, RemoteIteratorImpl::Mode::kStatement);
  // The first message carries the cursor metadata, so read it eagerly; any
  // rows it contains stay queued for Next(). On failure the returned
  // iterator carries the error (|*offset| stays unchanged, matching the
  // local implementation).
  if (base::Status s = impl->PrimeStatementStream(); !s.ok())
    return Iterator(std::move(impl));
  *offset = impl->tail_offset();
  if (!impl->statement_executed()) {
    // The stream ended with the first message; the impl's destructor leaves
    // the transport at a message boundary.
    return std::nullopt;
  }
  return Iterator(std::move(impl));
}

base::Status RemoteTraceProcessor::RegisterSqlPackage(SqlPackage package) {
  return RoundTrip(
      RpcProto::TPM_REGISTER_SQL_PACKAGE,
      [&](RpcProto* rpc) {
        auto* args = rpc->set_register_sql_package_args();
        args->set_package_name(package.name);
        args->set_allow_override(package.allow_override);
        for (const auto& module : package.modules) {
          auto* m = args->add_modules();
          m->set_name(module.first);
          m->set_sql(module.second);
        }
      },
      [](RpcProto::Decoder& d) {
        return ResultError(protos::pbzero::RegisterSqlPackageResult::Decoder(
            d.register_sql_package_result()));
      });
}

base::Status RemoteTraceProcessor::ComputeMetric(
    const std::vector<std::string>& metric_names,
    std::vector<uint8_t>* metrics_proto) {
  return RoundTrip(
      RpcProto::TPM_COMPUTE_METRIC,
      [&](RpcProto* rpc) {
        auto* args = rpc->set_compute_metric_args();
        for (const auto& name : metric_names)
          args->add_metric_names(name);
        args->set_format(protos::pbzero::ComputeMetricArgs::BINARY_PROTOBUF);
      },
      [&](RpcProto::Decoder& d) -> base::Status {
        protos::pbzero::ComputeMetricResult::Decoder result(d.metric_result());
        RETURN_IF_ERROR(ResultError(result));
        protozero::ConstBytes m = result.metrics();
        metrics_proto->assign(m.data, m.data + m.size);
        return base::OkStatus();
      });
}

base::Status RemoteTraceProcessor::ComputeMetricText(
    const std::vector<std::string>& metric_names,
    MetricResultFormat format,
    std::string* metrics_string) {
  return RoundTrip(
      RpcProto::TPM_COMPUTE_METRIC,
      [&](RpcProto* rpc) {
        auto* args = rpc->set_compute_metric_args();
        for (const auto& name : metric_names)
          args->add_metric_names(name);
        args->set_format(format == kJson
                             ? protos::pbzero::ComputeMetricArgs::JSON
                             : protos::pbzero::ComputeMetricArgs::TEXTPROTO);
      },
      [&](RpcProto::Decoder& d) -> base::Status {
        protos::pbzero::ComputeMetricResult::Decoder result(d.metric_result());
        RETURN_IF_ERROR(ResultError(result));
        *metrics_string = format == kJson
                              ? result.metrics_as_json().ToStdString()
                              : result.metrics_as_prototext().ToStdString();
        return base::OkStatus();
      });
}

std::vector<uint8_t> RemoteTraceProcessor::GetMetricDescriptors() {
  std::vector<uint8_t> descriptors;
  base::ignore_result(RoundTrip(
      RpcProto::TPM_GET_METRIC_DESCRIPTORS, [](RpcProto*) {},
      [&](RpcProto::Decoder& d) {
        protozero::ConstBytes ds = d.metric_descriptors();
        descriptors.assign(ds.data, ds.data + ds.size);
        return base::OkStatus();
      }));
  return descriptors;
}

base::Status RemoteTraceProcessor::Summarize(
    const TraceSummaryComputationSpec& computation,
    const std::vector<TraceSummarySpecBytes>& specs,
    std::vector<uint8_t>* output,
    const TraceSummaryOutputSpec& output_spec) {
  return RoundTrip(
      RpcProto::TPM_SUMMARIZE_TRACE,
      [&](RpcProto* rpc) {
        auto* args = rpc->set_trace_summary_args();
        auto* comp = args->set_computation_spec();
        if (computation.v2_metric_ids.has_value()) {
          for (const auto& id : *computation.v2_metric_ids)
            comp->add_metric_ids(id);
        } else {
          comp->set_run_all_metrics(true);
        }
        if (computation.metadata_query_id.has_value())
          comp->set_metadata_query_id(*computation.metadata_query_id);
        for (const auto& spec : specs) {
          if (spec.format == TraceSummarySpecBytes::Format::kBinaryProto) {
            args->add_proto_specs()->AppendRawProtoBytes(spec.ptr, spec.size);
          } else {
            args->add_textproto_specs(std::string(
                reinterpret_cast<const char*>(spec.ptr), spec.size));
          }
        }
        args->set_output_format(
            output_spec.format == TraceSummaryOutputSpec::Format::kTextProto
                ? protos::pbzero::TraceSummaryArgs::TEXTPROTO
                : protos::pbzero::TraceSummaryArgs::BINARY_PROTOBUF);
      },
      [&](RpcProto::Decoder& d) -> base::Status {
        protos::pbzero::TraceSummaryResult::Decoder result(
            d.trace_summary_result());
        RETURN_IF_ERROR(ResultError(result));
        if (output_spec.format == TraceSummaryOutputSpec::Format::kTextProto) {
          protozero::ConstChars out = result.textproto_summary();
          output->assign(out.data, out.data + out.size);
        } else {
          protozero::ConstBytes out = result.proto_summary();
          output->assign(out.data, out.data + out.size);
        }
        return base::OkStatus();
      });
}

void RemoteTraceProcessor::EnableMetatrace(MetatraceConfig config) {
  using ProtoEnum = protos::pbzero::MetatraceCategories;
  int32_t proto_categories = 0;
  if (config.categories & metatrace::MetatraceCategories::QUERY_TIMELINE)
    proto_categories |= ProtoEnum::QUERY_TIMELINE;
  if (config.categories & metatrace::MetatraceCategories::QUERY_DETAILED)
    proto_categories |= ProtoEnum::QUERY_DETAILED;
  if (config.categories & metatrace::MetatraceCategories::FUNCTION_CALL)
    proto_categories |= ProtoEnum::FUNCTION_CALL;
  if (config.categories & metatrace::MetatraceCategories::DB)
    proto_categories |= ProtoEnum::DB;
  if (config.categories & metatrace::MetatraceCategories::API_TIMELINE)
    proto_categories |= ProtoEnum::API_TIMELINE;
  base::ignore_result(RoundTrip(
      RpcProto::TPM_ENABLE_METATRACE,
      [&](RpcProto* rpc) {
        rpc->set_enable_metatrace_args()->set_categories(
            static_cast<ProtoEnum>(proto_categories));
      },
      [](RpcProto::Decoder&) { return base::OkStatus(); }));
}

base::Status RemoteTraceProcessor::DisableAndReadMetatrace(
    std::vector<uint8_t>* trace_proto) {
  return RoundTrip(
      RpcProto::TPM_DISABLE_AND_READ_METATRACE, [](RpcProto*) {},
      [&](RpcProto::Decoder& d) -> base::Status {
        protos::pbzero::DisableAndReadMetatraceResult::Decoder result(
            d.metatrace());
        RETURN_IF_ERROR(ResultError(result));
        protozero::ConstBytes m = result.metatrace();
        trace_proto->assign(m.data, m.data + m.size);
        return base::OkStatus();
      });
}

std::string RemoteTraceProcessor::GetCurrentTraceName() {
  std::string name;
  base::ignore_result(RoundTrip(
      RpcProto::TPM_GET_STATUS, [](RpcProto*) {},
      [&](RpcProto::Decoder& d) {
        protos::pbzero::StatusResult::Decoder status(d.status());
        name = status.loaded_trace_name().ToStdString();
        return base::OkStatus();
      }));
  return name;
}

size_t RemoteTraceProcessor::RestoreInitialTables() {
  base::ignore_result(RoundTrip(
      RpcProto::TPM_RESTORE_INITIAL_TABLES, [](RpcProto*) {},
      [](RpcProto::Decoder&) { return base::OkStatus(); }));
  return 0;
}

base::Status RemoteTraceProcessor::CreateSummarizer(
    std::unique_ptr<Summarizer>* out) {
  std::string id = "rtp-" + std::to_string(next_summarizer_id_++);
  RETURN_IF_ERROR(RoundTrip(
      RpcProto::TPM_CREATE_SUMMARIZER,
      [&](RpcProto* rpc) {
        rpc->set_create_summarizer_args()->set_summarizer_id(id);
      },
      [](RpcProto::Decoder& d) {
        return ResultError(protos::pbzero::CreateSummarizerResult::Decoder(
            d.create_summarizer_result()));
      }));
  *out = std::make_unique<RemoteSummarizer>(this, id);
  return base::OkStatus();
}

// --- Trace-mutation / load-side methods over the wire -----------------------

base::Status RemoteTraceProcessor::Parse(TraceBlobView blob) {
  return RoundTrip(
      RpcProto::TPM_APPEND_TRACE_DATA,
      [&](RpcProto* rpc) {
        rpc->set_append_trace_data(blob.data(), blob.size());
      },
      [](RpcProto::Decoder& d) {
        return ResultError(
            protos::pbzero::AppendTraceDataResult::Decoder(d.append_result()));
      });
}

void RemoteTraceProcessor::Flush() {
  // No RPC verb: the server flushes on NotifyEndOfFile(). No-op.
}

base::Status RemoteTraceProcessor::NotifyEndOfFile() {
  return RoundTrip(
      RpcProto::TPM_FINALIZE_TRACE_DATA, [](RpcProto*) {},
      [](RpcProto::Decoder& d) {
        return ResultError(protos::pbzero::FinalizeDataResult::Decoder(
            d.finalize_data_result()));
      });
}

void RemoteTraceProcessor::InterruptQuery() {
  // No-op over a remote transport: the in-flight request runs to completion on
  // the server. (The local one-shot path still handles Ctrl-C itself.)
}

// --- Methods with no RPC-protocol equivalent --------------------------------

void RemoteTraceProcessor::SetCurrentTraceName(const std::string&) {}

base::Status RemoteTraceProcessor::RegisterFileContent(const std::string&,
                                                       TraceBlob) {
  return base::ErrStatus("RegisterFileContent is not supported over --remote");
}

base::Status RemoteTraceProcessor::RegisterMetric(const std::string&,
                                                  const std::string&) {
  return base::ErrStatus("RegisterMetric is not supported over --remote");
}

base::Status RemoteTraceProcessor::ExtendMetricsProto(const uint8_t*, size_t) {
  return base::ErrStatus("ExtendMetricsProto is not supported over --remote");
}

base::Status RemoteTraceProcessor::ExtendMetricsProto(
    const uint8_t*,
    size_t,
    const std::vector<std::string>&) {
  return base::ErrStatus("ExtendMetricsProto is not supported over --remote");
}

RemoteSummarizer::~RemoteSummarizer() {
  base::ignore_result(rtp_->RoundTrip(
      RpcProto::TPM_DESTROY_SUMMARIZER,
      [&](RpcProto* rpc) {
        rpc->set_destroy_summarizer_args()->set_summarizer_id(id_);
      },
      [](RpcProto::Decoder&) { return base::OkStatus(); }));
}

base::Status RemoteSummarizer::UpdateSpec(const uint8_t* spec_data,
                                          size_t spec_size,
                                          SummarizerUpdateSpecResult* result) {
  return rtp_->RoundTrip(
      RpcProto::TPM_UPDATE_SUMMARIZER_SPEC,
      [&](RpcProto* rpc) {
        auto* args = rpc->set_update_summarizer_spec_args();
        args->set_summarizer_id(id_);
        args->set_spec()->AppendRawProtoBytes(spec_data, spec_size);
      },
      [&](RpcProto::Decoder& d) -> base::Status {
        protos::pbzero::UpdateSummarizerSpecResult::Decoder res(
            d.update_summarizer_spec_result());
        RETURN_IF_ERROR(ResultError(res));
        for (auto it = res.queries(); it; ++it) {
          protos::pbzero::SummarizerQuerySyncInfo::Decoder q(*it);
          SummarizerUpdateSpecResult::QuerySyncInfo info;
          info.query_id = q.query_id().ToStdString();
          if (q.has_error())
            info.error = q.error().ToStdString();
          info.was_updated = q.was_updated();
          info.was_dropped = q.was_dropped();
          result->queries.push_back(std::move(info));
        }
        return base::OkStatus();
      });
}

base::Status RemoteSummarizer::Query(const std::string& query_id,
                                     SummarizerQueryResult* result) {
  return rtp_->RoundTrip(
      RpcProto::TPM_QUERY_SUMMARIZER,
      [&](RpcProto* rpc) {
        auto* args = rpc->set_query_summarizer_args();
        args->set_summarizer_id(id_);
        args->set_query_id(query_id);
      },
      [&](RpcProto::Decoder& d) -> base::Status {
        protos::pbzero::QuerySummarizerResult::Decoder res(
            d.query_summarizer_result());
        result->exists = res.exists();
        RETURN_IF_ERROR(ResultError(res));
        if (res.exists()) {
          result->table_name = res.table_name().ToStdString();
          result->row_count = res.row_count();
          for (auto it = res.columns(); it; ++it)
            result->columns.push_back(it->as_std_string());
          result->duration_ms = res.duration_ms();
          result->sql = res.sql().ToStdString();
          result->textproto = res.textproto().ToStdString();
          result->standalone_sql = res.standalone_sql().ToStdString();
        }
        return base::OkStatus();
      });
}

}  // namespace perfetto::trace_processor
