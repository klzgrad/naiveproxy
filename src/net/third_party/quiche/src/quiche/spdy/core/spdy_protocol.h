// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains some protocol structures for use with SPDY 3 and HTTP 2
// The SPDY 3 spec can be found at:
// http://dev.chromium.org/spdy/spdy-protocol/spdy-protocol-draft3

#ifndef QUICHE_SPDY_CORE_SPDY_PROTOCOL_H_
#define QUICHE_SPDY_CORE_SPDY_PROTOCOL_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/spdy_alt_svc_wire_format.h"
#include "quiche/spdy/core/spdy_bitmasks.h"

namespace spdy {

// A stream ID is a 31-bit entity.
using SpdyStreamId = uint32_t;

// A SETTINGS ID is a 16-bit entity.
using SpdySettingsId = uint16_t;

// Specifies the stream ID used to denote the current session (for
// flow control).
inline constexpr SpdyStreamId kSessionFlowControlStreamId = 0;

// 0 is not a valid stream ID for any other purpose than flow control.
inline constexpr SpdyStreamId kInvalidStreamId = 0;

// Max stream id.
inline constexpr SpdyStreamId kMaxStreamId = 0x7fffffff;

// The maximum possible frame payload size allowed by the spec.
inline constexpr uint32_t kSpdyMaxFrameSizeLimit = (1 << 24) - 1;

// The initial value for the maximum frame payload size as per the spec. This is
// the maximum control frame size we accept.
inline constexpr uint32_t kHttp2DefaultFramePayloadLimit = 1 << 14;

// The maximum size of the control frames that we send, including the size of
// the header. This limit is arbitrary. We can enforce it here or at the
// application layer. We chose the framing layer, but this can be changed (or
// removed) if necessary later down the line.
inline constexpr size_t kHttp2MaxControlFrameSendSize =
    kHttp2DefaultFramePayloadLimit - 1;

// Number of octets in the frame header.
inline constexpr size_t kFrameHeaderSize = 9;

// The initial value for the maximum frame payload size as per the spec. This is
// the maximum control frame size we accept.
inline constexpr uint32_t kHttp2DefaultFrameSizeLimit =
    kHttp2DefaultFramePayloadLimit + kFrameHeaderSize;

// The initial value for the maximum size of the header list, "unlimited" (max
// unsigned 32-bit int) as per the spec.
inline constexpr uint32_t kSpdyInitialHeaderListSizeLimit = 0xFFFFFFFF;

// Maximum window size for a Spdy stream or session.
inline constexpr int32_t kSpdyMaximumWindowSize =
    0x7FFFFFFF;  // Max signed 32bit int

// Maximum padding size in octets for one DATA or HEADERS or PUSH_PROMISE frame.
inline constexpr int32_t kPaddingSizePerFrame = 256;

// The HTTP/2 connection preface, which must be the first bytes sent by the
// client upon starting an HTTP/2 connection, and which must be followed by a
// SETTINGS frame.  Note that even though |kHttp2ConnectionHeaderPrefix| is
// defined as a string literal with a null terminator, the actual connection
// preface is only the first |kHttp2ConnectionHeaderPrefixSize| bytes, which
// excludes the null terminator.
QUICHE_EXPORT extern const char* const kHttp2ConnectionHeaderPrefix;
inline constexpr int kHttp2ConnectionHeaderPrefixSize = 24;

// Wire values for HTTP2 frame types.
enum class SpdyFrameType : uint8_t {
  DATA = 0x00,
  HEADERS = 0x01,
  PRIORITY = 0x02,
  RST_STREAM = 0x03,
  SETTINGS = 0x04,
  PUSH_PROMISE = 0x05,
  PING = 0x06,
  GOAWAY = 0x07,
  WINDOW_UPDATE = 0x08,
  CONTINUATION = 0x09,
  // ALTSVC is a public extension.
  ALTSVC = 0x0a,
  PRIORITY_UPDATE = 0x10,
  ACCEPT_CH = 0x89,
};

// Flags on data packets.
enum SpdyDataFlags {
  DATA_FLAG_NONE = 0x00,
  DATA_FLAG_FIN = 0x01,
  DATA_FLAG_PADDED = 0x08,
};

// Flags on control packets
enum SpdyControlFlags {
  CONTROL_FLAG_NONE = 0x00,
  CONTROL_FLAG_FIN = 0x01,
};

enum SpdyPingFlags {
  PING_FLAG_ACK = 0x01,
};

// Used by HEADERS, PUSH_PROMISE, and CONTINUATION.
enum SpdyHeadersFlags {
  HEADERS_FLAG_END_HEADERS = 0x04,
  HEADERS_FLAG_PADDED = 0x08,
  HEADERS_FLAG_PRIORITY = 0x20,
};

enum SpdyPushPromiseFlags {
  PUSH_PROMISE_FLAG_END_PUSH_PROMISE = 0x04,
  PUSH_PROMISE_FLAG_PADDED = 0x08,
};

enum Http2SettingsControlFlags {
  SETTINGS_FLAG_ACK = 0x01,
};

// Wire values of HTTP/2 setting identifiers.
enum SpdyKnownSettingsId : SpdySettingsId {
  // HPACK header table maximum size.
  SETTINGS_HEADER_TABLE_SIZE = 0x1,
  SETTINGS_MIN = SETTINGS_HEADER_TABLE_SIZE,
  // Whether or not server push (PUSH_PROMISE) is enabled.
  SETTINGS_ENABLE_PUSH = 0x2,
  // The maximum number of simultaneous live streams in each direction.
  SETTINGS_MAX_CONCURRENT_STREAMS = 0x3,
  // Initial window size in bytes
  SETTINGS_INITIAL_WINDOW_SIZE = 0x4,
  // The size of the largest frame payload that a receiver is willing to accept.
  SETTINGS_MAX_FRAME_SIZE = 0x5,
  // The maximum size of header list that the sender is prepared to accept.
  SETTINGS_MAX_HEADER_LIST_SIZE = 0x6,
  // Enable Websockets over HTTP/2, see
  // https://httpwg.org/specs/rfc8441.html
  SETTINGS_ENABLE_CONNECT_PROTOCOL = 0x8,
  // Disable HTTP/2 priorities, see
  // https://tools.ietf.org/html/draft-ietf-httpbis-priority-02.
  SETTINGS_DEPRECATE_HTTP2_PRIORITIES = 0x9,
  SETTINGS_MAX = SETTINGS_DEPRECATE_HTTP2_PRIORITIES,
  // Experimental setting used to configure an alternative write scheduler.
  SETTINGS_EXPERIMENT_SCHEDULER = 0xFF45,
};

// This explicit operator is needed, otherwise compiler finds
// overloaded operator to be ambiguous.
QUICHE_EXPORT std::ostream& operator<<(std::ostream& out,
                                       SpdyKnownSettingsId id);

// This operator is needed, because SpdyFrameType is an enum class,
// therefore implicit conversion to underlying integer type is not allowed.
QUICHE_EXPORT std::ostream& operator<<(std::ostream& out,
                                       SpdyFrameType frame_type);

using SettingsMap = std::map<SpdySettingsId, uint32_t>;

// HTTP/2 error codes, RFC 7540 Section 7.
enum SpdyErrorCode : uint32_t {
  ERROR_CODE_NO_ERROR = 0x0,
  ERROR_CODE_PROTOCOL_ERROR = 0x1,
  ERROR_CODE_INTERNAL_ERROR = 0x2,
  ERROR_CODE_FLOW_CONTROL_ERROR = 0x3,
  ERROR_CODE_SETTINGS_TIMEOUT = 0x4,
  ERROR_CODE_STREAM_CLOSED = 0x5,
  ERROR_CODE_FRAME_SIZE_ERROR = 0x6,
  ERROR_CODE_REFUSED_STREAM = 0x7,
  ERROR_CODE_CANCEL = 0x8,
  ERROR_CODE_COMPRESSION_ERROR = 0x9,
  ERROR_CODE_CONNECT_ERROR = 0xa,
  ERROR_CODE_ENHANCE_YOUR_CALM = 0xb,
  ERROR_CODE_INADEQUATE_SECURITY = 0xc,
  ERROR_CODE_HTTP_1_1_REQUIRED = 0xd,
  ERROR_CODE_MAX = ERROR_CODE_HTTP_1_1_REQUIRED
};

// Type of priority write scheduler.
enum class WriteSchedulerType {
  LIFO,  // Last added stream has the highest priority.
  SPDY,  // Uses SPDY priorities described in
         // https://www.chromium.org/spdy/spdy-protocol/spdy-protocol-draft3-1#TOC-2.3.3-Stream-priority.
  HTTP2,  // Uses HTTP2 (tree-style) priority described in
          // https://tools.ietf.org/html/rfc7540#section-5.3.
  FIFO,   // Stream with the smallest stream ID has the highest priority.
};

// A SPDY priority is a number between 0 and 7 (inclusive).
typedef uint8_t SpdyPriority;

// Lowest and Highest here refer to SPDY priorities as described in
// https://www.chromium.org/spdy/spdy-protocol/spdy-protocol-draft3-1#TOC-2.3.3-Stream-priority
inline constexpr SpdyPriority kV3HighestPriority = 0;
inline constexpr SpdyPriority kV3LowestPriority = 7;

// Returns SPDY 3.x priority value clamped to the valid range of [0, 7].
QUICHE_EXPORT SpdyPriority ClampSpdy3Priority(SpdyPriority priority);

// HTTP/2 stream weights are integers in range [1, 256], as specified in RFC
// 7540 section 5.3.2. Default stream weight is defined in section 5.3.5.
inline constexpr int kHttp2MinStreamWeight = 1;
inline constexpr int kHttp2MaxStreamWeight = 256;
inline constexpr int kHttp2DefaultStreamWeight = 16;

// Returns HTTP/2 weight clamped to the valid range of [1, 256].
QUICHE_EXPORT int ClampHttp2Weight(int weight);

// Maps SPDY 3.x priority value in range [0, 7] to HTTP/2 weight value in range
// [1, 256], where priority 0 (i.e. highest precedence) corresponds to maximum
// weight 256 and priority 7 (lowest precedence) corresponds to minimum weight
// 1.
QUICHE_EXPORT int Spdy3PriorityToHttp2Weight(SpdyPriority priority);

// Maps HTTP/2 weight value in range [1, 256] to SPDY 3.x priority value in
// range [0, 7], where minimum weight 1 corresponds to priority 7 (lowest
// precedence) and maximum weight 256 corresponds to priority 0 (highest
// precedence).
QUICHE_EXPORT SpdyPriority Http2WeightToSpdy3Priority(int weight);

// Reserved ID for root stream of HTTP/2 stream dependency tree, as specified
// in RFC 7540 section 5.3.1.
const unsigned int kHttp2RootStreamId = 0;

typedef uint64_t SpdyPingId;

// Returns true if a given on-the-wire enumeration of a frame type is defined
// in a standardized HTTP/2 specification, false otherwise.
QUICHE_EXPORT bool IsDefinedFrameType(uint8_t frame_type_field);

// Parses a frame type from an on-the-wire enumeration.
// Behavior is undefined for invalid frame type fields; consumers should first
// use IsValidFrameType() to verify validity of frame type fields.
QUICHE_EXPORT SpdyFrameType ParseFrameType(uint8_t frame_type_field);

// Serializes a frame type to the on-the-wire value.
QUICHE_EXPORT uint8_t SerializeFrameType(SpdyFrameType frame_type);

// (HTTP/2) All standard frame types except WINDOW_UPDATE are
// (stream-specific xor connection-level). Returns false iff we know
// the given frame type does not align with the given streamID.
QUICHE_EXPORT bool IsValidHTTP2FrameStreamId(
    SpdyStreamId current_frame_stream_id, SpdyFrameType frame_type_field);

// Serialize |frame_type| to string for logging/debugging.
QUICHE_EXPORT const char* FrameTypeToString(SpdyFrameType frame_type);

// If |wire_setting_id| is the on-the-wire representation of a defined SETTINGS
// parameter, parse it to |*setting_id| and return true.
QUICHE_EXPORT bool ParseSettingsId(SpdySettingsId wire_setting_id,
                                   SpdyKnownSettingsId* setting_id);

// Returns a string representation of the |id| for logging/debugging. Returns
// the |id| prefixed with "SETTINGS_UNKNOWN_" for unknown SETTINGS IDs. To parse
// the |id| into a SpdyKnownSettingsId (if applicable), use ParseSettingsId().
QUICHE_EXPORT std::string SettingsIdToString(SpdySettingsId id);

// Parse |wire_error_code| to a SpdyErrorCode.
// Treat unrecognized error codes as INTERNAL_ERROR
// as recommended by the HTTP/2 specification.
QUICHE_EXPORT SpdyErrorCode ParseErrorCode(uint32_t wire_error_code);

// Serialize RST_STREAM or GOAWAY frame error code to string
// for logging/debugging.
QUICHE_EXPORT const char* ErrorCodeToString(SpdyErrorCode error_code);

// Serialize |type| to string for logging/debugging.
QUICHE_EXPORT const char* WriteSchedulerTypeToString(WriteSchedulerType type);

// Minimum size of a frame, in octets.
inline constexpr size_t kFrameMinimumSize = kFrameHeaderSize;

// Minimum frame size for variable size frame types (includes mandatory fields),
// frame size for fixed size frames, in octets.

inline constexpr size_t kDataFrameMinimumSize = kFrameHeaderSize;
inline constexpr size_t kHeadersFrameMinimumSize = kFrameHeaderSize;
// PRIORITY frame has stream_dependency (4 octets) and weight (1 octet) fields.
inline constexpr size_t kPriorityFrameSize = kFrameHeaderSize + 5;
// RST_STREAM frame has error_code (4 octets) field.
inline constexpr size_t kRstStreamFrameSize = kFrameHeaderSize + 4;
inline constexpr size_t kSettingsFrameMinimumSize = kFrameHeaderSize;
inline constexpr size_t kSettingsOneSettingSize =
    sizeof(uint32_t) + sizeof(SpdySettingsId);
// PUSH_PROMISE frame has promised_stream_id (4 octet) field.
inline constexpr size_t kPushPromiseFrameMinimumSize = kFrameHeaderSize + 4;
// PING frame has opaque_bytes (8 octet) field.
inline constexpr size_t kPingFrameSize = kFrameHeaderSize + 8;
// GOAWAY frame has last_stream_id (4 octet) and error_code (4 octet) fields.
inline constexpr size_t kGoawayFrameMinimumSize = kFrameHeaderSize + 8;
// WINDOW_UPDATE frame has window_size_increment (4 octet) field.
inline constexpr size_t kWindowUpdateFrameSize = kFrameHeaderSize + 4;
inline constexpr size_t kContinuationFrameMinimumSize = kFrameHeaderSize;
// ALTSVC frame has origin_len (2 octets) field.
inline constexpr size_t kGetAltSvcFrameMinimumSize = kFrameHeaderSize + 2;
// PRIORITY_UPDATE frame has prioritized_stream_id (4 octets) field.
inline constexpr size_t kPriorityUpdateFrameMinimumSize = kFrameHeaderSize + 4;
// ACCEPT_CH frame may have empty payload.
inline constexpr size_t kAcceptChFrameMinimumSize = kFrameHeaderSize;
// Each ACCEPT_CH frame entry has a 16-bit origin length and a 16-bit value
// length.
inline constexpr size_t kAcceptChFramePerEntryOverhead = 4;

// Maximum possible configurable size of a frame in octets.
inline constexpr size_t kMaxFrameSizeLimit =
    kSpdyMaxFrameSizeLimit + kFrameHeaderSize;
// Size of a header block size field.
inline constexpr size_t kSizeOfSizeField = sizeof(uint32_t);
// Initial window size for a stream in bytes.
inline constexpr int32_t kInitialStreamWindowSize = 64 * 1024 - 1;
// Initial window size for a session in bytes.
inline constexpr int32_t kInitialSessionWindowSize = 64 * 1024 - 1;
// The NPN string for HTTP2, "h2".
QUICHE_EXPORT extern const char* const kHttp2Npn;
// An estimate size of the HPACK overhead for each header field. 1 bytes for
// indexed literal, 1 bytes for key literal and length encoding, and 2 bytes for
// value literal and length encoding.
inline constexpr size_t kPerHeaderHpackOverhead = 4;

// Names of pseudo-headers defined for HTTP/2 requests.
QUICHE_EXPORT extern const char* const kHttp2AuthorityHeader;
QUICHE_EXPORT extern const char* const kHttp2MethodHeader;
QUICHE_EXPORT extern const char* const kHttp2PathHeader;
QUICHE_EXPORT extern const char* const kHttp2SchemeHeader;
QUICHE_EXPORT extern const char* const kHttp2ProtocolHeader;

// Name of pseudo-header defined for HTTP/2 responses.
QUICHE_EXPORT extern const char* const kHttp2StatusHeader;

QUICHE_EXPORT size_t GetNumberRequiredContinuationFrames(size_t size);

// Variant type that is either a SPDY 3.x priority value, or else an HTTP/2
// stream dependency tuple {parent stream ID, weight, exclusive bit}. Templated
// to allow for use by QUIC code; SPDY and HTTP/2 code should use the concrete
// type instantiation SpdyStreamPrecedence.
template <typename StreamIdType>
class QUICHE_EXPORT StreamPrecedence {
 public:
  // Constructs instance that is a SPDY 3.x priority. Clamps priority value to
  // the valid range [0, 7].
  explicit StreamPrecedence(SpdyPriority priority)
      : precedence_(ClampSpdy3Priority(priority)) {}

