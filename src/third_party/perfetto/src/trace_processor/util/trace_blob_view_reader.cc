
/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/util/trace_blob_view_reader.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "perfetto/trace_processor/trace_blob_view.h"

namespace perfetto::trace_processor::util {

void TraceBlobViewReader::PushBack(TraceBlobView data) {
  if (data.size() == 0) {
    return;
  }
  const size_t size = data.size();
  data_.emplace_back(Entry{end_offset_, std::move(data)});
  end_offset_ += size;
}

bool TraceBlobViewReader::PopFrontUntil(const size_t target_offset) {
  PERFETTO_CHECK(start_offset() <= target_offset);
  while (!data_.empty()) {
    Entry& entry = data_.front();
    if (target_offset == entry.start_offset) {
      return true;
    }
    const size_t bytes_to_pop = target_offset - entry.start_offset;
    if (entry.data.size() > bytes_to_pop) {
      entry.data =
          entry.data.slice_off(bytes_to_pop, entry.data.size() - bytes_to_pop);
      entry.start_offset += bytes_to_pop;
      return true;
    }
    data_.pop_front();
  }
  return target_offset == end_offset_;
}

template <typename Visitor>
auto TraceBlobViewReader::SliceOffImpl(const size_t offset,
                                       const size_t length,
                                       Visitor& visitor) const {
  // If the length is zero, then a zero-sized blob view is always appropriate.
  if (PERFETTO_UNLIKELY(length == 0)) {
    return visitor.OneSlice(TraceBlobView());
  }

  PERFETTO_DCHECK(offset >= start_offset());

  // Fast path: the slice fits entirely inside the first TBV, we can just slice
  // that directly without doing any searching. This will happen most of the
  // time when this class is used so optimize for it.
  bool is_fast_path =
      !data_.empty() &&
      offset + length <= data_.front().start_offset + data_.front().data.size();
  if (PERFETTO_LIKELY(is_fast_path)) {
    return visitor.OneSlice(data_.front().data.slice_off(
        offset - data_.front().start_offset, length));
  }

  // If we don't have any TBVs or the end of the slice does not fit, then we
  // cannot possibly return a full slice.
  if (PERFETTO_UNLIKELY(data_.empty() || offset + length > end_offset_)) {
    return visitor.NoData();
  }

  // Find the first block finishes *after* start_offset i.e. there is at least
  // one byte in that block which will end up in the slice. We know this *must*
  // exist because of the above check.
  auto it = std::upper_bound(
      data_.begin(), data_.end(), offset, [](size_t offset, const Entry& rhs) {
        return offset < rhs.start_offset + rhs.data.size();
      });
  PERFETTO_CHECK(it != data_.end());

  // If the slice fits entirely in the block we found, then just slice that
  // block avoiding any copies.
  size_t rel_off = offset - it->start_offset;
  if (rel_off + length <= it->data.size()) {
    return visitor.OneSlice(it->data.slice_off(rel_off, length));
  }

  auto res = visitor.StartMultiSlice(length);

  size_t res_offset = 0;
  size_t left = length;

  size_t size = it->data.length() - rel_off;
  visitor.AddSlice(res, res_offset, it->data.slice_off(rel_off, size));
  left -= size;
  res_offset += size;

  for (++it; left != 0; ++it) {
    size = std::min(left, it->data.size());
    visitor.AddSlice(res, res_offset, it->data.slice_off(0, size));
    left -= size;
    res_offset += size;
  }

  return visitor.Finalize(std::move(res));
}

std::optional<TraceBlobView> TraceBlobViewReader::SliceOff(
    size_t offset,
    size_t length) const {
  struct Visitor {
    static std::optional<TraceBlobView> NoData() { return std::nullopt; }

    static std::optional<TraceBlobView> OneSlice(TraceBlobView tbv) {
      return std::move(tbv);
    }

    static TraceBlob StartMultiSlice(size_t length) {
      return TraceBlob::Allocate(length);
    }

    static void AddSlice(TraceBlob& blob, size_t offset, TraceBlobView tbv) {
      memcpy(blob.data() + offset, tbv.data(), tbv.size());
    }

    static std::optional<TraceBlobView> Finalize(TraceBlob blob) {
      return TraceBlobView(std::move(blob));
    }

  } visitor;

  return SliceOffImpl(offset, length, visitor);
}

std::vector<TraceBlobView> TraceBlobViewReader::MultiSliceOff(
    size_t offset,
    size_t length) const {
  struct Visitor {
    static std::vector<TraceBlobView> NoData() { return {}; }

    static std::vector<TraceBlobView> OneSlice(TraceBlobView tbv) {
      std::vector<TraceBlobView> res;
      res.reserve(1);
      res.push_back(std::move(tbv));
      return res;
    }

    static std::vector<TraceBlobView> StartMultiSlice(size_t) { return {}; }

    static void AddSlice(std::vector<TraceBlobView>& vec,
                         size_t,
                         TraceBlobView tbv) {
      vec.push_back(std::move(tbv));
    }

    static std::vector<TraceBlobView> Finalize(std::vector<TraceBlobView> vec) {
      return vec;
    }

  } visitor;

  return SliceOffImpl(offset, length, visitor);
}

}  // namespace perfetto::trace_processor::util
