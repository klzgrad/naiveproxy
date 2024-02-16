#include "quiche/http2/adapter/nghttp2_adapter.h"

#include <memory>

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/nghttp2.h"
#include "quiche/http2/adapter/nghttp2_callbacks.h"
#include "quiche/http2/adapter/nghttp2_data_provider.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_endian.h"

namespace http2 {
namespace adapter {

namespace {

using ConnectionError = Http2VisitorInterface::ConnectionError;

// A metadata source that deletes itself upon completion.
class SelfDeletingMetadataSource : public MetadataSource {
 public:
  explicit SelfDeletingMetadataSource(std::unique_ptr<MetadataSource> source)
      : source_(std::move(source)) {}

  size_t NumFrames(size_t max_frame_size) const override {
    return source_->NumFrames(max_frame_size);
  }

  std::pair<int64_t, bool> Pack(uint8_t* dest, size_t dest_len) override {
    const auto result = source_->Pack(dest, dest_len);
    if (result.first < 0 || result.second) {
      delete this;
    }
    return result;
  }

  void OnFailure() override {
    source_->OnFailure();
    delete this;
  }

 private:
  std::unique_ptr<MetadataSource> source_;
};

}  // anonymous namespace

/* static */
std::unique_ptr<NgHttp2Adapter> NgHttp2Adapter::CreateClientAdapter(
    Http2VisitorInterface& visitor, const nghttp2_option* options) {
  auto adapter = new NgHttp2Adapter(visitor, Perspective::kClient, options);
  adapter->Initialize();
  return absl::WrapUnique(adapter);
}

/* static */
std::unique_ptr<NgHttp2Adapter> NgHttp2Adapter::CreateServerAdapter(
    Http2VisitorInterface& visitor, const nghttp2_option* options) {
  auto adapter = new NgHttp2Adapter(visitor, Perspective::kServer, options);
  adapter->Initialize();
  return absl::WrapUnique(adapter);
}

bool NgHttp2Adapter::IsServerSession() const {
  int result = nghttp2_session_check_server_session(session_->raw_ptr());
  QUICHE_DCHECK_EQ(perspective_ == Perspective::kServer, result > 0);
  return result > 0;
}

int64_t NgHttp2Adapter::ProcessBytes(absl::string_view bytes) {
  const int64_t processed_bytes = session_->ProcessBytes(bytes);
  if (processed_bytes < 0) {
    visitor_.OnConnectionError(ConnectionError::kParseError);
  }
  return processed_bytes;
}

void NgHttp2Adapter::SubmitSettings(absl::Span<const Http2Setting> settings) {
  // Submit SETTINGS, converting each Http2Setting to an nghttp2_settings_entry.
  std::vector<nghttp2_settings_entry> nghttp2_settings;
  absl::c_transform(settings, std::back_inserter(nghttp2_settings),
                    [](const Http2Setting& setting) {
                      return nghttp2_settings_entry{setting.id, setting.value};
                    });
  nghttp2_submit_settings(session_->raw_ptr(), NGHTTP2_FLAG_NONE,
                          nghttp2_settings.data(), nghttp2_settings.size());
}

void NgHttp2Adapter::SubmitPriorityForStream(Http2StreamId stream_id,
                                             Http2StreamId parent_stream_id,
                                             int weight, bool exclusive) {
  nghttp2_priority_spec priority_spec;
  nghttp2_priority_spec_init(&priority_spec, parent_stream_id, weight,
                             static_cast<int>(exclusive));
  nghttp2_submit_priority(session_->raw_ptr(), NGHTTP2_FLAG_NONE, stream_id,
                          &priority_spec);
}

void NgHttp2Adapter::SubmitPing(Http2PingId ping_id) {
  uint8_t opaque_data[8] = {};
  Http2PingId ping_id_to_serialize = quiche::QuicheEndian::HostToNet64(ping_id);
  std::memcpy(opaque_data, &ping_id_to_serialize, sizeof(Http2PingId));
  nghttp2_submit_ping(session_->raw_ptr(), NGHTTP2_FLAG_NONE, opaque_data);
}

void NgHttp2Adapter::SubmitShutdownNotice() {
  nghttp2_submit_shutdown_notice(session_->raw_ptr());
}

void NgHttp2Adapter::SubmitGoAway(Http2StreamId last_accepted_stream_id,
                                  Http2ErrorCode error_code,
                                  absl::string_view opaque_data) {
  nghttp2_submit_goaway(session_->raw_ptr(), NGHTTP2_FLAG_NONE,
                        last_accepted_stream_id,
                        static_cast<uint32_t>(error_code),
                        ToUint8Ptr(opaque_data.data()), opaque_data.size());
}

void NgHttp2Adapter::SubmitWindowUpdate(Http2StreamId stream_id,
                                        int window_increment) {
  nghttp2_submit_window_update(session_->raw_ptr(), NGHTTP2_FLAG_NONE,
                               stream_id, window_increment);
}

void NgHttp2Adapter::SubmitMetadata(Http2StreamId stream_id,
                                    size_t max_frame_size,
                                    std::unique_ptr<MetadataSource> source) {
  auto* wrapped_source = new SelfDeletingMetadataSource(std::move(source));
  const size_t num_frames = wrapped_source->NumFrames(max_frame_size);
  size_t num_successes = 0;
  for (size_t i = 1; i <= num_frames; ++i) {
    const int result = nghttp2_submit_extension(
        session_->raw_ptr(), kMetadataFrameType,
        i == num_frames ? kMetadataEndFlag : 0, stream_id, wrapped_source);
    if (result != 0) {
      QUICHE_LOG(DFATAL) << "Failed to submit extension frame " << i << " of "
                         << num_frames;
      break;
    }
    ++num_successes;
  }
  if (num_successes == 0) {
    delete wrapped_source;
  }
}

int NgHttp2Adapter::Send() {
  const int result = nghttp2_session_send(session_->raw_ptr());
  if (result != 0) {
    QUICHE_VLOG(1) << "nghttp2_session_send returned " << result;
    visitor_.OnConnectionError(ConnectionError::kSendError);
  }
  return result;
}

int NgHttp2Adapter::GetSendWindowSize() const {
  return session_->GetRemoteWindowSize();
}

int NgHttp2Adapter::GetStreamSendWindowSize(Http2StreamId stream_id) const {
  return nghttp2_session_get_stream_remote_window_size(session_->raw_ptr(),
                                                       stream_id);
}

int NgHttp2Adapter::GetStreamReceiveWindowLimit(Http2StreamId stream_id) const {
  return nghttp2_session_get_stream_effective_local_window_size(
      session_->raw_ptr(), stream_id);
}

int NgHttp2Adapter::GetStreamReceiveWindowSize(Http2StreamId stream_id) const {
  return nghttp2_session_get_stream_local_window_size(session_->raw_ptr(),
                                                      stream_id);
}

int NgHttp2Adapter::GetReceiveWindowSize() const {
  return nghttp2_session_get_local_window_size(session_->raw_ptr());
}

int NgHttp2Adapter::GetHpackEncoderDynamicTableSize() const {
  return nghttp2_session_get_hd_deflate_dynamic_table_size(session_->raw_ptr());
}

int NgHttp2Adapter::GetHpackDecoderDynamicTableSize() const {
  return nghttp2_session_get_hd_inflate_dynamic_table_size(session_->raw_ptr());
}

Http2StreamId NgHttp2Adapter::GetHighestReceivedStreamId() const {
  return nghttp2_session_get_last_proc_stream_id(session_->raw_ptr());
}

void NgHttp2Adapter::MarkDataConsumedForStream(Http2StreamId stream_id,
                                               size_t num_bytes) {
  int rc = session_->Consume(stream_id, num_bytes);
  if (rc != 0) {
    QUICHE_LOG(ERROR) << "Error " << rc << " marking " << num_bytes
                      << " bytes consumed for stream " << stream_id;
  }
}

void NgHttp2Adapter::SubmitRst(Http2StreamId stream_id,
                               Http2ErrorCode error_code) {
  int status =
      nghttp2_submit_rst_stream(session_->raw_ptr(), NGHTTP2_FLAG_NONE,
                                stream_id, static_cast<uint32_t>(error_code));
  if (status < 0) {
    QUICHE_LOG(WARNING) << "Reset stream failed: " << stream_id
                        << " with status code " << status;
  }
}

int32_t NgHttp2Adapter::SubmitRequest(
    absl::Span<const Header> headers,
    std::unique_ptr<DataFrameSource> data_source, void* stream_user_data) {
  auto nvs = GetNghttp2Nvs(headers);
  std::unique_ptr<nghttp2_data_provider> provider =
      MakeDataProvider(data_source.get());

  int32_t stream_id =
      nghttp2_submit_request(session_->raw_ptr(), nullptr, nvs.data(),
                             nvs.size(), provider.get(), stream_user_data);
  sources_.emplace(stream_id, std::move(data_source));
  QUICHE_VLOG(1) << "Submitted request with " << nvs.size()
                 << " request headers and user data " << stream_user_data
                 << "; resulted in stream " << stream_id;
  return stream_id;
}

int NgHttp2Adapter::SubmitResponse(
    Http2StreamId stream_id, absl::Span<const Header> headers,
    std::unique_ptr<DataFrameSource> data_source) {
  auto nvs = GetNghttp2Nvs(headers);
  std::unique_ptr<nghttp2_data_provider> provider =
      MakeDataProvider(data_source.get());

  sources_.emplace(stream_id, std::move(data_source));

  int result = nghttp2_submit_response(session_->raw_ptr(), stream_id,
                                       nvs.data(), nvs.size(), provider.get());
  QUICHE_VLOG(1) << "Submitted response with " << nvs.size()
                 << " response headers; result = " << result;
  return result;
}

int NgHttp2Adapter::SubmitTrailer(Http2StreamId stream_id,
                                  absl::Span<const Header> trailers) {
  auto nvs = GetNghttp2Nvs(trailers);
  int result = nghttp2_submit_trailer(session_->raw_ptr(), stream_id,
                                      nvs.data(), nvs.size());
  QUICHE_VLOG(1) << "Submitted trailers with " << nvs.size()
                 << " response trailers; result = " << result;
  return result;
}

void NgHttp2Adapter::SetStreamUserData(Http2StreamId stream_id,
                                       void* stream_user_data) {
  nghttp2_session_set_stream_user_data(session_->raw_ptr(), stream_id,
                                       stream_user_data);
}

void* NgHttp2Adapter::GetStreamUserData(Http2StreamId stream_id) {
  return nghttp2_session_get_stream_user_data(session_->raw_ptr(), stream_id);
}

bool NgHttp2Adapter::ResumeStream(Http2StreamId stream_id) {
  return 0 == nghttp2_session_resume_data(session_->raw_ptr(), stream_id);
}

void NgHttp2Adapter::RemoveStream(Http2StreamId stream_id) {
  sources_.erase(stream_id);
}

NgHttp2Adapter::NgHttp2Adapter(Http2VisitorInterface& visitor,
                               Perspective perspective,
                               const nghttp2_option* options)
    : Http2Adapter(visitor),
      visitor_(visitor),
      options_(options),
      perspective_(perspective) {}

NgHttp2Adapter::~NgHttp2Adapter() {}

void NgHttp2Adapter::Initialize() {
  nghttp2_option* owned_options = nullptr;
  if (options_ == nullptr) {
    nghttp2_option_new(&owned_options);
    // Set some common options for compatibility.
    nghttp2_option_set_no_closed_streams(owned_options, 1);
    nghttp2_option_set_no_auto_window_update(owned_options, 1);
    nghttp2_option_set_max_send_header_block_length(owned_options, 0x2000000);
    nghttp2_option_set_max_outbound_ack(owned_options, 10000);
    nghttp2_option_set_user_recv_extension_type(owned_options,
                                                kMetadataFrameType);
    options_ = owned_options;
  }

  session_ =
      std::make_unique<NgHttp2Session>(perspective_, callbacks::Create(),
                                       options_, static_cast<void*>(&visitor_));
  if (owned_options != nullptr) {
    nghttp2_option_del(owned_options);
  }
  options_ = nullptr;
}

}  // namespace adapter
}  // namespace http2
