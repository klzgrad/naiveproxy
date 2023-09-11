#ifndef QUICHE_SPDY_CORE_HTTP2_HEADER_STORAGE_H_
#define QUICHE_SPDY_CORE_HTTP2_HEADER_STORAGE_H_

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/spdy/core/spdy_simple_arena.h"

namespace spdy {

// This class provides a backing store for absl::string_views. It previously
// used custom allocation logic, but now uses an UnsafeArena instead. It has the
// property that absl::string_views that refer to data in Http2HeaderStorage are
// never invalidated until the Http2HeaderStorage is deleted or Clear() is
// called.
//
// Write operations always append to the last block. If there is not enough
// space to perform the write, a new block is allocated, and any unused space
// is wasted.
class QUICHE_EXPORT Http2HeaderStorage {
 public:
  Http2HeaderStorage();

  Http2HeaderStorage(const Http2HeaderStorage&) = delete;
  Http2HeaderStorage& operator=(const Http2HeaderStorage&) = delete;

  Http2HeaderStorage(Http2HeaderStorage&& other) = default;
  Http2HeaderStorage& operator=(Http2HeaderStorage&& other) = default;

  absl::string_view Write(absl::string_view s);

  // If |s| points to the most recent allocation from arena_, the arena will
  // reclaim the memory. Otherwise, this method is a no-op.
  void Rewind(absl::string_view s);

  void Clear() { arena_.Reset(); }

  // Given a list of fragments and a separator, writes the fragments joined by
  // the separator to a contiguous region of memory. Returns a absl::string_view
  // pointing to the region of memory.
  absl::string_view WriteFragments(
      const std::vector<absl::string_view>& fragments,
      absl::string_view separator);

  size_t bytes_allocated() const { return arena_.status().bytes_allocated(); }

 private:
  SpdySimpleArena arena_;
};

// Writes |fragments| to |dst|, joined by |separator|. |dst| must be large
// enough to hold the result. Returns the number of bytes written.
QUICHE_EXPORT size_t Join(char* dst,
                          const std::vector<absl::string_view>& fragments,
                          absl::string_view separator);

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_HTTP2_HEADER_STORAGE_H_
