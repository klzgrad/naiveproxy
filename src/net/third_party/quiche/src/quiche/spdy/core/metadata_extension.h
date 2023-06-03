#ifndef QUICHE_SPDY_CORE_METADATA_EXTENSION_H_
#define QUICHE_SPDY_CORE_METADATA_EXTENSION_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/spdy/core/hpack/hpack_encoder.h"
#include "quiche/spdy/core/http2_frame_decoder_adapter.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/spdy_protocol.h"
#include "quiche/spdy/core/zero_copy_output_buffer.h"

namespace spdy {

// An implementation of the ExtensionVisitorInterface that can parse
// METADATA frames. METADATA is a non-standard HTTP/2 extension developed and
// used internally at Google. A peer advertises support for METADATA by sending
// a setting with a setting ID of kMetadataExtensionId and a value of 1.
//
// Metadata is represented as a HPACK header block with literal encoding.
class QUICHE_EXPORT MetadataVisitor : public spdy::ExtensionVisitorInterface {
 public:
  using MetadataPayload = spdy::Http2HeaderBlock;

  static_assert(!std::is_copy_constructible<MetadataPayload>::value,
                "MetadataPayload should be a move-only type!");

  using OnMetadataSupport = std::function<void(bool)>;
  using OnCompletePayload =
      std::function<void(spdy::SpdyStreamId, MetadataPayload)>;

  // The HTTP/2 SETTINGS ID that is used to indicate support for METADATA
  // frames.
  static const spdy::SpdySettingsId kMetadataExtensionId;

  // The 8-bit frame type code for a METADATA frame.
  static const uint8_t kMetadataFrameType;

  // The flag that indicates the end of a logical metadata block. Due to frame
  // size limits, a single metadata block may be emitted as several HTTP/2
  // frames.
  static const uint8_t kEndMetadataFlag;

  // |on_payload| is invoked whenever a complete metadata payload is received.
  // |on_support| is invoked whenever the peer's advertised support for metadata
  // changes.
  MetadataVisitor(OnCompletePayload on_payload, OnMetadataSupport on_support);
  ~MetadataVisitor() override;

  MetadataVisitor(const MetadataVisitor&) = delete;
  MetadataVisitor& operator=(const MetadataVisitor&) = delete;

  // Interprets the non-standard setting indicating support for METADATA.
  void OnSetting(spdy::SpdySettingsId id, uint32_t value) override;

  // Returns true iff |type| indicates a METADATA frame.
  bool OnFrameHeader(spdy::SpdyStreamId stream_id, size_t length, uint8_t type,
                     uint8_t flags) override;

  // Consumes a METADATA frame payload. Invokes the registered callback when a
  // complete payload has been received.
  void OnFramePayload(const char* data, size_t len) override;

  // Returns true if the peer has advertised support for METADATA via the
  // appropriate setting.
  bool PeerSupportsMetadata() const {
    return peer_supports_metadata_ == MetadataSupportState::SUPPORTED;
  }

 private:
  enum class MetadataSupportState : uint8_t {
    UNSPECIFIED,
    SUPPORTED,
    NOT_SUPPORTED,
  };

  struct MetadataPayloadState;

  using StreamMetadataMap =
      absl::flat_hash_map<spdy::SpdyStreamId,
                          std::unique_ptr<MetadataPayloadState>>;

  OnCompletePayload on_payload_;
  OnMetadataSupport on_support_;
  StreamMetadataMap metadata_map_;
  spdy::SpdyStreamId current_stream_;
  MetadataSupportState peer_supports_metadata_;
};

// This class uses an HpackEncoder to serialize a METADATA block as a series of
// METADATA frames.
class QUICHE_EXPORT MetadataFrameSequence {
 public:
  MetadataFrameSequence(SpdyStreamId stream_id, spdy::Http2HeaderBlock payload);

  // Copies are not allowed.
  MetadataFrameSequence(const MetadataFrameSequence& other) = delete;
  MetadataFrameSequence& operator=(const MetadataFrameSequence& other) = delete;

  // True if Next() would return non-nullptr.
  bool HasNext() const;

  // Returns the next HTTP/2 METADATA frame for this block, unless the block has
  // been entirely serialized in frames returned by previous calls of Next(), in
  // which case returns nullptr.
  std::unique_ptr<spdy::SpdyFrameIR> Next();

  SpdyStreamId stream_id() const { return stream_id_; }

 private:
  SpdyStreamId stream_id_;
  Http2HeaderBlock payload_;
  HpackEncoder encoder_;
  std::unique_ptr<HpackEncoder::ProgressiveEncoder> progressive_encoder_;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_METADATA_EXTENSION_H_
