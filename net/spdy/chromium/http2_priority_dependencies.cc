// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/http2_priority_dependencies.h"
#include "net/spdy/platform/api/spdy_estimate_memory_usage.h"

namespace net {

Http2PriorityDependencies::Http2PriorityDependencies() {}

Http2PriorityDependencies::~Http2PriorityDependencies() {}

void Http2PriorityDependencies::OnStreamCreation(
    SpdyStreamId id,
    SpdyPriority priority,
    SpdyStreamId* dependent_stream_id,
    bool* exclusive) {
  DCHECK(entry_by_stream_id_.find(id) == entry_by_stream_id_.end());

  *dependent_stream_id = 0ul;
  *exclusive = true;

  // Dependent on the lowest-priority stream that has a priority >= |priority|.
  IdList::iterator parent;
  if (PriorityLowerBound(priority, &parent)) {
    *dependent_stream_id = parent->first;
  }

  id_priority_lists_[priority].push_back(std::make_pair(id, priority));
  IdList::iterator it = id_priority_lists_[priority].end();
  --it;
  entry_by_stream_id_[id] = it;
}

bool Http2PriorityDependencies::PriorityLowerBound(SpdyPriority priority,
                                                   IdList::iterator* bound) {
  for (int i = priority; i >= kV3HighestPriority; --i) {
    if (!id_priority_lists_[i].empty()) {
      *bound = id_priority_lists_[i].end();
      --(*bound);
      return true;
    }
  }
  return false;
}

bool Http2PriorityDependencies::ParentOfStream(SpdyStreamId id,
                                               IdList::iterator* parent) {
  EntryMap::iterator entry = entry_by_stream_id_.find(id);
  DCHECK(entry != entry_by_stream_id_.end());

  SpdyPriority priority = entry->second->second;
  IdList::iterator curr = entry->second;
  if (curr != id_priority_lists_[priority].begin()) {
    *parent = curr;
    --(*parent);
    return true;
  }

  // |id| is at the head of its priority list, so its parent is the last
  // entry of the next-highest priority band.
  if (priority == kV3HighestPriority) {
    return false;
  }
  return PriorityLowerBound(priority - 1, parent);
}

bool Http2PriorityDependencies::ChildOfStream(SpdyStreamId id,
                                              IdList::iterator* child) {
  EntryMap::iterator entry = entry_by_stream_id_.find(id);
  DCHECK(entry != entry_by_stream_id_.end());

  SpdyPriority priority = entry->second->second;
  *child = entry->second;
  ++(*child);
  if (*child != id_priority_lists_[priority].end()) {
    return true;
  }

  // |id| is at the end of its priority list, so its child is the stream
  // at the front of the next-lowest priority band.
  for (int i = priority + 1; i <= kV3LowestPriority; ++i) {
    if (!id_priority_lists_[i].empty()) {
      *child = id_priority_lists_[i].begin();
      return true;
    }
  }

  return false;
}

std::vector<Http2PriorityDependencies::DependencyUpdate>
Http2PriorityDependencies::OnStreamUpdate(SpdyStreamId id,
                                          SpdyPriority new_priority) {
  std::vector<DependencyUpdate> result;
  result.reserve(2);

  EntryMap::iterator curr_entry = entry_by_stream_id_.find(id);
  SpdyPriority old_priority = curr_entry->second->second;
  if (old_priority == new_priority) {
    return result;
  }

  IdList::iterator old_parent;
  bool old_has_parent = ParentOfStream(id, &old_parent);

  IdList::iterator new_parent;
  bool new_has_parent = PriorityLowerBound(new_priority, &new_parent);

  // If we move |id| from MEDIUM to LOW, where HIGH = {other_id}, MEDIUM = {id},
  // and LOW = {}, then PriorityLowerBound(new_priority) is |id|. In this corner
  // case, |id| does not change parents.
  if (new_has_parent && new_parent->first == id) {
    new_has_parent = old_has_parent;
    new_parent = old_parent;
  }

  // If the parent has changed, we generate dependency updates.
  if ((old_has_parent != new_has_parent) ||
      (old_has_parent && old_parent->first != new_parent->first)) {
    // If |id| has a child, then that child moves to be dependent on
    // |old_parent|.
    IdList::iterator old_child;
    if (ChildOfStream(id, &old_child)) {
      if (old_has_parent) {
        result.push_back({old_child->first, old_parent->first, true});
      } else {
        result.push_back({old_child->first, 0, true});
      }
    }

    // |id| moves to be dependent on |new_parent|.
    if (new_has_parent) {
      result.push_back({id, new_parent->first, true});
    } else {
      result.push_back({id, 0, true});
    }
  }

  // Move to the new priority.
  EntryMap::iterator old = entry_by_stream_id_.find(id);
  id_priority_lists_[old->second->second].erase(old->second);
  id_priority_lists_[new_priority].push_back(std::make_pair(id, new_priority));
  IdList::iterator it = id_priority_lists_[new_priority].end();
  --it;
  entry_by_stream_id_[id] = it;

  return result;
}

void Http2PriorityDependencies::OnStreamDestruction(SpdyStreamId id) {
  EntryMap::iterator emit = entry_by_stream_id_.find(id);
  DCHECK(emit != entry_by_stream_id_.end());

  IdList::iterator it = emit->second;
  id_priority_lists_[it->second].erase(it);
  entry_by_stream_id_.erase(emit);
}

size_t Http2PriorityDependencies::EstimateMemoryUsage() const {
  return SpdyEstimateMemoryUsage(id_priority_lists_);
  // TODO(xunjieli): https://crbug.com/690015. Include |entry_by_stream_id_|
  // when memory_usage_estimator.h supports std::list::iterator.
}

}  // namespace net
