// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_PACKET_NUMBER_INDEXED_QUEUE_H_
#define QUICHE_QUIC_CORE_PACKET_NUMBER_INDEXED_QUEUE_H_

#include "net/third_party/quiche/src/quic/core/quic_circular_deque.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_number.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"

namespace quic {

// PacketNumberIndexedQueue is a queue of mostly continuous numbered entries
// which supports the following operations:
// - adding elements to the end of the queue, or at some point past the end
// - removing elements in any order
// - retrieving elements
// If all elements are inserted in order, all of the operations above are
// amortized O(1) time.
//
// Internally, the data structure is a deque where each element is marked as
// present or not.  The deque starts at the lowest present index.  Whenever an
// element is removed, it's marked as not present, and the front of the deque is
// cleared of elements that are not present.
//
// The tail of the queue is not cleared due to the assumption of entries being
// inserted in order, though removing all elements of the queue will return it
// to its initial state.
//
// Note that this data structure is inherently hazardous, since an addition of
// just two entries will cause it to consume all of the memory available.
// Because of that, it is not a general-purpose container and should not be used
// as one.
// TODO(wub): Update the comments when deprecating
// --quic_bw_sampler_remove_packets_once_per_congestion_event.
template <typename T>
class QUIC_NO_EXPORT PacketNumberIndexedQueue {
 public:
  PacketNumberIndexedQueue() : number_of_present_entries_(0) {}

  // Retrieve the entry associated with the packet number.  Returns the pointer
  // to the entry in case of success, or nullptr if the entry does not exist.
  T* GetEntry(QuicPacketNumber packet_number);
  const T* GetEntry(QuicPacketNumber packet_number) const;

  // Inserts data associated |packet_number| into (or past) the end of the
  // queue, filling up the missing intermediate entries as necessary.  Returns
  // true if the element has been inserted successfully, false if it was already
  // in the queue or inserted out of order.
  template <typename... Args>
  bool Emplace(QuicPacketNumber packet_number, Args&&... args);

  // Removes data associated with |packet_number| and frees the slots in the
  // queue as necessary.
  bool Remove(QuicPacketNumber packet_number);

  // Same as above, but if an entry is present in the queue, also call f(entry)
  // before removing it.
  template <typename Function>
  bool Remove(QuicPacketNumber packet_number, Function f);

  // Remove up to, but not including |packet_number|.
  // Unused slots in the front are also removed, which means when the function
  // returns, |first_packet()| can be larger than |packet_number|.
  void RemoveUpTo(QuicPacketNumber packet_number);

  bool IsEmpty() const { return number_of_present_entries_ == 0; }

  // Returns the number of entries in the queue.
  size_t number_of_present_entries() const {
    return number_of_present_entries_;
  }

  // Returns the number of entries allocated in the underlying deque.  This is
  // proportional to the memory usage of the queue.
  size_t entry_slots_used() const { return entries_.size(); }

  // Packet number of the first entry in the queue.
  QuicPacketNumber first_packet() const { return first_packet_; }

  // Packet number of the last entry ever inserted in the queue.  Note that the
  // entry in question may have already been removed.  Zero if the queue is
  // empty.
  QuicPacketNumber last_packet() const {
    if (IsEmpty()) {
      return QuicPacketNumber();
    }
    return first_packet_ + entries_.size() - 1;
  }

 private:
  // Wrapper around T used to mark whether the entry is actually in the map.
  struct QUIC_NO_EXPORT EntryWrapper : T {
    // NOTE(wub): When quic_bw_sampler_remove_packets_once_per_congestion_event
    // is enabled, |present| is false if and only if this is a placeholder entry
    // for holes in the parent's |entries|.
    bool present;

    EntryWrapper() : present(false) {}

    template <typename... Args>
    explicit EntryWrapper(Args&&... args)
        : T(std::forward<Args>(args)...), present(true) {}
  };

  // Cleans up unused slots in the front after removing an element.
  void Cleanup();

