#include "quiche/binary_http/binary_http_message.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"
#include "quiche/common/quiche_status_utils.h"

namespace quiche {
namespace {

constexpr uint64_t kKnownLengthRequestFraming = 0;
constexpr uint64_t kKnownLengthResponseFraming = 1;
constexpr uint64_t kIndeterminateLengthRequestFraming = 2;
constexpr uint64_t kIndeterminateLengthResponseFraming = 3;
constexpr uint64_t kContentTerminator = 0;
constexpr int kMinInformationalStatusCode = 100;
constexpr int kMaxInformationalStatusCode = 199;
constexpr int kMinFinalStatusCode = 200;
constexpr int kMaxFinalStatusCode = 599;

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
absl::StatusOr<BinaryHttpMessage::FieldView> DecodeField(
    QuicheDataReader& reader, uint64_t name_length) {
  absl::string_view name;
  if (!reader.ReadStringPiece(&name, name_length)) {
    return absl::OutOfRangeError("Not enough data to read field name.");
  }
  absl::string_view value;
  if (!reader.ReadStringPieceVarInt62(&value)) {
    return absl::OutOfRangeError("Not enough data to read field value.");
  }
  return BinaryHttpMessage::FieldView{name, value};
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
    if (status_code >= kMinInformationalStatusCode &&
        status_code <= kMaxInformationalStatusCode) {
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

absl::StatusOr<std::string> EncodeBodyChunksImpl(
    absl::Span<const absl::string_view> body_chunks, bool body_chunks_done) {
  uint64_t total_length = 0;
  for (const auto& body_chunk : body_chunks) {
    if (body_chunk.empty()) {
      continue;
    }
    uint8_t body_chunk_var_int_length =
        QuicheDataWriter::GetVarInt62Len(body_chunk.size());
    if (body_chunk_var_int_length == 0) {
      return absl::InvalidArgumentError(
          "Body chunk size exceeds maximum length.");
    }
    total_length += body_chunk_var_int_length + body_chunk.size();
  }
  if (body_chunks_done) {
    total_length +=
        quiche::QuicheDataWriter::GetVarInt62Len(kContentTerminator);
  }
  if (total_length == 0) {
    return absl::InvalidArgumentError(
        "EncodeBodyChunks cannot be called without data unless "
        "body_chunks_done is true.");
  }

  std::string data(total_length, '\0');
  QuicheDataWriter writer(total_length, data.data());

  for (const auto& body_chunk : body_chunks) {
    if (body_chunk.empty()) {
      continue;
    }
    if (!writer.WriteStringPieceVarInt62(body_chunk)) {
      return absl::InternalError("Failed to write body chunk.");
    }
  }
  if (body_chunks_done) {
    if (!writer.WriteVarInt62(kContentTerminator)) {
      return absl::InternalError("Failed to write content terminator.");
    }
  }

  if (writer.remaining() != 0) {
    return absl::InternalError("Failed to write all data.");
  }
  return data;
}

// Initializes the checkpoint based on the provided data and any buffered data.
// If the buffer has data, the new data is appended to the buffer.
absl::string_view InitializeChunkedDecodingCheckpoint(absl::string_view data,
                                                      std::string& buffer) {
  absl::string_view checkpoint = data;
  // Prepend buffered data if present.
  if (!buffer.empty()) {
    absl::StrAppend(&buffer, data);
    checkpoint = buffer;
  }
  return checkpoint;
}

// Updates the checkpoint based on the current position of the reader.
void UpdateChunkedDecodingCheckpoint(const QuicheDataReader& reader,
                                     absl::string_view& checkpoint) {
  checkpoint = reader.PeekRemainingPayload();
}

// Buffers the checkpoint.
void BufferChunkedDecodingCheckpoint(absl::string_view checkpoint,
                                     std::string& buffer) {
  if (buffer != checkpoint) {
    buffer.assign(checkpoint);
  }
}

// Decodes the fields in the reader. Calls the field_handler for each field
// until the reader is done or the content terminator is encountered.
absl::Status DecodeContentTerminatedFieldSection(
    QuicheDataReader& reader, absl::string_view& checkpoint,
    quiche::UnretainedCallback<absl::Status(absl::string_view,
                                            absl::string_view)>
        field_handler) {
  uint64_t length_or_content_terminator = kContentTerminator;
  do {
    if (!reader.ReadVarInt62(&length_or_content_terminator)) {
      return absl::OutOfRangeError("Not enough data to read section.");
    }
    if (length_or_content_terminator != kContentTerminator) {
      const absl::StatusOr<BinaryHttpMessage::FieldView> field =
          DecodeField(reader, length_or_content_terminator);
      if (!field.ok()) {
        return field.status();
      }
      const absl::Status section_status =
          field_handler(field->name, field->value);
      if (!section_status.ok()) {
        return absl::InternalError(absl::StrCat("Failed to handle header: ",
                                                section_status.message()));
      }
    }
    // Either a field was successfully decoded or a content terminator was
    // encountered, update the checkpoint.
    UpdateChunkedDecodingCheckpoint(reader, checkpoint);
  } while (length_or_content_terminator != kContentTerminator);
  return absl::OkStatus();
}

// Decodes the body chunks in the reader. Calls the body_chunk_handler for
// each body chunk until the reader is done or the content terminator is
// encountered.
absl::Status DecodeContentTerminatedBodyChunkSection(
    QuicheDataReader& reader, absl::string_view& checkpoint,
    quiche::UnretainedCallback<absl::Status(absl::string_view)>
        body_chunk_handler) {
  uint64_t length_or_content_terminator = kContentTerminator;
  do {
    if (!reader.ReadVarInt62(&length_or_content_terminator)) {
      return absl::OutOfRangeError("Not enough data to read section.");
    }
    if (length_or_content_terminator != kContentTerminator) {
      absl::string_view body_chunk;
      if (!reader.ReadStringPiece(&body_chunk, length_or_content_terminator)) {
        return absl::OutOfRangeError("Failed to read body chunk.");
      }
      const absl::Status section_status = body_chunk_handler(body_chunk);
      if (!section_status.ok()) {
        return absl::InternalError(absl::StrCat("Failed to handle body chunk: ",
                                                section_status.message()));
      }
    }
    // Either a body chunk was successfully decoded or a content terminator was
    // encountered, update the checkpoint.
    UpdateChunkedDecodingCheckpoint(reader, checkpoint);
  } while (length_or_content_terminator != kContentTerminator);
  return absl::OkStatus();
}

absl::StatusOr<uint64_t> GetFieldSectionLength(
    absl::Span<const BinaryHttpMessage::FieldView> fields) {
  uint64_t field_section_length = 0;
  for (const auto& field : fields) {
    uint8_t var_int_length =
        QuicheDataWriter::GetVarInt62Len(field.name.size());
    if (var_int_length == 0) {
      return absl::InvalidArgumentError("Field name exceeds maximum length.");
    }
    field_section_length += var_int_length + field.name.size();

    var_int_length = QuicheDataWriter::GetVarInt62Len(field.value.size());
    if (var_int_length == 0) {
      return absl::InvalidArgumentError("Field value exceeds maximum length.");
    }
    field_section_length += var_int_length + field.value.size();
  }
  field_section_length +=
      quiche::QuicheDataWriter::GetVarInt62Len(kContentTerminator);
  return field_section_length;
}

absl::Status EncodeFields(absl::Span<const BinaryHttpMessage::FieldView> fields,
                          quiche::QuicheDataWriter& writer) {
  for (const auto& field : fields) {
    if (!writer.WriteStringPieceVarInt62(absl::AsciiStrToLower(field.name))) {
      return absl::InternalError("Failed to write field name.");
    }
    if (!writer.WriteStringPieceVarInt62(field.value)) {
      return absl::InternalError("Failed to write field value.");
    }
  }
  if (!writer.WriteVarInt62(kContentTerminator)) {
    return absl::InternalError("Failed to write content terminator.");
  }
  return absl::OkStatus();
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
  if (status_code < kMinInformationalStatusCode) {
    return absl::InvalidArgumentError("status code < 100");
  } else if (status_code > kMaxInformationalStatusCode) {
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

// Returns Ok status only if the decoding processes the Padding section
// successfully or if the message is truncated properly. All other points of
// return are errors.
absl::Status
BinaryHttpRequest::IndeterminateLengthDecoder::DecodeCheckpointData(
    bool end_stream, absl::string_view& checkpoint) {
  QuicheDataReader reader(checkpoint);
  switch (current_section_) {
    case IndeterminateLengthMessageSection::kEnd:
      return absl::InternalError("Decoder is invalid.");
    case IndeterminateLengthMessageSection::kControlData: {
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

      const absl::Status section_status =
          message_section_handler_.OnControlData(control_data.value());
      if (!section_status.ok()) {
        return absl::InternalError(absl::StrCat(
            "Failed to handle control data: ", section_status.message()));
      }
      UpdateChunkedDecodingCheckpoint(reader, checkpoint);
      current_section_ = IndeterminateLengthMessageSection::kHeader;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case IndeterminateLengthMessageSection::kHeader: {
      QUICHE_RETURN_IF_ERROR(DecodeContentTerminatedFieldSection(
          reader, checkpoint,
          absl::bind_front(&MessageSectionHandler::OnHeader,
                           &message_section_handler_)));
      const absl::Status section_status =
          message_section_handler_.OnHeadersDone();
      if (!section_status.ok()) {
        return absl::InternalError(absl::StrCat(
            "Failed to handle headers done: ", section_status.message()));
      }
      current_section_ = IndeterminateLengthMessageSection::kBody;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case IndeterminateLengthMessageSection::kBody: {
      // Body and trailers truncation is valid only if:
      // 1. There is no data to read after the headers section.
      // 2. This is signaled as the last piece of data (end_stream).
      if (reader.IsDoneReading() && end_stream) {
        absl::Status section_status =
            message_section_handler_.OnBodyChunksDone();
        if (!section_status.ok()) {
          return absl::InternalError(absl::StrCat(
              "Failed to handle body chunks done: ", section_status.message()));
        }
        section_status = message_section_handler_.OnTrailersDone();
        if (!section_status.ok()) {
          return absl::InternalError(absl::StrCat(
              "Failed to handle trailers done: ", section_status.message()));
        }
        return absl::OkStatus();
      }

      QUICHE_RETURN_IF_ERROR(DecodeContentTerminatedBodyChunkSection(
          reader, checkpoint,
          absl::bind_front(&MessageSectionHandler::OnBodyChunk,
                           &message_section_handler_)));
      const absl::Status section_status =
          message_section_handler_.OnBodyChunksDone();
      if (!section_status.ok()) {
        return absl::InternalError(absl::StrCat(
            "Failed to handle body chunks done: ", section_status.message()));
      }
      current_section_ = IndeterminateLengthMessageSection::kTrailer;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case IndeterminateLengthMessageSection::kTrailer: {
      // Trailers truncation is valid only if:
      // 1. There is no data to read after the body section.
      // 2. This is signaled as the last piece of data (end_stream).
      if (reader.IsDoneReading() && end_stream) {
        const absl::Status section_status =
            message_section_handler_.OnTrailersDone();
        if (!section_status.ok()) {
          return absl::InternalError(absl::StrCat(
              "Failed to handle trailers done: ", section_status.message()));
        }
        return absl::OkStatus();
      }

      QUICHE_RETURN_IF_ERROR(DecodeContentTerminatedFieldSection(
          reader, checkpoint,
          absl::bind_front(&MessageSectionHandler::OnTrailer,
                           &message_section_handler_)));
      const absl::Status section_status =
          message_section_handler_.OnTrailersDone();
      if (!section_status.ok()) {
        return absl::InternalError(absl::StrCat(
            "Failed to handle trailers done: ", section_status.message()));
      }
      current_section_ = IndeterminateLengthMessageSection::kPadding;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case IndeterminateLengthMessageSection::kPadding: {
      if (!IsValidPadding(reader.PeekRemainingPayload())) {
        return absl::InvalidArgumentError("Non-zero padding.");
      }
      return absl::OkStatus();
    }
  }
  // This should never happen because current_section_ is private and we only
  // ever set it to values handled by the switch statement above.
  return absl::InternalError(
      "Unexpected IndeterminateLengthMessageSection value.");
}

absl::Status BinaryHttpRequest::IndeterminateLengthDecoder::Decode(
    absl::string_view data, bool end_stream) {
  if (current_section_ == IndeterminateLengthMessageSection::kEnd) {
    return absl::InternalError("Decoder is invalid.");
  }

  absl::string_view checkpoint =
      InitializeChunkedDecodingCheckpoint(data, buffer_);
  absl::Status status = DecodeCheckpointData(end_stream, checkpoint);
  if (end_stream) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    buffer_.clear();
    // Out of range errors shall be treated as invalid argument errors when the
    // stream is ending.
    if (absl::IsOutOfRange(status)) {
      return absl::InvalidArgumentError(status.message());
    }
    return status;
  }
  if (absl::IsOutOfRange(status)) {
    BufferChunkedDecodingCheckpoint(checkpoint, buffer_);
    return absl::OkStatus();
  }
  if (!status.ok()) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
  }

  buffer_.clear();
  return status;
}

absl::StatusOr<std::string>
BinaryHttpRequest::IndeterminateLengthEncoder::EncodeFieldSection(
    absl::Span<const FieldView> fields) {
  absl::StatusOr<uint64_t> field_section_length = GetFieldSectionLength(fields);
  if (!field_section_length.ok()) {
    return field_section_length.status();
  }

  std::string data(*field_section_length, '\0');
  QuicheDataWriter writer(*field_section_length, data.data());

  absl::Status fields_encoded = EncodeFields(fields, writer);
  if (!fields_encoded.ok()) {
    return fields_encoded;
  }
  if (writer.remaining() != 0) {
    return absl::InternalError("Failed to write all fields.");
  }
  return data;
}

absl::StatusOr<std::string>
BinaryHttpRequest::IndeterminateLengthEncoder::EncodeControlData(
    const ControlData& control_data) {
  if (current_section_ != IndeterminateLengthMessageSection::kControlData) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InvalidArgumentError(
        "EncodeControlData called in wrong section.");
  }

  uint64_t total_length = quiche::QuicheDataWriter::GetVarInt62Len(
                              kIndeterminateLengthRequestFraming) +
                          StringPieceVarInt62Len(control_data.method) +
                          StringPieceVarInt62Len(control_data.scheme) +
                          StringPieceVarInt62Len(control_data.authority) +
                          StringPieceVarInt62Len(control_data.path);

  std::string data(total_length, '\0');
  QuicheDataWriter writer(total_length, data.data());
  if (!writer.WriteVarInt62(kIndeterminateLengthRequestFraming)) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InternalError("Failed to write framing indicator.");
  }
  if (!writer.WriteStringPieceVarInt62(control_data.method)) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InternalError("Failed to write method.");
  }
  if (!writer.WriteStringPieceVarInt62(control_data.scheme)) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InternalError("Failed to write scheme.");
  }
  if (!writer.WriteStringPieceVarInt62(control_data.authority)) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InternalError("Failed to write authority.");
  }
  if (!writer.WriteStringPieceVarInt62(control_data.path)) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InternalError("Failed to write path.");
  }
  if (writer.remaining() != 0) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InternalError("Failed to write all control data.");
  }