  // Constructs instance that is an HTTP/2 stream weight, parent stream ID, and
  // exclusive bit. Clamps stream weight to the valid range [1, 256].
  StreamPrecedence(StreamIdType parent_id, int weight, bool is_exclusive)
      : precedence_(Http2StreamDependency{parent_id, ClampHttp2Weight(weight),
                                          is_exclusive}) {}

  // Intentionally copyable, to support pass by value.
  StreamPrecedence(const StreamPrecedence& other) = default;
  StreamPrecedence& operator=(const StreamPrecedence& other) = default;

  // Returns true if this instance is a SPDY 3.x priority, or false if this
  // instance is an HTTP/2 stream dependency.
  bool is_spdy3_priority() const {
    return absl::holds_alternative<SpdyPriority>(precedence_);
  }

  // Returns SPDY 3.x priority value. If |is_spdy3_priority()| is true, this is
  // the value provided at construction, clamped to the legal priority
  // range. Otherwise, it is the HTTP/2 stream weight mapped to a SPDY 3.x
  // priority value, where minimum weight 1 corresponds to priority 7 (lowest
  // precedence) and maximum weight 256 corresponds to priority 0 (highest
  // precedence).
  SpdyPriority spdy3_priority() const {
    return is_spdy3_priority()
               ? absl::get<SpdyPriority>(precedence_)
               : Http2WeightToSpdy3Priority(
                     absl::get<Http2StreamDependency>(precedence_).weight);
  }

