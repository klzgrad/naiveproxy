// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_HTTP2_FRAME_DECODER_ADAPTER_H_
#define QUICHE_SPDY_CORE_HTTP2_FRAME_DECODER_ADAPTER_H_

#include <stddef.h>

#include <cstdint>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_decoder_adapter.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_header_table.h"
#include "net/third_party/quiche/src/spdy/core/spdy_alt_svc_wire_format.h"
#include "net/third_party/quiche/src/spdy/core/spdy_headers_handler_interface.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

namespace spdy {

class SpdyFramerVisitorInterface;
class ExtensionVisitorInterface;

}  // namespace spdy

// TODO(dahollings): Perform various renames/moves suggested in cl/164660364.

namespace http2 {

// Adapts SpdyFramer interface to use Http2FrameDecoder.
class QUICHE_EXPORT_PRIVATE Http2DecoderAdapter
    : public http2::Http2FrameDecoderListener {
 public:
  // HTTP2 states.
  enum SpdyState {
    SPDY_ERROR,
    SPDY_READY_FOR_FRAME,  // Framer is ready for reading the next frame.
    SPDY_FRAME_COMPLETE,  // Framer has finished reading a frame, need to reset.
    SPDY_READING_COMMON_HEADER,
    SPDY_CONTROL_FRAME_PAYLOAD,
    SPDY_READ_DATA_FRAME_PADDING_LENGTH,
    SPDY_CONSUME_PADDING,
    SPDY_IGNORE_REMAINING_PAYLOAD,
    SPDY_FORWARD_STREAM_FRAME,
    SPDY_CONTROL_FRAME_BEFORE_HEADER_BLOCK,
    SPDY_CONTROL_FRAME_HEADER_BLOCK,
    SPDY_GOAWAY_FRAME_PAYLOAD,
    SPDY_SETTINGS_FRAME_HEADER,
    SPDY_SETTINGS_FRAME_PAYLOAD,
    SPDY_ALTSVC_FRAME_PAYLOAD,
    SPDY_EXTENSION_FRAME_PAYLOAD,
  };

  // Framer error codes.
  enum SpdyFramerError {
    SPDY_NO_ERROR,
    SPDY_INVALID_STREAM_ID,            // Stream ID is invalid
    SPDY_INVALID_CONTROL_FRAME,        // Control frame is mal-formatted.
    SPDY_CONTROL_PAYLOAD_TOO_LARGE,    // Control frame payload was too large.
    SPDY_ZLIB_INIT_FAILURE,            // The Zlib library could not initialize.
    SPDY_UNSUPPORTED_VERSION,          // Control frame has unsupported version.
    SPDY_DECOMPRESS_FAILURE,           // There was an error decompressing.
    SPDY_COMPRESS_FAILURE,             // There was an error compressing.
    SPDY_GOAWAY_FRAME_CORRUPT,         // GOAWAY frame could not be parsed.
    SPDY_RST_STREAM_FRAME_CORRUPT,     // RST_STREAM frame could not be parsed.
    SPDY_INVALID_PADDING,              // HEADERS or DATA frame padding invalid
    SPDY_INVALID_DATA_FRAME_FLAGS,     // Data frame has invalid flags.
    SPDY_INVALID_CONTROL_FRAME_FLAGS,  // Control frame has invalid flags.
    SPDY_UNEXPECTED_FRAME,             // Frame received out of order.
    SPDY_INTERNAL_FRAMER_ERROR,        // SpdyFramer was used incorrectly.
    SPDY_INVALID_CONTROL_FRAME_SIZE,   // Control frame not sized to spec
    SPDY_OVERSIZED_PAYLOAD,            // Payload size was too large

    // HttpDecoder or HttpDecoderAdapter error.
    // See HpackDecodingError for description of each error code.
    SPDY_HPACK_INDEX_VARINT_ERROR,
    SPDY_HPACK_NAME_LENGTH_VARINT_ERROR,
    SPDY_HPACK_VALUE_LENGTH_VARINT_ERROR,
    SPDY_HPACK_NAME_TOO_LONG,
    SPDY_HPACK_VALUE_TOO_LONG,
    SPDY_HPACK_NAME_HUFFMAN_ERROR,
    SPDY_HPACK_VALUE_HUFFMAN_ERROR,
    SPDY_HPACK_MISSING_DYNAMIC_TABLE_SIZE_UPDATE,
    SPDY_HPACK_INVALID_INDEX,
    SPDY_HPACK_INVALID_NAME_INDEX,
    SPDY_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_NOT_ALLOWED,
    SPDY_HPACK_INITIAL_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_LOW_WATER_MARK,
    SPDY_HPACK_DYNAMIC_TABLE_SIZE_UPDATE_IS_ABOVE_ACKNOWLEDGED_SETTING,
    SPDY_HPACK_TRUNCATED_BLOCK,
    SPDY_HPACK_FRAGMENT_TOO_LONG,
    SPDY_HPACK_COMPRESSED_HEADER_SIZE_EXCEEDS_LIMIT,

    LAST_ERROR,  // Must be the last entry in the enum.
  };

  // For debugging.
  static const char* StateToString(int state);
  static const char* SpdyFramerErrorToString(SpdyFramerError spdy_framer_error);

  Http2DecoderAdapter();
  ~Http2DecoderAdapter() override;

  // Set callbacks to be called from the framer.  A visitor must be set, or
  // else the framer will likely crash.  It is acceptable for the visitor
  // to do nothing.  If this is called multiple times, only the last visitor
  // will be used.
  void set_visitor(spdy::SpdyFramerVisitorInterface* visitor);
  spdy::SpdyFramerVisitorInterface* visitor() const { return visitor_; }

  // Set extension callbacks to be called from the framer or decoder. Optional.
  // If called multiple times, only the last visitor will be used.
  void set_extension_visitor(spdy::ExtensionVisitorInterface* visitor);
  spdy::ExtensionVisitorInterface* extension_visitor() const {
    return extension_;
  }

  // Set debug callbacks to be called from the framer. The debug visitor is
  // completely optional and need not be set in order for normal operation.
  // If this is called multiple times, only the last visitor will be used.
  void set_debug_visitor(spdy::SpdyFramerDebugVisitorInterface* debug_visitor);
  spdy::SpdyFramerDebugVisitorInterface* debug_visitor() const {
    return debug_visitor_;
  }

  // Set debug callbacks to be called from the HPACK decoder.
  void SetDecoderHeaderTableDebugVisitor(
      std::unique_ptr<spdy::HpackHeaderTable::DebugVisitorInterface> visitor);

  // Sets whether or not ProcessInput returns after finishing a frame, or
  // continues processing additional frames. Normally ProcessInput processes
  // all input, but this method enables the caller (and visitor) to work with
  // a single frame at a time (or that portion of the frame which is provided
  // as input). Reset() does not change the value of this flag.
  void set_process_single_input_frame(bool v);
  bool process_single_input_frame() const {
    return process_single_input_frame_;
  }

  // Decode the |len| bytes of encoded HTTP/2 starting at |*data|. Returns
  // the number of bytes consumed. It is safe to pass more bytes in than
  // may be consumed. Should process (or otherwise buffer) as much as
  // available, unless process_single_input_frame is true.
  size_t ProcessInput(const char* data, size_t len);

  // Reset the decoder (used just for tests at this time).
  void Reset();

  // Current state of the decoder.
  SpdyState state() const;

  // Current error code (NO_ERROR if state != ERROR).
  SpdyFramerError spdy_framer_error() const;

  // Has any frame header looked like the start of an HTTP/1.1 (or earlier)
  // response? Used to detect if a backend/server that we sent a request to
  // has responded with an HTTP/1.1 (or earlier) response.
  bool probable_http_response() const;

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

  spdy::HpackDecoderAdapter* GetHpackDecoder();

  bool HasError() const;

 private:
  bool OnFrameHeader(const Http2FrameHeader& header) override;
  void OnDataStart(const Http2FrameHeader& header) override;
  void OnDataPayload(const char* data, size_t len) override;
  void OnDataEnd() override;
  void OnHeadersStart(const Http2FrameHeader& header) override;
  void OnHeadersPriority(const Http2PriorityFields& priority) override;
  void OnHpackFragment(const char* data, size_t len) override;
  void OnHeadersEnd() override;
  void OnPriorityFrame(const Http2FrameHeader& header,
                       const Http2PriorityFields& priority) override;
  void OnContinuationStart(const Http2FrameHeader& header) override;
  void OnContinuationEnd() override;
  void OnPadLength(size_t trailing_length) override;
  void OnPadding(const char* padding, size_t skipped_length) override;
  void OnRstStream(const Http2FrameHeader& header,
                   Http2ErrorCode http2_error_code) override;
  void OnSettingsStart(const Http2FrameHeader& header) override;
  void OnSetting(const Http2SettingFields& setting_fields) override;
  void OnSettingsEnd() override;
  void OnSettingsAck(const Http2FrameHeader& header) override;
  void OnPushPromiseStart(const Http2FrameHeader& header,
                          const Http2PushPromiseFields& promise,
                          size_t total_padding_length) override;
  void OnPushPromiseEnd() override;
  void OnPing(const Http2FrameHeader& header,
              const Http2PingFields& ping) override;
  void OnPingAck(const Http2FrameHeader& header,
                 const Http2PingFields& ping) override;
  void OnGoAwayStart(const Http2FrameHeader& header,
                     const Http2GoAwayFields& goaway) override;
  void OnGoAwayOpaqueData(const char* data, size_t len) override;
  void OnGoAwayEnd() override;
  void OnWindowUpdate(const Http2FrameHeader& header,
                      uint32_t increment) override;
  void OnAltSvcStart(const Http2FrameHeader& header,
                     size_t origin_length,
                     size_t value_length) override;
  void OnAltSvcOriginData(const char* data, size_t len) override;
  void OnAltSvcValueData(const char* data, size_t len) override;
  void OnAltSvcEnd() override;
  void OnUnknownStart(const Http2FrameHeader& header) override;
  void OnUnknownPayload(const char* data, size_t len) override;
  void OnUnknownEnd() override;
  void OnPaddingTooLong(const Http2FrameHeader& header,
                        size_t missing_length) override;
  void OnFrameSizeError(const Http2FrameHeader& header) override;

  size_t ProcessInputFrame(const char* data, size_t len);

  void DetermineSpdyState(DecodeStatus status);
  void ResetBetweenFrames();

  // ResetInternal is called from the constructor, and during tests, but not
  // otherwise (i.e. not between every frame).
  void ResetInternal();

  void set_spdy_state(SpdyState v);

  void SetSpdyErrorAndNotify(SpdyFramerError error);

  const Http2FrameHeader& frame_header() const;

  uint32_t stream_id() const;
  Http2FrameType frame_type() const;

  size_t remaining_total_payload() const;

  bool IsReadingPaddingLength();
  bool IsSkippingPadding();
  bool IsDiscardingPayload();
  // Called from OnXyz or OnXyzStart methods to decide whether it is OK to
  // handle the callback.
  bool IsOkToStartFrame(const Http2FrameHeader& header);
  bool HasRequiredStreamId(uint32_t stream_id);

  bool HasRequiredStreamId(const Http2FrameHeader& header);

  bool HasRequiredStreamIdZero(uint32_t stream_id);

  bool HasRequiredStreamIdZero(const Http2FrameHeader& header);

  void ReportReceiveCompressedFrame(const Http2FrameHeader& header);

  void CommonStartHpackBlock();

  // SpdyFramer calls HandleControlFrameHeadersData even if there are zero
  // fragment bytes in the first frame, so do the same.
  void MaybeAnnounceEmptyFirstHpackFragment();
  void CommonHpackFragmentEnd();

  // The most recently decoded frame header; invalid after we reached the end
  // of that frame.
  Http2FrameHeader frame_header_;

  // If decoding an HPACK block that is split across multiple frames, this holds
  // the frame header of the HEADERS or PUSH_PROMISE that started the block.
  Http2FrameHeader hpack_first_frame_header_;

  // Amount of trailing padding. Currently used just as an indicator of whether
  // OnPadLength has been called.
  quiche::QuicheOptional<size_t> opt_pad_length_;

  // Temporary buffers for the AltSvc fields.
  std::string alt_svc_origin_;
  std::string alt_svc_value_;

  // Listener used if we transition to an error state; the listener ignores all
  // the callbacks.
  Http2FrameDecoderNoOpListener no_op_listener_;

  spdy::SpdyFramerVisitorInterface* visitor_ = nullptr;
  spdy::SpdyFramerDebugVisitorInterface* debug_visitor_ = nullptr;

  // If non-null, unknown frames and settings are passed to the extension.
  spdy::ExtensionVisitorInterface* extension_ = nullptr;

  // The HPACK decoder to be used for this adapter. User is responsible for
  // clearing if the adapter is to be used for another connection.
  std::unique_ptr<spdy::HpackDecoderAdapter> hpack_decoder_ = nullptr;

  // The HTTP/2 frame decoder. Accessed via a unique_ptr to allow replacement
  // (e.g. in tests) when Reset() is called.
  std::unique_ptr<Http2FrameDecoder> frame_decoder_;

  // Next frame type expected. Currently only used for CONTINUATION frames,
  // but could be used for detecting whether the first frame is a SETTINGS
  // frame.
  // TODO(jamessyng): Provide means to indicate that decoder should require
  // SETTINGS frame as the first frame.
  Http2FrameType expected_frame_type_;

  // Attempt to duplicate the SpdyState and SpdyFramerError values that
  // SpdyFramer sets. Values determined by getting tests to pass.
  SpdyState spdy_state_;
  SpdyFramerError spdy_framer_error_;

  // The limit on the size of received HTTP/2 payloads as specified in the
  // SETTINGS_MAX_FRAME_SIZE advertised to peer.
  size_t recv_frame_size_limit_ = spdy::kHttp2DefaultFramePayloadLimit;

  // Has OnFrameHeader been called?
  bool decoded_frame_header_ = false;

  // Have we recorded an Http2FrameHeader for the current frame?
  // We only do so if the decoder will make multiple callbacks for
  // the frame; for example, for PING frames we don't make record
  // the frame header, but for ALTSVC we do.
  bool has_frame_header_ = false;

  // Have we recorded an Http2FrameHeader for the current HPACK block?
  // True only for multi-frame HPACK blocks.
  bool has_hpack_first_frame_header_ = false;

  // Has OnHeaders() already been called for current HEADERS block? Only
  // meaningful between OnHeadersStart and OnHeadersPriority.
  bool on_headers_called_;

  // Has OnHpackFragment() already been called for current HPACK block?
  // SpdyFramer will pass an empty buffer to the HPACK decoder if a HEADERS
  // or PUSH_PROMISE has no HPACK data in it (e.g. a HEADERS frame with only
  // padding). Detect that condition and replicate the behavior using this
  // field.
  bool on_hpack_fragment_called_;

  // Have we seen a frame header that appears to be an HTTP/1 response?
  bool latched_probable_http_response_ = false;

  // Is expected_frame_type_ set?
  bool has_expected_frame_type_ = false;

  // Is the current frame payload destined for |extension_|?
  bool handling_extension_payload_ = false;

  bool process_single_input_frame_ = false;
};

}  // namespace http2

