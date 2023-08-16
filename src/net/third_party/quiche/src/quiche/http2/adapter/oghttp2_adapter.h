#ifndef QUICHE_HTTP2_ADAPTER_OGHTTP2_ADAPTER_H_
#define QUICHE_HTTP2_ADAPTER_OGHTTP2_ADAPTER_H_

#include <cstdint>
#include <memory>

#include "quiche/http2/adapter/http2_adapter.h"
#include "quiche/http2/adapter/http2_session.h"
#include "quiche/http2/adapter/oghttp2_session.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

class QUICHE_EXPORT OgHttp2Adapter : public Http2Adapter {
 public:
  using Options = OgHttp2Session::Options;
  static std::unique_ptr<OgHttp2Adapter> Create(Http2VisitorInterface& visitor,
                                                Options options);

  ~OgHttp2Adapter() override;

  // From Http2Adapter.
  bool IsServerSession() const override;
  bool want_read() const override { return session_->want_read(); }
  bool want_write() const override { return session_->want_write(); }
  int64_t ProcessBytes(absl::string_view bytes) override;
  void SubmitSettings(absl::Span<const Http2Setting> settings) override;
  void SubmitPriorityForStream(Http2StreamId stream_id,
                               Http2StreamId parent_stream_id, int weight,
                               bool exclusive) override;
  void SubmitPing(Http2PingId ping_id) override;
  void SubmitShutdownNotice() override;
  void SubmitGoAway(Http2StreamId last_accepted_stream_id,
                    Http2ErrorCode error_code,
                    absl::string_view opaque_data) override;
  void SubmitWindowUpdate(Http2StreamId stream_id,
                          int window_increment) override;
  void SubmitMetadata(Http2StreamId stream_id, size_t max_frame_size,
                      std::unique_ptr<MetadataSource> source) override;
  int Send() override;
  int GetSendWindowSize() const override;
  int GetStreamSendWindowSize(Http2StreamId stream_id) const override;
  int GetStreamReceiveWindowLimit(Http2StreamId stream_id) const override;
  int GetStreamReceiveWindowSize(Http2StreamId stream_id) const override;
  int GetReceiveWindowSize() const override;
  int GetHpackEncoderDynamicTableSize() const override;
  int GetHpackEncoderDynamicTableCapacity() const;
  int GetHpackDecoderDynamicTableSize() const override;
  int GetHpackDecoderSizeLimit() const;
  Http2StreamId GetHighestReceivedStreamId() const override;
  void MarkDataConsumedForStream(Http2StreamId stream_id,
                                 size_t num_bytes) override;
  void SubmitRst(Http2StreamId stream_id, Http2ErrorCode error_code) override;
  int32_t SubmitRequest(absl::Span<const Header> headers,
                        std::unique_ptr<DataFrameSource> data_source,
                        void* user_data) override;
  int SubmitResponse(Http2StreamId stream_id, absl::Span<const Header> headers,
                     std::unique_ptr<DataFrameSource> data_source) override;

  int SubmitTrailer(Http2StreamId stream_id,
                    absl::Span<const Header> trailers) override;

  void SetStreamUserData(Http2StreamId stream_id, void* user_data) override;
  void* GetStreamUserData(Http2StreamId stream_id) override;
  bool ResumeStream(Http2StreamId stream_id) override;

 private:
  OgHttp2Adapter(Http2VisitorInterface& visitor, Options options);

  std::unique_ptr<OgHttp2Session> session_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_OGHTTP2_ADAPTER_H_
