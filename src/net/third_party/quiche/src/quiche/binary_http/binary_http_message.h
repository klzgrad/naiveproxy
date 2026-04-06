#ifndef QUICHE_BINARY_HTTP_BINARY_HTTP_MESSAGE_H_
#define QUICHE_BINARY_HTTP_BINARY_HTTP_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"

namespace quiche {

// Supports encoding and decoding Binary Http messages.
// Currently limited to known-length messages.
// https://www.ietf.org/archive/id/draft-ietf-httpbis-binary-message-06.html
class QUICHE_EXPORT BinaryHttpMessage {
 public:
  // A view of a field name and value. Used to pass around a field without
  // owning or copying the backing data.
  struct QUICHE_EXPORT FieldView {
    absl::string_view name;
    absl::string_view value;
  };
  // Name value pair of either a header or trailer field.
  struct QUICHE_EXPORT Field {
    std::string name;
    std::string value;
    bool operator==(const BinaryHttpMessage::Field& rhs) const {
      return name == rhs.name && value == rhs.value;
    }

    bool operator!=(const BinaryHttpMessage::Field& rhs) const {
      return !(*this == rhs);
    }

    std::string DebugString() const;
  };
  virtual ~BinaryHttpMessage() = default;

  // TODO(bschneider): Switch to use existing Http2HeaderBlock
  BinaryHttpMessage* AddHeaderField(Field header_field);

  const std::vector<Field>& GetHeaderFields() const {
    return header_fields_.fields();
  }

  BinaryHttpMessage* set_body(std::string body) {
    body_ = std::move(body);
    return this;
  }

  void swap_body(std::string& body) { body_.swap(body); }
  void set_num_padding_bytes(size_t num_padding_bytes) {
    num_padding_bytes_ = num_padding_bytes;
  }
  size_t num_padding_bytes() const { return num_padding_bytes_; }

  absl::string_view body() const { return body_; }

  // Returns the number of bytes `Serialize` will return, including padding.
  virtual size_t EncodedSize() const = 0;

  // Returns the Binary Http formatted message.
  virtual absl::StatusOr<std::string> Serialize() const = 0;
  // TODO(bschneider): Add AddTrailerField for chunked messages
  // TODO(bschneider): Add SetBodyCallback() for chunked messages

  virtual std::string DebugString() const;

 protected:
  class Fields {
   public:
    // Appends `field` to list of fields.  Can contain duplicates.
    void AddField(BinaryHttpMessage::Field field);

    const std::vector<BinaryHttpMessage::Field>& fields() const {
      return fields_;
    }

    bool operator==(const BinaryHttpMessage::Fields& rhs) const {
      return fields_ == rhs.fields_;
    }

    // Encode fields in insertion order.
    // https://www.ietf.org/archive/id/draft-ietf-httpbis-binary-message-06.html#name-header-and-trailer-field-li
    absl::Status Encode(quiche::QuicheDataWriter& writer) const;

    // The number of returned by EncodedFieldsSize
    // plus the number of bytes used in the varint holding that value.
    size_t EncodedSize() const;

   private:
    // Number of bytes of just the set of fields.
    size_t EncodedFieldsSize() const;

    // Fields in insertion order.
    std::vector<BinaryHttpMessage::Field> fields_;
  };

  // Checks equality excluding padding.
  bool IsPayloadEqual(const BinaryHttpMessage& rhs) const {
    // `has_host_` is derived from `header_fields_` so it doesn't need to be
    // tested directly.
    return body_ == rhs.body_ && header_fields_ == rhs.header_fields_;
  }

  absl::Status EncodeKnownLengthFieldsAndBody(
      quiche::QuicheDataWriter& writer) const;
  size_t EncodedKnownLengthFieldsAndBodySize() const;
  bool has_host() const { return has_host_; }

