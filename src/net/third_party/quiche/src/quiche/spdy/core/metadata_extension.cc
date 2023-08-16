#include "quiche/spdy/core/metadata_extension.h"

#include <list>
#include <memory>
#include <string>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/hpack/decoder/hpack_decoder.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/spdy/core/http2_header_block_hpack_listener.h"

namespace spdy {

// Non-standard constants related to METADATA frames.
const SpdySettingsId MetadataVisitor::kMetadataExtensionId = 0x4d44;
const uint8_t MetadataVisitor::kMetadataFrameType = 0x4d;
const uint8_t MetadataVisitor::kEndMetadataFlag = 0x4;

namespace {

const size_t kMaxMetadataBlockSize = 1 << 20;  // 1 MB

}  // anonymous namespace

MetadataFrameSequence::MetadataFrameSequence(SpdyStreamId stream_id,
                                             spdy::Http2HeaderBlock payload)
    : stream_id_(stream_id), payload_(std::move(payload)) {
  // Metadata should not use HPACK compression.
  encoder_.DisableCompression();
  HpackEncoder::Representations r;
  for (const auto& kv_pair : payload_) {
    r.push_back(kv_pair);
  }
  progressive_encoder_ = encoder_.EncodeRepresentations(r);
}

bool MetadataFrameSequence::HasNext() const {
  return progressive_encoder_->HasNext();
}

std::unique_ptr<spdy::SpdyFrameIR> MetadataFrameSequence::Next() {
  if (!HasNext()) {
    return nullptr;
  }
  // METADATA frames obey the HTTP/2 maximum frame size.
  std::string payload =
      progressive_encoder_->Next(spdy::kHttp2DefaultFramePayloadLimit);
  const bool end_metadata = !HasNext();
  const uint8_t flags = end_metadata ? MetadataVisitor::kEndMetadataFlag : 0;
  return std::make_unique<spdy::SpdyUnknownIR>(
      stream_id_, MetadataVisitor::kMetadataFrameType, flags,
      std::move(payload));
}

struct MetadataVisitor::MetadataPayloadState {
  MetadataPayloadState(size_t remaining, bool end)
      : bytes_remaining(remaining), end_metadata(end) {}
  std::list<std::string> buffer;
  size_t bytes_remaining;
  bool end_metadata;
};

MetadataVisitor::MetadataVisitor(OnCompletePayload on_payload,
                                 OnMetadataSupport on_support)
    : on_payload_(std::move(on_payload)),
      on_support_(std::move(on_support)),
      peer_supports_metadata_(MetadataSupportState::UNSPECIFIED) {}

MetadataVisitor::~MetadataVisitor() {}

void MetadataVisitor::OnSetting(SpdySettingsId id, uint32_t value) {
  QUICHE_VLOG(1) << "MetadataVisitor::OnSetting(" << id << ", " << value << ")";
  if (id == kMetadataExtensionId) {
    if (value == 0) {
      const MetadataSupportState previous_state = peer_supports_metadata_;
      peer_supports_metadata_ = MetadataSupportState::NOT_SUPPORTED;
      if (previous_state == MetadataSupportState::UNSPECIFIED ||
          previous_state == MetadataSupportState::SUPPORTED) {
        on_support_(false);
      }
    } else if (value == 1) {
      const MetadataSupportState previous_state = peer_supports_metadata_;
      peer_supports_metadata_ = MetadataSupportState::SUPPORTED;
      if (previous_state == MetadataSupportState::UNSPECIFIED ||
          previous_state == MetadataSupportState::NOT_SUPPORTED) {
        on_support_(true);
      }
    } else {
      QUICHE_LOG_EVERY_N_SEC(WARNING, 1)
          << "Unrecognized value for setting " << id << ": " << value;
    }
  }
}

bool MetadataVisitor::OnFrameHeader(SpdyStreamId stream_id, size_t length,
                                    uint8_t type, uint8_t flags) {
  QUICHE_VLOG(1) << "OnFrameHeader(stream_id=" << stream_id
                 << ", length=" << length << ", type=" << static_cast<int>(type)
                 << ", flags=" << static_cast<int>(flags);
  // TODO(birenroy): Consider disabling METADATA handling until our setting
  // advertising METADATA support has been acked.
  if (type != kMetadataFrameType) {
    return false;
  }
  auto it = metadata_map_.find(stream_id);
  if (it == metadata_map_.end()) {
    auto state = std::make_unique<MetadataPayloadState>(
        length, flags & kEndMetadataFlag);
    auto result =
        metadata_map_.insert(std::make_pair(stream_id, std::move(state)));
    QUICHE_BUG_IF(bug_if_2781_1, !result.second) << "Map insertion failed.";
    it = result.first;
  } else {
    QUICHE_BUG_IF(bug_22051_1, it->second->end_metadata)
        << "Inconsistent metadata payload state!";
    QUICHE_BUG_IF(bug_if_2781_2, it->second->bytes_remaining > 0)
        << "Incomplete metadata block!";
  }

  if (it->second == nullptr) {
    QUICHE_BUG(bug_2781_3) << "Null metadata payload state!";
    return false;
  }
  current_stream_ = stream_id;
  it->second->bytes_remaining = length;
  it->second->end_metadata = (flags & kEndMetadataFlag);
  return true;
}

void MetadataVisitor::OnFramePayload(const char* data, size_t len) {
  QUICHE_VLOG(1) << "OnFramePayload(stream_id=" << current_stream_
                 << ", len=" << len << ")";
  auto it = metadata_map_.find(current_stream_);
  if (it == metadata_map_.end() || it->second == nullptr) {
    QUICHE_BUG(bug_2781_4) << "Invalid order of operations on MetadataVisitor.";
  } else {
    MetadataPayloadState* state = it->second.get();  // For readability.
    state->buffer.push_back(std::string(data, len));
    if (len < state->bytes_remaining) {
      state->bytes_remaining -= len;
    } else {
      QUICHE_BUG_IF(bug_22051_2, len > state->bytes_remaining)
          << "Metadata payload overflow! len: " << len
          << " bytes_remaining: " << state->bytes_remaining;
      state->bytes_remaining = 0;
      if (state->end_metadata) {
        // The whole process of decoding the HPACK-encoded metadata block,
        // below, is more cumbersome than it ought to be.
        spdy::Http2HeaderBlockHpackListener listener;
        http2::HpackDecoder decoder(&listener, kMaxMetadataBlockSize);

        // If any operations fail, the decode process should be aborted.
        bool success = decoder.StartDecodingBlock();
        for (const std::string& slice : state->buffer) {
          if (!success) {
            break;
          }
          http2::DecodeBuffer buffer(slice.data(), slice.size());
          success = success && decoder.DecodeFragment(&buffer);
        }
        success =
            success && decoder.EndDecodingBlock() && !listener.hpack_error();
        if (success) {
          on_payload_(current_stream_, listener.release_header_block());
        }
        // TODO(birenroy): add varz counting metadata decode successes/failures.
        metadata_map_.erase(it);
      }
    }
  }
}

}  // namespace spdy
