// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_SPDY_FRAMER_H_
#define QUICHE_SPDY_CORE_SPDY_FRAMER_H_

#include <stddef.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_encoder.h"
#include "net/third_party/quiche/src/spdy/core/spdy_alt_svc_wire_format.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"
#include "net/third_party/quiche/src/spdy/core/spdy_headers_handler_interface.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"
#include "net/third_party/quiche/src/spdy/core/zero_copy_output_buffer.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_export.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_string_piece.h"

namespace spdy {

namespace test {

class SpdyFramerPeer;
class SpdyFramerTest_MultipleContinuationFramesWithIterator_Test;
class SpdyFramerTest_PushPromiseFramesWithIterator_Test;

}  // namespace test

class SPDY_EXPORT_PRIVATE SpdyFrameSequence {
 public:
  virtual ~SpdyFrameSequence() {}

  // Serializes the next frame in the sequence to |output|. Returns the number
  // of bytes written to |output|.
  virtual size_t NextFrame(ZeroCopyOutputBuffer* output) = 0;

  // Returns true iff there is at least one more frame in the sequence.
  virtual bool HasNextFrame() const = 0;

  // Get SpdyFrameIR of the frame to be serialized.
  virtual const SpdyFrameIR& GetIR() const = 0;
};

class SPDY_EXPORT_PRIVATE SpdyFramer {
 public:
  enum CompressionOption {
    ENABLE_COMPRESSION,
    DISABLE_COMPRESSION,
  };

  // Create a SpdyFrameSequence to serialize |frame_ir|.
  static std::unique_ptr<SpdyFrameSequence> CreateIterator(
      SpdyFramer* framer,
      std::unique_ptr<const SpdyFrameIR> frame_ir);

  // Gets the serialized flags for the given |frame|.
  static uint8_t GetSerializedFlags(const SpdyFrameIR& frame);

  // Serialize a data frame.
  static SpdySerializedFrame SerializeData(const SpdyDataIR& data_ir);
  // Serializes the data frame header and optionally padding length fields,
  // excluding actual data payload and padding.
  static SpdySerializedFrame SerializeDataFrameHeaderWithPaddingLengthField(
      const SpdyDataIR& data_ir);

  // Serializes a WINDOW_UPDATE frame. The WINDOW_UPDATE
  // frame is used to implement per stream flow control.
  static SpdySerializedFrame SerializeWindowUpdate(
      const SpdyWindowUpdateIR& window_update);

  explicit SpdyFramer(CompressionOption option);

  virtual ~SpdyFramer();

  // Set debug callbacks to be called from the framer. The debug visitor is
  // completely optional and need not be set in order for normal operation.
  // If this is called multiple times, only the last visitor will be used.
  void set_debug_visitor(SpdyFramerDebugVisitorInterface* debug_visitor);

  SpdySerializedFrame SerializeRstStream(
      const SpdyRstStreamIR& rst_stream) const;

  // Serializes a SETTINGS frame. The SETTINGS frame is
  // used to communicate name/value pairs relevant to the communication channel.
  SpdySerializedFrame SerializeSettings(const SpdySettingsIR& settings) const;

  // Serializes a PING frame. The unique_id is used to
  // identify the ping request/response.
  SpdySerializedFrame SerializePing(const SpdyPingIR& ping) const;

  // Serializes a GOAWAY frame. The GOAWAY frame is used
  // prior to the shutting down of the TCP connection, and includes the
  // stream_id of the last stream the sender of the frame is willing to process
  // to completion.
  SpdySerializedFrame SerializeGoAway(const SpdyGoAwayIR& goaway) const;

  // Serializes a HEADERS frame. The HEADERS frame is used
  // for sending headers.
  SpdySerializedFrame SerializeHeaders(const SpdyHeadersIR& headers);

  // Serializes a PUSH_PROMISE frame. The PUSH_PROMISE frame is used
  // to inform the client that it will be receiving an additional stream
  // in response to the original request. The frame includes synthesized
  // headers to explain the upcoming data.
  SpdySerializedFrame SerializePushPromise(
      const SpdyPushPromiseIR& push_promise);

  // Serializes a CONTINUATION frame. The CONTINUATION frame is used
  // to continue a sequence of header block fragments.
  SpdySerializedFrame SerializeContinuation(
      const SpdyContinuationIR& continuation) const;

  // Serializes an ALTSVC frame. The ALTSVC frame advertises the
  // availability of an alternative service to the client.
  SpdySerializedFrame SerializeAltSvc(const SpdyAltSvcIR& altsvc);

  // Serializes a PRIORITY frame. The PRIORITY frame advises a change in
  // the relative priority of the given stream.
  SpdySerializedFrame SerializePriority(const SpdyPriorityIR& priority) const;

  // Serializes an unknown frame given a frame header and payload.
  SpdySerializedFrame SerializeUnknown(const SpdyUnknownIR& unknown) const;