 private:
  std::string body_;
  Fields header_fields_;
  bool has_host_ = false;
  size_t num_padding_bytes_ = 0;
};

void QUICHE_EXPORT PrintTo(const BinaryHttpMessage::Field& msg,
                           std::ostream* os);

class QUICHE_EXPORT BinaryHttpRequest : public BinaryHttpMessage {
 public:
  // HTTP request must have method, scheme, and path fields.
  // The `authority` field is required unless a `host` header field is added.
  // If a `host` header field is added, `authority` is serialized as the empty
  // string.
  // Some examples are:
  //   scheme: HTTP
  //   authority: www.example.com
  //   path: /index.html
  struct QUICHE_EXPORT ControlData {
    std::string method;
    std::string scheme;
    std::string authority;
    std::string path;
    bool operator==(const BinaryHttpRequest::ControlData& rhs) const {
      return method == rhs.method && scheme == rhs.scheme &&
             authority == rhs.authority && path == rhs.path;
    }
    bool operator!=(const BinaryHttpRequest::ControlData& rhs) const {
      return !(*this == rhs);
    }
  };
  explicit BinaryHttpRequest(ControlData control_data)
      : control_data_(std::move(control_data)) {}

  // Deserialize
  static absl::StatusOr<BinaryHttpRequest> Create(absl::string_view data);

  size_t EncodedSize() const override;
  absl::StatusOr<std::string> Serialize() const override;
  const ControlData& control_data() const { return control_data_; }

  virtual std::string DebugString() const override;

  // Returns true if the contents of the requests are equal, excluding padding.
  bool IsPayloadEqual(const BinaryHttpRequest& rhs) const {
    return control_data_ == rhs.control_data_ &&
           BinaryHttpMessage::IsPayloadEqual(rhs);
  }

  bool operator==(const BinaryHttpRequest& rhs) const {
    return IsPayloadEqual(rhs) &&
           num_padding_bytes() == rhs.num_padding_bytes();
  }

  bool operator!=(const BinaryHttpRequest& rhs) const {
    return !(*this == rhs);
  }

  // Provides a Decode method that can be called multiple times as data is
  // received. The relevant MessageSectionHandler method will be called when
  // its corresponding section is successfully decoded.
  class QUICHE_EXPORT IndeterminateLengthDecoder;

  // Provides encoding methods for an Indeterminate-Length BHTTP request. The
  // encoder keeps track of what has been encoded so far to ensure sections are
  // encoded in the correct order, this means it can only be used for a single
  // request message.
  class QUICHE_EXPORT IndeterminateLengthEncoder;

 private:
  // The sections of an Indeterminate-Length BHTTP request.
  enum class IndeterminateLengthMessageSection {
    kControlData,
    kHeader,
    kBody,
    kTrailer,
    kPadding,
    // The decoder/encoder is set to end after the message is marked as complete
    // or if an error occurs while processing the message.
    kEnd,
  };
  absl::Status EncodeControlData(quiche::QuicheDataWriter& writer) const;

  size_t EncodedControlDataSize() const;

  // Returns Binary Http known length request formatted request.
  absl::StatusOr<std::string> EncodeAsKnownLength() const;

  const ControlData control_data_;
};

// Provides a Decode method that can be called multiple times as data is
// received. The relevant MessageSectionHandler method will be called when
// its corresponding section is successfully decoded.
class QUICHE_EXPORT BinaryHttpRequest::IndeterminateLengthDecoder {
 public:
  // The handler to invoke when a section is decoded successfully. The
  // handler can return an error if the decoded data cannot be processed
  // successfully.
  class QUICHE_EXPORT MessageSectionHandler {
   public:
    virtual ~MessageSectionHandler() = default;
    virtual absl::Status OnControlData(const ControlData& control_data) = 0;
    virtual absl::Status OnHeader(absl::string_view name,
                                  absl::string_view value) = 0;
    virtual absl::Status OnHeadersDone() = 0;
    virtual absl::Status OnBodyChunk(absl::string_view body_chunk) = 0;
    virtual absl::Status OnBodyChunksDone() = 0;
    virtual absl::Status OnTrailer(absl::string_view name,
                                   absl::string_view value) = 0;
    virtual absl::Status OnTrailersDone() = 0;
  };

