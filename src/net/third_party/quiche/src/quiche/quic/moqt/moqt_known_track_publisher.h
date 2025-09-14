// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_KNOWN_TRACK_PUBLISHER_H_
#define QUICHE_QUIC_MOQT_MOQT_KNOWN_TRACK_PUBLISHER_H_

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_publisher.h"

namespace moqt {

// MoqtKnownTrackPublisher is a publisher that supports publishing a set of
// well-known predefined tracks.
class MoqtKnownTrackPublisher : public MoqtPublisher {
 public:
  MoqtKnownTrackPublisher() = default;
  MoqtKnownTrackPublisher(const MoqtKnownTrackPublisher&) = delete;
  MoqtKnownTrackPublisher(MoqtKnownTrackPublisher&&) = delete;
  MoqtKnownTrackPublisher& operator=(const MoqtKnownTrackPublisher&) = delete;
  MoqtKnownTrackPublisher& operator=(MoqtKnownTrackPublisher&&) = delete;

  absl::StatusOr<std::shared_ptr<MoqtTrackPublisher>> GetTrack(
      const FullTrackName& track_name) override;
  void Add(std::shared_ptr<MoqtTrackPublisher> track_publisher);
  void Delete(const FullTrackName& track_name);

 private:
  absl::flat_hash_map<FullTrackName, std::shared_ptr<MoqtTrackPublisher>>
      tracks_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_KNOWN_TRACK_PUBLISHER_H_