  // Returns HTTP/2 parent stream ID. If |is_spdy3_priority()| is false, this is
  // the value provided at construction, otherwise it is |kHttp2RootStreamId|.
  StreamIdType parent_id() const {
    return is_spdy3_priority()
               ? kHttp2RootStreamId
               : absl::get<Http2StreamDependency>(precedence_).parent_id;
  }

  // Returns HTTP/2 stream weight. If |is_spdy3_priority()| is false, this is
  // the value provided at construction, clamped to the legal weight
  // range. Otherwise, it is the SPDY 3.x priority value mapped to an HTTP/2
  // stream weight, where priority 0 (i.e. highest precedence) corresponds to
  // maximum weight 256 and priority 7 (lowest precedence) corresponds to
  // minimum weight 1.
  int weight() const {
    return is_spdy3_priority()
               ? Spdy3PriorityToHttp2Weight(
                     absl::get<SpdyPriority>(precedence_))
               : absl::get<Http2StreamDependency>(precedence_).weight;
  }

  // Returns HTTP/2 parent stream exclusivity. If |is_spdy3_priority()| is
  // false, this is the value provided at construction, otherwise it is false.
  bool is_exclusive() const {
    return absl::holds_alternative<Http2StreamDependency>(precedence_) &&
           absl::get<Http2StreamDependency>(precedence_).is_exclusive;
  }

