// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H
#define QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H

#include <cstdint>
#include <list>
#include <optional>

#include "quiche/quic/moqt/moqt_messages.h"

namespace moqt {

struct SubscribeWindow {
  FullSequence start;
  std::optional<FullSequence> end;
  // Creates a half-open window.
  SubscribeWindow(uint64_t start_group, uint64_t start_object) {
    start = {start_group, start_object};
    end = std::nullopt;
  }
  // Creates a closed window.
  SubscribeWindow(uint64_t start_group, uint64_t start_object,
                  uint64_t end_group, uint64_t end_object) {
    start = {start_group, start_object};
    end = {end_group, end_object};
  }
  bool InWindow(const FullSequence& seq) const {
    if (seq < start) {
      return false;
    }
    if (!end.has_value() || seq < *end) {
      return true;
    }
    return false;
  }
};

// Class to keep track of the sequence number blocks to which a peer is
// subscribed.
class MoqtSubscribeWindows {
 public:
  MoqtSubscribeWindows() {}

  bool SequenceIsSubscribed(uint64_t group, uint64_t object) const {
    FullSequence seq(group, object);
    for (auto it : windows) {
      if (it.InWindow(seq)) {
        return true;
      }
    }
    return false;
  }

  // |window| has already been converted into absolute sequence numbers. An
  // optimization could consolidate overlapping subscribe windows.
  void AddWindow(SubscribeWindow window) { windows.push_front(window); }

  bool IsEmpty() const { return windows.empty(); }

 private:
  std::list<SubscribeWindow> windows;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_SUBSCRIBE_WINDOWS_H