namespace spdy {

// Http2DecoderAdapter will use the given visitor implementing this
// interface to deliver event callbacks as frames are decoded.
//
// Control frames that contain HTTP2 header blocks (HEADER, and PUSH_PROMISE)
// are processed in fashion that allows the decompressed header block to be
// delivered in chunks to the visitor.
// The following steps are followed:
//   1. OnHeaders, or OnPushPromise is called.
//   2. OnHeaderFrameStart is called; visitor is expected to return an instance
//      of SpdyHeadersHandlerInterface that will receive the header key-value
//      pairs.
//   3. OnHeaderFrameEnd is called, indicating that the full header block has
//      been delivered for the control frame.
// During step 2, if the visitor is not interested in accepting the header data,
// it should return a no-op implementation of SpdyHeadersHandlerInterface.
class QUICHE_EXPORT_PRIVATE SpdyFramerVisitorInterface {
 public:
  virtual ~SpdyFramerVisitorInterface() {}

  // Called if an error is detected in the SpdyFrame protocol.
  virtual void OnError(http2::Http2DecoderAdapter::SpdyFramerError error) = 0;

  // Called when the common header for a frame is received. Validating the
  // common header occurs in later processing.
  virtual void OnCommonHeader(SpdyStreamId /*stream_id*/,
                              size_t /*length*/,
                              uint8_t /*type*/,
                              uint8_t /*flags*/) {}