  // Facilitates test assertions.
  bool operator==(const StreamPrecedence& other) const {
    return precedence_ == other.precedence_;
  }

  bool operator!=(const StreamPrecedence& other) const {
    return !(*this == other);
  }

 private:
  struct QUICHE_EXPORT Http2StreamDependency {
    StreamIdType parent_id;
    int weight;
    bool is_exclusive;

    bool operator==(const Http2StreamDependency& other) const {
      return parent_id == other.parent_id && weight == other.weight &&
             is_exclusive == other.is_exclusive;
    }
  };

  absl::variant<SpdyPriority, Http2StreamDependency> precedence_;
};

typedef StreamPrecedence<SpdyStreamId> SpdyStreamPrecedence;

class SpdyFrameVisitor;

// Intermediate representation for HTTP2 frames.
class QUICHE_EXPORT SpdyFrameIR {
 public:
  virtual ~SpdyFrameIR() {}

  virtual void Visit(SpdyFrameVisitor* visitor) const = 0;
  virtual SpdyFrameType frame_type() const = 0;
  SpdyStreamId stream_id() const { return stream_id_; }
  virtual bool fin() const;
  // Returns an estimate of the size of the serialized frame, without applying
  // compression. May not be exact.
  virtual size_t size() const = 0;

  // Returns the number of bytes of flow control window that would be consumed
  // by this frame if written to the wire.
  virtual int flow_control_window_consumed() const;

 protected:
  SpdyFrameIR() : stream_id_(0) {}
  explicit SpdyFrameIR(SpdyStreamId stream_id) : stream_id_(stream_id) {}
  SpdyFrameIR(const SpdyFrameIR&) = delete;
  SpdyFrameIR& operator=(const SpdyFrameIR&) = delete;

 private:
  SpdyStreamId stream_id_;
};

// Abstract class intended to be inherited by IRs that have the option of a FIN
// flag.
class QUICHE_EXPORT SpdyFrameWithFinIR : public SpdyFrameIR {
 public:
  ~SpdyFrameWithFinIR() override {}
  bool fin() const override;
  void set_fin(bool fin) { fin_ = fin; }

