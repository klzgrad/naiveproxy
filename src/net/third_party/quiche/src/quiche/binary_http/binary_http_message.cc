#include "quiche/binary_http/binary_http_message.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"

namespace quiche {
namespace {

constexpr uint64_t kKnownLengthRequestFraming = 0;
constexpr uint64_t kKnownLengthResponseFraming = 1;
constexpr uint64_t kIndeterminateLengthRequestFraming = 2;
constexpr uint64_t kContentTerminator = 0;

// A view of a field name and value. Used to pass around a field without owning
// or copying the backing data.
struct FieldView {
  absl::string_view name;
  absl::string_view value;
};

bool ReadStringValue(quiche::QuicheDataReader& reader, std::string& data) {
  absl::string_view data_view;
  if (!reader.ReadStringPieceVarInt62(&data_view)) {
    return false;
  }
  data = std::string(data_view);
  return true;
}

bool IsValidPadding(absl::string_view data) {
  return std::all_of(data.begin(), data.end(),
                     [](char c) { return c == '\0'; });
}

absl::StatusOr<BinaryHttpRequest::ControlData> DecodeControlData(
    quiche::QuicheDataReader& reader) {
  BinaryHttpRequest::ControlData control_data;
  if (!ReadStringValue(reader, control_data.method)) {
    return absl::InvalidArgumentError("Failed to read method.");
  }
  if (!ReadStringValue(reader, control_data.scheme)) {
    return absl::InvalidArgumentError("Failed to read scheme.");
  }
  if (!ReadStringValue(reader, control_data.authority)) {
    return absl::InvalidArgumentError("Failed to read authority.");
  }
  if (!ReadStringValue(reader, control_data.path)) {
    return absl::InvalidArgumentError("Failed to read path.");
  }
  return control_data;
}

// Decodes a header/trailer name and value. This takes a length which represents
// only the name length.
absl::StatusOr<FieldView> DecodeField(QuicheDataReader& reader,
                                      uint64_t name_length) {
  absl::string_view name;
  if (!reader.ReadStringPiece(&name, name_length)) {
    return absl::OutOfRangeError("Not enough data to read field name.");
  }
  absl::string_view value;
  if (!reader.ReadStringPieceVarInt62(&value)) {
    return absl::OutOfRangeError("Not enough data to read field value.");
  }
  return FieldView{name, value};
}

absl::Status DecodeFields(quiche::QuicheDataReader& reader,
                          quiche::UnretainedCallback<void(
                              absl::string_view name, absl::string_view value)>
                              callback) {
  absl::string_view fields;
  if (!reader.ReadStringPieceVarInt62(&fields)) {
    return absl::InvalidArgumentError("Failed to read fields.");
  }
  quiche::QuicheDataReader fields_reader(fields);
  while (!fields_reader.IsDoneReading()) {
    absl::string_view name;
    if (!fields_reader.ReadStringPieceVarInt62(&name)) {
      return absl::InvalidArgumentError("Failed to read field name.");
    }
    absl::string_view value;
    if (!fields_reader.ReadStringPieceVarInt62(&value)) {
      return absl::InvalidArgumentError("Failed to read field value.");
    }
    callback(name, value);
  }
  return absl::OkStatus();
}

absl::Status DecodeFieldsAndBody(quiche::QuicheDataReader& reader,
                                 BinaryHttpMessage& message) {
  if (const absl::Status status = DecodeFields(
          reader,
          [&message](absl::string_view name, absl::string_view value) {
            message.AddHeaderField({std::string(name), std::string(value)});
          });
      !status.ok()) {
    return status;
  }
  // Exit early if message has been truncated.
  // https://www.rfc-editor.org/rfc/rfc9292#section-3.8
  if (reader.IsDoneReading()) {
    return absl::OkStatus();
  }

  absl::string_view body;
  if (!reader.ReadStringPieceVarInt62(&body)) {
    return absl::InvalidArgumentError("Failed to read body.");
  }
  message.set_body(std::string(body));
  // TODO(bschneider): Check for / read-in any trailer-fields
  return absl::OkStatus();
}

absl::StatusOr<BinaryHttpRequest> DecodeKnownLengthRequest(
    quiche::QuicheDataReader& reader) {
  const auto control_data = DecodeControlData(reader);
  if (!control_data.ok()) {
    return control_data.status();
  }
  BinaryHttpRequest request(std::move(*control_data));
  if (reader.IsDoneReading()) {
    // Per RFC 9292, Section 3.8, "Decoders MUST treat missing truncated fields
    // as equivalent to having been sent with the length field set to zero."
    // If we've run out of payload, stop parsing and return the request.
    return request;
  }
  if (const absl::Status status = DecodeFieldsAndBody(reader, request);
      !status.ok()) {
    return status;
  }
  if (!IsValidPadding(reader.PeekRemainingPayload())) {
    return absl::InvalidArgumentError("Non-zero padding.");
  }
  request.set_num_padding_bytes(reader.BytesRemaining());
  return request;
}

absl::StatusOr<BinaryHttpResponse> DecodeKnownLengthResponse(
    quiche::QuicheDataReader& reader) {
  std::vector<std::pair<uint16_t, std::vector<BinaryHttpMessage::Field>>>
      informational_responses;
  uint64_t status_code;
  bool reading_response_control_data = true;
  while (reading_response_control_data) {
    if (!reader.ReadVarInt62(&status_code)) {
      return absl::InvalidArgumentError("Failed to read status code.");
    }
    if (status_code >= 100 && status_code <= 199) {
      std::vector<BinaryHttpMessage::Field> fields;
      if (const absl::Status status = DecodeFields(
              reader,
              [&fields](absl::string_view name, absl::string_view value) {
                fields.push_back({std::string(name), std::string(value)});
              });
          !status.ok()) {
        return status;
      }
      informational_responses.emplace_back(status_code, std::move(fields));
    } else {
      reading_response_control_data = false;
    }
  }
  BinaryHttpResponse response(status_code);
  for (const auto& informational_response : informational_responses) {
    if (const absl::Status status = response.AddInformationalResponse(
            informational_response.first,
            std::move(informational_response.second));
        !status.ok()) {
      return status;
    }
  }
  if (const absl::Status status = DecodeFieldsAndBody(reader, response);
      !status.ok()) {
    return status;
  }
  if (!IsValidPadding(reader.PeekRemainingPayload())) {
    return absl::InvalidArgumentError("Non-zero padding.");
  }
  response.set_num_padding_bytes(reader.BytesRemaining());
  return response;
}

uint64_t StringPieceVarInt62Len(absl::string_view s) {
  return quiche::QuicheDataWriter::GetVarInt62Len(s.length()) + s.length();
}
}  // namespace

void BinaryHttpMessage::Fields::AddField(BinaryHttpMessage::Field field) {
  fields_.push_back(std::move(field));
}

// Encode fields in the order they were initially inserted.
// Updates do not change order.
absl::Status BinaryHttpMessage::Fields::Encode(
    quiche::QuicheDataWriter& writer) const {
  if (!writer.WriteVarInt62(EncodedFieldsSize())) {
    return absl::InvalidArgumentError("Failed to write encoded field size.");
  }
  for (const BinaryHttpMessage::Field& field : fields_) {
    if (!writer.WriteStringPieceVarInt62(field.name)) {
      return absl::InvalidArgumentError("Failed to write field name.");
    }
    if (!writer.WriteStringPieceVarInt62(field.value)) {
      return absl::InvalidArgumentError("Failed to write field value.");
    }
  }
  return absl::OkStatus();
}

size_t BinaryHttpMessage::Fields::EncodedSize() const {
  const size_t size = EncodedFieldsSize();
  return size + quiche::QuicheDataWriter::GetVarInt62Len(size);
}

size_t BinaryHttpMessage::Fields::EncodedFieldsSize() const {
  size_t size = 0;
  for (const BinaryHttpMessage::Field& field : fields_) {
    size += StringPieceVarInt62Len(field.name) +
            StringPieceVarInt62Len(field.value);
  }
  return size;
}

BinaryHttpMessage* BinaryHttpMessage::AddHeaderField(
    BinaryHttpMessage::Field field) {
  const std::string lower_name = absl::AsciiStrToLower(field.name);
  if (lower_name == "host") {
    has_host_ = true;
  }
  header_fields_.AddField({std::move(lower_name), std::move(field.value)});
  return this;
}

// Appends the encoded fields and body to data.
absl::Status BinaryHttpMessage::EncodeKnownLengthFieldsAndBody(
    quiche::QuicheDataWriter& writer) const {
  if (const absl::Status status = header_fields_.Encode(writer); !status.ok()) {
    return status;
  }
  if (!writer.WriteStringPieceVarInt62(body_)) {
    return absl::InvalidArgumentError("Failed to encode body.");
  }
  // TODO(bschneider): Consider support for trailer fields on known-length
  // requests. Trailers are atypical for a known-length request.
  return absl::OkStatus();
}

size_t BinaryHttpMessage::EncodedKnownLengthFieldsAndBodySize() const {
  return header_fields_.EncodedSize() + StringPieceVarInt62Len(body_);
}

absl::Status BinaryHttpResponse::AddInformationalResponse(
    uint16_t status_code, std::vector<Field> header_fields) {
  if (status_code < 100) {
    return absl::InvalidArgumentError("status code < 100");
  }
  if (status_code > 199) {
    return absl::InvalidArgumentError("status code > 199");
  }
  InformationalResponse data(status_code);
  for (Field& header : header_fields) {
    data.AddField(header.name, std::move(header.value));
  }
  informational_response_control_data_.push_back(std::move(data));
  return absl::OkStatus();
}

absl::StatusOr<std::string> BinaryHttpResponse::Serialize() const {
  // Only supporting known length requests so far.
  return EncodeAsKnownLength();
}

absl::StatusOr<std::string> BinaryHttpResponse::EncodeAsKnownLength() const {
  std::string data;
  data.resize(EncodedSize());
  quiche::QuicheDataWriter writer(data.size(), data.data());
  if (!writer.WriteVarInt62(kKnownLengthResponseFraming)) {
    return absl::InvalidArgumentError("Failed to write framing indicator");
  }
  // Informational response
  for (const auto& informational : informational_response_control_data_) {
    if (const absl::Status status = informational.Encode(writer);
        !status.ok()) {
      return status;
    }
  }
  if (!writer.WriteVarInt62(status_code_)) {
    return absl::InvalidArgumentError("Failed to write status code");
  }
  if (const absl::Status status = EncodeKnownLengthFieldsAndBody(writer);
      !status.ok()) {
    return status;
  }
  QUICHE_DCHECK_EQ(writer.remaining(), num_padding_bytes());
  writer.WritePadding();
  return data;
}

size_t BinaryHttpResponse::EncodedSize() const {
  size_t size = QuicheDataWriter::GetVarInt62Len(kKnownLengthResponseFraming);
  for (const auto& informational : informational_response_control_data_) {
    size += informational.EncodedSize();
  }
  return size + quiche::QuicheDataWriter::GetVarInt62Len(status_code_) +
         EncodedKnownLengthFieldsAndBodySize() + num_padding_bytes();
}

void BinaryHttpResponse::InformationalResponse::AddField(absl::string_view name,
                                                         std::string value) {
  fields_.AddField({absl::AsciiStrToLower(name), std::move(value)});
}

// Appends the encoded fields and body to data.
absl::Status BinaryHttpResponse::InformationalResponse::Encode(
    quiche::QuicheDataWriter& writer) const {
  writer.WriteVarInt62(status_code_);
  return fields_.Encode(writer);
}

size_t BinaryHttpResponse::InformationalResponse::EncodedSize() const {
  return quiche::QuicheDataWriter::GetVarInt62Len(status_code_) +
         fields_.EncodedSize();
}

absl::StatusOr<std::string> BinaryHttpRequest::Serialize() const {
  // Only supporting known length requests so far.
  return EncodeAsKnownLength();
}

// https://www.ietf.org/archive/id/draft-ietf-httpbis-binary-message-06.html#name-request-control-data
absl::Status BinaryHttpRequest::EncodeControlData(
    quiche::QuicheDataWriter& writer) const {
  if (!writer.WriteStringPieceVarInt62(control_data_.method)) {
    return absl::InvalidArgumentError("Failed to encode method.");
  }
  if (!writer.WriteStringPieceVarInt62(control_data_.scheme)) {
    return absl::InvalidArgumentError("Failed to encode scheme.");
  }
  // the Host header field is not replicated in the :authority field, as is
  // required for ensuring that the request is reproduced accurately; see
  // Section 8.1.2.3 of [H2].
  if (!has_host()) {
    if (!writer.WriteStringPieceVarInt62(control_data_.authority)) {
      return absl::InvalidArgumentError("Failed to encode authority.");
    }
  } else {
    if (!writer.WriteStringPieceVarInt62("")) {
      return absl::InvalidArgumentError("Failed to encode authority.");
    }
  }
  if (!writer.WriteStringPieceVarInt62(control_data_.path)) {
    return absl::InvalidArgumentError("Failed to encode path.");
  }
  return absl::OkStatus();
}

size_t BinaryHttpRequest::EncodedControlDataSize() const {
  size_t size = StringPieceVarInt62Len(control_data_.method) +
                StringPieceVarInt62Len(control_data_.scheme) +
                StringPieceVarInt62Len(control_data_.path);
  if (!has_host()) {
    size += StringPieceVarInt62Len(control_data_.authority);
  } else {
    size += StringPieceVarInt62Len("");
  }
  return size;
}

size_t BinaryHttpRequest::EncodedSize() const {
  return QuicheDataWriter::GetVarInt62Len(kKnownLengthRequestFraming) +
         EncodedControlDataSize() + EncodedKnownLengthFieldsAndBodySize() +
         num_padding_bytes();
}

// https://www.ietf.org/archive/id/draft-ietf-httpbis-binary-message-06.html#name-known-length-messages
absl::StatusOr<std::string> BinaryHttpRequest::EncodeAsKnownLength() const {
  std::string data;
  data.resize(EncodedSize());
  quiche::QuicheDataWriter writer(data.size(), data.data());
  if (!writer.WriteVarInt62(kKnownLengthRequestFraming)) {
    return absl::InvalidArgumentError("Failed to encode framing indicator.");
  }
  if (const absl::Status status = EncodeControlData(writer); !status.ok()) {
    return status;
  }
  if (const absl::Status status = EncodeKnownLengthFieldsAndBody(writer);
      !status.ok()) {
    return status;
  }
  QUICHE_DCHECK_EQ(writer.remaining(), num_padding_bytes());
  writer.WritePadding();
  return data;
}

absl::StatusOr<BinaryHttpRequest> BinaryHttpRequest::Create(
    absl::string_view data) {
  quiche::QuicheDataReader reader(data);
  uint64_t framing;
  if (!reader.ReadVarInt62(&framing)) {
    return absl::InvalidArgumentError("Missing framing indicator.");
  }
  if (framing == kKnownLengthRequestFraming) {
    return DecodeKnownLengthRequest(reader);
  }
  return absl::UnimplementedError(
      absl::StrCat("Unsupported framing type ", framing));
}

absl::Status
BinaryHttpRequest::IndeterminateLengthDecoder::DecodeContentTerminatedSection(
    QuicheDataReader& reader) {
  uint64_t length_or_content_terminator;
  do {
    if (!reader.ReadVarInt62(&length_or_content_terminator)) {
      return absl::OutOfRangeError("Not enough data to read section.");
    }
    if (length_or_content_terminator != kContentTerminator) {
      switch (current_section_) {
        case MessageSection::kHeader: {
          const absl::StatusOr<FieldView> field =
              DecodeField(reader, length_or_content_terminator);
          if (!field.ok()) {
            return field.status();
          }
          message_section_handler_.OnHeader(field->name, field->value);
          break;
        }
        case MessageSection::kBody: {
          absl::string_view body_chunk;
          if (!reader.ReadStringPiece(&body_chunk,
                                      length_or_content_terminator)) {
            return absl::OutOfRangeError("Failed to read body chunk.");
          }
          message_section_handler_.OnBodyChunk(body_chunk);
          break;
        }
        case MessageSection::kTrailer: {
          const absl::StatusOr<FieldView> field =
              DecodeField(reader, length_or_content_terminator);
          if (!field.ok()) {
            return field.status();
          }
          message_section_handler_.OnTrailer(field->name, field->value);
          break;
        }
        default:
          return absl::InternalError(
              "Unexpected section in DecodeContentTerminatedSection.");
      }
    }
    // Either a section was successfully decoded or a content terminator was
    // encountered, save the checkpoint.
    SaveCheckpoint(reader);
  } while (length_or_content_terminator != kContentTerminator);
  return absl::OkStatus();
}

// Returns Ok status only if the decoding processes the Padding section
// successfully or if the message is truncated properly. All other points of
// return are errors.
absl::Status
BinaryHttpRequest::IndeterminateLengthDecoder::DecodeCheckpointData(
    bool end_stream) {
  QuicheDataReader reader(checkpoint_view_);
  switch (current_section_) {
    case MessageSection::kEnd:
      return absl::InternalError("Decoder is invalid.");
    case MessageSection::kControlData: {
      uint64_t framing;
      if (!reader.ReadVarInt62(&framing)) {
        return absl::OutOfRangeError("Failed to read framing.");
      }
      if (framing != kIndeterminateLengthRequestFraming) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Unsupported framing type: 0x%02x", framing));
      }

      const absl::StatusOr<BinaryHttpRequest::ControlData> control_data =
          DecodeControlData(reader);
      // Only fails if there is not enough data to read the entire control data.
      if (!control_data.ok()) {
        return absl::OutOfRangeError("Failed to read control data.");
      }

      message_section_handler_.OnControlData(control_data.value());
      SaveCheckpoint(reader);
      current_section_ = MessageSection::kHeader;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case MessageSection::kHeader: {
      const absl::Status status = DecodeContentTerminatedSection(reader);
      if (!status.ok()) {
        return status;
      }
      message_section_handler_.OnHeadersDone();
      current_section_ = MessageSection::kBody;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case MessageSection::kBody: {
      if (!reader.IsDoneReading()) {
        maybe_truncated_ = false;
      }
      // Body and trailers truncation is valid only if:
      // 1. There is no data to read after the headers section.
      // 2. This is signaled as the last piece of data (end_stream).
      if (maybe_truncated_ && end_stream) {
        message_section_handler_.OnBodyChunksDone();
        message_section_handler_.OnTrailersDone();
        return absl::OkStatus();
      }

      const absl::Status status = DecodeContentTerminatedSection(reader);
      if (!status.ok()) {
        return status;
      }
      message_section_handler_.OnBodyChunksDone();
      current_section_ = MessageSection::kTrailer;
      // Reset the truncation flag before entering the trailers section.
      maybe_truncated_ = true;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case MessageSection::kTrailer: {
      if (!reader.IsDoneReading()) {
        maybe_truncated_ = false;
      }
      // Trailers truncation is valid only if:
      // 1. There is no data to read after the body section.
      // 2. This is signaled as the last piece of data (end_stream).
      if (maybe_truncated_ && end_stream) {
        message_section_handler_.OnTrailersDone();
        return absl::OkStatus();
      }

      const absl::Status status = DecodeContentTerminatedSection(reader);
      if (!status.ok()) {
        return status;
      }
      message_section_handler_.OnTrailersDone();
      current_section_ = MessageSection::kPadding;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case MessageSection::kPadding: {
      if (!IsValidPadding(reader.PeekRemainingPayload())) {
        return absl::InvalidArgumentError("Non-zero padding.");
      }
      return absl::OkStatus();
    }
  }
}

void BinaryHttpRequest::IndeterminateLengthDecoder::InitializeCheckpoint(
    absl::string_view data) {
  checkpoint_view_ = data;
  // Prepend buffered data if present. This is the data from a previous call to
  // Decode that could not finish because it needed this new data.
  if (!buffer_.empty()) {
    absl::StrAppend(&buffer_, data);
    checkpoint_view_ = buffer_;
  }
}

absl::Status BinaryHttpRequest::IndeterminateLengthDecoder::Decode(
    absl::string_view data, bool end_stream) {
  if (current_section_ == MessageSection::kEnd) {
    return absl::InternalError("Decoder is invalid.");
  }

  InitializeCheckpoint(data);
  absl::Status status = DecodeCheckpointData(end_stream);
  if (end_stream) {
    current_section_ = MessageSection::kEnd;
    buffer_.clear();
    return status;
  }
  if (absl::IsOutOfRange(status)) {
    BufferCheckpoint();
    return absl::OkStatus();
  }
  if (!status.ok()) {
    current_section_ = MessageSection::kEnd;
  }

  buffer_.clear();
  return status;
}

absl::StatusOr<BinaryHttpResponse> BinaryHttpResponse::Create(
    absl::string_view data) {
  quiche::QuicheDataReader reader(data);
  uint64_t framing;
  if (!reader.ReadVarInt62(&framing)) {
    return absl::InvalidArgumentError("Missing framing indicator.");
  }
  if (framing == kKnownLengthResponseFraming) {
    return DecodeKnownLengthResponse(reader);
  }
  return absl::UnimplementedError(
      absl::StrCat("Unsupported framing type ", framing));
}

std::string BinaryHttpMessage::DebugString() const {
  std::vector<std::string> headers;
  for (const auto& field : GetHeaderFields()) {
    headers.emplace_back(field.DebugString());
  }
  return absl::StrCat("BinaryHttpMessage{Headers{", absl::StrJoin(headers, ";"),
                      "}Body{", body(), "}}");
}

std::string BinaryHttpMessage::Field::DebugString() const {
  return absl::StrCat("Field{", name, "=", value, "}");
}

std::string BinaryHttpResponse::InformationalResponse::DebugString() const {
  std::vector<std::string> fs;
  for (const auto& field : fields()) {
    fs.emplace_back(field.DebugString());
  }
  return absl::StrCat("InformationalResponse{", absl::StrJoin(fs, ";"), "}");
}

std::string BinaryHttpResponse::DebugString() const {
  std::vector<std::string> irs;
  for (const auto& ir : informational_responses()) {
    irs.emplace_back(ir.DebugString());
  }
  return absl::StrCat("BinaryHttpResponse(", status_code_, "){",
                      BinaryHttpMessage::DebugString(), absl::StrJoin(irs, ";"),
                      "}");
}

std::string BinaryHttpRequest::DebugString() const {
  return absl::StrCat("BinaryHttpRequest{", BinaryHttpMessage::DebugString(),
                      "}");
}

void PrintTo(const BinaryHttpRequest& msg, std::ostream* os) {
  *os << msg.DebugString();
}

void PrintTo(const BinaryHttpResponse& msg, std::ostream* os) {
  *os << msg.DebugString();
}

void PrintTo(const BinaryHttpMessage::Field& msg, std::ostream* os) {
  *os << msg.DebugString();
}

}  // namespace quiche