  // Called when a data frame header is received. The frame's data
  // payload will be provided via subsequent calls to
  // OnStreamFrameData().
  virtual void OnDataFrameHeader(SpdyStreamId stream_id,
                                 size_t length,
                                 bool fin) = 0;

  // Called when data is received.
  // |stream_id| The stream receiving data.
  // |data| A buffer containing the data received.
  // |len| The length of the data buffer.
  virtual void OnStreamFrameData(SpdyStreamId stream_id,
                                 const char* data,
                                 size_t len) = 0;

  // Called when the other side has finished sending data on this stream.
  // |stream_id| The stream that was receiving data.
  virtual void OnStreamEnd(SpdyStreamId stream_id) = 0;

  // Called when padding length field is received on a DATA frame.
  // |stream_id| The stream receiving data.
  // |value| The value of the padding length field.
  virtual void OnStreamPadLength(SpdyStreamId /*stream_id*/, size_t /*value*/) {
  }

  // Called when padding is received (the trailing octets, not pad_len field) on
  // a DATA frame.
  // |stream_id| The stream receiving data.
  // |len| The number of padding octets.
  virtual void OnStreamPadding(SpdyStreamId stream_id, size_t len) = 0;

  // Called just before processing the payload of a frame containing header
  // data. Should return an implementation of SpdyHeadersHandlerInterface that
  // will receive headers for stream |stream_id|. The caller will not take
  // ownership of the headers handler. The same instance should remain live
  // and be returned for all header frames comprising a logical header block
  // (i.e. until OnHeaderFrameEnd() is called).
  virtual SpdyHeadersHandlerInterface* OnHeaderFrameStart(
      SpdyStreamId stream_id) = 0;

