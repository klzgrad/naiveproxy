#ifndef QUICHE_BINARY_HTTP_BINARY_HTTP_MESSAGE_H_
#define QUICHE_BINARY_HTTP_BINARY_HTTP_MESSAGE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_data_writer.h"

namespace quiche {

// Supports encoding and decoding Binary Http messages.
// Currently limited to known-length messages.
// https://www.ietf.org/archive/id/draft-ietf-httpbis-binary-message-06.html
class QUICHE_EXPORT_PRIVATE BinaryHttpMessage {
 public:
  // Name value pair of either a header or trailer field.
  struct Field {
    std::string name;
    std::string value;
    bool operator==(const BinaryHttpMessage::Field& rhs) const {
      return name == rhs.name && value == rhs.value;
    }
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

  absl::string_view body() const { return body_; }

  // Returns the Binary Http formatted message.
  virtual absl::StatusOr<std::string> Serialize() const = 0;
  // TODO(bschneider): Add AddTrailerField for chunked messages
  // TODO(bschneider): Add SetBodyCallback() for chunked messages
 protected:
  class Fields {
   public:
    // Appends `field` to list of fields.  Can contain duplicates.
    void AddField(BinaryHttpMessage::Field field);

    const std::vector<BinaryHttpMessage::Field>& fields() const {
      return fields_;
    }

    // Encode fields in insertion order.
    // https://www.ietf.org/archive/id/draft-ietf-httpbis-binary-message-06.html#name-header-and-trailer-field-li
    absl::Status Encode(quiche::QuicheDataWriter& writer) const;

    // The number of returned by EncodedFieldsSize
    // plus the number of bytes used in the varint holding that value.
    uint64_t EncodedSize() const;

   private:
    // Number of bytes of just the set of fields.
    uint64_t EncodedFieldsSize() const;

    // Fields in insertion order.
    std::vector<BinaryHttpMessage::Field> fields_;
  };

  absl::Status EncodeKnownLengthFieldsAndBody(
      quiche::QuicheDataWriter& writer) const;
  uint64_t EncodedKnownLengthFieldsAndBodySize() const;
  bool has_host() const { return has_host_; }

 private:
  std::string body_;
  Fields header_fields_;
  bool has_host_ = false;
};

class QUICHE_EXPORT_PRIVATE BinaryHttpRequest : public BinaryHttpMessage {
 public:
  // HTTP request must have all of the following fields.
  // Some examples are:
  //   scheme: HTTP
  //   authority: www.example.com
  //   path: /index.html
  struct ControlData {
    std::string method;
    std::string scheme;
    std::string authority;
    std::string path;
    bool operator==(const BinaryHttpRequest::ControlData& rhs) const {
      return method == rhs.method && scheme == rhs.scheme &&
             authority == rhs.authority && path == rhs.path;
    }
  };
  explicit BinaryHttpRequest(ControlData control_data)
      : control_data_(std::move(control_data)) {}

  // Deserialize
  static absl::StatusOr<BinaryHttpRequest> Create(absl::string_view data);

  absl::StatusOr<std::string> Serialize() const override;
  const ControlData& control_data() const { return control_data_; }

 private:
  absl::Status EncodeControlData(quiche::QuicheDataWriter& writer) const;

  uint64_t EncodedControlDataSize() const;

  uint64_t EncodedSize() const;

  // Returns Binary Http known length request formatted request.
  absl::StatusOr<std::string> EncodeAsKnownLength() const;

  const ControlData control_data_;
};

class QUICHE_EXPORT_PRIVATE BinaryHttpResponse : public BinaryHttpMessage {
 public:
  // https://www.ietf.org/archive/id/draft-ietf-httpbis-binary-message-06.html#name-response-control-data
  struct InformationalResponse {
    uint16_t status_code;
    const std::vector<Field>& header_fields;
    bool operator==(
        const BinaryHttpResponse::InformationalResponse& rhs) const {
      return status_code == rhs.status_code &&
             header_fields == rhs.header_fields;
    }
  };

  explicit BinaryHttpResponse(uint16_t status_code)
      : status_code_(status_code) {}

  // Deserialize
  static absl::StatusOr<BinaryHttpResponse> Create(absl::string_view data);

  absl::StatusOr<std::string> Serialize() const override;

  // Informational status codes must be between 100 and 199 inclusive.
  absl::Status AddInformationalResponse(uint16_t status_code,
                                        std::vector<Field> header_fields);

  uint16_t status_code() const { return status_code_; }
  // References in the returned `ResponseControlData` are invalidated on
  // `BinaryHttpResponse` object mutations.
  std::vector<InformationalResponse> GetInformationalResponse() const;

 private:
  //
  // Valid response codes are [100,199].
  // TODO(bschneider): Collapse this inner class into the public
  // `InformationalResponse` struct.
  class InformationalResponseInternal {
   public:
    explicit InformationalResponseInternal(uint16_t response_code)
        : response_code_(response_code) {}

    void AddField(absl::string_view name, std::string value);

    // Appends the encoded fields and body to `writer`.
    absl::Status Encode(quiche::QuicheDataWriter& writer) const;

    uint64_t EncodedSize() const;

    uint16_t response_code() const { return response_code_; }

    const std::vector<BinaryHttpMessage::Field>& fields() const {
      return fields_.fields();
    }

   private:
    const uint16_t response_code_;
    BinaryHttpMessage::Fields fields_;
  };

  // Returns Binary Http known length request formatted response.
  absl::StatusOr<std::string> EncodeAsKnownLength() const;

  uint64_t EncodedSize() const;

  std::vector<InformationalResponseInternal>
      informational_response_control_data_;
  const uint16_t status_code_;
};
}  // namespace quiche

#endif  // QUICHE_BINARY_HTTP_BINARY_HTTP_MESSAGE_H_