  current_section_ = IndeterminateLengthMessageSection::kHeader;
  return data;
}

absl::StatusOr<std::string>
BinaryHttpRequest::IndeterminateLengthEncoder::EncodeHeaders(
    absl::Span<const FieldView> headers) {
  if (current_section_ != IndeterminateLengthMessageSection::kHeader) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InvalidArgumentError("EncodeHeaders called in wrong section.");
  }
  absl::StatusOr<std::string> data = EncodeFieldSection(headers);
  if (!data.ok()) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return data;
  }
  current_section_ = IndeterminateLengthMessageSection::kBody;
  return data;
}

absl::StatusOr<std::string>
BinaryHttpRequest::IndeterminateLengthEncoder::EncodeBodyChunks(
    absl::Span<const absl::string_view> body_chunks, bool body_chunks_done) {
  if (current_section_ != IndeterminateLengthMessageSection::kBody) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InvalidArgumentError(
        "EncodeBodyChunks called in wrong section.");
  }
  absl::StatusOr<std::string> result =
      EncodeBodyChunksImpl(body_chunks, body_chunks_done);
  if (!result.ok()) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return result.status();
  }
  if (body_chunks_done) {
    current_section_ = IndeterminateLengthMessageSection::kTrailer;
  }
  return result;
}

absl::StatusOr<std::string>
BinaryHttpRequest::IndeterminateLengthEncoder::EncodeTrailers(
    absl::Span<const FieldView> trailers) {
  if (current_section_ != IndeterminateLengthMessageSection::kTrailer) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InvalidArgumentError(
        "EncodeTrailers called in wrong section.");
  }
  current_section_ = IndeterminateLengthMessageSection::kEnd;
  return EncodeFieldSection(trailers);
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

