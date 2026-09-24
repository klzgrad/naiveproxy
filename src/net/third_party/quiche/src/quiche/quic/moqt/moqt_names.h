// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_NAMES_H_
#define QUICHE_QUIC_MOQT_MOQT_NAMES_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/quiche_string_tuple.h"

namespace moqt {

// Protocol-specified limits on the length and structure of MoQT namespaces.
inline constexpr uint64_t kMaxNamespaceElements = 32;
inline constexpr size_t kMaxFullTrackNameSize = 4096;

// MOQT does not operate on tuples larger than 4096, which means we can encode
// tuple offsets as uint16_t.  We allow up to 8 inlined tuple elements, since
// picking a smaller value would not shrink the inlined vector in question.
using MoqtStringTuple = quiche::QuicheStringTuple<kMaxFullTrackNameSize, 8>;

// TrackNamespace represents a valid MOQT track namespace.
class TrackNamespace {
 public:
  static absl::StatusOr<TrackNamespace> Create(MoqtStringTuple tuple);

  static absl::StatusOr<TrackNamespace> Parse(absl::string_view input);

  TrackNamespace() = default;

  explicit TrackNamespace(std::initializer_list<absl::string_view> tuple);

  TrackNamespace(const TrackNamespace&) = default;
  TrackNamespace(TrackNamespace&&) = default;
  TrackNamespace& operator=(const TrackNamespace&) = default;
  TrackNamespace& operator=(TrackNamespace&&) = default;

  bool InNamespace(const TrackNamespace& other) const;
  [[nodiscard]] bool AddElement(absl::string_view element);
  bool PopElement();
  void Clear() { tuple_.Clear(); }
  void ReserveElements(size_t count) { tuple_.ReserveTupleElements(count); }

  [[nodiscard]] bool Append(absl::Span<const absl::string_view> span) {
    return tuple_.Append(span);
  }

  absl::StatusOr<TrackNamespace> AddSuffix(const TrackNamespace& suffix) const {
    TrackNamespace result = *this;
    if (!result.tuple_.Append(suffix.tuple_)) {
      return absl::OutOfRangeError("Combined namespace is too large");
    }
    return result;
  }

  absl::StatusOr<TrackNamespace> ExtractSuffix(
      const TrackNamespace& prefix) const {
    TrackNamespace result = *this;
    if (!result.tuple_.ConsumePrefix(prefix.tuple_)) {
      return absl::InvalidArgumentError("Prefix is not in namespace");
    }
    return result;
  }

  // Encodes the string representation of MOQT track namespace in the format
  // prescribed by the MOQT specification.
  std::string ToString() const;

  // Returns the number of elements in the tuple.
  size_t number_of_elements() const { return tuple_.size(); }
  // Returns the sum of the lengths of all elements in the tuple.
  size_t total_length() const { return tuple_.TotalBytes(); }
  bool empty() const { return tuple_.empty(); }

  auto operator<=>(const TrackNamespace& other) const {
    return tuple_ <=> other.tuple_;
  }
  bool operator==(const TrackNamespace&) const = default;

  const MoqtStringTuple& tuple() const { return tuple_; }

  template <typename H>
  friend H AbslHashValue(H h, const TrackNamespace& m) {
    return H::combine(std::move(h), m.tuple_);
  }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const TrackNamespace& track_namespace) {
    sink.Append(track_namespace.ToString());
  }

 private:
  TrackNamespace(MoqtStringTuple tuple) : tuple_(std::move(tuple)) {}

  MoqtStringTuple tuple_;
};

// FullTrackName represents a MOQT full track name.
class FullTrackName {
 public:
  static absl::StatusOr<FullTrackName> Create(TrackNamespace ns,
                                              std::string name);

  static absl::StatusOr<FullTrackName> Parse(absl::string_view input);

  FullTrackName() = default;

  // Convenience constructor. QUICHE_BUGs if the resulting full track name is
  // invalid.
  FullTrackName(TrackNamespace ns, absl::string_view name);
  FullTrackName(absl::string_view ns, absl::string_view name);
  FullTrackName(std::initializer_list<absl::string_view> ns,
                absl::string_view name);

  FullTrackName(const FullTrackName&) = default;
  FullTrackName(FullTrackName&&) = default;
  FullTrackName& operator=(const FullTrackName&) = default;
  FullTrackName& operator=(FullTrackName&&) = default;

  bool IsValid() const { return !name_.empty(); }

  const TrackNamespace& track_namespace() const { return namespace_; }
  absl::string_view name() const ABSL_ATTRIBUTE_LIFETIME_BOUND { return name_; }
  size_t length() const { return namespace_.total_length() + name_.length(); }

  // Encodes the string representation of MOQT full track name in the format
  // prescribed by the MOQT specification.
  std::string ToString() const;

  auto operator<=>(const FullTrackName&) const = default;
  template <typename H>
  friend H AbslHashValue(H h, const FullTrackName& m) {
    return H::combine(std::move(h), m.namespace_.tuple(), m.name_);
  }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const FullTrackName& full_track_name) {
    sink.Append(full_track_name.ToString());
  }

 private:
  struct FullTrackNameIsValidTag {};

  explicit FullTrackName(TrackNamespace ns, std::string name,
                         FullTrackNameIsValidTag);

  TrackNamespace namespace_;
  std::string name_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_NAMES_H_