  // Creates a new IndeterminateLengthDecoder. Does not take ownership of
  // `message_section_handler`, which must refer to a valid handler that
  // outlives this decoder.
  explicit IndeterminateLengthDecoder(
      MessageSectionHandler* absl_nonnull message_section_handler
          ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : message_section_handler_(*message_section_handler) {}

  // Decodes an Indeterminate-Length BHTTP request. As the caller receives
  // portions of the request, the caller can call this method with the request
  // portion. The class keeps track of the current message section that is
  // being decoded and buffers data if the section is not fully decoded so
  // that the next call can continue decoding from where it left off. It will
  // also invoke the appropriate MessageSectionHandler method when a section
  // is decoded successfully.
  // `end_stream` indicates that no more data will be provided to the decoder.
  // This is used to determine if a valid message was decoded properly given
  // the last piece of data provided, handling both complete messages and
  // truncated messages.
  absl::Status Decode(absl::string_view data, bool end_stream);

 private:
  // Carries out the decode logic from the checkpoint. Returns
  // OutOfRangeError if there is not enough data to decode the current
  // section. When a section is fully decoded, the checkpoint is updated.
  absl::Status DecodeCheckpointData(bool end_stream,
                                    absl::string_view& checkpoint);

  // The handler to invoke when a section is decoded successfully. The
  // handler can return an error if the decoded data cannot be processed
  // successfully. Not owned.
  MessageSectionHandler& message_section_handler_;
  // Stores the data that could not be processed due to missing data.
  std::string buffer_;
  // The current section that is being decoded.
  IndeterminateLengthMessageSection current_section_ =
      IndeterminateLengthMessageSection::kControlData;
};

// Provides encoding methods for an Indeterminate-Length BHTTP request. The
// encoder keeps track of what has been encoded so far to ensure sections are
// encoded in the correct order, this means it can only be used for a single
// request message.
class QUICHE_EXPORT BinaryHttpRequest::IndeterminateLengthEncoder {
 public:
  // Encodes the initial framing indicator and the specified control data.
  absl::StatusOr<std::string> EncodeControlData(
      const ControlData& control_data);
  // Encodes the specified headers and its content terminator.
  absl::StatusOr<std::string> EncodeHeaders(
      absl::Span<const FieldView> headers);
  // Encodes the specified body chunks. This can be called multiple times but
  // it needs to be called exactly once with `body_chunks_done` set to true at
  // the end to properly set the content terminator. Encoding body chunks is
  // optional since valid chunked messages can be truncated.
  absl::StatusOr<std::string> EncodeBodyChunks(
      absl::Span<const absl::string_view> body_chunks, bool body_chunks_done);
  // Encodes the specified trailers and its content terminator. Encoding
  // trailers is optional since valid chunked messages can be truncated.
  absl::StatusOr<std::string> EncodeTrailers(
      absl::Span<const FieldView> trailers);

 private:
  absl::StatusOr<std::string> EncodeFieldSection(
      absl::Span<const FieldView> fields);

  IndeterminateLengthMessageSection current_section_ =
      IndeterminateLengthMessageSection::kControlData;
};

void QUICHE_EXPORT PrintTo(const BinaryHttpRequest& msg, std::ostream* os);

class QUICHE_EXPORT BinaryHttpResponse : public BinaryHttpMessage {
 public:
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-binary-message-06.html#name-response-control-data
  // A response can contain 0 to N informational responses.  Each informational
  // response contains a status code followed by a header field. Valid status
  // codes are [100,199].
  class QUICHE_EXPORT InformationalResponse {
   public:
    explicit InformationalResponse(uint16_t status_code)
        : status_code_(status_code) {}
    InformationalResponse(uint16_t status_code,
                          const std::vector<BinaryHttpMessage::Field>& fields)
        : status_code_(status_code) {
      for (const BinaryHttpMessage::Field& field : fields) {
        AddField(field.name, field.value);
      }
    }

    bool operator==(
        const BinaryHttpResponse::InformationalResponse& rhs) const {
      return status_code_ == rhs.status_code_ && fields_ == rhs.fields_;
    }

    bool operator!=(
        const BinaryHttpResponse::InformationalResponse& rhs) const {
      return !(*this == rhs);
    }

    // Adds a field with the provided name, converted to lower case.
    // Fields are in the order they are added.
    void AddField(absl::string_view name, std::string value);