absl::StatusOr<std::string>
BinaryHttpResponse::IndeterminateLengthEncoder::EncodeFieldSection(
    std::optional<uint16_t> status_code, absl::Span<const FieldView> fields) {
  absl::StatusOr<uint64_t> field_section_length = GetFieldSectionLength(fields);
  if (!field_section_length.ok()) {
    return field_section_length.status();
  }
  uint64_t total_length = *field_section_length;
  if (current_section_ ==
      IndeterminateLengthMessageSection::kFramingIndicator) {
    total_length += quiche::QuicheDataWriter::GetVarInt62Len(
        kIndeterminateLengthResponseFraming);
  }
  if (status_code.has_value()) {
    total_length += QuicheDataWriter::GetVarInt62Len(*status_code);
  }

  std::string data(total_length, '\0');
  QuicheDataWriter writer(total_length, data.data());

  if (current_section_ ==
      IndeterminateLengthMessageSection::kFramingIndicator) {
    if (!writer.WriteVarInt62(kIndeterminateLengthResponseFraming)) {
      return absl::InternalError("Failed to write framing indicator.");
    }
    current_section_ =
        IndeterminateLengthMessageSection::kInformationalOrFinalStatusCode;
  }
  if (status_code.has_value() && !writer.WriteVarInt62(*status_code)) {
    return absl::InternalError("Failed to write status code.");
  }
  absl::Status fields_encoded = EncodeFields(fields, writer);
  if (!fields_encoded.ok()) {
    return fields_encoded;
  }
  if (writer.remaining() != 0) {
    return absl::InternalError("Failed to write all data.");
  }
  return data;
}

