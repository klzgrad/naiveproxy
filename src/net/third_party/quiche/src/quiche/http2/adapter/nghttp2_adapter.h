#ifndef QUICHE_HTTP2_ADAPTER_NGHTTP2_ADAPTER_H_
#define QUICHE_HTTP2_ADAPTER_NGHTTP2_ADAPTER_H_

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "quiche/http2/adapter/http2_adapter.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/nghttp2_session.h"
#include "quiche/http2/adapter/nghttp2_util.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

class QUICHE_EXPORT NgHttp2Adapter : public Http2Adapter {
 public:
  ~NgHttp2Adapter() override;

  // Creates an adapter that functions as a client. Does not take ownership of
  // |options|.
  static std::unique_ptr<NgHttp2Adapter> CreateClientAdapter(
      Http2VisitorInterface& visitor, const nghttp2_option* options = nullptr);

  // Creates an adapter that functions as a server. Does not take ownership of
  // |options|.
  static std::unique_ptr<NgHttp2Adapter> CreateServerAdapter(
      Http2VisitorInterface& visitor, const nghttp2_option* options = nullptr);

  bool IsServerSession() const override;
  bool want_read() const override { return session_->want_read(); }
  bool want_write() const override { return session_->want_write(); }

  int64_t ProcessBytes(absl::string_view bytes) override;
  void SubmitSettings(absl::Span<const Http2Setting> settings) override;
  void SubmitPriorityForStream(Http2StreamId stream_id,
                               Http2StreamId parent_stream_id, int weight,
                               bool exclusive) override;

  // Submits a PING on the connection. Note that nghttp2 automatically submits
  // PING acks upon receiving non-ack PINGs from the peer, so callers only use
  // this method to originate PINGs. See nghttp2_option_set_no_auto_ping_ack().
  void SubmitPing(Http2PingId ping_id) override;

  void SubmitShutdownNotice() override;
  void SubmitGoAway(Http2StreamId last_accepted_stream_id,
                    Http2ErrorCode error_code,
                    absl::string_view opaque_data) override;

  void SubmitWindowUpdate(Http2StreamId stream_id,
                          int window_increment) override;

  void SubmitRst(Http2StreamId stream_id, Http2ErrorCode error_code) override;

  void SubmitMetadata(Http2StreamId stream_id, size_t max_frame_size,
                      std::unique_ptr<MetadataSource> source) override;

  int Send() override;

  int GetSendWindowSize() const override;
  int GetStreamSendWindowSize(Http2StreamId stream_id) const override;

  int GetStreamReceiveWindowLimit(Http2StreamId stream_id) const override;
  int GetStreamReceiveWindowSize(Http2StreamId stream_id) const override;
  int GetReceiveWindowSize() const override;

  int GetHpackEncoderDynamicTableSize() const override;
  int GetHpackDecoderDynamicTableSize() const override;

  Http2StreamId GetHighestReceivedStreamId() const override;

  void MarkDataConsumedForStream(Http2StreamId stream_id,
                                 size_t num_bytes) override;

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

  void FrameNotSent(Http2StreamId stream_id, uint8_t frame_type) override;

  // Removes references to the `stream_id` from this adapter.
  void RemoveStream(Http2StreamId stream_id);

  // Accessor for testing.
  size_t sources_size() const { return sources_.size(); }
  size_t stream_metadata_size() const { return stream_metadata_.size(); }
  size_t pending_metadata_count(Http2StreamId stream_id) const {
    if (auto it = stream_metadata_.find(stream_id);
        it != stream_metadata_.end()) {
      return it->second.size();
    }
    return 0;
  }

 private:
  class NotifyingMetadataSource;

  NgHttp2Adapter(Http2VisitorInterface& visitor, Perspective perspective,
                 const nghttp2_option* options);

  // Performs any necessary initialization of the underlying HTTP/2 session,
  // such as preparing initial SETTINGS.
  void Initialize();

  void RemovePendingMetadata(Http2StreamId stream_id);

  std::unique_ptr<NgHttp2Session> session_;
  Http2VisitorInterface& visitor_;
  const nghttp2_option* options_;
  Perspective perspective_;

  using MetadataSourceVec =
      absl::InlinedVector<std::unique_ptr<MetadataSource>, 2>;
  using MetadataMap = absl::flat_hash_map<Http2StreamId, MetadataSourceVec>;
  MetadataMap stream_metadata_;

  absl::flat_hash_map<int32_t, std::unique_ptr<DataFrameSource>> sources_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_NGHTTP2_ADAPTER_H_