  // Serialize a frame of unknown type.
  SpdySerializedFrame SerializeFrame(const SpdyFrameIR& frame);

  // Serialize a data frame.
  bool SerializeData(const SpdyDataIR& data,
                     ZeroCopyOutputBuffer* output) const;

  // Serializes the data frame header and optionally padding length fields,
  // excluding actual data payload and padding.
  bool SerializeDataFrameHeaderWithPaddingLengthField(
      const SpdyDataIR& data,
      ZeroCopyOutputBuffer* output) const;

  bool SerializeRstStream(const SpdyRstStreamIR& rst_stream,
                          ZeroCopyOutputBuffer* output) const;

  // Serializes a SETTINGS frame. The SETTINGS frame is
  // used to communicate name/value pairs relevant to the communication channel.
  bool SerializeSettings(const SpdySettingsIR& settings,
                         ZeroCopyOutputBuffer* output) const;

  // Serializes a PING frame. The unique_id is used to
  // identify the ping request/response.
  bool SerializePing(const SpdyPingIR& ping,
                     ZeroCopyOutputBuffer* output) const;

  // Serializes a GOAWAY frame. The GOAWAY frame is used
  // prior to the shutting down of the TCP connection, and includes the
  // stream_id of the last stream the sender of the frame is willing to process
  // to completion.
  bool SerializeGoAway(const SpdyGoAwayIR& goaway,
                       ZeroCopyOutputBuffer* output) const;

  // Serializes a HEADERS frame. The HEADERS frame is used
  // for sending headers.
  bool SerializeHeaders(const SpdyHeadersIR& headers,
                        ZeroCopyOutputBuffer* output);

  // Serializes a WINDOW_UPDATE frame. The WINDOW_UPDATE
  // frame is used to implement per stream flow control.
  bool SerializeWindowUpdate(const SpdyWindowUpdateIR& window_update,
                             ZeroCopyOutputBuffer* output) const;

  // Serializes a PUSH_PROMISE frame. The PUSH_PROMISE frame is used
  // to inform the client that it will be receiving an additional stream
  // in response to the original request. The frame includes synthesized
  // headers to explain the upcoming data.
  bool SerializePushPromise(const SpdyPushPromiseIR& push_promise,
                            ZeroCopyOutputBuffer* output);

  // Serializes a CONTINUATION frame. The CONTINUATION frame is used
  // to continue a sequence of header block fragments.
  bool SerializeContinuation(const SpdyContinuationIR& continuation,
                             ZeroCopyOutputBuffer* output) const;

  // Serializes an ALTSVC frame. The ALTSVC frame advertises the
  // availability of an alternative service to the client.
  bool SerializeAltSvc(const SpdyAltSvcIR& altsvc,
                       ZeroCopyOutputBuffer* output);

  // Serializes a PRIORITY frame. The PRIORITY frame advises a change in
  // the relative priority of the given stream.
  bool SerializePriority(const SpdyPriorityIR& priority,
                         ZeroCopyOutputBuffer* output) const;

  // Serializes an unknown frame given a frame header and payload.
  bool SerializeUnknown(const SpdyUnknownIR& unknown,
                        ZeroCopyOutputBuffer* output) const;

  // Serialize a frame of unknown type.
  size_t SerializeFrame(const SpdyFrameIR& frame, ZeroCopyOutputBuffer* output);

  // Returns whether this SpdyFramer will compress header blocks using HPACK.
  bool compression_enabled() const {
    return compression_option_ == ENABLE_COMPRESSION;
  }

  void SetHpackIndexingPolicy(HpackEncoder::IndexingPolicy policy) {
    GetHpackEncoder()->SetIndexingPolicy(std::move(policy));
  }

  // Updates the maximum size of the header encoder compression table.
  void UpdateHeaderEncoderTableSize(uint32_t value);

  // Returns the maximum size of the header encoder compression table.
  size_t header_encoder_table_size() const;

  void SetEncoderHeaderTableDebugVisitor(
      std::unique_ptr<HpackHeaderTable::DebugVisitorInterface> visitor);

  // Get (and lazily initialize) the HPACK encoder state.
  HpackEncoder* GetHpackEncoder();

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

 protected:
  friend class test::SpdyFramerPeer;
  friend class test::SpdyFramerTest_MultipleContinuationFramesWithIterator_Test;
  friend class test::SpdyFramerTest_PushPromiseFramesWithIterator_Test;

  // Iteratively converts a SpdyFrameIR into an appropriate sequence of Spdy
  // frames.
  // Example usage:
  // std::unique_ptr<SpdyFrameSequence> it = CreateIterator(framer, frame_ir);
  // while (it->HasNextFrame()) {
  //   if(it->NextFrame(output) == 0) {
  //     // Write failed;
  //   }
  // }
  class SPDY_EXPORT_PRIVATE SpdyFrameIterator : public SpdyFrameSequence {
   public:
    // Creates an iterator with the provided framer.
    // Does not take ownership of |framer|.
    // |framer| must outlive this instance.
    explicit SpdyFrameIterator(SpdyFramer* framer);
    ~SpdyFrameIterator() override;