  // Called after processing the payload of a frame containing header data.
  virtual void OnHeaderFrameEnd(SpdyStreamId stream_id) = 0;

  // Called when a RST_STREAM frame has been parsed.
  virtual void OnRstStream(SpdyStreamId stream_id,
                           SpdyErrorCode error_code) = 0;

  // Called when a SETTINGS frame is received.
  virtual void OnSettings() {}

  // Called when a complete setting within a SETTINGS frame has been parsed.
  // Note that |id| may or may not be a SETTINGS ID defined in the HTTP/2 spec.
  virtual void OnSetting(SpdySettingsId id, uint32_t value) = 0;

  // Called when a SETTINGS frame is received with the ACK flag set.
  virtual void OnSettingsAck() {}

  // Called before and after parsing SETTINGS id and value tuples.
  virtual void OnSettingsEnd() = 0;

  // Called when a PING frame has been parsed.
  virtual void OnPing(SpdyPingId unique_id, bool is_ack) = 0;

  // Called when a GOAWAY frame has been parsed.
  virtual void OnGoAway(SpdyStreamId last_accepted_stream_id,
                        SpdyErrorCode error_code) = 0;

  // Called when a HEADERS frame is received.
  // Note that header block data is not included. See OnHeaderFrameStart().
  // |stream_id| The stream receiving the header.
  // |has_priority| Whether or not the headers frame included a priority value,
  //     and stream dependency info.
  // |weight| If |has_priority| is true, then weight (in the range [1, 256])
  //     for the receiving stream, otherwise 0.
  // |parent_stream_id| If |has_priority| is true the parent stream of the
  //     receiving stream, else 0.
  // |exclusive| If |has_priority| is true the exclusivity of dependence on the
  //     parent stream, else false.
  // |fin| Whether FIN flag is set in frame headers.
  // |end| False if HEADERs frame is to be followed by a CONTINUATION frame,
  //     or true if not.
  virtual void OnHeaders(SpdyStreamId stream_id,
                         bool has_priority,
                         int weight,
                         SpdyStreamId parent_stream_id,
                         bool exclusive,
                         bool fin,
                         bool end) = 0;

