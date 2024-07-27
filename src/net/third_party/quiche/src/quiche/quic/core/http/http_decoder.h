// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_HTTP_DECODER_H_
#define QUICHE_QUIC_CORE_HTTP_HTTP_DECODER_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

namespace test {

class HttpDecoderPeer;

}  // namespace test

class QuicDataReader;

// A class for decoding the HTTP frames that are exchanged in an HTTP over QUIC
// session.
class QUICHE_EXPORT HttpDecoder {
 public:
  class QUICHE_EXPORT Visitor {
   public:
    virtual ~Visitor() {}

    // Called if an error is detected.
    virtual void OnError(HttpDecoder* decoder) = 0;

    // All the following methods return true to continue decoding,
    // and false to pause it.
    // On*FrameStart() methods are called after the frame header is completely
    // processed.  At that point it is safe to consume |header_length| bytes.

    // Called when a MAX_PUSH_ID frame has been successfully parsed.
    virtual bool OnMaxPushIdFrame() = 0;

    // Called when a GOAWAY frame has been successfully parsed.
    virtual bool OnGoAwayFrame(const GoAwayFrame& frame) = 0;

    // Called when a SETTINGS frame has been received.
    virtual bool OnSettingsFrameStart(QuicByteCount header_length) = 0;

    // Called when a SETTINGS frame has been successfully parsed.
    virtual bool OnSettingsFrame(const SettingsFrame& frame) = 0;

    // Called when a DATA frame has been received.
    // |header_length| and |payload_length| are the length of DATA frame header
    // and payload, respectively.
    virtual bool OnDataFrameStart(QuicByteCount header_length,
                                  QuicByteCount payload_length) = 0;
    // Called when part of the payload of a DATA frame has been read.  May be
    // called multiple times for a single frame.  |payload| is guaranteed to be
    // non-empty.
    virtual bool OnDataFramePayload(absl::string_view payload) = 0;
    // Called when a DATA frame has been completely processed.
    virtual bool OnDataFrameEnd() = 0;

    // Called when a HEADERS frame has been received.
    // |header_length| and |payload_length| are the length of HEADERS frame
    // header and payload, respectively.
    virtual bool OnHeadersFrameStart(QuicByteCount header_length,
                                     QuicByteCount payload_length) = 0;
    // Called when part of the payload of a HEADERS frame has been read.  May be
    // called multiple times for a single frame.  |payload| is guaranteed to be
    // non-empty.
    virtual bool OnHeadersFramePayload(absl::string_view payload) = 0;
    // Called when a HEADERS frame has been completely processed.
    virtual bool OnHeadersFrameEnd() = 0;

    // Called when a PRIORITY_UPDATE frame has been received.
    // |header_length| contains PRIORITY_UPDATE frame length and payload length.
    virtual bool OnPriorityUpdateFrameStart(QuicByteCount header_length) = 0;

    // Called when a PRIORITY_UPDATE frame has been successfully parsed.
    virtual bool OnPriorityUpdateFrame(const PriorityUpdateFrame& frame) = 0;

    // Called when an ACCEPT_CH frame has been received.
    // |header_length| contains ACCEPT_CH frame length and payload length.
    virtual bool OnAcceptChFrameStart(QuicByteCount header_length) = 0;

    // Called when an ACCEPT_CH frame has been successfully parsed.
    virtual bool OnAcceptChFrame(const AcceptChFrame& frame) = 0;

    // Called when a WEBTRANSPORT_STREAM frame type and the session ID varint
    // immediately following it has been received.  Any further parsing should
    // be done by the stream itself, and not the parser. Note that this does not
    // return bool, because WEBTRANSPORT_STREAM always causes the parsing
    // process to cease.
    virtual void OnWebTransportStreamFrameType(
        QuicByteCount header_length, WebTransportSessionId session_id) = 0;

    // Called when a METADATA frame has been received.
    // |header_length| and |payload_length| are the length of the frame header
    // and payload, respectively.
    virtual bool OnMetadataFrameStart(QuicByteCount header_length,
                                      QuicByteCount payload_length) = 0;