 protected:
  explicit SpdyFrameWithFinIR(SpdyStreamId stream_id)
      : SpdyFrameIR(stream_id), fin_(false) {}
  SpdyFrameWithFinIR(const SpdyFrameWithFinIR&) = delete;
  SpdyFrameWithFinIR& operator=(const SpdyFrameWithFinIR&) = delete;

 private:
  bool fin_;
};

// Abstract class intended to be inherited by IRs that contain a header
// block. Implies SpdyFrameWithFinIR.
class QUICHE_EXPORT SpdyFrameWithHeaderBlockIR : public SpdyFrameWithFinIR {
 public:
  ~SpdyFrameWithHeaderBlockIR() override;

  const Http2HeaderBlock& header_block() const { return header_block_; }
  void set_header_block(Http2HeaderBlock header_block) {
    // Deep copy.
    header_block_ = std::move(header_block);
  }
  void SetHeader(absl::string_view name, absl::string_view value) {
    header_block_[name] = value;
  }

 protected:
  SpdyFrameWithHeaderBlockIR(SpdyStreamId stream_id,
                             Http2HeaderBlock header_block);
  SpdyFrameWithHeaderBlockIR(const SpdyFrameWithHeaderBlockIR&) = delete;
  SpdyFrameWithHeaderBlockIR& operator=(const SpdyFrameWithHeaderBlockIR&) =
      delete;

 private:
  Http2HeaderBlock header_block_;
};

class QUICHE_EXPORT SpdyDataIR : public SpdyFrameWithFinIR {
 public:
  // Performs a deep copy on data.
  SpdyDataIR(SpdyStreamId stream_id, absl::string_view data);

  // Performs a deep copy on data.
  SpdyDataIR(SpdyStreamId stream_id, const char* data);

  // Moves data into data_store_. Makes a copy if passed a non-movable string.
  SpdyDataIR(SpdyStreamId stream_id, std::string data);

  // Use in conjunction with SetDataShallow() for shallow-copy on data.
  explicit SpdyDataIR(SpdyStreamId stream_id);
  SpdyDataIR(const SpdyDataIR&) = delete;
  SpdyDataIR& operator=(const SpdyDataIR&) = delete;

  ~SpdyDataIR() override;

  const char* data() const { return data_; }
  size_t data_len() const { return data_len_; }

  bool padded() const { return padded_; }

  int padding_payload_len() const { return padding_payload_len_; }

  void set_padding_len(int padding_len) {
    QUICHE_DCHECK_GT(padding_len, 0);
    QUICHE_DCHECK_LE(padding_len, kPaddingSizePerFrame);
    padded_ = true;
    // The pad field takes one octet on the wire.
    padding_payload_len_ = padding_len - 1;
  }

  // Deep-copy of data (keep private copy).
  void SetDataDeep(absl::string_view data) {
    data_store_ = std::make_unique<std::string>(data.data(), data.size());
    data_ = data_store_->data();
    data_len_ = data.size();
  }

  // Shallow-copy of data (do not keep private copy).
  void SetDataShallow(absl::string_view data) {
    data_store_.reset();
    data_ = data.data();
    data_len_ = data.size();
  }

  // Use this method if we don't have a contiguous buffer and only
  // need a length.
  void SetDataShallow(size_t len) {
    data_store_.reset();
    data_ = nullptr;
    data_len_ = len;
  }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  int flow_control_window_consumed() const override;

  size_t size() const override;

 private:
  // Used to store data that this SpdyDataIR should own.
  std::unique_ptr<std::string> data_store_;
  const char* data_;
  size_t data_len_;

  bool padded_;
  // padding_payload_len_ = desired padding length - len(padding length field).
  int padding_payload_len_;
};

class QUICHE_EXPORT SpdyRstStreamIR : public SpdyFrameIR {
 public:
  SpdyRstStreamIR(SpdyStreamId stream_id, SpdyErrorCode error_code);
  SpdyRstStreamIR(const SpdyRstStreamIR&) = delete;
  SpdyRstStreamIR& operator=(const SpdyRstStreamIR&) = delete;

  ~SpdyRstStreamIR() override;

  SpdyErrorCode error_code() const { return error_code_; }
  void set_error_code(SpdyErrorCode error_code) { error_code_ = error_code; }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

 private:
  SpdyErrorCode error_code_;
};

class QUICHE_EXPORT SpdySettingsIR : public SpdyFrameIR {
 public:
  SpdySettingsIR();
  SpdySettingsIR(const SpdySettingsIR&) = delete;
  SpdySettingsIR& operator=(const SpdySettingsIR&) = delete;
  ~SpdySettingsIR() override;

  // Overwrites as appropriate.
  const SettingsMap& values() const { return values_; }
  void AddSetting(SpdySettingsId id, int32_t value) { values_[id] = value; }

  bool is_ack() const { return is_ack_; }
  void set_is_ack(bool is_ack) { is_ack_ = is_ack; }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

 private:
  SettingsMap values_;
  bool is_ack_;
};

class QUICHE_EXPORT SpdyPingIR : public SpdyFrameIR {
 public:
  explicit SpdyPingIR(SpdyPingId id) : id_(id), is_ack_(false) {}
  SpdyPingIR(const SpdyPingIR&) = delete;
  SpdyPingIR& operator=(const SpdyPingIR&) = delete;
  SpdyPingId id() const { return id_; }

  bool is_ack() const { return is_ack_; }
  void set_is_ack(bool is_ack) { is_ack_ = is_ack; }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

 private:
  SpdyPingId id_;
  bool is_ack_;
};

class QUICHE_EXPORT SpdyGoAwayIR : public SpdyFrameIR {
 public:
  // References description, doesn't copy it, so description must outlast
  // this SpdyGoAwayIR.
  SpdyGoAwayIR(SpdyStreamId last_good_stream_id, SpdyErrorCode error_code,
               absl::string_view description);