std::string
BinaryHttpResponse::IndeterminateLengthEncoder::GetMessageSectionString(
    IndeterminateLengthMessageSection section) const {
  switch (section) {
    case IndeterminateLengthMessageSection::kFramingIndicator:
      return "FramingIndicator";
    case IndeterminateLengthMessageSection::kInformationalOrFinalStatusCode:
      return "InformationalOrFinalStatusCode";
    case IndeterminateLengthMessageSection::kInformationalResponseHeader:
      return "InformationalResponseHeader";
    case IndeterminateLengthMessageSection::kFinalResponseHeader:
      return "FinalResponseHeader";
    case IndeterminateLengthMessageSection::kBody:
      return "Body";
    case IndeterminateLengthMessageSection::kTrailer:
      return "Trailer";
    case IndeterminateLengthMessageSection::kPadding:
      return "Padding";
    case IndeterminateLengthMessageSection::kEnd:
      return "End";
    default:
      return "Unknown";
  }
}

absl::StatusOr<std::string>
BinaryHttpResponse::IndeterminateLengthEncoder::EncodeInformationalResponse(
    uint16_t status_code, absl::Span<const FieldView> fields) {
  if (current_section_ !=
          IndeterminateLengthMessageSection::kFramingIndicator &&
      current_section_ !=
          IndeterminateLengthMessageSection::kInformationalOrFinalStatusCode) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InvalidArgumentError(absl::StrCat(
        "EncodeInformationalResponse called in incorrect section: ",
        GetMessageSectionString(current_section_)));
  }
  if (status_code < kMinInformationalStatusCode ||
      status_code > kMaxInformationalStatusCode) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid informational response status code: ", status_code));
  }

  absl::StatusOr<std::string> data = EncodeFieldSection(status_code, fields);
  if (!data.ok()) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
  }
  current_section_ =
      IndeterminateLengthMessageSection::kInformationalOrFinalStatusCode;
  return data;
}

