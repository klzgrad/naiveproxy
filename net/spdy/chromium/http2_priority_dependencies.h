// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CHROMIUM_HTTP2_PRIORITY_DEPENDENCIES_H_
#define NET_SPDY_CHROMIUM_HTTP2_PRIORITY_DEPENDENCIES_H_

#include <list>
#include <map>
#include <utility>
#include <vector>

#include "net/base/net_export.h"
#include "net/spdy/core/spdy_protocol.h"

namespace net {

// A helper class encapsulating the state and logic to set dependencies of
// HTTP2 streams based on their SpdyPriority and the ordering
// of creation and deletion of the streams.
class NET_EXPORT_PRIVATE Http2PriorityDependencies {
 public:
  Http2PriorityDependencies();
  ~Http2PriorityDependencies();

  // Called when a stream is created. This is used for both client-initiated
  // and server-initiated (pushed) streams.
  // On return, |*dependent_stream_id| is set to the stream id that
  // this stream should be made dependent on, and |*exclusive| set to
  // whether that dependency should be exclusive.
  void OnStreamCreation(SpdyStreamId id,
                        SpdyPriority priority,
                        SpdyStreamId* dependent_stream_id,
                        bool* exclusive);

  // Called when a stream is destroyed.
  void OnStreamDestruction(SpdyStreamId id);

  struct DependencyUpdate {
    SpdyStreamId id;
    SpdyStreamId dependent_stream_id;
    bool exclusive;
  };

  // Called when a stream's priority has changed. Returns a list of
  // dependency updates that should be sent to the server to describe
  // the requested priority change. The updates should be sent in the
  // given order.
  std::vector<DependencyUpdate> OnStreamUpdate(SpdyStreamId id,
                                               SpdyPriority new_priority);

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

 private:
  // The requirements for the internal data structure for this class are:
  //     a) Constant time insertion of entries at the end of the list,
  //     b) Fast removal of any entry based on its id.
  //     c) Constant time lookup of the entry at the end of the list.
  // std::list would satisfy (a) & (c), but some form of map is
  // needed for (b).  The priority must be included in the map
  // entries so that deletion can determine which list in id_priority_lists_
  // to erase from.
  using IdList = std::list<std::pair<SpdyStreamId, SpdyPriority>>;
  using EntryMap = std::map<SpdyStreamId, IdList::iterator>;

  IdList id_priority_lists_[kV3LowestPriority + 1];

  // Tracks the location of an id anywhere in the above vector of lists.
  // Iterators to list elements remain valid until those particular elements
  // are erased.
  EntryMap entry_by_stream_id_;

  // Finds the lowest-priority stream that has a priority >= |priority|.
  // Returns false if there are no such streams.
  // Otherwise, returns true and sets |*bound|.
  bool PriorityLowerBound(SpdyPriority priority, IdList::iterator* bound);

  // Finds the stream just above |id| in the total order.
  // Returns false if there are no streams with a higher priority.
  // Otherwise, returns true and sets |*parent|.
  bool ParentOfStream(SpdyStreamId id, IdList::iterator* parent);

  // Finds the stream just below |id| in the total order.
  // Returns false if there are no streams with a lower priority.
  // Otherwise, returns true and sets |*child|.
  bool ChildOfStream(SpdyStreamId id, IdList::iterator* child);
};

}  // namespace net

#endif  // NET_SPDY_CHROMIUM_HTTP2_PRIORITY_DEPENDENCIES_H_