  // References description, doesn't copy it, so description must outlast
  // this SpdyGoAwayIR.
  SpdyGoAwayIR(SpdyStreamId last_good_stream_id, SpdyErrorCode error_code,
               const char* description);

  // Moves description into description_store_, so caller doesn't need to
  // keep description live after constructing this SpdyGoAwayIR.
  SpdyGoAwayIR(SpdyStreamId last_good_stream_id, SpdyErrorCode error_code,
               std::string description);
  SpdyGoAwayIR(const SpdyGoAwayIR&) = delete;
  SpdyGoAwayIR& operator=(const SpdyGoAwayIR&) = delete;

  ~SpdyGoAwayIR() override;

  SpdyStreamId last_good_stream_id() const { return last_good_stream_id_; }
  void set_last_good_stream_id(SpdyStreamId last_good_stream_id) {
    QUICHE_DCHECK_EQ(0u, last_good_stream_id & ~kStreamIdMask);
    last_good_stream_id_ = last_good_stream_id;
  }
  SpdyErrorCode error_code() const { return error_code_; }
  void set_error_code(SpdyErrorCode error_code) {
    // TODO(hkhalil): Check valid ranges of error_code?
    error_code_ = error_code;
  }

  const absl::string_view& description() const { return description_; }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

 private:
  SpdyStreamId last_good_stream_id_;
  SpdyErrorCode error_code_;
  const std::string description_store_;
  const absl::string_view description_;
};

class QUICHE_EXPORT SpdyHeadersIR : public SpdyFrameWithHeaderBlockIR {
 public:
  explicit SpdyHeadersIR(SpdyStreamId stream_id)
      : SpdyHeadersIR(stream_id, Http2HeaderBlock()) {}
  SpdyHeadersIR(SpdyStreamId stream_id, Http2HeaderBlock header_block)
      : SpdyFrameWithHeaderBlockIR(stream_id, std::move(header_block)) {}
  SpdyHeadersIR(const SpdyHeadersIR&) = delete;
  SpdyHeadersIR& operator=(const SpdyHeadersIR&) = delete;

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

  bool has_priority() const { return has_priority_; }
  void set_has_priority(bool has_priority) { has_priority_ = has_priority; }
  int weight() const { return weight_; }
  void set_weight(int weight) { weight_ = weight; }
  SpdyStreamId parent_stream_id() const { return parent_stream_id_; }
  void set_parent_stream_id(SpdyStreamId id) { parent_stream_id_ = id; }
  bool exclusive() const { return exclusive_; }
  void set_exclusive(bool exclusive) { exclusive_ = exclusive; }
  bool padded() const { return padded_; }
  int padding_payload_len() const { return padding_payload_len_; }
  void set_padding_len(int padding_len) {
    QUICHE_DCHECK_GT(padding_len, 0);
    QUICHE_DCHECK_LE(padding_len, kPaddingSizePerFrame);
    padded_ = true;
    // The pad field takes one octet on the wire.
    padding_payload_len_ = padding_len - 1;
  }

 private:
  bool has_priority_ = false;
  int weight_ = kHttp2DefaultStreamWeight;
  SpdyStreamId parent_stream_id_ = 0;
  bool exclusive_ = false;
  bool padded_ = false;
  int padding_payload_len_ = 0;
};

class QUICHE_EXPORT SpdyWindowUpdateIR : public SpdyFrameIR {
 public:
  SpdyWindowUpdateIR(SpdyStreamId stream_id, int32_t delta)
      : SpdyFrameIR(stream_id) {
    set_delta(delta);
  }
  SpdyWindowUpdateIR(const SpdyWindowUpdateIR&) = delete;
  SpdyWindowUpdateIR& operator=(const SpdyWindowUpdateIR&) = delete;

  int32_t delta() const { return delta_; }
  void set_delta(int32_t delta) {
    QUICHE_DCHECK_LE(0, delta);
    QUICHE_DCHECK_LE(delta, kSpdyMaximumWindowSize);
    delta_ = delta;
  }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

 private:
  int32_t delta_;
};

class QUICHE_EXPORT SpdyPushPromiseIR : public SpdyFrameWithHeaderBlockIR {
 public:
  SpdyPushPromiseIR(SpdyStreamId stream_id, SpdyStreamId promised_stream_id)
      : SpdyPushPromiseIR(stream_id, promised_stream_id, Http2HeaderBlock()) {}
  SpdyPushPromiseIR(SpdyStreamId stream_id, SpdyStreamId promised_stream_id,
                    Http2HeaderBlock header_block)
      : SpdyFrameWithHeaderBlockIR(stream_id, std::move(header_block)),
        promised_stream_id_(promised_stream_id),
        padded_(false),
        padding_payload_len_(0) {}
  SpdyPushPromiseIR(const SpdyPushPromiseIR&) = delete;
  SpdyPushPromiseIR& operator=(const SpdyPushPromiseIR&) = delete;
  SpdyStreamId promised_stream_id() const { return promised_stream_id_; }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

  bool padded() const { return padded_; }
  int padding_payload_len() const { return padding_payload_len_; }
  void set_padding_len(int padding_len) {
    QUICHE_DCHECK_GT(padding_len, 0);
    QUICHE_DCHECK_LE(padding_len, kPaddingSizePerFrame);
    padded_ = true;
    // The pad field takes one octet on the wire.
    padding_payload_len_ = padding_len - 1;
  }

 private:
  SpdyStreamId promised_stream_id_;

