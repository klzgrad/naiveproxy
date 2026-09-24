// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_names.h"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>

#include "absl/container/fixed_array.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_status_utils.h"

namespace moqt {

namespace {

bool IsTrackNameSafeCharacter(char c) {
  return absl::ascii_isalnum(c) || c == '_';
}

// Appends escaped version of a track name component into `output`.  It is up to
// the caller to reserve() an appropriate amount of space in advance.  The text
// format is defined in
// https://www.ietf.org/archive/id/draft-ietf-moq-transport-16.html#name-representing-namespace-and-amount
void EscapeTrackNameComponent(absl::string_view input, std::string& output) {
  for (char c : input) {
    if (IsTrackNameSafeCharacter(c)) {
      output.push_back(c);
    } else {
      output.push_back('.');
      absl::StrAppend(&output, absl::Hex(c, absl::kZeroPad2));
    }
  }
}

// Similarly to the function above, the caller should call reserve() on `output`
// before calling.
void AppendEscapedTrackNameTuple(const MoqtStringTuple& tuple,
                                 std::string& output) {
  for (size_t i = 0; i < tuple.size(); ++i) {
    EscapeTrackNameComponent(tuple[i], output);
    if (i < (tuple.size() - 1)) {
      output.push_back('-');
    }
  }
}

[[nodiscard]] bool HexCharToLowerHalfOfByte(char c, char& output) {
  if (c >= '0' && c <= '9') {
    output |= c - '0';
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    output |= (c - 'a') + 0xa;
    return true;
  }
  return false;
}

// Custom hex-to-binary converter that disallows uppercase hex, as the
// specification explicitly requires lowercase hex.
[[nodiscard]] bool HexEncodedBitToByte(char c0, char c1, char& output) {
  output = 0;
  if (!HexCharToLowerHalfOfByte(c0, output)) {
    return false;
  }
  output <<= 4;
  if (!HexCharToLowerHalfOfByte(c1, output)) {
    return false;
  }
  return true;
}

// The MOQT specification is currently ambiguous regarding how permissive the
// parsing should be.  We are intentionally taking a "strict" approach, in which
// any track name that this code parses successfully will result in a byte-exact
// serialization from `ToString()`.  For the discussion of this issue, see
// <https://github.com/moq-wg/moq-transport/issues/1501>.
//
// It is up to the caller to reserve the capacity in `output_tuple` in advance.
absl::Status UnescapeTrackNameComponent(absl::string_view input,
                                        MoqtStringTuple& output_tuple) {
  // The unescaping algorithm always results in strings of the same or smaller
  // size, so the fixed array of size `input.size()` will always fit the output.
  absl::FixedArray<char> output_buffer(input.size());
  absl::Span<char> output = absl::MakeSpan(output_buffer);
  while (!input.empty()) {
    if (IsTrackNameSafeCharacter(input[0])) {
      output[0] = input[0];
      input.remove_prefix(1);
      output.remove_prefix(1);
      continue;
    }
    if (input[0] == '.') {
      if (input.size() < 3) {
        return absl::InvalidArgumentError("Incomplete escape sequence");
      }
      if (!HexEncodedBitToByte(input[1], input[2], output[0])) {
        return absl::InvalidArgumentError("Invalid hex in an escape sequence");
      }
      if (IsTrackNameSafeCharacter(output[0])) {
        return absl::InvalidArgumentError("Hex-encoding a safe character");
      }
      input.remove_prefix(3);
      output.remove_prefix(1);
      continue;
    }
    return absl::InvalidArgumentError(
        absl::StrFormat("Invalid character 0x%02x encountered", input[0]));
  }
  size_t output_size = output_buffer.size() - output.size();
  if (!output_tuple.Add(absl::string_view(output_buffer.data(), output_size))) {
    return absl::OutOfRangeError("Maximum tuple size exceeded");
  }
  return absl::OkStatus();
}

absl::StatusOr<MoqtStringTuple> ParseNameTuple(absl::string_view input) {
  // Special-case empty namespace, to indicate it is {} and not {""}.
  if (input.empty()) {
    return MoqtStringTuple();
  }

  ssize_t bytes_to_reserve = input.size();
  ssize_t elements_to_reserve = 1;
  for (char c : input) {
    if (c == '-') {
      ++elements_to_reserve;
      --bytes_to_reserve;
    }
    if (c == '.') {
      bytes_to_reserve -= 2;
    }
  }

  MoqtStringTuple tuple;
  if (bytes_to_reserve > 0) {
    // A malformed name such as `......` will result in `bytes_to_reserve` being
    // negative.
    tuple.ReserveDataBytes(bytes_to_reserve);
  }
  tuple.ReserveTupleElements(elements_to_reserve);
  for (absl::string_view bit : absl::StrSplit(input, '-')) {
    QUICHE_RETURN_IF_ERROR(UnescapeTrackNameComponent(bit, tuple));
  }
  QUICHE_DCHECK_EQ(tuple.TotalBytes(), bytes_to_reserve);
  QUICHE_DCHECK_EQ(tuple.size(), elements_to_reserve);
  return tuple;
}

}  // namespace

absl::StatusOr<TrackNamespace> TrackNamespace::Create(MoqtStringTuple tuple) {
  if (tuple.size() > kMaxNamespaceElements) {
    return absl::OutOfRangeError(
        absl::StrFormat("Tuple has %d elements, whereas MOQT only allows %d",
                        tuple.size(), kMaxNamespaceElements));
  }
  return TrackNamespace(std::move(tuple));
}

absl::StatusOr<TrackNamespace> TrackNamespace::Parse(absl::string_view input) {
  absl::StatusOr<MoqtStringTuple> tuple = ParseNameTuple(input);
  QUICHE_RETURN_IF_ERROR(tuple.status());
  return Create(*std::move(tuple));
}

TrackNamespace::TrackNamespace(std::initializer_list<absl::string_view> tuple) {
  bool success = tuple_.Append(tuple);
  if (!success) {
    QUICHE_BUG(TrackNamespace_constructor)
        << "Invalid namespace supplied to the TrackNamspace constructor";
    tuple_ = MoqtStringTuple();
    return;
  }
}

bool TrackNamespace::InNamespace(const TrackNamespace& other) const {
  return tuple_.IsPrefix(other.tuple_);
}

bool TrackNamespace::AddElement(absl::string_view element) {
  if (tuple_.size() >= kMaxNamespaceElements) {
    return false;
  }
  return tuple_.Add(element);
}
bool TrackNamespace::PopElement() {
  if (tuple_.empty()) {
    return false;
  }
  return tuple_.Pop();
}

std::string TrackNamespace::ToString() const {
  std::string output;
  output.reserve(3 * tuple_.TotalBytes() + tuple_.size());
  AppendEscapedTrackNameTuple(tuple_, output);
  return output;
}

absl::StatusOr<FullTrackName> FullTrackName::Create(TrackNamespace ns,
                                                    std::string name) {
  const size_t total_length = ns.total_length() + name.size();
  if (ns.total_length() + name.size() > kMaxFullTrackNameSize) {
    return absl::OutOfRangeError(
        absl::StrFormat("Attempting to create a full track name of size %d, "
                        "whereas at most %d bytes are allowed by the protocol",
                        total_length, kMaxFullTrackNameSize));
  }
  return FullTrackName(std::move(ns), std::move(name),
                       FullTrackNameIsValidTag());
}

absl::StatusOr<FullTrackName> FullTrackName::Parse(absl::string_view input) {
  absl::StatusOr<MoqtStringTuple> tuple = ParseNameTuple(input);
  QUICHE_RETURN_IF_ERROR(tuple.status());
  if (tuple->size() < 3) {
    return absl::InvalidArgumentError("Full track name is missing elements");
  }
  std::string name(tuple->back());
  tuple->Pop();
  if (!tuple->back().empty()) {
    return absl::InvalidArgumentError(
        "Full track name must use -- as a separator");
  }
  tuple->Pop();
  if (tuple->size() == 1 && tuple->ValueAt(0) == "") {
    // Special case handling for empty namespace.
    tuple->Pop();
  }
  absl::StatusOr<TrackNamespace> ns = TrackNamespace::Create(*std::move(tuple));
  QUICHE_RETURN_IF_ERROR(ns.status());
  return Create(*std::move(ns), std::move(name));
}

FullTrackName::FullTrackName(TrackNamespace ns, absl::string_view name)
    : namespace_(std::move(ns)), name_(name) {
  if (namespace_.total_length() + name.size() > kMaxFullTrackNameSize) {
    QUICHE_BUG(Moqt_full_track_name_too_large_01)
        << "Constructing a Full Track Name that is too large.";
    namespace_.Clear();
    name_.clear();
  }
}
FullTrackName::FullTrackName(absl::string_view ns, absl::string_view name)
    : namespace_(TrackNamespace({ns})), name_(name) {
  if (namespace_.total_length() + name.size() > kMaxFullTrackNameSize) {
    QUICHE_BUG(Moqt_full_track_name_too_large_02)
        << "Constructing a Full Track Name that is too large.";
    namespace_.Clear();
    name_.clear();
  }
}
FullTrackName::FullTrackName(std::initializer_list<absl::string_view> ns,
                             absl::string_view name)
    : namespace_(ns), name_(name) {
  if (namespace_.total_length() + name.size() > kMaxFullTrackNameSize) {
    QUICHE_BUG(Moqt_full_track_name_too_large_03)
        << "Constructing a Full Track Name that is too large.";
    namespace_.Clear();
    name_.clear();
  }
}

FullTrackName::FullTrackName(TrackNamespace ns, std::string name,
                             FullTrackNameIsValidTag)
    : namespace_(std::move(ns)), name_(std::move(name)) {}

std::string FullTrackName::ToString() const {
  std::string output;
  output.reserve(3 * namespace_.total_length() +
                 namespace_.number_of_elements() + 3 * name_.size() + 2);
  AppendEscapedTrackNameTuple(namespace_.tuple(), output);
  output.append("--");
  EscapeTrackNameComponent(name_, output);
  return output;
}

}  // namespace moqt
