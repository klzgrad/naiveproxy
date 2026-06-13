// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_STREAM_MAP_H
#define QUICHE_QUIC_MOQT_STREAM_MAP_H

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/container/btree_map.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// A map of outgoing data streams indexed by object sequence numbers.
class QUICHE_EXPORT SendStreamMap {
 public:
  SendStreamMap() = default;

  std::optional<webtransport::StreamId> GetStreamFor(
      DataStreamIndex index) const;
  void AddStream(DataStreamIndex index, webtransport::StreamId stream_id);
  void RemoveStream(DataStreamIndex index);
  std::vector<webtransport::StreamId> GetAllStreams() const;
  std::vector<webtransport::StreamId> GetStreamsForGroup(
      uint64_t group_id) const;

 private:
  absl::btree_map<DataStreamIndex, webtransport::StreamId> send_streams_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_STREAM_MAP_H