  bool padded_;
  int padding_payload_len_;
};

class QUICHE_EXPORT SpdyContinuationIR : public SpdyFrameIR {
 public:
  explicit SpdyContinuationIR(SpdyStreamId stream_id);
  SpdyContinuationIR(const SpdyContinuationIR&) = delete;
  SpdyContinuationIR& operator=(const SpdyContinuationIR&) = delete;
  ~SpdyContinuationIR() override;

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  bool end_headers() const { return end_headers_; }
  void set_end_headers(bool end_headers) { end_headers_ = end_headers; }
  const std::string& encoding() const { return encoding_; }
  void take_encoding(std::string encoding) { encoding_ = std::move(encoding); }
  size_t size() const override;

 private:
  std::string encoding_;
  bool end_headers_;
};

class QUICHE_EXPORT SpdyAltSvcIR : public SpdyFrameIR {
 public:
  explicit SpdyAltSvcIR(SpdyStreamId stream_id);
  SpdyAltSvcIR(const SpdyAltSvcIR&) = delete;
  SpdyAltSvcIR& operator=(const SpdyAltSvcIR&) = delete;
  ~SpdyAltSvcIR() override;

  std::string origin() const { return origin_; }
  const SpdyAltSvcWireFormat::AlternativeServiceVector& altsvc_vector() const {
    return altsvc_vector_;
  }

  void set_origin(std::string origin) { origin_ = std::move(origin); }
  void add_altsvc(const SpdyAltSvcWireFormat::AlternativeService& altsvc) {
    altsvc_vector_.push_back(altsvc);
  }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

 private:
  std::string origin_;
  SpdyAltSvcWireFormat::AlternativeServiceVector altsvc_vector_;
};

class QUICHE_EXPORT SpdyPriorityIR : public SpdyFrameIR {
 public:
  SpdyPriorityIR(SpdyStreamId stream_id, SpdyStreamId parent_stream_id,
                 int weight, bool exclusive)
      : SpdyFrameIR(stream_id),
        parent_stream_id_(parent_stream_id),
        weight_(weight),
        exclusive_(exclusive) {}
  SpdyPriorityIR(const SpdyPriorityIR&) = delete;
  SpdyPriorityIR& operator=(const SpdyPriorityIR&) = delete;
  SpdyStreamId parent_stream_id() const { return parent_stream_id_; }
  int weight() const { return weight_; }
  bool exclusive() const { return exclusive_; }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

 private:
  SpdyStreamId parent_stream_id_;
  int weight_;
  bool exclusive_;
};

class QUICHE_EXPORT SpdyPriorityUpdateIR : public SpdyFrameIR {
 public:
  SpdyPriorityUpdateIR(SpdyStreamId stream_id,
                       SpdyStreamId prioritized_stream_id,
                       std::string priority_field_value)
      : SpdyFrameIR(stream_id),
        prioritized_stream_id_(prioritized_stream_id),
        priority_field_value_(std::move(priority_field_value)) {}
  SpdyPriorityUpdateIR(const SpdyPriorityUpdateIR&) = delete;
  SpdyPriorityUpdateIR& operator=(const SpdyPriorityUpdateIR&) = delete;
  SpdyStreamId prioritized_stream_id() const { return prioritized_stream_id_; }
  const std::string& priority_field_value() const {
    return priority_field_value_;
  }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

 private:
  SpdyStreamId prioritized_stream_id_;
  std::string priority_field_value_;
};

struct QUICHE_EXPORT AcceptChOriginValuePair {
  std::string origin;
  std::string value;
  bool operator==(const AcceptChOriginValuePair& rhs) const {
    return origin == rhs.origin && value == rhs.value;
  }
};

class QUICHE_EXPORT SpdyAcceptChIR : public SpdyFrameIR {
 public:
  SpdyAcceptChIR(std::vector<AcceptChOriginValuePair> entries)
      : entries_(std::move(entries)) {}
  SpdyAcceptChIR(const SpdyAcceptChIR&) = delete;
  SpdyAcceptChIR& operator=(const SpdyAcceptChIR&) = delete;

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  size_t size() const override;

  const std::vector<AcceptChOriginValuePair>& entries() const {
    return entries_;
  }

 private:
  std::vector<AcceptChOriginValuePair> entries_;
};

// Represents a frame of unrecognized type.
class QUICHE_EXPORT SpdyUnknownIR : public SpdyFrameIR {
 public:
  SpdyUnknownIR(SpdyStreamId stream_id, uint8_t type, uint8_t flags,
                std::string payload)
      : SpdyFrameIR(stream_id),
        type_(type),
        flags_(flags),
        length_(payload.size()),
        payload_(std::move(payload)) {}
  SpdyUnknownIR(const SpdyUnknownIR&) = delete;
  SpdyUnknownIR& operator=(const SpdyUnknownIR&) = delete;
  uint8_t type() const { return type_; }
  uint8_t flags() const { return flags_; }
  size_t length() const { return length_; }
  const std::string& payload() const { return payload_; }

  void Visit(SpdyFrameVisitor* visitor) const override;

  SpdyFrameType frame_type() const override;

  int flow_control_window_consumed() const override;

  size_t size() const override;

 protected:
  // Allows subclasses to overwrite the default payload length.
  void set_length(size_t length) { length_ = length; }

 private:
  uint8_t type_;
  uint8_t flags_;
  size_t length_;
  const std::string payload_;
};

class QUICHE_EXPORT SpdySerializedFrame {
 public:
  SpdySerializedFrame()
      : frame_(const_cast<char*>("")), size_(0), owns_buffer_(false) {}

  // Create a valid SpdySerializedFrame using a pre-created buffer.
  // If |owns_buffer| is true, this class takes ownership of the buffer and will
  // delete it on cleanup.  The buffer must have been created using new char[].
  // If |owns_buffer| is false, the caller retains ownership of the buffer and
  // is responsible for making sure the buffer outlives this frame.  In other
  // words, this class does NOT create a copy of the buffer.
  SpdySerializedFrame(char* data, size_t size, bool owns_buffer)
      : frame_(data), size_(size), owns_buffer_(owns_buffer) {}

