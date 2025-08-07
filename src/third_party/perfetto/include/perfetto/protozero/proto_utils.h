/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INCLUDE_PERFETTO_PROTOZERO_PROTO_UTILS_H_
#define INCLUDE_PERFETTO_PROTOZERO_PROTO_UTILS_H_

#include <stddef.h>

#include <cinttypes>
#include <type_traits>

#include "perfetto/base/logging.h"
#include "perfetto/public/pb_utils.h"

// Helper macro for the constexpr functions containing
// the switch statement: if C++14 is supported, this macro
// resolves to `constexpr` and just `inline` otherwise.
#if __cpp_constexpr >= 201304
#define PERFETTO_PROTOZERO_CONSTEXPR14_OR_INLINE constexpr
#else
#define PERFETTO_PROTOZERO_CONSTEXPR14_OR_INLINE inline
#endif

namespace protozero {
namespace proto_utils {

// See https://developers.google.com/protocol-buffers/docs/encoding wire types.
// This is a type encoded into the proto that provides just enough info to
// find the length of the following value.
enum class ProtoWireType : uint32_t {
  kVarInt = 0,
  kFixed64 = 1,
  kLengthDelimited = 2,
  kFixed32 = 5,
};

// This is the type defined in the proto for each field. This information
// is used to decide the translation strategy when writing the trace.
enum class ProtoSchemaType {
  kUnknown = 0,
  kDouble,
  kFloat,
  kInt64,
  kUint64,
  kInt32,
  kFixed64,
  kFixed32,
  kBool,
  kString,
  kGroup,  // Deprecated (proto2 only)
  kMessage,
  kBytes,
  kUint32,
  kEnum,
  kSfixed32,
  kSfixed64,
  kSint32,
  kSint64,
};

inline const char* ProtoSchemaToString(ProtoSchemaType v) {
  switch (v) {
    case ProtoSchemaType::kUnknown:
      return "unknown";
    case ProtoSchemaType::kDouble:
      return "double";
    case ProtoSchemaType::kFloat:
      return "float";
    case ProtoSchemaType::kInt64:
      return "int64";
    case ProtoSchemaType::kUint64:
      return "uint64";
    case ProtoSchemaType::kInt32:
      return "int32";
    case ProtoSchemaType::kFixed64:
      return "fixed64";
    case ProtoSchemaType::kFixed32:
      return "fixed32";
    case ProtoSchemaType::kBool:
      return "bool";
    case ProtoSchemaType::kString:
      return "string";
    case ProtoSchemaType::kGroup:
      return "group";
    case ProtoSchemaType::kMessage:
      return "message";
    case ProtoSchemaType::kBytes:
      return "bytes";
    case ProtoSchemaType::kUint32:
      return "uint32";
    case ProtoSchemaType::kEnum:
      return "enum";
    case ProtoSchemaType::kSfixed32:
      return "sfixed32";
    case ProtoSchemaType::kSfixed64:
      return "sfixed64";
    case ProtoSchemaType::kSint32:
      return "sint32";
    case ProtoSchemaType::kSint64:
      return "sint64";
  }
  // For gcc:
  PERFETTO_DCHECK(false);
  return "";
}

// Maximum message size supported: 256 MiB (4 x 7-bit due to varint encoding).
constexpr size_t kMessageLengthFieldSize = 4;
constexpr size_t kMaxMessageLength = (1u << (kMessageLengthFieldSize * 7)) - 1;
constexpr size_t kMaxOneByteMessageLength = (1 << 7) - 1;

// Field tag is encoded as 32-bit varint (5 bytes at most).
// Largest value of simple (not length-delimited) field is 64-bit varint
// (10 bytes at most). 15 bytes buffer is enough to store a simple field.
constexpr size_t kMaxTagEncodedSize = 5;
constexpr size_t kMaxSimpleFieldEncodedSize = kMaxTagEncodedSize + 10;

// Proto types: (int|uint|sint)(32|64), bool, enum.
constexpr uint32_t MakeTagVarInt(uint32_t field_id) {
  return (field_id << 3) | static_cast<uint32_t>(ProtoWireType::kVarInt);
}

// Proto types: fixed64, sfixed64, fixed32, sfixed32, double, float.
template <typename T>
constexpr uint32_t MakeTagFixed(uint32_t field_id) {
  static_assert(sizeof(T) == 8 || sizeof(T) == 4, "Value must be 4 or 8 bytes");
  return (field_id << 3) |
         static_cast<uint32_t>((sizeof(T) == 8 ? ProtoWireType::kFixed64
                                               : ProtoWireType::kFixed32));
}

// Proto types: string, bytes, embedded messages.
constexpr uint32_t MakeTagLengthDelimited(uint32_t field_id) {
  return (field_id << 3) |
         static_cast<uint32_t>(ProtoWireType::kLengthDelimited);
}

// Proto types: sint64, sint32.
template <typename T>
inline typename std::make_unsigned<T>::type ZigZagEncode(T value) {
  using UnsignedType = typename std::make_unsigned<T>::type;

  // Right-shift of negative values is implementation specific.
  // Assert the implementation does what we expect, which is that shifting any
  // positive value by sizeof(T) * 8 - 1 gives an all 0 bitmap, and a negative
  // value gives an all 1 bitmap.
  constexpr uint64_t kUnsignedZero = 0u;
  constexpr int64_t kNegativeOne = -1;
  constexpr int64_t kPositiveOne = 1;
  static_assert(static_cast<uint64_t>(kNegativeOne >> 63) == ~kUnsignedZero,
                "implementation does not support assumed rightshift");
  static_assert(static_cast<uint64_t>(kPositiveOne >> 63) == kUnsignedZero,
                "implementation does not support assumed rightshift");

  return (static_cast<UnsignedType>(value) << 1) ^
         static_cast<UnsignedType>(value >> (sizeof(T) * 8 - 1));
}

// Proto types: sint64, sint32.
template <typename T>
inline typename std::make_signed<T>::type ZigZagDecode(T value) {
  using UnsignedType = typename std::make_unsigned<T>::type;
  using SignedType = typename std::make_signed<T>::type;
  auto u_value = static_cast<UnsignedType>(value);
  auto mask = static_cast<UnsignedType>(-static_cast<SignedType>(u_value & 1));
  return static_cast<SignedType>((u_value >> 1) ^ mask);
}

template <typename T>
auto ExtendValueForVarIntSerialization(T value) -> typename std::make_unsigned<
    typename std::conditional<std::is_unsigned<T>::value, T, int64_t>::type>::
    type {
  // If value is <= 0 we must first sign extend to int64_t (see [1]).
  // Finally we always cast to an unsigned value to to avoid arithmetic
  // (sign expanding) shifts in the while loop.
  // [1]: "If you use int32 or int64 as the type for a negative number, the
  // resulting varint is always ten bytes long".
  // - developers.google.com/protocol-buffers/docs/encoding
  // So for each input type we do the following casts:
  // uintX_t -> uintX_t -> uintX_t
  // int8_t  -> int64_t -> uint64_t
  // int16_t -> int64_t -> uint64_t
  // int32_t -> int64_t -> uint64_t
  // int64_t -> int64_t -> uint64_t
  using MaybeExtendedType =
      typename std::conditional<std::is_unsigned<T>::value, T, int64_t>::type;
  using UnsignedType = typename std::make_unsigned<MaybeExtendedType>::type;

  MaybeExtendedType extended_value = static_cast<MaybeExtendedType>(value);
  UnsignedType unsigned_value = static_cast<UnsignedType>(extended_value);

  return unsigned_value;
}

template <typename T>
inline uint8_t* WriteVarInt(T value, uint8_t* target) {
  auto unsigned_value = ExtendValueForVarIntSerialization(value);

  while (unsigned_value >= 0x80) {
    *target++ = static_cast<uint8_t>(unsigned_value) | 0x80;
    unsigned_value >>= 7;
  }
  *target = static_cast<uint8_t>(unsigned_value);
  return target + 1;
}

// Writes a fixed-size redundant encoding of the given |value|. This is
// used to backfill fixed-size reservations for the length field using a
// non-canonical varint encoding (e.g. \x81\x80\x80\x00 instead of \x01).
// See https://github.com/google/protobuf/issues/1530.
// This is used mainly in two cases:
// 1) At trace writing time, when starting a nested messages. The size of a
//    nested message is not known until all its field have been written.
//    |kMessageLengthFieldSize| bytes are reserved to encode the size field and
//    backfilled at the end.
// 2) When rewriting a message at trace filtering time, in protozero/filtering.
//    At that point we know only the upper bound of the length (a filtered
//    message is <= the original one) and we backfill after the message has been
//    filtered.
inline void WriteRedundantVarInt(uint32_t value,
                                 uint8_t* buf,
                                 size_t size = kMessageLengthFieldSize) {
  for (size_t i = 0; i < size; ++i) {
    const uint8_t msb = (i < size - 1) ? 0x80 : 0;
    buf[i] = static_cast<uint8_t>(value) | msb;
    value >>= 7;
  }
}

template <uint32_t field_id>
void StaticAssertSingleBytePreamble() {
  static_assert(field_id < 16,
                "Proto field id too big to fit in a single byte preamble");
}

// Parses a VarInt from the encoded buffer [start, end). |end| is STL-style and
// points one byte past the end of buffer.
// The parsed int value is stored in the output arg |value|. Returns a pointer
// to the next unconsumed byte (so start < retval <= end) or |start| if the
// VarInt could not be fully parsed because there was not enough space in the
// buffer.
inline const uint8_t* ParseVarInt(const uint8_t* start,
                                  const uint8_t* end,
                                  uint64_t* out_value) {
  return PerfettoPbParseVarInt(start, end, out_value);
}

enum class RepetitionType {
  kNotRepeated,
  kRepeatedPacked,
  kRepeatedNotPacked,
};

// Provide a common base struct for all templated FieldMetadata types to allow
// simple checks if a given type is a FieldMetadata or not.
struct FieldMetadataBase {
  constexpr FieldMetadataBase() = default;
};

template <uint32_t field_id,
          RepetitionType repetition_type,
          ProtoSchemaType proto_schema_type,
          typename CppFieldType,
          typename MessageType>
struct FieldMetadata : public FieldMetadataBase {
  constexpr FieldMetadata() = default;

  static constexpr int kFieldId = field_id;
  // Whether this field is repeated, packed (repeated [packed-true]) or not
  // (optional).
  static constexpr RepetitionType kRepetitionType = repetition_type;
  // Proto type of this field (e.g. int64, fixed32 or nested message).
  static constexpr ProtoSchemaType kProtoFieldType = proto_schema_type;
  // C++ type of this field (for nested messages - C++ protozero class).
  using cpp_field_type = CppFieldType;
  // Protozero message which this field belongs to.
  using message_type = MessageType;
};

}  // namespace proto_utils
}  // namespace protozero

#endif  // INCLUDE_PERFETTO_PROTOZERO_PROTO_UTILS_H_
