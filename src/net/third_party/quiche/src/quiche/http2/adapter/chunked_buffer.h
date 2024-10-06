#ifndef QUICHE_HTTP2_ADAPTER_CHUNKED_BUFFER_H_
#define QUICHE_HTTP2_ADAPTER_CHUNKED_BUFFER_H_

#include <memory>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_circular_deque.h"

namespace http2 {
namespace adapter {

// A simple buffer class that organizes its memory as a queue of contiguous
// regions. Data is written to the end, and read from the beginning.
class QUICHE_EXPORT ChunkedBuffer {
 public:
  ChunkedBuffer() = default;

  // Appends data to the buffer.
  void Append(absl::string_view data);
  void Append(std::unique_ptr<char[]> data, size_t size);

  // Reads data from the buffer non-destructively.
  absl::string_view GetPrefix() const;
  std::vector<absl::string_view> Read() const;

  // Removes the first `n` bytes of the buffer. Invalidates any `string_view`s
  // read from the buffer.
  void RemovePrefix(size_t n);

  // Returns true iff the buffer contains no data to read.
  bool Empty() const;

 private:
  static constexpr size_t kDefaultChunkSize = 1024;

  // Describes a contiguous region of memory contained in the ChunkedBuffer. In
  // the common case, data is appended to the buffer by copying it to the final
  // chunk, or adding a unique_ptr to the list of chunks. Data is consumed from
  // the beginning of the buffer, so the first chunk may have a nonzero offset
  // from the start of the memory region to the first byte of readable data.
  struct Chunk {
    // A contiguous region of memory.
    std::unique_ptr<char[]> data;
    // The size of the contiguous memory.
    const size_t size;
    // The region occupied by live data that can be read from the buffer. A
    // subset of `data`.
    absl::string_view live;

    void RemovePrefix(size_t n);
    void AppendSuffix(absl::string_view to_append);

    bool Empty() const { return live.empty(); }

    // Returns the offset of the live data from the beginning of the chunk.
    size_t LiveDataOffset() const { return live.data() - data.get(); }
    // Returns the size of the free space at the end of the chunk.
    size_t TailBytesFree() const {
      return size - live.size() - LiveDataOffset();
    }
  };

  // Returns the number of tail bytes free in the last chunk in the buffer, or
  // zero.
  size_t TailBytesFree() const;

  // Ensures that the last chunk in the buffer has at least this many tail bytes
  // free.
  void EnsureTailBytesFree(size_t n);

  // Removes the first chunk, unless it is the last chunk in the buffer and its
  // size is kDefaultChunkSize.
  void TrimFirstChunk();

  quiche::QuicheCircularDeque<Chunk> chunks_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_CHUNKED_BUFFER_H_