absl::StatusOr<std::string>
BinaryHttpResponse::IndeterminateLengthEncoder::EncodeHeaders(
    uint16_t status_code, absl::Span<const FieldView> headers) {
  if (current_section_ !=
          IndeterminateLengthMessageSection::kFramingIndicator &&
      current_section_ !=
          IndeterminateLengthMessageSection::kInformationalOrFinalStatusCode) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InvalidArgumentError(
        absl::StrCat("EncodeHeaders called in incorrect section: ",
                     GetMessageSectionString(current_section_)));
  }
  if (status_code < kMinFinalStatusCode || status_code > kMaxFinalStatusCode) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid response status code: ", status_code));
  }

  absl::StatusOr<std::string> data = EncodeFieldSection(status_code, headers);
  if (!data.ok()) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return data;
  }
  current_section_ = IndeterminateLengthMessageSection::kBody;
  return data;
}

absl::StatusOr<std::string>
BinaryHttpResponse::IndeterminateLengthEncoder::EncodeBodyChunks(
    absl::Span<const absl::string_view> body_chunks, bool body_chunks_done) {
  if (current_section_ != IndeterminateLengthMessageSection::kBody) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InvalidArgumentError(
        absl::StrCat("EncodeBodyChunks called in incorrect section: ",
                     GetMessageSectionString(current_section_)));
  }
  absl::StatusOr<std::string> result =
      EncodeBodyChunksImpl(body_chunks, body_chunks_done);
  if (!result.ok()) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return result.status();
  }
  if (body_chunks_done) {
    current_section_ = IndeterminateLengthMessageSection::kTrailer;
  }
  return result;
}