  // Called when a WINDOW_UPDATE frame has been parsed.
  virtual void OnWindowUpdate(SpdyStreamId stream_id,
                              int delta_window_size) = 0;

  // Called when a goaway frame opaque data is available.
  // |goaway_data| A buffer containing the opaque GOAWAY data chunk received.
  // |len| The length of the header data buffer. A length of zero indicates
  //       that the header data block has been completely sent.
  // When this function returns true the visitor indicates that it accepted
  // all of the data. Returning false indicates that that an error has
  // occurred while processing the data. Default implementation returns true.
  virtual bool OnGoAwayFrameData(const char* goaway_data, size_t len);

  // Called when a PUSH_PROMISE frame is received.
  // Note that header block data is not included. See OnHeaderFrameStart().
  virtual void OnPushPromise(SpdyStreamId stream_id,
                             SpdyStreamId promised_stream_id,
                             bool end) = 0;

  // Called when a CONTINUATION frame is received.
  // Note that header block data is not included. See OnHeaderFrameStart().
  virtual void OnContinuation(SpdyStreamId stream_id, bool end) = 0;

  // Called when an ALTSVC frame has been parsed.
  virtual void OnAltSvc(
      SpdyStreamId /*stream_id*/,
      quiche::QuicheStringPiece /*origin*/,
      const SpdyAltSvcWireFormat::AlternativeServiceVector& /*altsvc_vector*/) {
  }

