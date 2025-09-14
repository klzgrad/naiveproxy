#include "quiche/http2/adapter/chunked_buffer.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace http2 {
namespace adapter {

namespace {

constexpr size_t kKilobyte = 1024;
size_t RoundUpToNearestKilobyte(size_t n) {
  // The way to think of this bit math is: it fills in all of the least
  // significant bits less than 1024, then adds one. This guarantees that all of
  // those bits end up as 0, hence rounding up to a multiple of 1024.
  return ((n - 1) | (kKilobyte - 1)) + 1;
}

}  // namespace

void ChunkedBuffer::Append(absl::string_view data) {
  // Appends the data by copying it.
  const size_t to_copy = std::min(TailBytesFree(), data.size());
  if (to_copy > 0) {
    chunks_.back().AppendSuffix(data.substr(0, to_copy));
    data.remove_prefix(to_copy);
  }
  EnsureTailBytesFree(data.size());
  chunks_.back().AppendSuffix(data);
}

void ChunkedBuffer::Append(std::unique_ptr<char[]> data, size_t size) {
  if (TailBytesFree() >= size) {
    // Copies the data into the existing last chunk, since it will fit.
    Chunk& c = chunks_.back();
    c.AppendSuffix(absl::string_view(data.get(), size));
    return;
  }
  while (!chunks_.empty() && chunks_.front().Empty()) {
    chunks_.pop_front();
  }
  // Appends the memory to the end of the deque, since it won't fit in an
  // existing chunk.
  absl::string_view v = {data.get(), size};
  chunks_.push_back({std::move(data), size, v});
}

absl::string_view ChunkedBuffer::GetPrefix() const {
  if (chunks_.empty()) {
    return "";
  }
  return chunks_.front().live;
}

std::vector<absl::string_view> ChunkedBuffer::Read() const {
  std::vector<absl::string_view> result;
  result.reserve(chunks_.size());
  for (const Chunk& c : chunks_) {
    result.push_back(c.live);
  }
  return result;
}

void ChunkedBuffer::RemovePrefix(size_t n) {
  while (!Empty() && n > 0) {
    Chunk& c = chunks_.front();
    const size_t to_remove = std::min(n, c.live.size());
    c.RemovePrefix(to_remove);
    n -= to_remove;
    if (c.Empty()) {
      TrimFirstChunk();
    }
  }
}

bool ChunkedBuffer::Empty() const {
  return chunks_.empty() ||
         (chunks_.size() == 1 && chunks_.front().live.empty());
}

void ChunkedBuffer::Chunk::RemovePrefix(size_t n) {
  QUICHE_DCHECK_GE(live.size(), n);
  live.remove_prefix(n);
}

void ChunkedBuffer::Chunk::AppendSuffix(absl::string_view to_append) {
  QUICHE_DCHECK_GE(TailBytesFree(), to_append.size());
  if (live.empty()) {
    std::copy(to_append.begin(), to_append.end(), data.get());
    // Live needs to be initialized, since it points to nullptr.
    live = absl::string_view(data.get(), to_append.size());
  } else {
    std::copy(to_append.begin(), to_append.end(),
              const_cast<char*>(live.data()) + live.size());
    // Live can be extended, since it already points to valid data.
    live = absl::string_view(live.data(), live.size() + to_append.size());
  }
}

size_t ChunkedBuffer::TailBytesFree() const {
  if (chunks_.empty()) {
    return 0;
  }
  return chunks_.back().TailBytesFree();
}

void ChunkedBuffer::EnsureTailBytesFree(size_t n) {
  if (TailBytesFree() >= n) {
    return;
  }
  const size_t to_allocate = RoundUpToNearestKilobyte(n);
  auto data = std::unique_ptr<char[]>(new char[to_allocate]);
  chunks_.push_back({std::move(data), to_allocate, ""});
}

void ChunkedBuffer::TrimFirstChunk() {
  // Leave the first chunk, if it's the only one and already the default size.
  if (chunks_.empty() ||
      (chunks_.size() == 1 && chunks_.front().size == kDefaultChunkSize)) {
    return;
  }
  chunks_.pop_front();
}

}  // namespace adapter
}  // namespace http2