  const EntryWrapper* GetEntryWrapper(QuicPacketNumber offset) const;
  EntryWrapper* GetEntryWrapper(QuicPacketNumber offset) {
    const auto* const_this = this;
    return const_cast<EntryWrapper*>(const_this->GetEntryWrapper(offset));
  }

  QuicCircularDeque<EntryWrapper> entries_;
  // NOTE(wub): When --quic_bw_sampler_remove_packets_once_per_congestion_event
  // is enabled, |number_of_present_entries_| only represents number of holes,
  // which does not include number of acked or lost packets.
  size_t number_of_present_entries_;
  QuicPacketNumber first_packet_;
};

template <typename T>
T* PacketNumberIndexedQueue<T>::GetEntry(QuicPacketNumber packet_number) {
  EntryWrapper* entry = GetEntryWrapper(packet_number);
  if (entry == nullptr) {
    return nullptr;
  }
  return entry;
}

template <typename T>
const T* PacketNumberIndexedQueue<T>::GetEntry(
    QuicPacketNumber packet_number) const {
  const EntryWrapper* entry = GetEntryWrapper(packet_number);
  if (entry == nullptr) {
    return nullptr;
  }
  return entry;
}

template <typename T>
template <typename... Args>
bool PacketNumberIndexedQueue<T>::Emplace(QuicPacketNumber packet_number,
                                          Args&&... args) {
  if (!packet_number.IsInitialized()) {
    QUIC_BUG << "Try to insert an uninitialized packet number";
    return false;
  }

  if (IsEmpty()) {
    DCHECK(entries_.empty());
    DCHECK(!first_packet_.IsInitialized());

    entries_.emplace_back(std::forward<Args>(args)...);
    number_of_present_entries_ = 1;
    first_packet_ = packet_number;
    return true;
  }

  // Do not allow insertion out-of-order.
  if (packet_number <= last_packet()) {
    return false;
  }

  // Handle potentially missing elements.
  size_t offset = packet_number - first_packet_;
  if (offset > entries_.size()) {
    entries_.resize(offset);
  }

  number_of_present_entries_++;
  entries_.emplace_back(std::forward<Args>(args)...);
  DCHECK_EQ(packet_number, last_packet());
  return true;
}

template <typename T>
bool PacketNumberIndexedQueue<T>::Remove(QuicPacketNumber packet_number) {
  return Remove(packet_number, [](const T&) {});
}

template <typename T>
template <typename Function>
bool PacketNumberIndexedQueue<T>::Remove(QuicPacketNumber packet_number,
                                         Function f) {
  EntryWrapper* entry = GetEntryWrapper(packet_number);
  if (entry == nullptr) {
    return false;
  }
  f(*static_cast<const T*>(entry));
  entry->present = false;
  number_of_present_entries_--;

  if (packet_number == first_packet()) {
    Cleanup();
  }
  return true;
}

template <typename T>
void PacketNumberIndexedQueue<T>::RemoveUpTo(QuicPacketNumber packet_number) {
  while (!entries_.empty() && first_packet_.IsInitialized() &&
         first_packet_ < packet_number) {
    if (entries_.front().present) {
      number_of_present_entries_--;
    }
    entries_.pop_front();
    first_packet_++;
  }
  Cleanup();
}

template <typename T>
void PacketNumberIndexedQueue<T>::Cleanup() {
  while (!entries_.empty() && !entries_.front().present) {
    entries_.pop_front();
    first_packet_++;
  }
  if (entries_.empty()) {
    first_packet_.Clear();
  }
}

template <typename T>
auto PacketNumberIndexedQueue<T>::GetEntryWrapper(
    QuicPacketNumber packet_number) const -> const EntryWrapper* {
  if (!packet_number.IsInitialized() || IsEmpty() ||
      packet_number < first_packet_) {
    return nullptr;
  }

  uint64_t offset = packet_number - first_packet_;
  if (offset >= entries_.size()) {
    return nullptr;
  }

  const EntryWrapper* entry = &entries_[offset];
  if (!entry->present) {
    return nullptr;
  }

  return entry;
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_PACKET_NUMBER_INDEXED_QUEUE_H_