    const std::vector<BinaryHttpMessage::Field>& fields() const {
      return fields_.fields();
    }

    uint16_t status_code() const { return status_code_; }

    std::string DebugString() const;

   private:
    // Give BinaryHttpResponse access to Encoding functionality.
    friend class BinaryHttpResponse;

    size_t EncodedSize() const;

    // Appends the encoded fields and body to `writer`.
    absl::Status Encode(quiche::QuicheDataWriter& writer) const;

    const uint16_t status_code_;
    BinaryHttpMessage::Fields fields_;
  };

  explicit BinaryHttpResponse(uint16_t status_code)
      : status_code_(status_code) {}

  // Deserialize
  static absl::StatusOr<BinaryHttpResponse> Create(absl::string_view data);

  size_t EncodedSize() const override;
  absl::StatusOr<std::string> Serialize() const override;

  // Informational status codes must be between 100 and 199 inclusive.
  absl::Status AddInformationalResponse(uint16_t status_code,
                                        std::vector<Field> header_fields);

  uint16_t status_code() const { return status_code_; }

  // References in the returned `ResponseControlData` are invalidated on
  // `BinaryHttpResponse` object mutations.
  const std::vector<InformationalResponse>& informational_responses() const {
    return informational_response_control_data_;
  }

  virtual std::string DebugString() const override;

  // Returns true if the contents of the requests are equal, excluding padding.
  bool IsPayloadEqual(const BinaryHttpResponse& rhs) const {
    return informational_response_control_data_ ==
               rhs.informational_response_control_data_ &&
           status_code_ == rhs.status_code_ &&
           BinaryHttpMessage::IsPayloadEqual(rhs);
  }

  bool operator==(const BinaryHttpResponse& rhs) const {
    return IsPayloadEqual(rhs) &&
           num_padding_bytes() == rhs.num_padding_bytes();
  }

  bool operator!=(const BinaryHttpResponse& rhs) const {
    return !(*this == rhs);
  }

  // Provides encoding methods for an Indeterminate-Length BHTTP response. The
  // encoder keeps track of what has been encoded so far to ensure sections are
  // encoded in the correct order, this means it can only be used for a single
  // BHTTP response message.
  class QUICHE_EXPORT IndeterminateLengthEncoder;

  // Provides a Decode method that can be called multiple times as data is
  // received. The relevant MessageSectionHandler method will be called when
  // its corresponding section is successfully decoded.
  class QUICHE_EXPORT IndeterminateLengthDecoder;

 private:
  enum class IndeterminateLengthMessageSection {
    kFramingIndicator,
    kInformationalOrFinalStatusCode,
    kInformationalResponseHeader,
    kFinalResponseHeader,
    kBody,
    kTrailer,
    kPadding,
    kEnd,
  };
  // Returns Binary Http known length request formatted response.
  absl::StatusOr<std::string> EncodeAsKnownLength() const;

  std::vector<InformationalResponse> informational_response_control_data_;
  const uint16_t status_code_;
};

// Provides a Decode method that can be called multiple times as data is
// received. The relevant MessageSectionHandler method will be called when
// its corresponding section is successfully decoded.
class QUICHE_EXPORT BinaryHttpResponse::IndeterminateLengthDecoder {
 public:
  // The handler to invoke when a section is decoded successfully. The
  // handler can return an error if the decoded data cannot be processed
  // successfully.
  class QUICHE_EXPORT MessageSectionHandler {
   public:
    virtual ~MessageSectionHandler() = default;
    virtual absl::Status OnInformationalResponseStatusCode(
        uint16_t status_code) = 0;
    virtual absl::Status OnInformationalResponseHeader(
        absl::string_view name, absl::string_view value) = 0;
    virtual absl::Status OnInformationalResponseDone() = 0;
    virtual absl::Status OnInformationalResponsesSectionDone() = 0;
    virtual absl::Status OnFinalResponseStatusCode(uint16_t status_code) = 0;
    virtual absl::Status OnFinalResponseHeader(absl::string_view name,
                                               absl::string_view value) = 0;
    virtual absl::Status OnFinalResponseHeadersDone() = 0;
    virtual absl::Status OnBodyChunk(absl::string_view body_chunk) = 0;
    virtual absl::Status OnBodyChunksDone() = 0;
    virtual absl::Status OnTrailer(absl::string_view name,
                                   absl::string_view value) = 0;
    virtual absl::Status OnTrailersDone() = 0;
  };