absl::StatusOr<std::string>
BinaryHttpResponse::IndeterminateLengthEncoder::EncodeTrailers(
    absl::Span<const FieldView> trailers) {
  if (current_section_ != IndeterminateLengthMessageSection::kTrailer) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    return absl::InvalidArgumentError(
        absl::StrCat("EncodeTrailers called in incorrect section: ",
                     GetMessageSectionString(current_section_)));
  }

  absl::StatusOr<std::string> data =
      EncodeFieldSection(/*status_code=*/std::nullopt, trailers);

  current_section_ = IndeterminateLengthMessageSection::kEnd;
  return data;
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

absl::Status BinaryHttpResponse::IndeterminateLengthDecoder::DecodeStatusCode(
    QuicheDataReader& reader, absl::string_view& checkpoint) {
  uint64_t status_code = 0;
  if (!reader.ReadVarInt62(&status_code)) {
    return absl::OutOfRangeError("Failed to read status code.");
  } else if (status_code < kMinInformationalStatusCode ||
             status_code > kMaxFinalStatusCode) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid response status code: ", status_code));
  } else if (status_code >= kMinInformationalStatusCode &&
             status_code <= kMaxInformationalStatusCode) {
    if (absl::Status status =
            message_section_handler_.OnInformationalResponseStatusCode(
                status_code);
        !status.ok()) {
      return absl::InternalError(
          absl::StrCat("Failed to handle informational response status "
                       "code: ",
                       status.message()));
    }
    current_section_ =
        IndeterminateLengthMessageSection::kInformationalResponseHeader;
  } else {
    if (absl::Status status =
            message_section_handler_.OnInformationalResponsesSectionDone();
        !status.ok()) {
      return absl::InternalError(absl::StrCat(
          "Failed to handle informational responses section done: ",
          status.message()));
    } else if (status = message_section_handler_.OnFinalResponseStatusCode(
                   status_code);
               !status.ok()) {
      return absl::InternalError(absl::StrCat(
          "Failed to handle final response status code: ", status.message()));
    }
    current_section_ = IndeterminateLengthMessageSection::kFinalResponseHeader;
  }
  UpdateChunkedDecodingCheckpoint(reader, checkpoint);
  return absl::OkStatus();
}