  SpdySerializedFrame(SpdySerializedFrame&& other)
      : frame_(other.frame_),
        size_(other.size_),
        owns_buffer_(other.owns_buffer_) {
    // |other| is no longer responsible for the buffer.
    other.owns_buffer_ = false;
  }
  SpdySerializedFrame(const SpdySerializedFrame&) = delete;
  SpdySerializedFrame& operator=(const SpdySerializedFrame&) = delete;

  SpdySerializedFrame& operator=(SpdySerializedFrame&& other) {
    // Free buffer if necessary.
    if (owns_buffer_) {
      delete[] frame_;
    }
    // Take over |other|.
    frame_ = other.frame_;
    size_ = other.size_;
    owns_buffer_ = other.owns_buffer_;
    // |other| is no longer responsible for the buffer.
    other.owns_buffer_ = false;
    return *this;
  }

  ~SpdySerializedFrame() {
    if (owns_buffer_) {
      delete[] frame_;
    }
  }

  // Provides access to the frame bytes, which is a buffer containing the frame
  // packed as expected for sending over the wire.
  char* data() const { return frame_; }

  // Returns the actual size of the underlying buffer.
  size_t size() const { return size_; }

  operator absl::string_view() const {
    return absl::string_view{frame_, size_};
  }

  operator std::string() const { return std::string{frame_, size_}; }

  // Returns a buffer containing the contents of the frame, of which the caller
  // takes ownership, and clears this SpdySerializedFrame.
  char* ReleaseBuffer() {
    char* buffer;
    if (owns_buffer_) {
      // If the buffer is owned, relinquish ownership to the caller.
      buffer = frame_;
      owns_buffer_ = false;
    } else {
      // Otherwise, we need to make a copy to give to the caller.
      buffer = new char[size_];
      memcpy(buffer, frame_, size_);
    }
    *this = SpdySerializedFrame();
    return buffer;
  }

 protected:
  char* frame_;

 private:
  size_t size_;
  bool owns_buffer_;
};

// This interface is for classes that want to process SpdyFrameIRs without
// having to know what type they are.  An instance of this interface can be
// passed to a SpdyFrameIR's Visit method, and the appropriate type-specific
// method of this class will be called.
class QUICHE_EXPORT SpdyFrameVisitor {
 public:
  SpdyFrameVisitor() {}
  SpdyFrameVisitor(const SpdyFrameVisitor&) = delete;
  SpdyFrameVisitor& operator=(const SpdyFrameVisitor&) = delete;
  virtual ~SpdyFrameVisitor() {}

  virtual void VisitRstStream(const SpdyRstStreamIR& rst_stream) = 0;
  virtual void VisitSettings(const SpdySettingsIR& settings) = 0;
  virtual void VisitPing(const SpdyPingIR& ping) = 0;
  virtual void VisitGoAway(const SpdyGoAwayIR& goaway) = 0;
  virtual void VisitHeaders(const SpdyHeadersIR& headers) = 0;
  virtual void VisitWindowUpdate(const SpdyWindowUpdateIR& window_update) = 0;
  virtual void VisitPushPromise(const SpdyPushPromiseIR& push_promise) = 0;
  virtual void VisitContinuation(const SpdyContinuationIR& continuation) = 0;
  virtual void VisitAltSvc(const SpdyAltSvcIR& altsvc) = 0;
  virtual void VisitPriority(const SpdyPriorityIR& priority) = 0;
  virtual void VisitData(const SpdyDataIR& data) = 0;
  virtual void VisitPriorityUpdate(
      const SpdyPriorityUpdateIR& priority_update) = 0;
  virtual void VisitAcceptCh(const SpdyAcceptChIR& accept_ch) = 0;
  virtual void VisitUnknown(const SpdyUnknownIR& /*unknown*/) {
    // TODO(birenroy): make abstract.
  }
};

// Optionally, and in addition to SpdyFramerVisitorInterface, a class supporting
// SpdyFramerDebugVisitorInterface may be used in conjunction with SpdyFramer in
// order to extract debug/internal information about the SpdyFramer as it
// operates.
//
// Most HTTP2 implementations need not bother with this interface at all.
class QUICHE_EXPORT SpdyFramerDebugVisitorInterface {
 public:
  virtual ~SpdyFramerDebugVisitorInterface() {}

  // Called after compressing a frame with a payload of
  // a list of name-value pairs.
  // |payload_len| is the uncompressed payload size.
  // |frame_len| is the compressed frame size.
  virtual void OnSendCompressedFrame(SpdyStreamId /*stream_id*/,
                                     SpdyFrameType /*type*/,
                                     size_t /*payload_len*/,
                                     size_t /*frame_len*/) {}

  // Called when a frame containing a compressed payload of
  // name-value pairs is received.
  // |frame_len| is the compressed frame size.
  virtual void OnReceiveCompressedFrame(SpdyStreamId /*stream_id*/,
                                        SpdyFrameType /*type*/,
                                        size_t /*frame_len*/) {}
};

// Calculates the number of bytes required to serialize a SpdyHeadersIR, not
// including the bytes to be used for the encoded header set.
size_t GetHeaderFrameSizeSansBlock(const SpdyHeadersIR& header_ir);

// Calculates the number of bytes required to serialize a SpdyPushPromiseIR,
// not including the bytes to be used for the encoded header set.
size_t GetPushPromiseFrameSizeSansBlock(
    const SpdyPushPromiseIR& push_promise_ir);

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_SPDY_PROTOCOL_H_
