// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_TYPES_H_
#define QUICHE_QUIC_MOQT_MOQT_TYPES_H_

#include <cstdint>

#include "absl/strings/str_format.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_data_writer.h"

namespace moqt {

inline constexpr uint64_t kMaxGroupId = quiche::kVarInt62MaxValue;
inline constexpr uint64_t kMaxObjectId = quiche::kVarInt62MaxValue;
// Location as defined in
// https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#location-structure
struct Location {
  uint64_t group = 0;
  uint64_t object = 0;

  Location() = default;
  Location(uint64_t group, uint64_t object) : group(group), object(object) {}

  // Location order as described in
  // https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#location-structure
  auto operator<=>(const Location&) const = default;

  Location Next() const {
    if (object == kMaxObjectId) {
      if (group == kMaxObjectId) {
        return Location(0, 0);
      }
      return Location(group + 1, 0);
    }
    return Location(group, object + 1);
  }

  template <typename H>
  friend H AbslHashValue(H h, const Location& m);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Location& sequence) {
    absl::Format(&sink, "(%d; %d)", sequence.group, sequence.object);
  }
};

template <typename H>
H AbslHashValue(H h, const Location& m) {
  return H::combine(std::move(h), m.group, m.object);
}

enum class QUICHE_EXPORT MoqtObjectStatus : uint64_t {
  kNormal = 0x0,
  kObjectDoesNotExist = 0x1,
  kEndOfGroup = 0x3,
  kEndOfTrack = 0x4,
  kInvalidObjectStatus = 0x5,
};

// A tuple uniquely identifying a WebTransport data stream associated with a
// subscription. By convention, if a DataStreamIndex is necessary for a datagram
// track, `subgroup` is set to zero.
struct DataStreamIndex {
  uint64_t group = 0;
  uint64_t subgroup = 0;

  DataStreamIndex() = default;
  DataStreamIndex(uint64_t group, uint64_t subgroup)
      : group(group), subgroup(subgroup) {}

  auto operator<=>(const DataStreamIndex&) const = default;

  template <typename H>
  friend H AbslHashValue(H h, const DataStreamIndex& index) {
    return H::combine(std::move(h), index.group, index.subgroup);
  }
};

enum class QUICHE_EXPORT SubscribeNamespaceOption : uint64_t {
  kPublish = 0x00,
  kNamespace = 0x01,
  kBoth = 0x02,
};
static constexpr uint64_t kMaxSubscribeOption = 0x02;

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_TYPES_H_
