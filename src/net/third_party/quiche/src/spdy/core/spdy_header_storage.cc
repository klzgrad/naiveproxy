#include "spdy/core/spdy_header_storage.h"

#include <cstring>

#include "spdy/platform/api/spdy_logging.h"

namespace spdy {
namespace {

// SpdyHeaderStorage allocates blocks of this size by default.
const size_t kDefaultStorageBlockSize = 2048;

}  // namespace

SpdyHeaderStorage::SpdyHeaderStorage() : arena_(kDefaultStorageBlockSize) {}

absl::string_view SpdyHeaderStorage::Write(const absl::string_view s) {
  return absl::string_view(arena_.Memdup(s.data(), s.size()), s.size());
}

void SpdyHeaderStorage::Rewind(const absl::string_view s) {
  arena_.Free(const_cast<char*>(s.data()), s.size());
}

absl::string_view SpdyHeaderStorage::WriteFragments(
    const std::vector<absl::string_view>& fragments,
    absl::string_view separator) {
  if (fragments.empty()) {
    return absl::string_view();
  }
  size_t total_size = separator.size() * (fragments.size() - 1);
  for (const absl::string_view& fragment : fragments) {
    total_size += fragment.size();
  }
  char* dst = arena_.Alloc(total_size);
  size_t written = Join(dst, fragments, separator);
  QUICHE_DCHECK_EQ(written, total_size);
  return absl::string_view(dst, total_size);
}

size_t Join(char* dst,
            const std::vector<absl::string_view>& fragments,
            absl::string_view separator) {
  if (fragments.empty()) {
    return 0;
  }
  auto* original_dst = dst;
  auto it = fragments.begin();
  memcpy(dst, it->data(), it->size());
  dst += it->size();
  for (++it; it != fragments.end(); ++it) {
    memcpy(dst, separator.data(), separator.size());
    dst += separator.size();
    memcpy(dst, it->data(), it->size());
    dst += it->size();
  }
  return dst - original_dst;
}

}  // namespace spdy