  // Called when a PRIORITY frame is received.
  // |stream_id| The stream to update the priority of.
  // |parent_stream_id| The parent stream of |stream_id|.
  // |weight| Stream weight, in the range [1, 256].
  // |exclusive| Whether |stream_id| should be an only child of
  //     |parent_stream_id|.
  virtual void OnPriority(SpdyStreamId stream_id,
                          SpdyStreamId parent_stream_id,
                          int weight,
                          bool exclusive) = 0;

  // Called when a frame type we don't recognize is received.
  // Return true if this appears to be a valid extension frame, false otherwise.
  // We distinguish between extension frames and nonsense by checking
  // whether the stream id is valid.
  virtual bool OnUnknownFrame(SpdyStreamId stream_id, uint8_t frame_type) = 0;
};

class QUICHE_EXPORT_PRIVATE ExtensionVisitorInterface {
 public:
  virtual ~ExtensionVisitorInterface() {}

  // Called when SETTINGS are received, including non-standard SETTINGS.
  virtual void OnSetting(SpdySettingsId id, uint32_t value) = 0;

  // Called when non-standard frames are received.
  virtual bool OnFrameHeader(SpdyStreamId stream_id,
                             size_t length,
                             uint8_t type,
                             uint8_t flags) = 0;

  // The payload for a single frame may be delivered as multiple calls to
  // OnFramePayload. Since the length field is passed in OnFrameHeader, there is
  // no explicit indication of the end of the frame payload.
  virtual void OnFramePayload(const char* data, size_t len) = 0;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_HTTP2_FRAME_DECODER_ADAPTER_H_