    // Serializes the next frame in the sequence to |output|. Returns the number
    // of bytes written to |output|.
    size_t NextFrame(ZeroCopyOutputBuffer* output) override;

    // Returns true iff there is at least one more frame in the sequence.
    bool HasNextFrame() const override;

    // SpdyFrameIterator is neither copyable nor movable.
    SpdyFrameIterator(const SpdyFrameIterator&) = delete;
    SpdyFrameIterator& operator=(const SpdyFrameIterator&) = delete;

   protected:
    virtual size_t GetFrameSizeSansBlock() const = 0;
    virtual bool SerializeGivenEncoding(const std::string& encoding,
                                        ZeroCopyOutputBuffer* output) const = 0;

    SpdyFramer* GetFramer() const { return framer_; }

    void SetEncoder(const SpdyFrameWithHeaderBlockIR* ir) {
      encoder_ =
          framer_->GetHpackEncoder()->EncodeHeaderSet(ir->header_block());
    }

    bool has_next_frame() const { return has_next_frame_; }

   private:
    SpdyFramer* const framer_;
    std::unique_ptr<HpackEncoder::ProgressiveEncoder> encoder_;
    bool is_first_frame_;
    bool has_next_frame_;
  };

  // Iteratively converts a SpdyHeadersIR (with a possibly huge
  // SpdyHeaderBlock) into an appropriate sequence of SpdySerializedFrames, and
  // write to the output.
  class SPDY_EXPORT_PRIVATE SpdyHeaderFrameIterator : public SpdyFrameIterator {
   public:
    // Does not take ownership of |framer|. Take ownership of |headers_ir|.
    SpdyHeaderFrameIterator(SpdyFramer* framer,
                            std::unique_ptr<const SpdyHeadersIR> headers_ir);

    ~SpdyHeaderFrameIterator() override;

   private:
    const SpdyFrameIR& GetIR() const override;
    size_t GetFrameSizeSansBlock() const override;
    bool SerializeGivenEncoding(const std::string& encoding,
                                ZeroCopyOutputBuffer* output) const override;

    const std::unique_ptr<const SpdyHeadersIR> headers_ir_;
  };

  // Iteratively converts a SpdyPushPromiseIR (with a possibly huge
  // SpdyHeaderBlock) into an appropriate sequence of SpdySerializedFrames, and
  // write to the output.
  class SPDY_EXPORT_PRIVATE SpdyPushPromiseFrameIterator
      : public SpdyFrameIterator {
   public:
    // Does not take ownership of |framer|. Take ownership of |push_promise_ir|.
    SpdyPushPromiseFrameIterator(
        SpdyFramer* framer,
        std::unique_ptr<const SpdyPushPromiseIR> push_promise_ir);

    ~SpdyPushPromiseFrameIterator() override;

   private:
    const SpdyFrameIR& GetIR() const override;
    size_t GetFrameSizeSansBlock() const override;
    bool SerializeGivenEncoding(const std::string& encoding,
                                ZeroCopyOutputBuffer* output) const override;

    const std::unique_ptr<const SpdyPushPromiseIR> push_promise_ir_;
  };

  // Converts a SpdyFrameIR into one Spdy frame (a sequence of length 1), and
  // write it to the output.
  class SPDY_EXPORT_PRIVATE SpdyControlFrameIterator
      : public SpdyFrameSequence {
   public:
    SpdyControlFrameIterator(SpdyFramer* framer,
                             std::unique_ptr<const SpdyFrameIR> frame_ir);
    ~SpdyControlFrameIterator() override;

    size_t NextFrame(ZeroCopyOutputBuffer* output) override;

    bool HasNextFrame() const override;

    const SpdyFrameIR& GetIR() const override;

   private:
    SpdyFramer* const framer_;
    std::unique_ptr<const SpdyFrameIR> frame_ir_;
    bool has_next_frame_ = true;
  };

 private:
  void SerializeHeadersBuilderHelper(const SpdyHeadersIR& headers,
                                     uint8_t* flags,
                                     size_t* size,
                                     std::string* hpack_encoding,
                                     int* weight,
                                     size_t* length_field);
  void SerializePushPromiseBuilderHelper(const SpdyPushPromiseIR& push_promise,
                                         uint8_t* flags,
                                         std::string* hpack_encoding,
                                         size_t* size);

  std::unique_ptr<HpackEncoder> hpack_encoder_;

  SpdyFramerDebugVisitorInterface* debug_visitor_;

  // Determines whether HPACK compression is used.
  const CompressionOption compression_option_;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_SPDY_FRAMER_H_
