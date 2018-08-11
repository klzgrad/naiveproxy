// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_write_queue.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/logging.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "net/spdy/spdy_buffer.h"
#include "net/spdy/spdy_buffer_producer.h"
#include "net/spdy/spdy_stream.h"

namespace net {

SpdyWriteQueue::PendingWrite::PendingWrite() = default;

SpdyWriteQueue::PendingWrite::PendingWrite(
    spdy::SpdyFrameType frame_type,
    std::unique_ptr<SpdyBufferProducer> frame_producer,
    const base::WeakPtr<SpdyStream>& stream,
    const MutableNetworkTrafficAnnotationTag& traffic_annotation)
    : frame_type(frame_type),
      frame_producer(std::move(frame_producer)),
      stream(stream),
      traffic_annotation(traffic_annotation),
      has_stream(stream.get() != nullptr) {}

SpdyWriteQueue::PendingWrite::~PendingWrite() = default;

SpdyWriteQueue::PendingWrite::PendingWrite(PendingWrite&& other) = default;
SpdyWriteQueue::PendingWrite& SpdyWriteQueue::PendingWrite::operator=(
    PendingWrite&& other) = default;

size_t SpdyWriteQueue::PendingWrite::EstimateMemoryUsage() const {
  return base::trace_event::EstimateMemoryUsage(frame_producer);
}

SpdyWriteQueue::SpdyWriteQueue() : removing_writes_(false) {}

SpdyWriteQueue::~SpdyWriteQueue() {
  Clear();
}

bool SpdyWriteQueue::IsEmpty() const {
  for (int i = MINIMUM_PRIORITY; i <= MAXIMUM_PRIORITY; i++) {
    if (!queue_[i].empty())
      return false;
  }
  return true;
}

void SpdyWriteQueue::Enqueue(
    RequestPriority priority,
    spdy::SpdyFrameType frame_type,
    std::unique_ptr<SpdyBufferProducer> frame_producer,
    const base::WeakPtr<SpdyStream>& stream,
    const NetworkTrafficAnnotationTag& traffic_annotation) {
  CHECK(!removing_writes_);
  CHECK_GE(priority, MINIMUM_PRIORITY);
  CHECK_LE(priority, MAXIMUM_PRIORITY);
  if (stream.get())
    DCHECK_EQ(stream->priority(), priority);
  queue_[priority].push_back(
      {frame_type, std::move(frame_producer), stream,
       MutableNetworkTrafficAnnotationTag(traffic_annotation)});
}

bool SpdyWriteQueue::Dequeue(
    spdy::SpdyFrameType* frame_type,
    std::unique_ptr<SpdyBufferProducer>* frame_producer,
    base::WeakPtr<SpdyStream>* stream,
    MutableNetworkTrafficAnnotationTag* traffic_annotation) {
  CHECK(!removing_writes_);
  for (int i = MAXIMUM_PRIORITY; i >= MINIMUM_PRIORITY; --i) {
    if (!queue_[i].empty()) {
      PendingWrite pending_write = std::move(queue_[i].front());
      queue_[i].pop_front();
      *frame_type = pending_write.frame_type;
      *frame_producer = std::move(pending_write.frame_producer);
      *stream = pending_write.stream;
      *traffic_annotation = pending_write.traffic_annotation;
      if (pending_write.has_stream)
        DCHECK(stream->get());
      return true;
    }
  }
  return false;
}

void SpdyWriteQueue::RemovePendingWritesForStream(
    const base::WeakPtr<SpdyStream>& stream) {
  CHECK(!removing_writes_);
  removing_writes_ = true;
  RequestPriority priority = stream->priority();
  CHECK_GE(priority, MINIMUM_PRIORITY);
  CHECK_LE(priority, MAXIMUM_PRIORITY);

  DCHECK(stream.get());
#if DCHECK_IS_ON()
  // |stream| should not have pending writes in a queue not matching
  // its priority.
  for (int i = MINIMUM_PRIORITY; i <= MAXIMUM_PRIORITY; ++i) {
    if (priority == i)
      continue;
    for (auto it = queue_[i].begin(); it != queue_[i].end(); ++it)
      DCHECK_NE(it->stream.get(), stream.get());
  }
#endif

  // Defer deletion until queue iteration is complete, as
  // SpdyBuffer::~SpdyBuffer() can result in callbacks into SpdyWriteQueue.
  std::vector<std::unique_ptr<SpdyBufferProducer>> erased_buffer_producers;

  // Do the actual deletion and removal, preserving FIFO-ness.
  base::circular_deque<PendingWrite>& queue = queue_[priority];
  auto out_it = queue.begin();
  for (auto it = queue.begin(); it != queue.end(); ++it) {
    if (it->stream.get() == stream.get()) {
      erased_buffer_producers.push_back(std::move(it->frame_producer));
    } else {
      *out_it = std::move(*it);
      ++out_it;
    }
  }
  queue.erase(out_it, queue.end());
  removing_writes_ = false;
}

void SpdyWriteQueue::RemovePendingWritesForStreamsAfter(
    spdy::SpdyStreamId last_good_stream_id) {
  CHECK(!removing_writes_);
  removing_writes_ = true;
  std::vector<std::unique_ptr<SpdyBufferProducer>> erased_buffer_producers;

  for (int i = MINIMUM_PRIORITY; i <= MAXIMUM_PRIORITY; ++i) {
    // Do the actual deletion and removal, preserving FIFO-ness.
    base::circular_deque<PendingWrite>& queue = queue_[i];
    auto out_it = queue.begin();
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      if (it->stream.get() && (it->stream->stream_id() > last_good_stream_id ||
                               it->stream->stream_id() == 0)) {
        erased_buffer_producers.push_back(std::move(it->frame_producer));
      } else {
        *out_it = std::move(*it);
        ++out_it;
      }
    }
    queue.erase(out_it, queue.end());
  }
  removing_writes_ = false;
}

void SpdyWriteQueue::Clear() {
  CHECK(!removing_writes_);
  removing_writes_ = true;
  std::vector<std::unique_ptr<SpdyBufferProducer>> erased_buffer_producers;

  for (int i = MINIMUM_PRIORITY; i <= MAXIMUM_PRIORITY; ++i) {
    for (auto it = queue_[i].begin(); it != queue_[i].end(); ++it) {
      erased_buffer_producers.push_back(std::move(it->frame_producer));
    }
    queue_[i].clear();
  }
  removing_writes_ = false;
}

size_t SpdyWriteQueue::EstimateMemoryUsage() const {
  return base::trace_event::EstimateMemoryUsage(queue_);
}

}  // namespace net