// Returns Ok status only if the decoding processes the Padding section
// successfully or if the message is truncated properly. All other points of
// return are errors.
absl::Status
BinaryHttpResponse::IndeterminateLengthDecoder::DecodeCheckpointData(
    bool end_stream, absl::string_view& checkpoint) {
  QuicheDataReader reader(checkpoint);
  // We only enter the loop if there is data in the reader or if end_stream is
  // true. The check for end_stream is used when the reader is done to validate
  // if it is a truncated message.
  while (!reader.IsDoneReading() || end_stream) {
    switch (current_section_) {
      case IndeterminateLengthMessageSection::kEnd:
        return absl::InternalError("Decoder is invalid.");
      case IndeterminateLengthMessageSection::kFramingIndicator: {
        uint64_t framing = 0;
        if (!reader.ReadVarInt62(&framing)) {
          return absl::OutOfRangeError("Failed to read framing.");
        } else if (framing != kIndeterminateLengthResponseFraming) {
          return absl::InvalidArgumentError(
              absl::StrFormat("Unsupported framing type: 0x%02x", framing));
        }
        current_section_ =
            IndeterminateLengthMessageSection::kInformationalOrFinalStatusCode;
        UpdateChunkedDecodingCheckpoint(reader, checkpoint);
        break;
      }
      case IndeterminateLengthMessageSection::kInformationalOrFinalStatusCode: {
        QUICHE_RETURN_IF_ERROR(DecodeStatusCode(reader, checkpoint));
        break;
      }
      case IndeterminateLengthMessageSection::kInformationalResponseHeader: {
        QUICHE_RETURN_IF_ERROR(DecodeContentTerminatedFieldSection(
            reader, checkpoint,
            absl::bind_front(
                &MessageSectionHandler::OnInformationalResponseHeader,
                &message_section_handler_)));
        if (absl::Status status =
                message_section_handler_.OnInformationalResponseDone();
            !status.ok()) {
          return absl::InternalError(
              absl::StrCat("Failed to handle informational response done: ",
                           status.message()));
        }
        current_section_ =
            IndeterminateLengthMessageSection::kInformationalOrFinalStatusCode;
        break;
      }
      case IndeterminateLengthMessageSection::kFinalResponseHeader: {
        QUICHE_RETURN_IF_ERROR(DecodeContentTerminatedFieldSection(
            reader, checkpoint,
            absl::bind_front(&MessageSectionHandler::OnFinalResponseHeader,
                             &message_section_handler_)));
        if (absl::Status status =
                message_section_handler_.OnFinalResponseHeadersDone();
            !status.ok()) {
          return absl::InternalError(absl::StrCat(
              "Failed to handle headers done: ", status.message()));
        }
        current_section_ = IndeterminateLengthMessageSection::kBody;
        UpdateChunkedDecodingCheckpoint(reader, checkpoint);
        break;
      }
      case IndeterminateLengthMessageSection::kBody: {
        // Body and trailers truncation is valid only if:
        // 1. There is no data to read after the headers section.
        // 2. This is signaled as the last piece of data (end_stream).
        if (reader.IsDoneReading() && end_stream) {
          if (absl::Status status = message_section_handler_.OnBodyChunksDone();
              !status.ok()) {
            return absl::InternalError(absl::StrCat(
                "Failed to handle body chunks done: ", status.message()));
          } else if (status = message_section_handler_.OnTrailersDone();
                     !status.ok()) {
            return absl::InternalError(absl::StrCat(
                "Failed to handle trailers done: ", status.message()));
          }
          return absl::OkStatus();
        }

        QUICHE_RETURN_IF_ERROR(DecodeContentTerminatedBodyChunkSection(
            reader, checkpoint,
            absl::bind_front(&MessageSectionHandler::OnBodyChunk,
                             &message_section_handler_)));
        if (absl::Status status = message_section_handler_.OnBodyChunksDone();
            !status.ok()) {
          return absl::InternalError(absl::StrCat(
              "Failed to handle body chunks done: ", status.message()));
        }
        current_section_ = IndeterminateLengthMessageSection::kTrailer;
        break;
      }
      case IndeterminateLengthMessageSection::kTrailer: {
        // Trailers truncation is valid only if:
        // 1. There is no data to read after the body section.
        // 2. This is signaled as the last piece of data (end_stream).
        if (reader.IsDoneReading() && end_stream) {
          if (absl::Status status = message_section_handler_.OnTrailersDone();
              !status.ok()) {
            return absl::InternalError(absl::StrCat(
                "Failed to handle trailers done: ", status.message()));
          }
          return absl::OkStatus();
        }

        QUICHE_RETURN_IF_ERROR(DecodeContentTerminatedFieldSection(
            reader, checkpoint,
            absl::bind_front(&MessageSectionHandler::OnTrailer,
                             &message_section_handler_)));
        if (absl::Status status = message_section_handler_.OnTrailersDone();
            !status.ok()) {
          return absl::InternalError(absl::StrCat(
              "Failed to handle trailers done: ", status.message()));
        }
        current_section_ = IndeterminateLengthMessageSection::kPadding;
        break;
      }
      case IndeterminateLengthMessageSection::kPadding: {
        if (!IsValidPadding(reader.PeekRemainingPayload())) {
          return absl::InvalidArgumentError("Non-zero padding.");
        }
        return absl::OkStatus();
      }
    }
  }
  // No data in the reader and no end_stream, signal that more data is pending
  // using OutOfRange.
  return absl::OutOfRangeError("Decoding is pending.");
}

absl::Status BinaryHttpResponse::IndeterminateLengthDecoder::Decode(
    absl::string_view data, bool end_stream) {
  if (current_section_ == IndeterminateLengthMessageSection::kEnd) {
    return absl::InternalError("Decoder is invalid.");
  }

  absl::string_view checkpoint =
      InitializeChunkedDecodingCheckpoint(data, buffer_);
  absl::Status status = DecodeCheckpointData(end_stream, checkpoint);
  if (end_stream) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
    buffer_.clear();
    // Out of range errors shall be treated as invalid argument errors when the
    // stream is ending.
    if (absl::IsOutOfRange(status)) {
      return absl::InvalidArgumentError(status.message());
    }
    return status;
  } else if (absl::IsOutOfRange(status)) {
    BufferChunkedDecodingCheckpoint(checkpoint, buffer_);
    return absl::OkStatus();
  } else if (!status.ok()) {
    current_section_ = IndeterminateLengthMessageSection::kEnd;
  }

  buffer_.clear();
  return status;
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