    // Called when part of the payload of the METADATA frame has been read.  May
    // be called multiple times for a single frame.  |payload| is guaranteed to
    // be non-empty.
    virtual bool OnMetadataFramePayload(absl::string_view payload) = 0;

    // Called when the METADATA frame has been completely processed.
    virtual bool OnMetadataFrameEnd() = 0;

    // Called when a frame of unknown type |frame_type| has been received.
    // Frame type might be reserved, Visitor must make sure to ignore.
    // |header_length| and |payload_length| are the length of the frame header
    // and payload, respectively.
    virtual bool OnUnknownFrameStart(uint64_t frame_type,
                                     QuicByteCount header_length,
                                     QuicByteCount payload_length) = 0;
    // Called when part of the payload of the unknown frame has been read.  May
    // be called multiple times for a single frame.  |payload| is guaranteed to
    // be non-empty.
    virtual bool OnUnknownFramePayload(absl::string_view payload) = 0;
    // Called when the unknown frame has been completely processed.
    virtual bool OnUnknownFrameEnd() = 0;
  };

  // |visitor| must be non-null, and must outlive HttpDecoder.
  explicit HttpDecoder(Visitor* visitor);

  ~HttpDecoder();

  // Processes the input and invokes the appropriate visitor methods, until a
  // visitor method returns false or an error occurs.  Returns the number of
  // bytes processed.  Does not process any input if called after an error.
  // Paused processing can be resumed by calling ProcessInput() again with the
  // unprocessed portion of data.  Must not be called after an error has
  // occurred.
  QuicByteCount ProcessInput(const char* data, QuicByteCount len);

  // Decode settings frame from |data|.
  // Upon successful decoding, |frame| will be populated, and returns true.
  // This method is not used for regular processing of incoming data.
  static bool DecodeSettings(const char* data, QuicByteCount len,
                             SettingsFrame* frame);

  // Returns an error code other than QUIC_NO_ERROR if and only if
  // Visitor::OnError() has been called.
  QuicErrorCode error() const { return error_; }

  const std::string& error_detail() const { return error_detail_; }

  // Returns true if input data processed so far ends on a frame boundary.
  bool AtFrameBoundary() const { return state_ == STATE_READING_FRAME_TYPE; }

  // Indicates that WEBTRANSPORT_STREAM should be parsed.
  void EnableWebTransportStreamParsing() { allow_web_transport_stream_ = true; }

  std::string DebugString() const;

 private:
  friend test::HttpDecoderPeer;

  // Represents the current state of the parsing state machine.
  enum HttpDecoderState {
    STATE_READING_FRAME_LENGTH,
    STATE_READING_FRAME_TYPE,

    // States used for buffered frame types
    STATE_BUFFER_OR_PARSE_PAYLOAD,

    // States used for non-buffered frame types
    STATE_READING_FRAME_PAYLOAD,
    STATE_FINISH_PARSING,

    STATE_PARSING_NO_LONGER_POSSIBLE,
    STATE_ERROR
  };

  // Reads the type of a frame from |reader|. Sets error_ and error_detail_
  // if there are any errors.  Also calls OnDataFrameStart() or
  // OnHeadersFrameStart() for appropriate frame types. Returns whether the
  // processing should continue.
  bool ReadFrameType(QuicDataReader* reader);

  // Reads the length of a frame from |reader|. Sets error_ and error_detail_
  // if there are any errors.  Returns whether processing should continue.
  bool ReadFrameLength(QuicDataReader* reader);

  // Returns whether the current frame is of a buffered type.
  // The payload of buffered frames is buffered by HttpDecoder, and parsed by
  // HttpDecoder after the entire frame has been received.  (Copying to the
  // buffer is skipped if the ProcessInput() call covers the entire payload.)
  // Frames that are not buffered have every payload fragment synchronously
  // passed to the Visitor without buffering.
  bool IsFrameBuffered();