  // Does not take ownership of `message_section_handler`, which must refer to a
  // valid handler that outlives this decoder.
  explicit IndeterminateLengthDecoder(
      MessageSectionHandler* absl_nonnull message_section_handler
          ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : message_section_handler_(*message_section_handler) {}

  // Decodes an Indeterminate-Length BHTTP response. As the caller receives
  // portions of the response, the caller can call this method with the
  // response portion. The class keeps track of the current message section
  // that is being decoded and buffers data if the section is not fully
  // decoded so that the next call can continue decoding from where it left
  // off. It will also invoke the appropriate MessageSectionHandler method
  // when a section is decoded successfully. `end_stream` indicates that no
  // more data will be provided to the decoder. This is used to determine if a
  // valid message was decoded properly given the last piece of data provided,
  // handling both complete messages and valid truncated messages.
  absl::Status Decode(absl::string_view data, bool end_stream);

 private:
  // Carries out the decode logic from the checkpoint. Returns
  // OutOfRangeError if there is not enough data to decode the current
  // section. When a section is fully decoded, the checkpoint is updated.
  absl::Status DecodeCheckpointData(bool end_stream,
                                    absl::string_view& checkpoint);
  // Decodes the informational response or final response status code and
  // updates the checkpoint.
  absl::Status DecodeStatusCode(QuicheDataReader& reader,
                                absl::string_view& checkpoint);

  // The handler to invoke when a section is decoded successfully. The
  // handler can return an error if the decoded data cannot be processed
  // successfully. Not owned.
  MessageSectionHandler& message_section_handler_;
  // Stores the data that could not be processed due to missing data.
  std::string buffer_;
  // The current section that is being decoded.
  IndeterminateLengthMessageSection current_section_ =
      IndeterminateLengthMessageSection::kFramingIndicator;
};

// Provides encoding methods for an Indeterminate-Length BHTTP response. The
// encoder keeps track of what has been encoded so far to ensure sections are
// encoded in the correct order, this means it can only be used for a single
// BHTTP response message.
class QUICHE_EXPORT BinaryHttpResponse::IndeterminateLengthEncoder {
 public:
  // Encodes the specified informational response status code, fields, and its
  // content terminator.
  absl::StatusOr<std::string> EncodeInformationalResponse(
      uint16_t status_code, absl::Span<const FieldView> fields);
  // Encodes the specified status code, headers, and its content terminator.
  absl::StatusOr<std::string> EncodeHeaders(
      uint16_t status_code, absl::Span<const FieldView> headers);
  // Encodes the specified body chunks. This can be called multiple times but
  // it needs to be called exactly once with `body_chunks_done` set to true at
  // the end to properly set the content terminator. Encoding body chunks is
  // optional since valid chunked messages can be truncated. Cannot be called
  // without any data unless `body_chunks_done` is true.
  absl::StatusOr<std::string> EncodeBodyChunks(
      absl::Span<const absl::string_view> body_chunks, bool body_chunks_done);
  // Encodes the specified trailers and its content terminator. Encoding
  // trailers is optional since valid chunked messages can be truncated.
  absl::StatusOr<std::string> EncodeTrailers(
      absl::Span<const FieldView> trailers);

 private:
  absl::StatusOr<std::string> EncodeFieldSection(
      std::optional<uint16_t> status_code, absl::Span<const FieldView> fields);
  std::string GetMessageSectionString(
      IndeterminateLengthMessageSection section) const;

  IndeterminateLengthMessageSection current_section_ =
      IndeterminateLengthMessageSection::kFramingIndicator;
};

void QUICHE_EXPORT PrintTo(const BinaryHttpResponse& msg, std::ostream* os);
}  // namespace quiche

#endif  // QUICHE_BINARY_HTTP_BINARY_HTTP_MESSAGE_H_