  // For buffered frame types, calls BufferOrParsePayload().  For other frame
  // types, reads the payload of the current frame from |reader| and calls
  // visitor methods.  Returns whether processing should continue.
  bool ReadFramePayload(QuicDataReader* reader);

  // For buffered frame types, this method is only called if frame payload is
  // empty, and it calls BufferOrParsePayload().  For other frame types, this
  // method directly calls visitor methods to signal that frame had been
  // received completely.  Returns whether processing should continue.
  bool FinishParsing();

  // Reset internal fields to prepare for reading next frame.
  void ResetForNextFrame();

  // Read payload of unknown frame from |reader| and call
  // Visitor::OnUnknownFramePayload().  Returns true decoding should continue,
  // false if it should be paused.
  bool HandleUnknownFramePayload(QuicDataReader* reader);

  // Buffers any remaining frame payload from |*reader| into |buffer_| if
  // necessary.  Parses the frame payload if complete.  Parses out of |*reader|
  // without unnecessary copy if |*reader| contains entire payload.
  // Returns whether processing should continue.
  // Must only be called when current frame type is buffered.
  bool BufferOrParsePayload(QuicDataReader* reader);

  // Parses the entire payload of certain kinds of frames that are parsed in a
  // single pass.  |reader| must have at least |current_frame_length_| bytes.
  // Returns whether processing should continue.
  // Must only be called when current frame type is buffered.
  bool ParseEntirePayload(QuicDataReader* reader);

  // Buffers any remaining frame length field from |reader| into
  // |length_buffer_|.
  void BufferFrameLength(QuicDataReader* reader);

  // Buffers any remaining frame type field from |reader| into |type_buffer_|.
  void BufferFrameType(QuicDataReader* reader);

  // Sets |error_| and |error_detail_| accordingly.
  void RaiseError(QuicErrorCode error, std::string error_detail);

  // Parses the payload of a SETTINGS frame from |reader| into |frame|.
  bool ParseSettingsFrame(QuicDataReader* reader, SettingsFrame* frame);

  // Parses the payload of a PRIORITY_UPDATE frame (draft-02, type 0xf0700)
  // from |reader| into |frame|.
  bool ParsePriorityUpdateFrame(QuicDataReader* reader,
                                PriorityUpdateFrame* frame);

  // Parses the payload of an ACCEPT_CH frame from |reader| into |frame|.
  bool ParseAcceptChFrame(QuicDataReader* reader, AcceptChFrame* frame);

  // Returns the max frame size of a given |frame_type|.
  QuicByteCount MaxFrameLength(uint64_t frame_type);

  // Visitor to invoke when messages are parsed.
  Visitor* const visitor_;  // Unowned.
  // Whether WEBTRANSPORT_STREAM should be parsed.
  bool allow_web_transport_stream_;
  // Current state of the parsing.
  HttpDecoderState state_;
  // Type of the frame currently being parsed.
  uint64_t current_frame_type_;
  // Size of the frame's length field.
  QuicByteCount current_length_field_length_;
  // Remaining length that's needed for the frame's length field.
  QuicByteCount remaining_length_field_length_;
  // Length of the payload of the frame currently being parsed.
  QuicByteCount current_frame_length_;
  // Remaining payload bytes to be parsed.
  QuicByteCount remaining_frame_length_;
  // Length of the frame's type field.
  QuicByteCount current_type_field_length_;
  // Remaining length that's needed for the frame's type field.
  QuicByteCount remaining_type_field_length_;
  // Last error.
  QuicErrorCode error_;
  // The issue which caused |error_|
  std::string error_detail_;
  // Remaining unparsed data.
  std::string buffer_;
  // Remaining unparsed length field data.
  std::array<char, sizeof(uint64_t)> length_buffer_;
  // Remaining unparsed type field data.
  std::array<char, sizeof(uint64_t)> type_buffer_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_HTTP_DECODER_H_
